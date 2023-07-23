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

#define SERIAL_PORT 1

int vlvl;

static void serial_write(const char *s, unsigned int n)
{
    unsigned int i;
    for (i= 0; i < n; i++)
        uart_tx_byte(SERIAL_PORT, *s++);
}

static struct console_output_driver serial_consout = {
    .write = &serial_write
};

static struct console_output_driver prom_consout = {
    .write = &prom_write
};

int start(unsigned long r3, unsigned long r4, unsigned long r5)
{
    prom_init((void *)r5);
    prom_handle ph = prom_finddevice("/");
    char model[32] = {};
    prom_getprop(ph, "model", model, sizeof(model));
    if (strcmp(model, "Pegasos2") != 0) {
        prom_puts(prom_stdout, "Unknown machine");
        prom_exit();
    }
    cfg_init();
    if (cfg_is_option('O', 'f')) {
        console_add_output_driver(&prom_consout);
    }
    if (cfg_is_option('O', 's')) {
        uart_init(SERIAL_PORT);
        console_add_output_driver(&serial_consout);
    }
    putchar('\n');
    puts(&verstag[7]);

    char *p = cfg_get_option('V');
    if (p && *p >= '0' && *p <= '9') vlvl = *p - '0';

    if (cfg_is_option('A', 'p'))
        pegasos2_init();
    if (cfg_is_option('A', 'b'))
        boot_aos(r3, r4);

    puts("Booting failed, exiting.");
    prom_exit();
    return 0;
}
