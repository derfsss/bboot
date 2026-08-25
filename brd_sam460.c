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

/*
 * Extended bd_t for PPC460EX (SAM460ex)
 *
 * The AmigaOS kernel expects the full U-Boot bd_t layout for CONFIG_440 /
 * CONFIG_460EX, which has additional fields beyond the common bd_t.
 * We define the full structure here to match what U-Boot provides.
 */
typedef struct {
    /* Common fields (must match bd_t in u-boot.h) */
    unsigned long   bi_memstart;
    unsigned long   bi_memsize;
    unsigned long   bi_flashstart;
    unsigned long   bi_flashsize;
    unsigned long   bi_flashoffset;
    unsigned long   bi_sramstart;
    unsigned long   bi_sramsize;
    unsigned long   bi_bootflags;
    unsigned long   bi_ip_addr;
    unsigned char   bi_enetaddr[6];
    unsigned short  bi_ethspeed;
    unsigned long   bi_intfreq;
    unsigned long   bi_busfreq;
    unsigned long   bi_baudrate;
    /* PPC440/460EX specific fields */
    unsigned char   bi_s_version[4];
    unsigned char   bi_r_version[32];
    unsigned int    bi_procfreq;
    unsigned int    bi_plb_busfreq;
    unsigned int    bi_pci_busfreq;
    unsigned char   bi_pci_enetaddr[6];
    /* CONFIG_HAS_ETH1 */
    unsigned char   bi_enet1addr[6];
    /* CONFIG_460EX specific */
    unsigned int    bi_opbfreq;
    int             bi_iic_fast[2];
    /* CONFIG_460EX: 4 phy entries */
    int             bi_phynum[4];
    int             bi_phymode[4];
} bd_460ex_t;

static bd_460ex_t bd;
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
 *
 * Layout matches U-Boot's init.S TLB table for SAM460ex so that the
 * AmigaOS kernel finds PCI at the expected virtual addresses:
 *   PCI memory: VA 0x80000000 = PCI bus 0x80000000 (identity mapped)
 *   PCI regs/IO: VA 0xD0000000 -> PA 0xC_00000000
 */

/* TLB Word 0 flags */
#define TLB_VALID   0x00000200   /* V bit */
#define TLB_256K    0x00000040   /* DSIZ = 0100 = 256KB */
#define TLB_256M    0x00000090   /* DSIZ = 1001 = 256MB */

/* TLB Word 2 flags */
#define TLB_I       0x00000400   /* Cache inhibited */
#define TLB_G       0x00000100   /* Guarded */
#define TLB_SX      0x00000004   /* Supervisor execute */
#define TLB_SW      0x00000002   /* Supervisor write */
#define TLB_SR      0x00000001   /* Supervisor read */

#define TLB_MMIO    (TLB_I | TLB_G | TLB_SX | TLB_SW | TLB_SR)
#define TLB_RAM     (TLB_SX | TLB_SW | TLB_SR)   /* cached, not guarded */

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

static void setup_tlb(uint32_t memsize)
{
    /*
     * RAM: VA 0x00000000 -> PA 0x00000000 in 256MB pages, cached.
     *
     * QEMU's -kernel boot hands us a single fabricated TLB entry 0 for
     * VA 0 -> PA 0 with a size of 2GB, which cannot be encoded in the
     * 44x TLBHI DSIZ field (256MB is the largest page). A guest reading
     * that entry back with tlbre gets DSIZ = 1 (4KB), so the AmigaOS
     * loader, which does a read-modify-write of the entry mapping VA 0
     * to enable caching and then rfi's, shrinks RAM to 4KB and faults on
     * the rfi. Replace it with properly encoded entries like U-Boot's
     * init.S does. Overwriting entry 0 in place is safe: the first page
     * still covers the code doing the write.
     */
    unsigned int pages = (memsize + 0xfffffff) >> 28;
    if (!pages) pages = 1;
    if (pages > 8) pages = 8;       /* entries 0 and 53-59: up to 2GB */
    for (unsigned int i = 0; i < pages; i++) {
        write_tlb(i ? 60 - (int)i : 0,
            (i << 28) | TLB_VALID | TLB_256M,
            (i << 28),              /* RPN, ERPN = 0 */
            TLB_RAM);
    }

    /*
     * Entry 60: PCI memory window 2
     * VA 0x90000000 -> PA 0xC_90000000, 256MB, cache-inhibited + guarded
     * Matches U-Boot: tlbentry(CONFIG_SYS_PCI_MEMBASE+0x10000000, ...)
     */
    write_tlb(60,
        0x90000000 | TLB_VALID | TLB_256M,
        0x90000000 | 0xC,       /* RPN=0x90000000 | ERPN=C */
        TLB_MMIO);

    /*
     * Entry 61: PCI memory window 1
     * VA 0x80000000 -> PA 0xC_80000000, 256MB, cache-inhibited + guarded
     * Matches U-Boot: tlbentry(CONFIG_SYS_PCI_MEMBASE, SZ_256M, ...)
     * VA = PCI bus addr so kernel can use BAR values directly as VAs
     */
    write_tlb(61,
        0x80000000 | TLB_VALID | TLB_256M,
        0x80000000 | 0xC,       /* RPN=0x80000000 | ERPN=C */
        TLB_MMIO);

    /*
     * Entry 62: OPB peripheral space (UART, I2C, GPIO, etc.)
     * VA 0xEF600000 -> PA 0x4_EF600000, 256KB, cache-inhibited + guarded
     * U-Boot maps 16M at 0xEF000000; 256K at 0xEF600000 covers the UART
     */
    write_tlb(62,
        0xEF600000 | TLB_VALID | TLB_256K,
        0xEF600000 | 0x4,       /* RPN | ERPN=4 */
        TLB_MMIO);

    /*
     * Entry 63: PCI config + I/O + internal regs
     * VA 0xD0000000 -> PA 0xC_00000000, 256MB, cache-inhibited + guarded
     * Matches U-Boot: tlbentry(CONFIG_SYS_PCI_BASE, SZ_256M, 0, 0xC, ...)
     * Covers: PCI config at VA 0xDEC00000, PCI I/O at VA 0xD8000000
     */
    write_tlb(63,
        0xD0000000 | TLB_VALID | TLB_256M,
        0x00000000 | 0xC,       /* RPN=0 | ERPN=C */
        TLB_MMIO);
}

/*
 * PCI setup for SAM460ex PCIX0 host bridge
 *
 * Address mapping (matching U-Boot Sam460ex configuration):
 *   Config addr/data: PA 0xC_0EC00000 -> VA 0xDEC00000
 *   I/O window:       PA 0xC_08000000 -> VA 0xD8000000 (64KB)
 *   Memory window:    PA 0xC_80000000 -> VA 0x80000000 -> PCI 0x80000000
 *   Memory window 2:  PA 0xC_90000000 -> VA 0x90000000 -> PCI 0x90000000
 *
 * POM/PIM registers at PA 0xC_0EC80000 -> VA 0xDEC80000
 */

/* POM/PIM register addresses (VA via TLB entry 63) */
#define PCIX_REG_BASE   0xDEC80000
#define POM0LAL         (PCIX_REG_BASE + 0x68)
#define POM0LAH         (PCIX_REG_BASE + 0x6c)
#define POM0SA          (PCIX_REG_BASE + 0x70)
#define POM0PCIAL       (PCIX_REG_BASE + 0x74)
#define POM0PCIAH       (PCIX_REG_BASE + 0x78)
#define POM1LAL         (PCIX_REG_BASE + 0x7c)
#define POM1LAH         (PCIX_REG_BASE + 0x80)
#define POM1SA          (PCIX_REG_BASE + 0x84)
#define POM1PCIAL       (PCIX_REG_BASE + 0x88)
#define POM1PCIAH       (PCIX_REG_BASE + 0x8c)
#define PIM0SAL         (PCIX_REG_BASE + 0x98)
#define PIM0LAL         (PCIX_REG_BASE + 0x9c)
#define PIM0LAH         (PCIX_REG_BASE + 0xa0)

static void setup_pom(void)
{
    /* Disable outbound windows during setup */
    write32_le(POM0SA, 0);
    write32_le(POM1SA, 0);

    /* POM0: CPU PA 0xC_80000000 -> PCI bus 0x80000000, 256MB */
    write32_le(POM0LAL, 0x80000000);
    write32_le(POM0LAH, 0x0000000C);
    write32_le(POM0PCIAL, 0x80000000);
    write32_le(POM0PCIAH, 0x00000000);
    write32_le(POM0SA, 0xF0000001);     /* ~(256MB-1) | enable */

    /* POM1: CPU PA 0xC_90000000 -> PCI bus 0x90000000, 256MB */
    write32_le(POM1LAL, 0x90000000);
    write32_le(POM1LAH, 0x0000000C);
    write32_le(POM1PCIAL, 0x90000000);
    write32_le(POM1PCIAH, 0x00000000);
    write32_le(POM1SA, 0xF0000001);     /* ~(256MB-1) | enable */

    /* PIM0: PCI DMA addr 0 -> CPU PA 0, sized to RAM (512MB default) */
    write32_le(PIM0LAL, 0x00000000);
    write32_le(PIM0LAH, 0x00000000);
    write32_le(PIM0SAL, 0xE0000003);    /* ~(512MB-1) | prefetch | enable */
}

/*
 * PCI IRQ assignment matching U-Boot's assign_pci_irq():
 *   SM502 (dev 6) -> IRQ 116 (UIC3 pin 20)
 *   All other PCI devices -> IRQ 32 (UIC1 pin 0)
 */
#define SM502_VENDOR_DEVICE  0x050112  /* vendev for SM501: 126f:0501 */
static uint8_t pci_irq_map[4] = {32, 32, 32, 32};

static void setup_pci_params(void)
{
    pci_set_addr(0xDEC00000, 0xDEC00004);
    iobase = 0xD8000000;
    iolen = 0x10000;
    ioavail = 0x100;
    membase = 0x80000000;
    memlen = 0x20000000;    /* 512MB: POM0 (256MB) + POM1 (256MB) */
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
    /* Map IRQ: SM502 (vendor 126f, device 0501) gets IRQ 116 (UIC3-20),
     * all other PCI devices get IRQ 32 (UIC1-0) per U-Boot assign_pci_irq */
    uint8_t irq = 0xff;
    int pin = pci_read_config8(devfn, REG_INTERRUPT_PIN);
    if (pin > 0 && pin < 5) {
        uint16_t vendor = vendev & 0xffff;
        uint16_t device = vendev >> 16;
        if (vendor == 0x126f && device == 0x0501)
            irq = 116;     /* SM502: UIC3 pin 20 */
        else
            irq = pci_irq_map[(PCI_SLOT(devfn) + pin - 1) % 4];
    }
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
    setup_pom();
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
    brd.claim = &no_claim;
    brd.setup = &sam460_setup;
    brd.exec_addr = (void *)0x1000000;
    brd.serial_base = 0xEF600300;
    brd.info = &bd;

    /* Read the FDT before touching the TLB: it is in RAM, still covered
     * by the mapping QEMU set up, and setup_tlb() needs the RAM size. */
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

    setup_tlb(memsize);

    /*
     * Stack near the top of RAM, like U-Boot leaves it: it relocates
     * itself there and puts sp below its image, so AmigaOS expects the
     * top of memory to be firmware territory and everything else free.
     * QEMU's -kernel entry instead leaves sp at 16MB-8, immediately
     * below brd.exec_addr, where the loader allocates over it.
     */
    unsigned long ram_top = memsize;
    if (ram_top > 8UL << 28) ram_top = 8UL << 28;   /* what setup_tlb maps */
    brd.stack_top = (ram_top - 0x100000) & ~0xfUL;

    /* Common bd_t fields */
    bd.bi_memstart = 0;
    bd.bi_memsize = memsize;
    bd.bi_flashstart = 0xfff00000;
    bd.bi_flashsize = 1;
    bd.bi_bootflags = 1;
    bd.bi_intfreq = 1150000000;     /* 1150 MHz CPU */
    bd.bi_busfreq = 230000000;      /* 230 MHz PLB */
    bd.bi_baudrate = 115200;

    /* PPC460EX specific fields */
    bd.bi_procfreq = 1150000000;    /* CPU freq in Hz */
    bd.bi_plb_busfreq = 230000000;  /* PLB bus speed in Hz */
    bd.bi_pci_busfreq = 66000000;   /* PCI bus speed in Hz (66 MHz PCI-X) */
    bd.bi_opbfreq = 115000000;      /* OPB clock in Hz (PLB/2) */
}
