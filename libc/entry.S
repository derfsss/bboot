/*
 * BBoot boot loader
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

	.globl _start;
_start:
	/* Make sure we have a stack */
	cmpwi 0,1,0
	bne 1f
	lis 1,__executable_start@h
	ori 1,1,__executable_start@l
	addi 1,1,-8
1:	lis 2,__toc_start@h
	ori 2,2,__toc_start@l
	b start
