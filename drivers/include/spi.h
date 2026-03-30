#ifndef SPI_H
#define SPI_H

#include <stdint.h>

void    spi_init(void);
void    spi_write(uint8_t reg, uint8_t data);
uint8_t spi_read(uint8_t reg);
// Lecture atomique de deux registres consécutifs via auto-incrément SPI
void    spi_read_multi(uint8_t reg, uint8_t *low, uint8_t *high);

#endif
