/*
 * BBoor config handling
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include "drivers/prom.h"

/* Configuration is done by an option string read from /options/bboot
 * The configuration string consists of space separated list of settings
 * denoted by capital letters that can be followed by lower case option letters.
 * Valid settings an their options are:
 * O_utput: f_irmware, s_erial
 * V_erbose level: 0-9, 0: errors only. 9: max
 */

#define DEFAULT_OPTS "Os V5"

static char options[128];

void cfg_init(void)
{
    if (prom_get_options("bboot", options, sizeof(options) - 1) <= 0)
        strcpy(options, DEFAULT_OPTS);
    int i, n = strspn(options, " \t");
    if (n) memmove(options, &options[n], strlen(options) - n + 1);
    n = strlen(options) - 1;
    for (i = n; i && (options[i] == ' ' || options[i] == '\t' || options[i] == '\r' || options[i] == '\n'); i--)
        /*NOP*/;
    if (i != n) options[i+1] = '\0';
}

/* Return pointer to options of setting s */
char *cfg_get_option(const char s)
{
    char *ret = strchr(options, s);
    return (ret ? ret + 1 : NULL);
}

/* Check that setting s has option letter o */
int cfg_is_option(const char s, const char o)
{
    char *p = strchr(options, s);
    if (!p) return 0;
    int i, n = strcspn(p, " \t");
    for (i = 0; i < n; i++, p++)
        if (*p == o) return 1;
    return 0;
}
