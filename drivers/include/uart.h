#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_print_int(int32_t n);
int  uart_rx_ready(void);
char uart_getc(void);

#endif
