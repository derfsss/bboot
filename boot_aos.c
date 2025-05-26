/*
 * BBoot boot loader AmigaOS boot support
 * (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "aoslist.h"
#include "bboot.h"
#include "zip/zip.h"

#define KICKLIST_ADDR 0xec00000
#define KICKLAYOUT "Kickstart/Kicklayout"

typedef struct {
    node_t node;
    char *name;
    char *options;
    uint16_t id;
    uint16_t flags;
    void *alloc;
    void *dealloc;
    void *address;
    uint32_t size;
} module_t;

static void add_module(list_t *list, module_t *mod, void *data, unsigned long len, const char *name)
{
    char *n = (char *)mod + sizeof(*mod);
    strcpy(n, name);
    mod->name = n;
    mod->options = NULL;
    mod->id = 2;
    mod->flags = 3;
    mod->alloc = &mod->alloc;
    mod->dealloc = &mod->dealloc;
    mod->address = data;
    mod->size = len;
    alist_add_tail(list, (node_t *)mod);
}

/* Extract a module with path from zip and add it to list
 * module data is allocated from data or from avail if data is NULL
 * module struct allocated from avail or after data if data is NULL
 * returns number of bytes claimed in kicklist or 0 on errot
 */
static unsigned long load_module_from_zip(zip_t *zip, char *path, void *data, void *avail, list_t *list)
{
    char *name = strrchr(path, '/');
    if (name) name++; else name = path;
    int nlen = strlen(name);
    if (!nlen) {
        puts("Invalid file name");
        return 0;
    }
    if (vlvl == 1)
        putchar('.');
    else if (vlvl > 1)
        printf("Loading %s\n", name);
    if (zip_file_find(zip, path)) {
        printf("Could not find file '%s'\n", path);
        return 0;
    }
    module_t *mod;
    unsigned long clen = zip_file_uncompressedSize(zip);
    if (!data) {
        clen = ROUND_UP(clen, 4);
        mod = avail + clen;
        clen += sizeof(*mod) + nlen + 1;
        clen = ROUND_UP(clen, 32);
    }
    void *bin = brd.claim((data ?: avail), clen);
    if (!bin) {
        puts("Could not allocate memory");
        return 0;
    }
    if (zip_file_extract(zip, bin)) {
        puts("Could not extract file");
        return 0;
    }
    if (data) {
        clen = sizeof(*mod) + nlen + 1;
        clen = ROUND_UP(clen, 32);
        mod = brd.claim(avail, clen);
        if (!mod) {
            puts("Could not allocate memory");
            return 0;
        }
    }
    add_module(list, mod, bin, zip_file_uncompressedSize(zip), name);
    if (data) mod->id = 1;
    return clen;
}

#define KEYWORD(p, str, len) (!strncmp(p, str, len) && (p[len] == ' ' || p[len] == '\t'))
#define SPCORTAB(p) (*p == ' ' || *p == '\t')

void *boot_aos_zipkick(const char *zipdata, unsigned long ziplen, int config, unsigned long *avail_ret)
{
    if (!ziplen) ziplen = zip_findLen(zipdata, 16 * 1024 * 1024);
    VLVL(3, printf("Checking initrd at 0x%lx-0x%lx (%lu bytes)\n", (unsigned long)zipdata,
                   (unsigned long)zipdata + ziplen, ziplen));
    if (!zipdata || !ziplen) {
        puts("Missing initrd");
        return NULL;
    }
    zip_t z = {};
    if (zip_openBuffer(&z, zipdata, ziplen)) {
        puts("Invalid zip file");
        return NULL;
    }
    VLVL(2, printf("Found zip with %lu entries\n", zip_numEntries(&z)));

    /* Find and extract Kicklayout */
    if (zip_file_find(&z, KICKLAYOUT)) {
        printf("Could not find file '%s'\n", KICKLAYOUT);
        return NULL;
    }
    /* Assume we have free space after the zip */
    unsigned long avail = (unsigned long)zipdata + ziplen;
    unsigned long kicklayout_len = zip_file_uncompressedSize(&z);
    char *kicklayout = brd.claim((void *)avail, kicklayout_len + 2);
    if (!kicklayout) {
        puts("Could not allocate memory");
        return NULL;
    }
    if (zip_file_extract(&z, kicklayout)) {
        puts("Could not extract file");
        return NULL;
    }
    kicklayout[kicklayout_len] = '\n';
    kicklayout[kicklayout_len + 1] = '\0';

    char *p = kicklayout;
    VLVL(3, printf("Parsing Kicklayout at %p (%lu bytes)\n", p, kicklayout_len));
    avail = KICKLIST_ADDR;
    list_t *kicklist = NULL;
    unsigned long inc;
    char *q, *end;
    int c, l;
    for (c = 0, l = 1;
         p && p - kicklayout < (int)kicklayout_len;
         l++, p = end + 1) {
        if (*p == '\0') break;
        while (SPCORTAB(p) || *p == '\r') p++;
        end = strchr(p, '\n');
        if (!end) goto error;
        if (*p == ';' || *p == '\n') continue;
        *end = '\0';
        for (q = end - 1; q > p && (SPCORTAB(q) || *q == '\r'); q--) /*NOP*/;
        if (++q < end) *q = '\0';
        if (KEYWORD(p, "LABEL", 5)) {
            if (++c == config) {
                for (p += 5; SPCORTAB(p); p++) /*NOP*/;
                VLVL(1, printf("Booting config %d: %s\n", c, p));
                kicklist = brd.claim((void *)avail, sizeof(*kicklist));
                if (!kicklist) {
                    puts("Could not allocate memory");
                    return NULL;
                }
                avail += sizeof(*kicklist);
                alist_init(kicklist);
                continue;
            }
            if (c > config) break;
        }
        if (c != config) continue;
        if (kicklist->l_head->n_succ == NULL) {
            /* Look for EXEC first before MODULEs*/
            if (KEYWORD(p, "EXEC", 4)) {
                for (p += 4; SPCORTAB(p); p++) /*NOP*/;
                inc = load_module_from_zip(&z, p, brd.exec_addr, (void *)avail, kicklist);
                if (!inc) {
                    puts("Could not load module");
                    return NULL;
                }
                avail += inc;
            } else goto error;
        } else {
            if (KEYWORD(p, "MODULE", 6)) {
                for (p += 6; SPCORTAB(p); p++) /*NOP*/;
                inc = load_module_from_zip(&z, p, NULL, (void *)avail, kicklist);
                if (!inc) {
                    puts("Could not load module");
                    return NULL;
                }
                avail += inc;
            } else goto error;
        }
    }
    if (!kicklist || kicklist->l_head->n_succ == NULL) {
        puts("Could not find config in Kicklayout");
        return NULL;
    }
    if (avail_ret) *avail_ret = avail;
    return kicklist;
error:
    printf("Error in Kicklayout line %u\n", l);
    return NULL;
}
