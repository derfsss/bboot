/*
 * BBoot boot loader serial driver
 * Copyright (c) 2023 BALATON Zoltan
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UART8250_H
#define UART8250_H

void uart_init(unsigned int base);
void uart_tx_byte(unsigned int base, unsigned char data);
unsigned char uart_rx_byte(unsigned int base);
void uart_tx_flush(unsigned int base);

#endif
