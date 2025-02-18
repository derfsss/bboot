/*
 * BBoot boot loader Pegasos2 support for AmigaOS
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* AmigaOS relies on firmware to set up devices so check it or do it if needed
 * Also patch 64 bit BARs that the pegasos2 kernel cannot handle
 */

/* The debug messages printed show information about PCI devices and any
 * changes made to them. In each line, values left of the | (bar) come from the
 * device tree, those right of the | are read from PCI. All values are in hex.
 * For PCI buses io and mem window base/size are shown where BARs are mapped.
 * For each PCI device bus:slot.func vendor:device IDs and class code are shown
 * from device tree and after the | vendor and device ID, INT line and pin and
 * the command register value from PCI. After that each BAR is listed with
 * phys.hi phys.mid phys.low and two size cells then after the | the value from
 * the corresponding PCI register which can be followed by ! (bang) and the new
 * value if it was modified.
 * Other changes to device tree values or PCI interrupt setting are also noted
 * in messages above the BAR list when changes were made.
 * When running this under pegasos2.rom, only 64 bit BARs should be changed,
 * other values are already set up by the firmware and listed for reference.
 * When running with VOF this will create assigned-addresses for all BARs and
 * may fix up some other values if needed.
 * If "Added assigned-addresses" is shown with no BARs listed then an empty
 * property was added as mandated by the standard if it was missing.
 */

#include <stdio.h>
#include <string.h>
#include <types.h>
#include "drivers/pci.h"
#include "drivers/prom.h"
#include "ppc-mmio.h"
#include "bboot.h"

#define PHYS_HI_PREFETCH (1UL << 30)
#define PHYS_HI_TYPE(x) (((x) >> 24) & 3)

static uint32_t iobase, iolen, membase, memlen;
static uint32_t ioresavail, ioavail, memavail;

static uint32_t cells[7*5];

#define IORES_START 0x1000
#define IORES_TOP 0x1200

#define MV64361_BASE 0xf1000000

static uint32_t addr_for_bar(uint32_t physhi, uint32_t size)
{
    if (!size) return 0;
    uint32_t ret = 0;
    switch(PHYS_HI_TYPE(physhi)) {
    case 1:
    {
        uint32_t *avail = &ioavail;
        if (PCI_BUS(physhi) == 0 && PCI_SLOT(physhi) == 0xc) avail = &ioresavail;
        *avail = ROUND_UP(*avail, size);
        if (*avail + size > iolen) {
            printf("Cannot fit BAR %08x\n", physhi);
            return 0;
        }
        ret = iobase + *avail;
        *avail += size;
        if (avail == &ioresavail && *avail >= IORES_TOP) {
            /* Hack: rewind to fit the last two sound BARs in reserved area */
            *avail = IORES_START + 0x30;
        }
        break;
    }
    case 2:
    case 3:
        memavail = ROUND_UP(memavail, size);
        if (memavail - membase + size > memlen) {
            printf("Cannot fit BAR %08x\n", physhi);
            return 0;
        }
        ret = memavail;
        memavail += size;
        break;
    }
    return ret;
}

static void check_one_bus(prom_handle ph)
{
    char name[64];
    for (ph = prom_child(ph); ph; ph = prom_peer(ph)) {
        int ret = prom_getprop(ph, "name", name, sizeof(name));
        if (ret < 0) {
            puts("Cannot get node name");
            continue;
        }
        uint32_t vendor_id, device_id, class_code;
        if (prom_getprop(ph, "vendor-id", &vendor_id, 4) != 4 ||
            prom_getprop(ph, "device-id", &device_id, 4) != 4 ||
            prom_getprop(ph, "class-code", &class_code, 4) != 4) {
            puts("Cannot get device info");
            continue;
        }
        ret = prom_getprop(ph, "reg", cells, sizeof(cells));
        if (ret < 5) {
            puts("Missing reg property");
            continue;
        }
        uint32_t devfn = cells[0];
        uint32_t vendev = pci_read_config32(devfn, REG_VENDOR_ID);
        if (vendev == 0x82311106) {
            /* ISA PIC init: enable level trigger for some interrupts */
            write8(iobase + 0x4d0, 0xa4);
            write8(iobase + 0x4d1, 0xe);
        }
        /* VOF fixup: enable on board USB functions */
        if (PCI_BUS(devfn) == 0 && PCI_SLOT(devfn) == 0xc &&
            (PCI_FUNC(devfn) == 2 || PCI_FUNC(devfn) == 3))
            pci_write_config16(devfn, REG_COMMAND, 7);
        /* Enable LSI SCSI cards */
        if (vendor_id == 0x1000 && device_id < 0x14)
            pci_write_config16(devfn, REG_COMMAND, 7);
        printf("/pci@%x/%s:\t%x:%x.%x \t%04x:%04x %x | %08x %04x %x\n", membase, name, PCI_BUS(devfn),
               PCI_SLOT(devfn), PCI_FUNC(devfn), vendor_id, device_id, class_code, vendev,
               pci_read_config16(devfn, REG_INTERRUPT_LINE), pci_read_config16(devfn, REG_COMMAND));

        int i, n = ret / sizeof(cells[0]);
        for (i = 5; i < n; i += 5) {
            /* we do this later but print it here to not clobber output */
            if (PHYS_HI_TYPE(cells[i]) == 3)
                printf("Truncated 64 bit BAR %08x\n", cells[i]);
            if ((cells[i] & 0xff) == 0x28)
                puts("Fixed ROM BAR");
        }

        int setprop = 0; /* need to set assigned-addresses property */
        ret = prom_getprop(ph, "assigned-addresses", cells, sizeof(cells));
        if (ret < 0) {
            if (n < 6) {
                /* No BARs to assign, add empty property as per the standard */
                if (prom_setprop(ph, "assigned-addresses", cells, 0) < 0)
                    puts("Failed to set assigned-addresses");
                else
                    puts("Added assigned-addresses");
                continue;
            }
            /* otherwise assign addresses */
            for (i = 0; i < n - 5; i += 5) {
                memcpy(&cells[i], &cells[i + 5], 5 * sizeof(cells[9]));
                cells[i + 2] = addr_for_bar(cells[i], cells[i + 4]);
                if (cells[i + 3]) puts("BAR too big");
            }
            ret = i * sizeof(cells[0]);
            setprop = 1;
            printf("Added assigned-addresses");
            uint8_t int_line = pci_read_config8(devfn, REG_INTERRUPT_LINE);
            if ((int_line == 0 || int_line == 0xff) && vendev == (device_id << 16 | vendor_id)) {
                pci_write_config8(devfn, REG_INTERRUPT_LINE, 9);
                printf(", set interrupt %04x", pci_read_config16(devfn, REG_INTERRUPT_LINE));
            }
            putchar('\n');
        }
        n = ret / sizeof(cells[0]);
        for (i = 0; i < n; i += 5) {
            /* truncate 64 bit BAR here */
            if (PHYS_HI_TYPE(cells[i]) == 3) {
                cells[i] &= ~(1UL << 24);
                setprop = 1;
            }
            /* VOF fixup: correct ROM BAR reg offset */
            if ((cells[i] & 0xff) == 0x28) {
                cells[i] += 8;
                setprop = 1;
            }
            uint32_t val = cells[i + 2];
            if (PHYS_HI_TYPE(cells[i]) == 1) val = (val - iobase) | 1;
            if (cells[i] & PHYS_HI_PREFETCH) val |= 8;
            uint32_t reg = pci_read_config32(devfn, cells[i] & 0xff);
            if (reg & 4) val |= 4;
            printf(" %8x %8x %8x  %8x %8x  | %08x", cells[i], cells[i + 1], cells[i + 2], cells[i + 3], cells[i + 4], reg);
            if (reg != val && vendev == (device_id << 16 | vendor_id)) {
                printf(" ! %08x", val);
                pci_write_config32(devfn, cells[i] & 0xff, val);
            }
            putchar('\n');
        }
        if (setprop && prom_setprop(ph, "assigned-addresses", cells, n * sizeof(cells[0])) < 0)
            puts("Failed to set assigned-addresses");
    }
}

static int bases_for_bus(prom_handle ph)
{
    int i, n = prom_getprop(ph, "ranges", cells, sizeof(cells));
    if (n <= 0) {
        puts("Cannot get ranges");
        return 0;
    }
    n /= sizeof(cells[0]);
    for (i = 0; i < n; i += 6) {
        switch (PHYS_HI_TYPE(cells[i])) {
        case 1:
            iobase = cells[i + 3];
            iolen = cells[i + 5];
            break;
        case 2:
            membase = cells[i + 3];
            memlen = cells[i + 5];
            break;
        }
    }
    if (!iobase || !iolen || !membase || !memlen) {
        puts("Cannot get ranges");
        return 0;
    }
    printf("/pci@%x: io %x/%x mem %x/%x\n", membase, iobase, iolen, membase, memlen);
    memavail = membase;
    unsigned long pci_cfgbase;
    if (membase == 0x80000000) {
        ioresavail = IORES_START; /* reserved for south bridge functions */
        ioavail = IORES_TOP;
        pci_cfgbase = MV64361_BASE + 0xc78;
    } else if (membase == 0xc0000000) {
        ioresavail = iolen; /* no reserved space on this bus */
        ioavail = IORES_START;
        pci_cfgbase = MV64361_BASE + 0xcf8;
    } else {
        puts("Unknown PCI bus");
        return 0;
    }
    pci_set_addr(pci_cfgbase, pci_cfgbase + 4);
    return 1;
}

static void pegasos2_setup(void)
{
    prom_handle ph = prom_finddevice("/pci");
    if (ph && bases_for_bus(ph)) check_one_bus(ph);
    ph = prom_peer(ph);
    if (ph && bases_for_bus(ph)) check_one_bus(ph);

    /* VOF fixup: Make sure /rtas node has a name property */
    ph = prom_finddevice("/rtas");
    char rtas[] = "rtas";
    prom_setprop(ph, "name", rtas, sizeof(rtas));
}

static void *of_claim(void *addr, unsigned int size)
{
    return prom_claim(addr, size, 0);
}

void pegasos2_init(void)
{
    brd.claim = &of_claim;
    brd.setup = &pegasos2_setup;
    brd.info = prom_cientry();
    brd.exec_addr = (void *)0x400000;
    brd.serial_base = 0xfe0002f8;
}
