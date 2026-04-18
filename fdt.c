/*
 * Minimal Flattened Device Tree parser for BBoot
 * Only supports reading properties from top-level nodes.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include "bboot.h"
#include "fdt.h"

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

#define ALIGN4(x) (((x) + 3) & ~3)

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

static inline uint32_t fdt32(const void *p)
{
    return BE32(*(const uint32_t *)p);
}

int fdt_check(const void *fdt)
{
    return fdt32(fdt) == FDT_MAGIC ? 0 : -1;
}

/* Find a property in a named node (one level below root).
 * Returns property data pointer and sets *lenp, or NULL if not found.
 */
static const void *fdt_find_prop(const void *fdt, const char *node_name,
                                 const char *prop_name, int *lenp)
{
    const struct fdt_header *h = fdt;
    uint32_t off_struct = fdt32(&h->off_dt_struct);
    uint32_t off_strings = fdt32(&h->off_dt_strings);
    const char *strings = (const char *)fdt + off_strings;
    const uint32_t *p = (const uint32_t *)((const char *)fdt + off_struct);
    int depth = 0;
    int in_target = 0;

    while (1) {
        uint32_t tag = fdt32(p);
        p++;
        switch (tag) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)p;
            int nlen = strlen(name);
            p = (const uint32_t *)((const char *)p + ALIGN4(nlen + 1));
            depth++;
            if (depth == 2 && !strcmp(name, node_name))
                in_target = 1;
            break;
        }
        case FDT_END_NODE:
            if (depth == 2 && in_target)
                return NULL; /* property not found in target node */
            depth--;
            in_target = 0;
            if (depth < 0) return NULL;
            break;
        case FDT_PROP: {
            uint32_t len = fdt32(p); p++;
            uint32_t nameoff = fdt32(p); p++;
            const void *data = p;
            p = (const uint32_t *)((const char *)p + ALIGN4(len));
            if (in_target && !strcmp(strings + nameoff, prop_name)) {
                if (lenp) *lenp = len;
                return data;
            }
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            return NULL;
        default:
            return NULL;
        }
    }
}

int fdt_get_prop_u32(const void *fdt, const char *node_name,
                     const char *prop_name, uint32_t *out)
{
    int len = 0;
    const void *data = fdt_find_prop(fdt, node_name, prop_name, &len);
    if (!data || len < 4) return -1;
    *out = fdt32(data);
    return 0;
}

int fdt_get_prop_u32_idx(const void *fdt, const char *node_name,
                         const char *prop_name, int idx, uint32_t *out)
{
    int len = 0;
    const void *data = fdt_find_prop(fdt, node_name, prop_name, &len);
    if (!data || len < (int)((idx + 1) * sizeof(uint32_t))) return -1;
    *out = fdt32((const char *)data + idx * sizeof(uint32_t));
    return 0;
}

int fdt_get_prop_str(const void *fdt, const char *node_name,
                     const char *prop_name, char *buf, int buflen)
{
    int len = 0;
    const void *data = fdt_find_prop(fdt, node_name, prop_name, &len);
    if (!data || len < 1) return -1;
    int copy = len < buflen ? len : buflen - 1;
    memcpy(buf, data, copy);
    buf[copy] = '\0';
    return copy;
}
