/*
 * BBoor boot loader
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
        uart_tx_byte(brd.serial_port, *s++);
}

static struct console_output_driver serial_consout = {
    .write = &serial_write
};

static struct console_output_driver prom_consout = {
    .write = &prom_write
};

int start(unsigned long r3, unsigned long r4, unsigned long r5)
{
    if (r5) {
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
    } else {
        goto error;
    }
    cfg_init();
    if (cfg_is_option('O', 'f')) {
        console_add_output_driver(&prom_consout);
    }
    if (cfg_is_option('O', 's')) {
        uart_init(brd.serial_port);
        console_add_output_driver(&serial_consout);
    }
    putchar('\n');
    puts(&verstag[7]);

    char *p = cfg_get_option('V');
    if (p && *p >= '0' && *p <= '9') vlvl = *p - '0';

    if (cfg_is_option('A', 'p') && brd.setup)
        brd.setup();

    if (cfg_is_option('A', 'b')) {
        unsigned long initrd_addr, initrd_len;
        if (r5) {
            initrd_addr = r3;
            initrd_len = r4;
            if (!initrd_addr) {
                if (prom_get_chosen("linux,initrd-start", &initrd_addr, sizeof(initrd_addr)) <= 0) {
                    puts("Cannot get inird start");
                    goto error;
                }
                if (prom_get_chosen("linux,initrd-end", &initrd_len, sizeof(initrd_len)) <= 0) {
                    puts("Cannot get inird end");
                    goto error;
                }
                initrd_len -= initrd_addr;
            }
        }
        unsigned long avail;
        void *kicklist = boot_aos_zipkick((char *)initrd_addr, initrd_len, 1, &avail);
        if (!kicklist) goto error;

        char *args = "";
        if (r5) {
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
        }
        VLVL(3, puts("Starting exec"));
        ((loader_func)brd.exec_addr)("AmigaOS4", kicklist, brd.info, args);
    }

    puts("Booting failed, exiting.");
error:
    if (r5) prom_exit();
    return 0;
}
