	.globl _start;
_start:
	lis 2,__toc_start@h
	ori 2,2,__toc_start@l
	b start
