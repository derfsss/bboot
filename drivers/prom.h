/*
 *  prom.h - Routines for talking to the Open Firmware PROM
 *
 *  Copyright (C) 2001 Ethan Benson
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

#ifndef PROM_H
#define PROM_H

#include "stdarg.h"

typedef void *prom_handle;
typedef void *ihandle;
typedef void *phandle;

#define PROM_INVALID_HANDLE	((prom_handle)-1UL)
#define PROM_CLAIM_MAX_ADDR	0x10000000

extern void prom_init(void *pe);
void *prom_cientry(void);

/* I/O */

extern prom_handle prom_stdin;
extern prom_handle prom_stdout;

prom_handle prom_open(char *spec);
void prom_close(prom_handle file);

void prom_putchar(char);
void prom_puts(prom_handle file, char *s);

/* memory */

//void *prom_claim_chunk(void *virt, unsigned int size);
//void *prom_claim_chunk_top(unsigned int size);
void *prom_claim(void *virt, unsigned int size, unsigned int align);
void *prom_release(void *virt, unsigned int size);
void prom_print_available(char *prop);

/* packages and device nodes */

prom_handle prom_finddevice(char *name);
prom_handle prom_findpackage(char *path);
prom_handle prom_child(prom_handle parent);
prom_handle prom_parent(prom_handle child);
prom_handle prom_peer(prom_handle ph);
int prom_nextprop(prom_handle pack, char *name, void *mem);
int prom_getprop(prom_handle dev, char *name, void *buf, int len);
int prom_setprop(prom_handle dev, char *name, void *buf, int len);
int prom_getproplen(prom_handle, const char *);
int prom_package_to_path(prom_handle pack, void *mem, int len);
//int prom_next_node(prom_handle *nodep);

/* misc */

char *prom_getargs(void);
int prom_setargs(char *args);

void prom_exit(void);
//void prom_sleep(int seconds);

//int prom_interpret(char *forth);

int prom_get_chosen(char *name, void *mem, int len);
int prom_get_options(char *name, void *mem, int len);
int prom_set_options(char *name, void *mem, int len);

//int prom_getms(void);

void *call_prom(const char *service, int nargs, int nret, ...);

#endif
