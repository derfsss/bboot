/*
 * BBoor boot loader
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include "drivers/prom.h"
#include "drivers/console.h"
#include "bboot.h"

static char verstag[] = "\0$VER: BBoot " STRINGIFY(VERSION) "." STRINGIFY(REVISION) " (" BUILDDATE ")";

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

    console_init();
    putchar('\n');
    puts(&verstag[7]);

    pegasos2_init();
    boot_aos(r3, r4);

    puts("Booting failed, exiting.");
    prom_exit();
    return 0;
}
