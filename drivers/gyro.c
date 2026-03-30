#include "gyro.h"
#include "spi.h"

// Registres I3G4250D
#define WHO_AM_I   0x0F   // Doit retourner 0xD3
#define CTRL_REG1  0x20
#define CTRL_REG4  0x23
#define STATUS_REG 0x27
#define OUT_Z_L    0x2C
#define OUT_Z_H    0x2D

#define STATUS_ZRDY (1 << 2)  // bit 2 : nouvelle donnée Z disponible

int gyro_init(void) {
    if (spi_read(WHO_AM_I) != 0xD3) {
        return 0;
    }

    // CTRL_REG1 = 0xFF :
    //   DR[1:0] = 11 → ODR 800 Hz (au lieu de 100 Hz)
    //   BW[1:0] = 11 → Bandwidth 110 Hz
    //   PD=1, Zen=Yen=Xen=1
    // Plus de samples par seconde = intégration plus précise
    spi_write(CTRL_REG1, 0xFF);

    // CTRL_REG4 = 0x80 :
    //   BDU=1 → Block Data Update : le gyro ne met à jour OUT_Z_H
    //           qu'après que OUT_Z_L a été lu, évitant les valeurs à cheval
    //           sur deux cycles (même sans auto-incrément)
    //   FS = 00 → ±245 dps
    spi_write(CTRL_REG4, 0x80);

    return 1;
}

// Retourne 1 si une nouvelle mesure Z est prête (STATUS_REG.ZRDY)
// Le bit est effacé automatiquement à la lecture de OUT_Z_H (avec BDU=1)
int gyro_data_ready(void) {
    return (spi_read(STATUS_REG) & STATUS_ZRDY) != 0;
}

// Lecture atomique : CS reste bas pendant OUT_Z_L et OUT_Z_H
// → les deux octets appartiennent garantiment au même sample
int16_t gyro_read_z(void) {
    uint8_t low, high;
    spi_read_multi(OUT_Z_L, &low, &high);
    return (int16_t)((high << 8) | low);
}
