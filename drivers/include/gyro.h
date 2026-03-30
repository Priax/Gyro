#ifndef GYRO_H
#define GYRO_H

#include <stdint.h>

// Retourne 1 si le gyro répond correctement, 0 sinon
int gyro_init(void);

// Retourne 1 si une nouvelle mesure Z est disponible (STATUS_REG.ZRDY)
int gyro_data_ready(void);

// Lit la vitesse angulaire sur l'axe Z (en raw, signé 16 bits)
int16_t gyro_read_z(void);

#endif
