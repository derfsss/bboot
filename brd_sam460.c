/*
 * BBoot boot loader SAM460ex support for AmigaOS
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <types.h>
#include "drivers/pci.h"
#include "ppc-mmio.h"
#include "u-boot.h"
#include "bboot.h"
#include "fdt.h"

static bd_t bd;
static uint32_t iobase, iolen, membase, memlen;
static uint32_t ioavail, memavail;

/*
 * PPC460EX BookE TLB setup
 *
 * The SAM460ex uses 36-bit physical addresses for peripherals.
 * We need TLB entries to map these into the 32-bit effective address space.
 *
 * TLB Word 0: EPN[0:21] | V(bit22) | TS(bit23) | DSIZ(bits24-27)
 * TLB Word 1: RPN[0:21] | ERPN(bits28-31)
 * TLB Word 2: storage attributes and permissions
 */

/* TLB Word 0 flags (PPC44x bit numbering) */
#define TLB_VALID   0x00000200   /* V bit */
#define TLB_64K     0x00000040   /* DSIZ = 0100 */
#define TLB_256M    0x00000090   /* DSIZ = 1001 */

/* TLB Word 2 flags */
#define TLB_I       0x00000400   /* Cache inhibited */
#define TLB_G       0x00000100   /* Guarded */
#define TLB_SX      0x00000004   /* Supervisor execute */
#define TLB_SW      0x00000002   /* Supervisor write */
#define TLB_SR      0x00000001   /* Supervisor read */

#define TLB_MMIO    (TLB_I | TLB_G | TLB_SX | TLB_SW | TLB_SR)

static void write_tlb(int index, uint32_t w0, uint32_t w1, uint32_t w2)
{
    asm volatile(
        "mtspr 946, %4\n"       /* MMUCR = 0 (TID=0, TS=0) */
        "tlbwe %0, %3, 0\n"    /* Write TLB word 0 */
        "tlbwe %1, %3, 1\n"    /* Write TLB word 1 */
        "tlbwe %2, %3, 2\n"    /* Write TLB word 2 */
        "isync\n"
        :
        : "r"(w0), "r"(w1), "r"(w2), "r"(index), "r"(0)
        : "memory"
    );
}

static void setup_tlb(void)
{
    /*
     * Entry 62: OPB peripheral space (UART, I2C, GPIO, etc.)
     * VA 0xEF600000 -> PA 0x4_EF600000, 64KB, cache-inhibited + guarded
     */
    write_tlb(62,
        0xEF600000 | TLB_VALID | TLB_64K,
        0xEF600000 | 0x4,       /* RPN | ERPN=4 */
        TLB_MMIO);

    /*
     * Entry 63: PCI config + I/O space
     * VA 0xC0000000 -> PA 0xC_00000000, 256MB, cache-inhibited + guarded
     * Covers: PCI config at VA 0xC0EC0000, PCI I/O at VA 0xC8000000
     */
    write_tlb(63,
        0xC0000000 | TLB_VALID | TLB_256M,
        0x00000000 | 0xC,       /* RPN=0 | ERPN=C */
        TLB_MMIO);
}

/*
 * PCI setup for SAM460ex PCIX0 host bridge
 *
 * Config addr/data: PA 0xC_0EC00000 -> VA 0xC0EC0000
 * I/O window:       PA 0xC_08000000 -> VA 0xC8000000 (64KB)
 * Memory window:    PA 0xD_80000000 (PCI addr 0xD8000000, 256MB)
 */

/* PCI interrupts all route to UIC1 pin 0 */
static uint8_t pci_irq_map[4] = {2, 2, 2, 2};

static void setup_pci_params(void)
{
    pci_set_addr(0xC0EC0000, 0xC0EC0004);
    iobase = 0xC8000000;
    iolen = 0x10000;
    ioavail = 0x100;
    membase = 0xD8000000;
    memlen = 0x10000000;
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

static void setup_pci_device(uint32_t devfn)
{
    uint32_t vendev = pci_read_config32(devfn, REG_VENDOR_ID);
    if (vendev == 0xffffffff || vendev == 0x00000000) return;
    uint16_t cmd = pci_read_config16(devfn, REG_COMMAND);
    printf("%x:%x.%x %04x:%04x %8x %04x %x\n", PCI_BUS(devfn),
           PCI_SLOT(devfn), PCI_FUNC(devfn), vendev & 0xffff,
           vendev >> 16, pci_read_config32(devfn, REG_REVISION_ID),
           pci_read_config16(devfn, REG_INTERRUPT_LINE), cmd);
    cmd &= ~(REG_COMMAND_BM | REG_COMMAND_MEM | REG_COMMAND_IO);
    pci_write_config16(devfn, REG_COMMAND, cmd);
    /* Map IRQ */
    uint8_t irq = 0xff;
    int pin = pci_read_config8(devfn, REG_INTERRUPT_PIN);
    if (pin > 0 && pin < 5)
        irq = pci_irq_map[(PCI_SLOT(devfn) + pin - 1) % 4];
    if (irq != 0xff)
        pci_write_config8(devfn, REG_INTERRUPT_LINE, irq);
    /* Map BARs */
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
            pci_write_config8(devfn, REG_LATENCY_TIMER, 0x80);
        } else puts("Cannot fit BAR");
        if (type & BAR_MEM_LIMIT_64) ++i;
    }
}

static void sam460_setup(void)
{
    setup_pci_params();
    printf("/pci@%x: io %x/%x mem %x/%x\n", membase, iobase, iolen,
           membase, memlen);
    for (int i = 0; i < 32; i++) {
        uint32_t devfn = PCI_DEV(0, i, 0);
        setup_pci_device(devfn);
        if (pci_read_config8(devfn, REG_HEADER_TYPE) &
            HEADER_TYPE_MULTIFUNCTION) {
            for (int j = 1; j < 8; j++)
                setup_pci_device(PCI_DEV(0, i, j));
        }
    }
}

static void *no_claim(void *addr, unsigned int size)
{
    return (unsigned long)addr + size < bd.bi_memsize ? addr : NULL;
}

void sam460_init(unsigned long fdt_addr)
{
    setup_tlb();

    brd.claim = &no_claim;
    brd.setup = &sam460_setup;
    brd.exec_addr = (void *)0x1000000;
    brd.serial_base = 0xEF600300;
    brd.info = &bd;

    const void *fdt = (const void *)fdt_addr;
    uint32_t memsize = 512 * 1024 * 1024;
    if (fdt_check(fdt) == 0) {
        /* /memory/reg = <addr_hi addr_lo size> - size is at index 2 */
        fdt_get_prop_u32_idx(fdt, "memory", "reg", 2, &memsize);

        /* Get initrd location from /chosen */
        uint32_t initrd_start = 0, initrd_end = 0;
        fdt_get_prop_u32(fdt, "chosen", "linux,initrd-start", &initrd_start);
        fdt_get_prop_u32(fdt, "chosen", "linux,initrd-end", &initrd_end);
        brd.initrd_start = initrd_start;
        brd.initrd_end = initrd_end;

        /* Get boot arguments from /chosen */
        static char bootargs_buf[256];
        if (fdt_get_prop_str(fdt, "chosen", "bootargs",
                             bootargs_buf, sizeof(bootargs_buf)) > 0)
            brd.bootargs = bootargs_buf;
    }

    bd.bi_memsize = memsize;
    bd.bi_flashstart = 0xfff00000;
    bd.bi_flashsize = 1;
    bd.bi_bootflags = 1;
    bd.bi_intfreq = 1150000000;     /* 1150 MHz CPU */
    bd.bi_busfreq = 230000000;      /* 230 MHz PLB */
    bd.bi_baudrate = 115200;
}
