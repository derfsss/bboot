/*
 * BBoot boot loader
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.globl _start;
_start:
#ifdef TARGET_G5
	/*
	 * PPC970 (G5) resets in 64-bit mode (MSR[SF]=1). bboot, the AmigaOS
	 * loader, and the 32-bit execsg kernel body all assume 32-bit-wraparound
	 * addressing - e.g. printf_core's switch jump table does base+signed_offset
	 * that only lands on the right target when effective addresses wrap at
	 * 2^32 (in 64-bit mode the add carries into bit 32 and branches to a bogus
	 * 0x1_xxxxxxxx address). Drop MSR[SF] here so the whole boot chain runs in
	 * 32-bit mode. This binary (bboot-g5) is only ever entered on a 970.
	 */
	mfmsr	9
	clrldi	9,9,1		/* MSR[SF] = 0 -> 32-bit mode */
	mtmsrd	9
	isync
#endif
	/* Make sure we have a stack */
	cmpwi 0,1,0
	bne 1f
	lis 1,__executable_start@h
	ori 1,1,__executable_start@l
	addi 1,1,-8
1:	mr 8,2
	lis 2,__toc_start@h
	ori 2,2,__toc_start@l
	b start
