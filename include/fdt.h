/*
 * Minimal Flattened Device Tree parser for BBoot
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FDT_H
#define FDT_H

#include <stdint.h>

#define FDT_MAGIC 0xD00DFEED

int fdt_check(const void *fdt);
int fdt_get_prop_u32(const void *fdt, const char *node_name,
                     const char *prop_name, uint32_t *out);
int fdt_get_prop_u32_idx(const void *fdt, const char *node_name,
                         const char *prop_name, int idx, uint32_t *out);
int fdt_get_prop_str(const void *fdt, const char *node_name,
                     const char *prop_name, char *buf, int buflen);

#endif
