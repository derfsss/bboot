/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2008 Advanced Micro Devices, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "console.h"
#include "uart8250.h"

#define SERIAL_PORT 1

void serial_putchar(unsigned int c)
{
	uart_tx_byte(SERIAL_PORT, c);
}

static struct console_output_driver serial_consout = {
	.putchar = &serial_putchar
};

struct console_output_driver *console_out;

void console_add_output_driver(struct console_output_driver *out)
{
	out->next = console_out;
	console_out = out;
}

void console_init(void)
{
	uart_init(SERIAL_PORT);
	console_add_output_driver(&serial_consout);
}

static void device_putchar(unsigned char c)
{
	struct console_output_driver *out;
	for (out = console_out; out != 0; out = out->next)
		out->putchar(c);
}

int putchar(int c)
{
	c &= 0xff;
	if (c == '\n')
		device_putchar('\r');
	device_putchar(c);
	return c;
}

int puts(const char *s)
{
	int n = 0;

	while (*s) {
		putchar(*s++);
		n++;
	}

	putchar('\n');
	return n + 1;
}
