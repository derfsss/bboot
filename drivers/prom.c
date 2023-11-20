/*
 *  Open Firmware client interface routines for BBoot
 *  Copyright (c) 2023 BALATON Zoltan
 *
 *  Based on:
 *  prom.c - Routines for talking to the Open Firmware PROM
 *
 *  Copyright (C) 2001, 2002 Ethan Benson
 *
 *  Copyright (C) 1999 Benjamin Herrenschmidt
 *
 *  Copyright (C) 1999 Marius Vollmer
 *
 *  Copyright (C) 1996 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "prom.h"
#include "stdio.h"
#include "string.h"

#include "types.h"

ihandle prom_stdin, prom_stdout;

static ihandle prom_chosen, prom_options = PROM_INVALID_HANDLE;

struct prom_args {
    const char *service;
    int nargs;
    int nret;
    void *args[10];
} __attribute__((packed));

typedef int (*prom_entry)(struct prom_args *);

static prom_entry prom;

void *prom_cientry(void)
{
    return prom;
}

void *call_prom(const char *service, int nargs, int nret, ...)
{
    va_list list;
    int i;
    struct prom_args prom_args;

    prom_args.service = service;
    prom_args.nargs = nargs;
    prom_args.nret = nret;
    va_start(list, nret);
    for (i = 0; i < nargs; ++i)
        prom_args.args[i] = va_arg(list, void *);
    va_end(list);
    for (i = 0; i < nret; ++i)
        prom_args.args[i + nargs] = 0;
    prom(&prom_args);
    if (nret > 0)
        return prom_args.args[nargs];
    else
        return 0;
}

prom_handle prom_finddevice(char *name)
{
    return call_prom("finddevice", 1, 1, name);
}

prom_handle prom_findpackage(char *path)
{
    return call_prom("find-package", 1, 1, path);
}

prom_handle prom_child(prom_handle parent)
{
    return call_prom("child", 1, 1, parent);
}

prom_handle prom_parent(prom_handle child)
{
    return call_prom("parent", 1, 1, child);
}

prom_handle prom_peer(prom_handle ph)
{
    return call_prom("peer", 1, 1, ph);
}

int prom_nextprop(prom_handle pack, char *name, void *mem)
{
    return (int)call_prom("nextprop", 3, 1, pack, name, mem);
}

int prom_getprop(prom_handle pack, char *name, void *mem, int len)
{
    return (int)call_prom("getprop", 4, 1, pack, name, mem, len);
}

int prom_getproplen(prom_handle pack, const char *name)
{
    return (int)call_prom("getproplen", 2, 1, pack, name);
}

int prom_setprop(prom_handle pack, char *name, void *mem, int len)
{
    return (int)call_prom("setprop", 4, 1, pack, name, mem, len);
}

int prom_package_to_path(prom_handle pack, void *mem, int len)
{
    return (int)call_prom("package-to-path", 3, 1, pack, mem, len);
}

int prom_get_chosen(char *name, void *mem, int len)
{
    return prom_getprop(prom_chosen, name, mem, len);
}

int prom_get_options(char *name, void *mem, int len)
{
    if (prom_options == PROM_INVALID_HANDLE)
        return -1;
    return prom_getprop(prom_options, name, mem, len);
}

int prom_set_options(char *name, void *mem, int len)
{
    if (prom_options == PROM_INVALID_HANDLE)
        return -1;
    return prom_setprop(prom_options, name, mem, len);
}

void prom_init(void *pe)
{
    prom = pe;

    prom_chosen = prom_finddevice("/chosen");
    if (prom_chosen == (void *)-1)
        prom_exit();
    prom_options = prom_finddevice("/options");
    if (prom_get_chosen("stdout", &prom_stdout, sizeof(prom_stdout)) <= 0)
        prom_exit();

    prom_puts(prom_stdout, "\nOF interface initialized");
}

prom_handle prom_open(char *spec)
{
    return call_prom("open", 1, 1, spec, strlen(spec));
}

void prom_close(prom_handle file)
{
    call_prom("close", 1, 0, file);
}

void prom_putchar(char c)
{
    if (c == '\n')
        call_prom("write", 3, 1, prom_stdout, "\r\n", 2);
    else
        call_prom("write", 3, 1, prom_stdout, &c, 1);
}

void prom_puts(prom_handle file, char *s)
{
    const char *p, *q;

    for (p = s; *p != 0; p = q) {
        for (q = p; *q != 0 && *q != '\n'; ++q)
            /*NOP*/;
        if (q > p)
            call_prom("write", 3, 1, file, p, q - p);
        if (*q != 0) {
            ++q;
            call_prom("write", 3, 1, file, "\r\n", 2);
        }
    }
}

void prom_write(const char *s, unsigned int n)
{
    call_prom("write", 3, 1, prom_stdout, s, n);
}


void prom_exit()
{
    call_prom("exit", 0, 0);
}
#if 0
void prom_sleep(int seconds)
{
    int end;
    end = (prom_getms() + (seconds * 1000));
    while (prom_getms() <= end);
}

/* if address given is claimed look for other addresses to get the needed
 * space before giving up
 */
void *prom_claim_chunk(void *virt, unsigned int size)
{
    void *found, *addr;
    for (addr = virt;
         addr <= (void*)PROM_CLAIM_MAX_ADDR;
         addr += 0x100000 / sizeof(addr)) {
        found = call_prom("claim", 3, 1, addr, size, 0);
        if (found != (void *)-1)
            return(found);
    }
    return((void*)-1);
}

/* Start from top of memory and work down to get the needed space */
void *prom_claim_chunk_top(unsigned int size)
{
    void *found, *addr;
    for (addr = (void*)PROM_CLAIM_MAX_ADDR;
         addr >= (void *)size;
         addr -= 0x100000 / sizeof(addr)) {
        found = call_prom("claim", 3, 1, addr, size, 0);
        if (found != (void *)-1)
            return(found);
    }
    return((void*)-1);
}
#endif
void *prom_claim(void *virt, unsigned int size, unsigned int align)
{
    return call_prom("claim", 3, 1, virt, size, align);
}

void *prom_release(void *virt, unsigned int size)
{
    return call_prom("release", 2, 0, virt, size);
}

char *prom_getargs()
{
    static char args[256];
    int l;

    l = prom_get_chosen("bootargs", args, sizeof(args) - 1);
    args[l] = '\0';
    return args;
}

int prom_setargs(char *args)
{
    int l = strlen(args)+1;
    return (int)call_prom("setprop", 4, 1, prom_chosen, "bootargs", args, l);
}
#if 0
int prom_interpret(char *forth)
{
    return (int)call_prom("interpret", 1, 1, forth);
}

int prom_getms(void)
{
    return (int) call_prom("milliseconds", 0, 1);
}

int prom_next_node(prom_handle *nodep)
{
    prom_handle node = *nodep;
    if (node && (*nodep = prom_child(node)) != 0) return 1;
    if ((*nodep = prom_peer(node)) != 0) return 1;
    for (;;) {
        if ((node = prom_parent(node)) == 0) return 0;
        if ((*nodep = prom_peer(node)) != 0) return 1;
    }
}
#endif
