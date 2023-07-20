/*
 * AmigaOS boot support for bboot
 * (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "aoslist.h"
#include "bboot.h"
#include "drivers/prom.h"
#include "zip/zip.h"

#define EXEC_ADDR 0x400000
#define KICKLIST_ADDR 0xec00000

typedef void (*loader_func)(const char *id, void *kicklist, void *ofci, char *cmdline);

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
    printf("Loading %s\n", name);
    if (zip_file_find(zip, path)) {
        puts("Could not find file");
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
    void *bin = prom_claim((data ?: avail), clen, 0);
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
        mod = prom_claim(avail, clen, 0);
        if (!mod) {
            puts("Could not allocate memory");
            return 0;
        }
    }
    add_module(list, mod, bin, zip_file_uncompressedSize(zip), name);
    if (data) mod->id = 1;
    return clen;
}

static int boot_aos_zipkick(const char *zipdata, unsigned long ziplen, int config)
{
    zip_t z = {};
    if (zip_openBuffer(&z, zipdata, ziplen)) {
        puts("Invalid zip file");
        return 1;
    }
    printf("Found zip with %lu entries\n", zip_numEntries(&z));

    /* Find and extract Kicklayout */
    if (zip_file_find(&z, "Kickstart/Kicklayout")) {
        puts("Could not find Kicklayout");
        return 1;
    }
    /* Assume we have free space after the zip */
    unsigned long avail = (unsigned long)zipdata + ziplen;
    unsigned long kicklayout_len = zip_file_uncompressedSize(&z);
    char *kicklayout = prom_claim((void *)avail, kicklayout_len + 1, 0);
    if (!kicklayout) {
        puts("Could not allocate memory");
        return 1;
    }
    if (zip_file_extract(&z, kicklayout)) {
        puts("Could not extract file");
        return 1;
    }
    kicklayout[kicklayout_len] = '\0';

    char *p = kicklayout;
    printf("Parsing Kicklayout at %p (%lu bytes)\n", p, kicklayout_len);
    avail = KICKLIST_ADDR;
    list_t *kicklist = NULL;
    unsigned long inc;
    char *end;
    int c, l;
    for (c = 0, l = 1;
         p && p - kicklayout < (int)kicklayout_len;
         l++, p = end + 1) {
        if (*p == '\0') break;
        while (*p == ' ' || *p == '\t') p++;
        end = strchr(p, '\n');
        if (!end) goto error;
        if (*p == ';' || *p == '\n') continue;
        *end = '\0';
        if (!strncmp(p, "LABEL ", 6)) {
            if (++c == config) {
                printf("Booting config %d: %s\n", c, &p[6]);
                kicklist = prom_claim((void *)avail, sizeof(*kicklist), 0);
                if (!kicklist) {
                    puts("Could not allocate memory");
                    return 1;
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
            if (!strncmp(p, "EXEC ", 5)) {
                for (p += 4; *p == ' ' || *p == '\t'; p++) /*NOP*/;
                inc = load_module_from_zip(&z, p, (void *)EXEC_ADDR, (void *)avail, kicklist);
                if (!inc) {
                    puts("Could not load module");
                    return 1;
                }
                avail += inc;
            } else goto error;
        } else {
            if (!strncmp(p, "MODULE ", 7)) {
                for (p += 6; *p == ' ' || *p == '\t'; p++) /*NOP*/;
                inc = load_module_from_zip(&z, p, NULL, (void *)avail, kicklist);
                if (!inc) {
                    puts("Could not load module");
                    return 1;
                }
                avail += inc;
            } else goto error;
        }
    }
    if (!kicklist || kicklist->l_head->n_succ == NULL) {
        puts("Could not find config in Kicklayout");
        return 1;
    }

    char *propname, *args = "";
    prom_handle ph = prom_finddevice("/options");
    if (ph != PROM_INVALID_HANDLE) {
        propname = "os4_commandline";
    } else {
        ph = prom_finddevice("/chosen");
        propname = "bootargs";
    }
    int args_len = prom_getproplen(ph, propname);
    if (args_len > 0) {
        char *buf = prom_claim((void *)avail, args_len, 0);
        if (buf) {
            avail += args_len;
            if (prom_getprop(ph, propname, buf, args_len) == args_len)
                args = buf;
        }
    }

    puts("Starting exec");
    ((loader_func)EXEC_ADDR)("AmigaOS4", kicklist, prom_cientry(), args);
    return 0;
error:
    printf("Error in Kicklayout line %u\n", l);
    return 1;
}

int boot_aos(unsigned long initrd_addr, unsigned long initrd_len)
{
    if (!initrd_addr) {
        if (prom_get_chosen("linux,initrd-start", &initrd_addr, sizeof(initrd_addr)) <= 0) {
            puts("Cannot get inird start");
            prom_exit();
        }
        if (prom_get_chosen("linux,initrd-end", &initrd_len, sizeof(initrd_len)) <= 0) {
            puts("Cannot get inird end");
            prom_exit();
        }
        initrd_len -= initrd_addr;
    }
    if (!initrd_addr || !initrd_len) {
        puts("Missing initrd");
        return 1;
    }
    printf("Checking initrd at 0x%lx-0x%lx (%lu bytes)\n", initrd_addr, initrd_addr + initrd_len, initrd_len);
    return boot_aos_zipkick((char *)initrd_addr, initrd_len, 1);
}
