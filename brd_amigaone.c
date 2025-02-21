/*
 * BBoot boot loader AmigaOne support for AmigaOS
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <types.h>
#include "drivers/pci.h"
#include "ppc-mmio.h"
#include "u-boot.h"
#include "bboot.h"

static bd_t bd;
static uint32_t iobase, iolen, membase, memlen;
static uint32_t ioavail, memavail;

static uint8_t pci_irq_map[4] = {9, 10, 11, 7};

static void setup_pci_params(void)
{
    pci_set_addr(0xfec00cf8, 0xfee00cfc);
    iobase = 0xfe000000;
    iolen = 0xc00000;
    ioavail = 0x800000;
    membase = 0x80000000;
    memlen = 0x7d000000;
    memavail = membase;
}

static uint32_t addr_for_bar(int type, uint32_t size)
{
    if (!size) return 0;
    uint32_t ret;
    if (type == BAR_SPACE_IO) {
        ret = ROUND_UP(ioavail, size);
        if (ret + size > iolen) return 0;
        ioavail = ret + size;
    } else {
        ret = ROUND_UP(memavail, size);
        if (ret + size > membase + memlen) return 0;
        memavail = ret + size;
    }
    return ret;
}

static void setup_isa(uint32_t devfn)
{
    /* IDE IRQ routing 14/15 */
    pci_write_config8(devfn, 0x4a, 0x04);
    /* Parallel DRQ 3, Floppy DRQ 2 (default) */
    pci_write_config8(devfn, 0x50, 0x0e);
    /* IRQ 6 for floppy, IRQ 7 for parallel port */
    pci_write_config8(devfn, 0x51, 0x76);
    /* IRQ Routing for serial ports (take IRQ 3 and 4) */
    pci_write_config8(devfn, 0x52, 0x34);
    /* All PIRQs level triggered. */
    pci_write_config8(devfn, 0x54, 0x00);
    /* PIRQ routing */
    pci_write_config8(devfn, 0x55, pci_irq_map[0] << 4);
    pci_write_config8(devfn, 0x56, pci_irq_map[2] << 4 | pci_irq_map[1]);
    pci_write_config8(devfn, 0x57, pci_irq_map[3] << 4);
}

static void setup_fdc(uint32_t devfn)
{
    /*  Enable Configuration mode */
    pci_write_config8(devfn, 0x85, 3);
    /*  Set floppy controller port to 0x3F0. */
    write8(iobase + 0x3f0, 0xe3);
    write8(iobase + 0x3f1, 0x3f << 2);
    /*  Enable floppy controller */
    write8(iobase + 0x3f0, 0xe2);
    uint8_t c = read8(iobase + 0x3f1);
    write8(iobase + 0x3f0, 0xe2);
    write8(iobase + 0x3f1, c | 0x10);
    /*  Switch of configuration mode */
    pci_write_config8(devfn, 0x85, 1);
}

static void setup_ide(uint32_t devfn)
{
    /*  Enable both IDE channels. */
    pci_write_config8(devfn, 0x40, 0x03);
    /*  Enable Bus master, Memory and IO Space */
    pci_write_config8(devfn, REG_COMMAND, 0x07);
    /*  Set to compatibility mode */
    pci_write_config8(devfn, REG_PROG_IF, 0x8a);
    /*  Set to legacy interrupt mode */
    pci_write_config8(devfn, REG_INTERRUPT_PIN, 0x00);
    pci_write_config8(devfn, REG_INTERRUPT_LINE, 0xff);
    pci_write_config8(devfn, REG_LATENCY_TIMER, 0x20);
}

static void setup_pci_device(uint32_t devfn)
{
    uint32_t vendev = pci_read_config32(devfn, REG_VENDOR_ID);
    if (vendev == 0xffffffff) return;
    uint16_t cmd = pci_read_config16(devfn, REG_COMMAND);
    printf("%x:%x.%x %04x:%04x %8x %04x %x\n", PCI_BUS(devfn), PCI_SLOT(devfn), PCI_FUNC(devfn),
           vendev & 0xffff, vendev >> 16, pci_read_config32(devfn, REG_REVISION_ID),
           pci_read_config16(devfn, REG_INTERRUPT_LINE), cmd);
    cmd &= ~(REG_COMMAND_BM | REG_COMMAND_MEM | REG_COMMAND_IO);
    pci_write_config16(devfn, REG_COMMAND, cmd);
    uint8_t latency_timer = 0x80;
    uint8_t irq = 0xff;
    if (PCI_SLOT(devfn) == 7) {
        switch PCI_FUNC(devfn) {
        case 0:
            setup_isa(devfn);
            setup_fdc(devfn);
            return;
        case 1:
            setup_ide(devfn);
            return;
        case 2:
        case 3:
            /* Modify some values in the USB controller */
            pci_write_config8(devfn, 0x05, 0x17);
            pci_write_config8(devfn, 0x06, 0x01);
            pci_write_config8(devfn, 0x41, 0x12);
            pci_write_config8(devfn, 0x42, 0x03);
            latency_timer = 0x40;
            irq = 5;
            break;
        case 4:
        case 6:
            return; /* ignore PM and modem functions */
        }
    }
    /* map IRQ */
    int pin = pci_read_config8(devfn, REG_INTERRUPT_PIN);
    if (pin > 0 && pin < 5 && irq == 0xff) {
        switch PCI_SLOT(devfn) {
        case 6:
            irq = pci_irq_map[3];
            break;
        case 7:
            irq = pci_irq_map[pin - 1];
            break;
        default:
            irq = pci_irq_map[(PCI_SLOT(devfn) + pin - 1) % 4];
        }
    }
    if (irq != 0xff)
        pci_write_config8(devfn, REG_INTERRUPT_LINE, irq);
    /* map BARs */
    for (int i = 0; i < 6; i++) {
        pci_write_config32(devfn, PCI_BAR(i), 0xffffffff);
        uint32_t bar = pci_read_config32(devfn, PCI_BAR(i));
        if (!bar) continue;
        int type;
        uint32_t size;
        if (bar & BAR_SPACE_IO) {
            type = bar & BAR_IO_ATTR_MASK;
            size = ~(bar & ~BAR_IO_ATTR_MASK) + 1;
            cmd |= REG_COMMAND_IO;
        } else {
            type = bar & BAR_MEM_ATTR_MASK;
            size = ~(bar & ~BAR_MEM_ATTR_MASK) + 1;
            cmd |= REG_COMMAND_MEM;
        }
        uint32_t base = addr_for_bar(type, size);
        printf("\t%x %x %8x %8x %x\n", PCI_BAR(i), type, base, size, bar);
        if (base) {
            pci_write_config32(devfn, PCI_BAR(i), base | type);
            pci_write_config16(devfn, REG_COMMAND, cmd | REG_COMMAND_BM);
            pci_write_config8(devfn, REG_CACHE_LINE_SIZE, 8);
            pci_write_config8(devfn, REG_LATENCY_TIMER, latency_timer);
        } else puts("Cannot fit BAR");
        if (type & BAR_MEM_LIMIT_64) ++i;
    }
}

static void amigaone_setup(void)
{
    setup_pci_params();
    printf("/pci@%x: io %x/%x mem %x/%x\n", membase, iobase, iolen, membase, memlen);
    for (int i = 0; i < 32; i++) {
        uint32_t devfn = PCI_DEV(0, i, 0);
        setup_pci_device(devfn);
        if (pci_read_config8(devfn, REG_HEADER_TYPE) & HEADER_TYPE_MULTIFUNCTION) {
            for (int j = 1; j < 8; j++) {
                setup_pci_device(PCI_DEV(0, i, j));
            }
        }
    }
}

static unsigned int find_memory_size(unsigned int start)
{
    volatile unsigned int *p;
    unsigned int i = start ?: 0x80000000;
    while (i > sizeof(*p)) {
        p = (unsigned int *)(start + i - sizeof(*p));
        *p = 0xaaaaaaaa;
        if (*p == 0xaaaaaaaa) {
            *p = 0;
            break;
        }
        i /= 2;
    }
    /* Check if there's more */
    if (start + i + 32 * 1024 < 0x80000000) {
        /* There may be 32k init ram at 1GB so skip that */
        p = (unsigned int *)(start + i + 32 * 1024);
        *p = 0xaaaaaaaa;
        if (*p == 0xaaaaaaaa) {
            *p = 0;
            i += find_memory_size(i);
        }
    }
    return i;
}

static void *no_claim(void *addr, unsigned int size)
{
    return (unsigned long)addr + size < bd.bi_memsize ? addr : NULL;
}

void amigaone_init(void)
{
    brd.claim = &no_claim;
    brd.setup = &amigaone_setup;
    brd.exec_addr = (void *)0x1000000;
    brd.serial_base = 0xfe0003f8;
    brd.info = &bd;

    bd.bi_memsize = find_memory_size(0);
    bd.bi_flashstart = 0xfff00000;
    bd.bi_flashsize = 1;
    bd.bi_bootflags = 1;
    bd.bi_intfreq = 1150000000;
    bd.bi_busfreq = 100000000;
    bd.bi_baudrate = 115200;
}
