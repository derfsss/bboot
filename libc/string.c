/*
 * String handling functions
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>

size_t strspn(const char *s, const char *a)
{
    int i;
    for (i = 0; s[i]; i++)
        if (!strchr(a, s[i])) break;
    return i;
}

size_t strcspn(const char *s, const char *r)
{
    int i;
    for (i = 0; s[i]; i++)
        if (strchr(r, s[i])) break;
    return i;
}
