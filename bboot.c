/*
 * BBoot boot loader
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include "drivers/console.h"
#include "drivers/prom.h"
#include "drivers/uart8250.h"
#include "bboot.h"

static char verstag[] = "\0$VER: BBoot " STRINGIFY(VERSION) "." STRINGIFY(REVISION) " (" BUILDDATE ")";

int vlvl;
brd_t brd;

typedef void (*loader_func)(const char *id, void *kicklist, void *info, char *cmdline);

static void serial_write(const char *s, unsigned int n)
{
    while (n--)
        uart_tx_byte(brd.serial_base, *s++);
}

static struct console_output_driver serial_consout = {
    .write = &serial_write
};

static struct console_output_driver prom_consout = {
    .write = &prom_write
};

int start(unsigned long r3, unsigned long r4, unsigned long r5,
          unsigned long r6, unsigned long r7, unsigned long r8)
{
    int of = 0;
    if (r6 == 0x45504150) {         /* ePAPR magic: SAM460 via QEMU -kernel */
        sam460_init(r3);            /* r3 = FDT address */
    } else if (r8 == 1024 || !r5) {
        amigaone_init();
    } else {
        of = 1;
        prom_init((void *)r5);
        prom_handle ph = prom_finddevice("/");
        char model[32] = {};
        prom_getprop(ph, "model", model, sizeof(model));
        if (!strcmp(model, "Pegasos2")) {
            pegasos2_init();
        } else {
            prom_puts(prom_stdout, "Unknown machine");
            goto error;
        }
    }
    cfg_init();
    if (cfg_is_option('O', 'f')) {
        console_add_output_driver(&prom_consout);
    }
    if (cfg_is_option('O', 's')) {
        uart_init(brd.serial_base);
        console_add_output_driver(&serial_consout);
    }
    putchar('\n');
    puts(&verstag[7]);

    char *p = cfg_get_option('V');
    if (p && *p >= '0' && *p <= '9') vlvl = *p - '0';

    if (cfg_is_option('A', 'p') && brd.setup)
        brd.setup();

    if (cfg_is_option('A', 'b')) {
        unsigned long initrd_addr = 0, initrd_len = 0;
        if (of) {
            initrd_addr = r3;
            initrd_len = r4;
            if (!initrd_addr) {
                if (prom_get_chosen("linux,initrd-start", &initrd_addr, sizeof(initrd_addr)) <= 0) {
                    VLVL(5, puts("Could not get initrd start"));
                }
                if (prom_get_chosen("linux,initrd-end", &initrd_len, sizeof(initrd_len)) <= 0) {
                    VLVL(5, puts("Could not get initrd end"));
                }
                if (initrd_len) initrd_len -= initrd_addr;
            }
        } else if (brd.initrd_start) {
            initrd_addr = brd.initrd_start;
            initrd_len = brd.initrd_end > brd.initrd_start ?
                         brd.initrd_end - brd.initrd_start : 0;
        } else {
            if (r4) {
                initrd_addr = r4;
                initrd_len = r5 > r4 ? r5 - r4 : 0;
            }
        }
        if (!initrd_addr) {
            initrd_addr = 0x600000;
            initrd_len = 0;
        }
        unsigned long avail;
        void *kicklist = boot_aos_zipkick((char *)initrd_addr, initrd_len, 1, &avail);
        if (!kicklist) goto error;

        char *args = "";
        if (of) {
            char *propname;
            prom_handle ph = prom_finddevice("/options");
            if (ph != PROM_INVALID_HANDLE) {
                propname = "os4_commandline";
            } else {
                ph = prom_finddevice("/chosen");
                propname = "bootargs";
            }
            int args_len = prom_getproplen(ph, propname);
            if (args_len > 0) {
                char *buf = brd.claim((void *)avail, args_len);
                if (buf) {
                    avail += args_len;
                    if (prom_getprop(ph, propname, buf, args_len) == args_len)
                      args = buf;
                }
            }
        } else if (brd.bootargs) {
            args = brd.bootargs;
        } else if (r6 && r7 > r6) {
            args = (char *)r6;
        }
        VLVL(3, puts("Starting exec"));
        if (brd.stack_top) {
            /*
             * Hand over on a stack near the top of RAM, which is where
             * U-Boot leaves it. QEMU's -kernel entry on sam460ex leaves
             * sp a few bytes below the address we load the AmigaOS
             * loader at, and the loader keeps using the sp it inherits
             * while allocating around its own image, so it ends up
             * writing over its own stack frames. Does not return.
             */
            register const char *a0 asm("r3") = "AmigaOS4";
            register void *a1 asm("r4") = kicklist;
            register void *a2 asm("r5") = brd.info;
            register char *a3 asm("r6") = args;
            asm volatile(
                "mr 1,%[sp]\n"
                "mtctr %[fn]\n"
                "bctrl\n"
                :
                : [sp] "r" (brd.stack_top), [fn] "r" (brd.exec_addr),
                  "r" (a0), "r" (a1), "r" (a2), "r" (a3)
                : "ctr", "lr", "memory");
        } else {
            ((loader_func)brd.exec_addr)("AmigaOS4", kicklist, brd.info, args);
        }
    }

    puts("Booting failed, exiting.");
error:
    if (of) prom_exit();
    return 0;
}
