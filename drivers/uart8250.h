#ifndef UART8250_H
#define UART8250_H

void uart_init(int idx);
void uart_tx_byte(int idx, unsigned char data);
unsigned char uart_rx_byte(int idx);
void uart_tx_flush(int idx);

#endif
