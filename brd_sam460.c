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

/*
 * TLB word encodings, named as in U-Boot's asm/mmu.h so the table below
 * can be read next to board/ACube/Sam460ex/init.S.
 */

/* Word 0: EPN | size | V */
#define TLB_VALID   0x00000200
#define TLB_1K      0x00000000
#define TLB_4K      0x00000010
#define TLB_16K     0x00000020
#define TLB_64K     0x00000030
#define TLB_256K    0x00000040
#define TLB_1M      0x00000050
#define TLB_16M     0x00000070
#define TLB_256M    0x00000090

/* Word 2: storage attributes and access control */
#define SA_W        0x00000800   /* write-through */
#define SA_I        0x00000400   /* caching inhibited */
#define SA_M        0x00000200   /* memory coherence */
#define SA_G        0x00000100   /* guarded */
#define AC_X        0x00000024   /* execute, user + supervisor */
#define AC_W        0x00000012   /* write, user + supervisor */
#define AC_R        0x00000009   /* read, user + supervisor */
#define AC_RW       (AC_R | AC_W)
#define AC_RWX      (AC_R | AC_W | AC_X)

static void write_tlb(int index, uint32_t w0, uint32_t w1, uint32_t w2)
{
    asm volatile(
        "mtspr 946, %4\n"      /* MMUCR = 0 (TID=0, TS=0) */
        "tlbwe %0, %3, 0\n"    /* Write TLB word 0 */
        "tlbwe %1, %3, 1\n"    /* Write TLB word 1 */
        "tlbwe %2, %3, 2\n"    /* Write TLB word 2 */
        "isync\n"
        :
        : "r"(w0), "r"(w1), "r"(w2), "r"(index), "r"(0)
        : "memory"
    );
}

/*
 * The mapping U-Boot leaves in place for AmigaOS, transcribed from
 * board/ACube/Sam460ex/init.S of ACube's U-Boot 2015.d. SDRAM is not in
 * this table: U-Boot programs it at run time from the DDR2 init.
 *
 * QEMU's -kernel entry gives us only one fabricated entry for VA 0 with a
 * 2GB size that cannot be encoded in the 44x DSIZ field - read back with
 * tlbre it becomes 4KB - so we program the whole layout ourselves.
 */
static const struct {
    uint32_t epn, size, rpn, erpn, attr;
} tlbtab[] = {
    /* EBC boot space (flash) */
    { 0xF0000000, TLB_256M, 0xF0000000, 0x4, AC_RWX | SA_G },
    /* internal PCI registers, config and I/O space */
    { 0xD0000000, TLB_256M, 0x00000000, 0xC, AC_RW | SA_G | SA_I },
    /* PCI memory windows 1 and 2 */
    { 0x80000000, TLB_256M, 0x80000000, 0xC, AC_RW | SA_G | SA_I },
    { 0x90000000, TLB_256M, 0x90000000, 0xC, AC_RW | SA_G | SA_I },
    /* PCIe memory windows */
    { 0xA0000000, TLB_256M, 0xA0000000, 0xD, AC_RW | SA_G | SA_I },
    { 0xB0000000, TLB_256M, 0xB0000000, 0xD, AC_RW | SA_G | SA_I },
    { 0xC0000000, TLB_256M, 0xC0000000, 0xD, AC_RW | SA_G | SA_I },
    /* PCIe config space */
    { 0xE0000000, TLB_16M, 0x00000000, 0xD, AC_RW | SA_G | SA_I },
    { 0xE1000000, TLB_16M, 0x20000000, 0xD, AC_RW | SA_G | SA_I },
    { 0xE3000000, TLB_1K, 0x10000000, 0xD, AC_RW | SA_G | SA_I },
    { 0xE3001000, TLB_1K, 0x30000000, 0xD, AC_RW | SA_G | SA_I },
    /* PCIe UTL registers */
    { 0xE4000000, TLB_16K, 0x08010000, 0xC, AC_RW | SA_G | SA_I },
    /* on chip memory */
    { 0xE5000000, TLB_1M, 0x00000000, 0x4, AC_RWX | SA_I },
    /* local configuration registers: UART, I2C, GPIO, EBC, SDRAM, EMAC */
    { 0xEF000000, TLB_16M, 0xEF000000, 0x4, AC_RWX | SA_G | SA_I },
    /* AHB: internal USB and SATA */
    { 0xE2000000, TLB_1M, 0xBFF00000, 0x4, AC_RWX | SA_G | SA_I },
};

static void setup_tlb(uint32_t memsize)
{
    unsigned int i, n = sizeof(tlbtab) / sizeof(tlbtab[0]);

    /*
     * Slot order matters, so keep U-Boot's: the static table occupies the
     * low indices (entry 0 is the flash window) and SDRAM goes in the
     * entries after it, which is where U-Boot's program_tlb() finds free
     * slots. The AmigaOS kernel recycles TLB entries from the bottom once
     * it takes over; with RAM in entry 0 it unmaps low memory out from
     * under its own exception vectors and loops in an ITLB fault.
     *
     * Write the SDRAM entries first: overwriting entry 0, which is the
     * only mapping QEMU's -kernel entry gives us, is only safe once
     * another valid entry already covers the code doing the write.
     *
     * SDRAM uses 256MB pages, the largest the 44x TLB encodes, with the
     * attributes U-Boot's program_tlb() applies: every access bit set and
     * caching inhibited, since Sam460ex does not define CONFIG_4xx_DCACHE.
     * The AmigaOS loader turns caching on itself later.
     */
    unsigned int pages = (memsize + 0xfffffff) >> 28;
    if (!pages) pages = 1;
    if (pages > 64 - n) pages = 64 - n;
    for (i = 0; i < pages; i++) {
        write_tlb(n + i, (i << 28) | TLB_VALID | TLB_256M, i << 28,
                  AC_RWX | SA_I);
    }

    for (i = 0; i < n; i++) {
        write_tlb(i, tlbtab[i].epn | TLB_VALID | tlbtab[i].size,
                  tlbtab[i].rpn | tlbtab[i].erpn, tlbtab[i].attr);
    }
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
