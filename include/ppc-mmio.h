/*
 * PPC MMIO routines
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_MMIO_H
#define PPC_MMIO_H

#include <types.h>

static inline uint8_t read8(unsigned long addr)
{
    const volatile uint8_t *ad = (volatile uint8_t *)addr;
    uint8_t ret;
    asm volatile ("lbz%U1%X1 %0,%1; eieio":"=r"(ret):"m"(*ad));
    return ret;
}

static inline void write8(unsigned long addr, uint8_t val)
{
    volatile uint8_t *ad = (volatile uint8_t *)addr;
    asm volatile ("stb%U0%X0 %1,%0; eieio":"=m"(*ad):"r"(val));
}

static inline uint16_t read16(unsigned long addr)
{
    const volatile uint16_t *ad = (volatile uint16_t *)addr;
    uint16_t ret;
    asm volatile ("lhz%U1%X1 %0,%1; eieio":"=r"(ret):"m"(*ad));
    return ret;
}

static inline void write16(unsigned long addr, uint16_t val)
{
    volatile uint16_t *ad = (volatile uint16_t *)addr;
    asm volatile ("sth%U0%X0 %1,%0; eieio":"=m"(*ad):"r"(val));
}

static inline uint16_t read16_le(unsigned long addr)
{
    const volatile uint16_t *ad = (volatile uint16_t *)addr;
    uint16_t ret;
    asm volatile ("lhbrx %0,0,%1; eieio":"=r"(ret):"r"(ad),"m"(*ad));
    return ret;
}

static inline void write16_le(unsigned long addr, uint16_t val)
{
    volatile uint16_t *ad = (volatile uint16_t *)addr;
    asm volatile ("sthbrx %1,0,%2; eieio":"=m"(*ad):"r"(val),"r"(ad));
}

static inline uint32_t read32(unsigned long addr)
{
    const volatile uint32_t *ad = (volatile uint32_t *)addr;
    uint32_t ret;
    asm volatile ("lwz%U1%X1 %0,%1; eieio":"=r"(ret):"m"(*ad));
    return ret;
}

static inline void write32(unsigned long addr, uint32_t val)
{
    volatile uint32_t *ad = (volatile uint32_t *)addr;
    asm volatile ("stw%U0%X0 %1,%0; eieio":"=m"(*ad):"r"(val));
}

static inline uint32_t read32_le(unsigned long addr)
{
    const volatile uint32_t *ad = (volatile uint32_t *)addr;
    uint32_t ret;
    asm volatile ("lwbrx %0,0,%1; eieio":"=r"(ret):"r"(ad),"m"(*ad));
    return ret;
}

static inline void write32_le(unsigned long addr, uint32_t val)
{
    volatile uint32_t *ad = (volatile uint32_t *)addr;
    asm volatile ("stwbrx %1,0,%2; eieio":"=m"(*ad):"r"(val),"r"(ad));
}

#endif
