/*
 * BBoor boot loader console driver
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include "console.h"

static struct console_output_driver *console_out;

void console_add_output_driver(struct console_output_driver *out)
{
    out->next = console_out;
    console_out = out;
}

int putchar(int c)
{
    char cc = c;
    struct console_output_driver *out;
    for (out = console_out; out; out = out->next) {
        if (cc == '\n')
            out->write("\r\n", 2);
        else
            out->write(&cc, 1);
    }
    return cc;
}

int puts(const char *s)
{
    int n = strlen(s);

    struct console_output_driver *out;
    for (out = console_out; out; out = out->next) {
        out->write(s, n);
        out->write("\r\n", 2);
    }
    return n + 1;
}

void putchars(const char *s, size_t n)
{
    struct console_output_driver *out;
    for (out = console_out; out; out = out->next)
        out->write(s, n);
}
