#include "spi.h"
#include <stdint.h>

// Broches SPI1 (UM1570, Table 7)
//   PA5 → SPI1_SCK   (AF5)
//   PA6 → SPI1_MISO  (AF5)
//   PA7 → SPI1_MOSI  (AF5)
//   PE3 → CS (GPIO output, actif bas)

#define RCC_BASE    0x40021000
#define GPIOA_BASE  0x48000000
#define GPIOE_BASE  0x48001000
#define SPI1_BASE   0x40013000

#define RCC_AHBENR   (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB2ENR  (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_AHBENR_GPIOAEN  (1 << 17)
#define RCC_AHBENR_GPIOEEN  (1 << 21)
#define RCC_APB2ENR_SPI1EN  (1 << 12)

#define GPIOA_MODER  (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRL   (*(volatile uint32_t *)(GPIOA_BASE + 0x20))

#define GPIOE_MODER  (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_BSRR   (*(volatile uint32_t *)(GPIOE_BASE + 0x18))

#define SPI1_CR1   (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI1_CR2   (*(volatile uint32_t *)(SPI1_BASE + 0x04))
#define SPI1_SR    (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI1_DR    (*(volatile uint8_t  *)(SPI1_BASE + 0x0C))

#define SPI_SR_TXE   (1 << 1)
#define SPI_SR_RXNE  (1 << 0)

#define CS_LOW()   (GPIOE_BSRR = (1 << (3 + 16)))
#define CS_HIGH()  (GPIOE_BSRR = (1 << 3))

void spi_init(void) {
    RCC_AHBENR  |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOEEN;
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;

    // PA5, PA6, PA7 en Alternate Function
    GPIOA_MODER &= ~((0x3 << (5*2)) | (0x3 << (6*2)) | (0x3 << (7*2)));
    GPIOA_MODER |=   (0x2 << (5*2)) | (0x2 << (6*2)) | (0x2 << (7*2));

    // AF5 sur PA5, PA6, PA7
    GPIOA_AFRL &= ~((0xF << (5*4)) | (0xF << (6*4)) | (0xF << (7*4)));
    GPIOA_AFRL |=   (0x5 << (5*4)) | (0x5 << (6*4)) | (0x5 << (7*4));

    // PE3 en output (CS)
    GPIOE_MODER &= ~(0x3 << (3*2));
    GPIOE_MODER |=  (0x1 << (3*2));
    CS_HIGH();

    // SPI1 : mode 3 (CPOL=1 CPHA=1), master, fPCLK/8 = 1MHz, SSM/SSI
    SPI1_CR1 = (1 << 1)    // CPOL = 1
             | (1 << 0)    // CPHA = 1
             | (1 << 2)    // MSTR
             | (0x2 << 3)  // BR = fPCLK/8
             | (1 << 9)    // SSM
             | (1 << 8);   // SSI

    // FRXTH=1 : seuil RXNE sur 8 bits
    SPI1_CR2 = (1 << 12);

    SPI1_CR1 |= (1 << 6);  // SPE
}

#define SPI_TIMEOUT 100000u

static uint8_t spi_transfer(uint8_t data) {
    uint32_t t;
    t = SPI_TIMEOUT;
    while (!(SPI1_SR & SPI_SR_TXE)  && --t);
    SPI1_DR = data;
    t = SPI_TIMEOUT;
    while (!(SPI1_SR & SPI_SR_RXNE) && --t);

    return SPI1_DR;
}

void spi_write(uint8_t reg, uint8_t data) {
    CS_LOW();
    spi_transfer(reg & 0x7F);  // bit7=0 → write
    spi_transfer(data);
    CS_HIGH();
}

uint8_t spi_read(uint8_t reg) {
    uint8_t result;
    CS_LOW();
    spi_transfer(reg | 0x80);  // bit7=1 → read
    result = spi_transfer(0x00);
    CS_HIGH();
    return result;
}

// Lecture atomique de deux registres consécutifs.
// bit7=1 (read) + bit6=1 (auto-incrément) : CS reste bas pendant les deux octets.
// Garantit que LOW et HIGH appartiennent au même sample gyro.
void spi_read_multi(uint8_t reg, uint8_t *low, uint8_t *high) {
    CS_LOW();
    spi_transfer(reg | 0xC0);    // read + auto-incrément
    *low  = spi_transfer(0x00);  // OUT_Z_L
    *high = spi_transfer(0x00);  // OUT_Z_H
    CS_HIGH();
}
