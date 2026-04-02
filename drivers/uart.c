#include "uart.h"
#include <stdint.h>

// USART1 sur PC4 (TX) — SB13/SB15 ON sur MB1035-E02 → câblé au ST-LINK/V2-B
// Baudrate 115200 @ 8 MHz HSI : BRR = 8000000 / 115200 ≈ 69

#define RCC_BASE    0x40021000
#define GPIOC_BASE  0x48000800
#define USART1_BASE 0x40013800

#define RCC_AHBENR        (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB2ENR       (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_AHBENR_GPIOCEN    (1 << 19)
#define RCC_APB2ENR_USART1EN  (1 << 14)

#define GPIOC_MODER  (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_AFRL   (*(volatile uint32_t *)(GPIOC_BASE + 0x20))

#define USART1_CR1  (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_BRR  (*(volatile uint32_t *)(USART1_BASE + 0x0C))
#define USART1_ISR  (*(volatile uint32_t *)(USART1_BASE + 0x1C))
#define USART1_TDR  (*(volatile uint32_t *)(USART1_BASE + 0x28))

#define USART_ISR_TXE  (1 << 7)
#define USART_CR1_UE   (1 << 0)
#define USART_CR1_TE   (1 << 3)

void uart_init(void) {
    RCC_AHBENR  |= RCC_AHBENR_GPIOCEN;
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;

    // PC4 en Alternate Function, AF7 = USART1_TX
    GPIOC_MODER &= ~(0x3 << (4 * 2));
    GPIOC_MODER |=  (0x2 << (4 * 2));
    GPIOC_AFRL  &= ~(0xF << (4 * 4));
    GPIOC_AFRL  |=  (0x7 << (4 * 4));

    USART1_BRR = 625;  // 115200 @ 72 MHz
    USART1_CR1 = USART_CR1_TE | USART_CR1_UE;
}

void uart_putc(char c) {
    uint32_t t = 100000u;
    while (!(USART1_ISR & USART_ISR_TXE) && --t);
    USART1_TDR = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

void uart_print_int(int32_t n) {
    char buf[12];
    int  i = 0;

    if (n < 0) {
        uart_putc('-');
        uint32_t u = (n == (int32_t)0x80000000) ? (uint32_t)0x80000000 : (uint32_t)(-n);
        do { buf[i++] = '0' + (u % 10); u /= 10; } while (u);
    } else {
        uint32_t u = (uint32_t)n;
        do { buf[i++] = '0' + (u % 10); u /= 10; } while (u);
    }

    for (int j = i - 1; j >= 0; j--) uart_putc(buf[j]);
}
