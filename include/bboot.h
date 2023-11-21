/*
 * BBoor boot loader
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BBOOT_H
#define BBOOT_H

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

/* These require d to be a power of 2 */
#define ROUND_DOWN(n, d) ((n) & -(0 ? (n) : (d)))
#define ROUND_UP(n, d) ROUND_DOWN((n) + (d) - 1, (d))

#if _BIG_ENDIAN
#define BE16(x) (x)
#define LE16(x) __builtin_bswap16(x)
#define BE32(x) (x)
#define LE32(x) __builtin_bswap32(x)
#else
#define BE16(x) __builtin_bswap16(x)
#define LE16(x) (x)
#define BE32(x) __builtin_bswap32(x)
#define LE32(x) (x)
#endif

extern int vlvl; /* verbosity level */
#define VLVL(l, x) do if (vlvl >= l) { x; } while(0)

typedef struct {
    void *(*claim)(void *addr, unsigned int size);
    void (*setup)(void);
    void *info;
    void *exec_addr;
    int serial_port;
} brd_t;

extern brd_t brd;

void cfg_init(void);
char *cfg_get_option(const char s);
int cfg_is_option(const char s, const char o);
void *boot_aos_zipkick(const char *zipdata, unsigned long ziplen, int config, unsigned long *avail_ret);
void pegasos2_init(void);

#endif
