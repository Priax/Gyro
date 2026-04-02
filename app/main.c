#include <stdint.h>
#include "spi.h"
#include "gyro.h"
#include "uart.h"
#include "scheduler.h"

void systick_init(void);

// LEDs PE8-PE15 dans l'ordre circulaire physique sur la carte
#define RCC_BASE   0x40021000
#define GPIOE_BASE 0x48001000

#define RCC_AHBENR  (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define GPIOE_MODER (*(volatile uint32_t *)(GPIOE_BASE + 0x00))
#define GPIOE_ODR   (*(volatile uint32_t *)(GPIOE_BASE + 0x14))
#define RCC_AHBENR_GPIOEEN (1 << 21)

static const int led_pins[8] = {9, 10, 11, 12, 13, 14, 15, 8};

// Paramètres d'intégration
//
// ODR = 800 Hz → période d'échantillonnage T = 1/800 = 1.25 ms
//
// Sensibilité I3G4250D à ±245 dps : 8.75 mdps/LSB
// Vitesse réelle (dps) = gz_raw * 0.00875
//
//   angle par sample = gz_raw * 8.75 [mdps/LSB] * 1.25 [ms/sample]
//                    = gz_raw * 10.9375 µdeg/sample
//                    = gz_raw * 875 / 80000  mdeg/sample
#define DEAD_ZONE        100
#define ANGLE_FULL_TURN  360000 // milli-degrés

static int32_t angle_mdeg = 0;
static int16_t gz = 0;

static void leds_init(void) {
    RCC_AHBENR |= RCC_AHBENR_GPIOEEN;
    for (int i = 8; i <= 15; i++) {
        GPIOE_MODER &= ~(0x3 << (i * 2));
        GPIOE_MODER |=  (0x1 << (i * 2));
    }
    GPIOE_ODR &= ~(0xFF << 8);
}

static void led_show(int index) {
    GPIOE_ODR &= ~(0xFF << 8);
    GPIOE_ODR |= (1 << led_pins[index & 7]);
}

static void leds_blink_error(void) {
    for (int i = 0; i < 6; i++) {
        GPIOE_ODR |=  (0xFF << 8);
        for (volatile uint32_t n = 300000; n--; );
        GPIOE_ODR &= ~(0xFF << 8);
        for (volatile uint32_t n = 300000; n--; );
    }
}

void gyro_task(void) {
    if (!gyro_data_ready())
        return;

    gz = gyro_read_z();

    if (gz > -DEAD_ZONE && gz < DEAD_ZONE)
        gz = 0;

    angle_mdeg += (int32_t)gz * 875 / 80000;

    angle_mdeg %= ANGLE_FULL_TURN;
    if (angle_mdeg < 0) angle_mdeg += ANGLE_FULL_TURN;
}

void led_task(void) {
    int index = angle_mdeg / (ANGLE_FULL_TURN / 8);
    led_show(index);
}

void uart_task(void) {
    uart_puts("gz (negatif = sens anti horaire): ");
    uart_print_int(gz);
    uart_puts("\t");
    uart_puts("angle: ");
    uart_print_int(angle_mdeg / 1000);
    uart_puts("\r\n");
}

int main(void) {
    leds_init();
    spi_init();
    uart_init();

    uart_puts("\r\nGyro Scheduler\r\n");

    if (!gyro_init()) {
        uart_puts("ERREUR: WHO_AM_I != 0xD3, gyro non detecte\r\n");
        leds_blink_error();
        while (1);
    }

    systick_init();
    scheduler_init();

    scheduler_add_task(gyro_task,   1);
    scheduler_add_task(led_task,   10);
    scheduler_add_task(uart_task, 500);

    while (1) {
        scheduler_run();
    }
}
