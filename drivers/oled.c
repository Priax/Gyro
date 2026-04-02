#include "oled.h"
#include <stdint.h>

// ── Adresses registres ────────────────────────────────────────────────────────

#define RCC_BASE    0x40021000
#define GPIOB_BASE  0x48000400
#define SPI2_BASE   0x40003800

#define RCC_AHBENR          (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#define RCC_AHBENR_GPIOBEN  (1u << 18)
#define RCC_APB1ENR_SPI2EN  (1u << 14)

#define GPIOB_MODER (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_BSRR  (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_AFRH  (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

#define SPI2_CR1 (*(volatile uint32_t *)(SPI2_BASE + 0x00))
#define SPI2_CR2 (*(volatile uint32_t *)(SPI2_BASE + 0x04))
#define SPI2_SR  (*(volatile uint32_t *)(SPI2_BASE + 0x08))
#define SPI2_DR  (*(volatile uint8_t  *)(SPI2_BASE + 0x0C))

#define SPI_SR_TXE  (1u << 1)
#define SPI_SR_RXNE (1u << 0)
#define SPI_SR_BSY  (1u << 7)

// ── Contrôle des broches GPIO ─────────────────────────────────────────────────

// PB0 = RES, PB1 = DC, PB2 = CS
#define RES_HIGH() (GPIOB_BSRR = (1u << 0))
#define RES_LOW()  (GPIOB_BSRR = (1u << (0 + 16)))
#define DC_HIGH()  (GPIOB_BSRR = (1u << 1))
#define DC_LOW()   (GPIOB_BSRR = (1u << (1 + 16)))
#define CS_HIGH()  (GPIOB_BSRR = (1u << 2))
#define CS_LOW()   (GPIOB_BSRR = (1u << (2 + 16)))

// ── Framebuffer ───────────────────────────────────────────────────────────────

#define OLED_W     128
#define OLED_PAGES   8   // 64 px / 8 bits = 8 pages

// fb[page][col] : bit 0 = pixel le plus haut de la page
static uint8_t fb[OLED_PAGES][OLED_W];

// ── Police de caractères 5×8 ──────────────────────────────────────────────────
// 96 caractères ASCII 32–127. Chaque entrée = 5 octets (colonnes).
// Dans chaque colonne : bit 0 = pixel du haut, bit 7 = pixel du bas.

static const uint8_t font5x8[96][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32  (espace)
    {0x00,0x00,0x5F,0x00,0x00}, // 33  !
    {0x00,0x07,0x00,0x07,0x00}, // 34  "
    {0x14,0x7F,0x14,0x7F,0x14}, // 35  #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36  $
    {0x23,0x13,0x08,0x64,0x62}, // 37  %
    {0x36,0x49,0x55,0x22,0x50}, // 38  &
    {0x00,0x05,0x03,0x00,0x00}, // 39  '
    {0x00,0x1C,0x22,0x41,0x00}, // 40  (
    {0x00,0x41,0x22,0x1C,0x00}, // 41  )
    {0x14,0x08,0x3E,0x08,0x14}, // 42  *
    {0x08,0x08,0x3E,0x08,0x08}, // 43  +
    {0x00,0x50,0x30,0x00,0x00}, // 44  ,
    {0x08,0x08,0x08,0x08,0x08}, // 45  -
    {0x00,0x60,0x60,0x00,0x00}, // 46  .
    {0x20,0x10,0x08,0x04,0x02}, // 47  /
    {0x3E,0x51,0x49,0x45,0x3E}, // 48  0
    {0x00,0x42,0x7F,0x40,0x00}, // 49  1
    {0x42,0x61,0x51,0x49,0x46}, // 50  2
    {0x21,0x41,0x45,0x4B,0x31}, // 51  3
    {0x18,0x14,0x12,0x7F,0x10}, // 52  4
    {0x27,0x45,0x45,0x45,0x39}, // 53  5
    {0x3C,0x4A,0x49,0x49,0x30}, // 54  6
    {0x01,0x71,0x09,0x05,0x03}, // 55  7
    {0x36,0x49,0x49,0x49,0x36}, // 56  8
    {0x06,0x49,0x49,0x29,0x1E}, // 57  9
    {0x00,0x36,0x36,0x00,0x00}, // 58  :
    {0x00,0x56,0x36,0x00,0x00}, // 59  ;
    {0x08,0x14,0x22,0x41,0x00}, // 60  <
    {0x14,0x14,0x14,0x14,0x14}, // 61  =
    {0x00,0x41,0x22,0x14,0x08}, // 62  >
    {0x02,0x01,0x51,0x09,0x06}, // 63  ?
    {0x32,0x49,0x79,0x41,0x3E}, // 64  @
    {0x7E,0x11,0x11,0x11,0x7E}, // 65  A
    {0x7F,0x49,0x49,0x49,0x36}, // 66  B
    {0x3E,0x41,0x41,0x41,0x22}, // 67  C
    {0x7F,0x41,0x41,0x22,0x1C}, // 68  D
    {0x7F,0x49,0x49,0x49,0x41}, // 69  E
    {0x7F,0x09,0x09,0x09,0x01}, // 70  F
    {0x3E,0x41,0x49,0x49,0x7A}, // 71  G
    {0x7F,0x08,0x08,0x08,0x7F}, // 72  H
    {0x00,0x41,0x7F,0x41,0x00}, // 73  I
    {0x20,0x40,0x41,0x3F,0x01}, // 74  J
    {0x7F,0x08,0x14,0x22,0x41}, // 75  K
    {0x7F,0x40,0x40,0x40,0x40}, // 76  L
    {0x7F,0x02,0x04,0x02,0x7F}, // 77  M
    {0x7F,0x04,0x08,0x10,0x7F}, // 78  N
    {0x3E,0x41,0x41,0x41,0x3E}, // 79  O
    {0x7F,0x09,0x09,0x09,0x06}, // 80  P
    {0x3E,0x41,0x51,0x21,0x5E}, // 81  Q
    {0x7F,0x09,0x19,0x29,0x46}, // 82  R
    {0x46,0x49,0x49,0x49,0x31}, // 83  S
    {0x01,0x01,0x7F,0x01,0x01}, // 84  T
    {0x3F,0x40,0x40,0x40,0x3F}, // 85  U
    {0x1F,0x20,0x40,0x20,0x1F}, // 86  V
    {0x3F,0x40,0x38,0x40,0x3F}, // 87  W
    {0x63,0x14,0x08,0x14,0x63}, // 88  X
    {0x07,0x08,0x70,0x08,0x07}, // 89  Y
    {0x61,0x51,0x49,0x45,0x43}, // 90  Z
    {0x00,0x7F,0x41,0x41,0x00}, // 91  [
    {0x02,0x04,0x08,0x10,0x20}, // 92  backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 93  ]
    {0x04,0x02,0x01,0x02,0x04}, // 94  ^
    {0x40,0x40,0x40,0x40,0x40}, // 95  _
    {0x00,0x01,0x02,0x04,0x00}, // 96  `
    {0x20,0x54,0x54,0x54,0x78}, // 97  a
    {0x7F,0x48,0x44,0x44,0x38}, // 98  b
    {0x38,0x44,0x44,0x44,0x20}, // 99  c
    {0x38,0x44,0x44,0x48,0x7F}, // 100 d
    {0x38,0x54,0x54,0x54,0x18}, // 101 e
    {0x08,0x7E,0x09,0x01,0x02}, // 102 f
    {0x0C,0x52,0x52,0x52,0x3E}, // 103 g
    {0x7F,0x08,0x04,0x04,0x78}, // 104 h
    {0x00,0x44,0x7D,0x40,0x00}, // 105 i
    {0x20,0x40,0x44,0x3D,0x00}, // 106 j
    {0x7F,0x10,0x28,0x44,0x00}, // 107 k
    {0x00,0x41,0x7F,0x40,0x00}, // 108 l
    {0x7C,0x04,0x18,0x04,0x78}, // 109 m
    {0x7C,0x08,0x04,0x04,0x78}, // 110 n
    {0x38,0x44,0x44,0x44,0x38}, // 111 o
    {0x7C,0x14,0x14,0x14,0x08}, // 112 p
    {0x08,0x14,0x14,0x18,0x7C}, // 113 q
    {0x7C,0x08,0x04,0x04,0x08}, // 114 r
    {0x48,0x54,0x54,0x54,0x20}, // 115 s
    {0x04,0x3F,0x44,0x40,0x20}, // 116 t
    {0x3C,0x40,0x40,0x40,0x3C}, // 117 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 118 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 119 w
    {0x44,0x28,0x10,0x28,0x44}, // 120 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 121 y
    {0x44,0x64,0x54,0x4C,0x44}, // 122 z
    {0x00,0x08,0x36,0x41,0x00}, // 123 {
    {0x00,0x00,0x7F,0x00,0x00}, // 124 |
    {0x00,0x41,0x36,0x08,0x00}, // 125 }
    {0x10,0x08,0x08,0x10,0x08}, // 126 ~
    {0x00,0x00,0x00,0x00,0x00}, // 127 DEL
};

// ── SPI2 bas niveau ───────────────────────────────────────────────────────────

#define SPI2_TIMEOUT 100000u

static void spi2_transfer(uint8_t data) {
    uint32_t t = SPI2_TIMEOUT;
    while (!(SPI2_SR & SPI_SR_TXE) && --t);
    SPI2_DR = data;
    t = SPI2_TIMEOUT;
    while (!(SPI2_SR & SPI_SR_RXNE) && --t);
    (void)SPI2_DR;  // vider le registre de réception
}

// Attend la fin des transferts en cours avant de toucher aux GPIO
static void spi2_wait_idle(void) {
    uint32_t t = SPI2_TIMEOUT;
    while ((SPI2_SR & SPI_SR_BSY) && --t);
}

// ── SSD1306 commande / données ────────────────────────────────────────────────

static void ssd1306_cmd(uint8_t cmd) {
    DC_LOW();
    CS_LOW();
    spi2_transfer(cmd);
    spi2_wait_idle();
    CS_HIGH();
}

// ── Initialisation ────────────────────────────────────────────────────────────

static void delay_us(uint32_t n) {
    // ~72 cycles/µs à 72 MHz, l'optimiseur -O0 préserve la boucle
    while (n--) {
        for (volatile uint32_t i = 72; i--; );
    }
}

void oled_init(void) {
    // 1. Horloges
    RCC_AHBENR  |= RCC_AHBENR_GPIOBEN;
    RCC_APB1ENR |= RCC_APB1ENR_SPI2EN;

    // 2. PB0 (RES), PB1 (DC), PB2 (CS) en sortie
    GPIOB_MODER &= ~((0x3u << (0*2)) | (0x3u << (1*2)) | (0x3u << (2*2)));
    GPIOB_MODER |=   (0x1u << (0*2)) | (0x1u << (1*2)) | (0x1u << (2*2));
    RES_HIGH();
    DC_LOW();
    CS_HIGH();

    // 3. PB13 (SCK) et PB15 (MOSI) en Alternate Function AF5
    GPIOB_MODER &= ~((0x3u << (13*2)) | (0x3u << (15*2)));
    GPIOB_MODER |=   (0x2u << (13*2)) | (0x2u << (15*2));
    GPIOB_AFRH  &= ~((0xFu << ((13-8)*4)) | (0xFu << ((15-8)*4)));
    GPIOB_AFRH  |=   (0x5u << ((13-8)*4)) | (0x5u << ((15-8)*4));

    // 4. SPI2 : mode 0 (CPOL=0 CPHA=0), maître, fPCLK/8 = 9 MHz, SSM/SSI
    SPI2_CR1 = (1u << 2)    // MSTR
             | (0x2u << 3)  // BR = fPCLK/8
             | (1u << 9)    // SSM
             | (1u << 8);   // SSI
    SPI2_CR2 = (1u << 12);  // FRXTH = 1 (seuil RXNE sur 8 bits)
    SPI2_CR1 |= (1u << 6);  // SPE

    // 5. Reset matériel du SSD1306
    RES_LOW();
    delay_us(10);
    RES_HIGH();
    delay_us(10);

    // 6. Séquence d'initialisation SSD1306 (128×64, charge pump interne)
    ssd1306_cmd(0xAE);        // display off
    ssd1306_cmd(0xD5); ssd1306_cmd(0x80); // fréquence d'horloge / ratio
    ssd1306_cmd(0xA8); ssd1306_cmd(0x3F); // multiplex ratio = 64 lignes
    ssd1306_cmd(0xD3); ssd1306_cmd(0x00); // display offset = 0
    ssd1306_cmd(0x40);                    // start line = 0
    ssd1306_cmd(0x8D); ssd1306_cmd(0x14); // charge pump ON
    ssd1306_cmd(0x20); ssd1306_cmd(0x00); // horizontal addressing mode
    ssd1306_cmd(0xA1);                    // segment re-map (col 127 → SEG0)
    ssd1306_cmd(0xC8);                    // COM scan direction inversée
    ssd1306_cmd(0xDA); ssd1306_cmd(0x12); // COM pins config
    ssd1306_cmd(0x81); ssd1306_cmd(0xCF); // contraste
    ssd1306_cmd(0xD9); ssd1306_cmd(0xF1); // pre-charge period
    ssd1306_cmd(0xDB); ssd1306_cmd(0x40); // VCOMH deselect level
    ssd1306_cmd(0xA4);                    // afficher le contenu de la RAM
    ssd1306_cmd(0xA6);                    // affichage normal (non inversé)
    ssd1306_cmd(0xAF);                    // display on
}

// ── Framebuffer ───────────────────────────────────────────────────────────────

void oled_clear(void) {
    for (int p = 0; p < OLED_PAGES; p++)
        for (int c = 0; c < OLED_W; c++)
            fb[p][c] = 0x00;
}

void oled_flush(void) {
    // Définir la fenêtre d'écriture : colonnes 0–127, pages 0–7
    ssd1306_cmd(0x21); ssd1306_cmd(0); ssd1306_cmd(127);
    ssd1306_cmd(0x22); ssd1306_cmd(0); ssd1306_cmd(7);

    // Envoyer les 1024 octets du framebuffer en mode données (DC=1)
    DC_HIGH();
    CS_LOW();
    for (int p = 0; p < OLED_PAGES; p++)
        for (int c = 0; c < OLED_W; c++)
            spi2_transfer(fb[p][c]);
    spi2_wait_idle();
    CS_HIGH();
}

// ── Primitives de dessin ──────────────────────────────────────────────────────

void oled_draw_pixel(int x, int y, int on) {
    if (x < 0 || x >= OLED_W || y < 0 || y >= (OLED_PAGES * 8))
        return;
    int page = y / 8;
    int bit  = y % 8;
    if (on)
        fb[page][x] |=  (uint8_t)(1u << bit);
    else
        fb[page][x] &= ~(uint8_t)(1u << bit);
}

void oled_draw_char(int x, int row, char c) {
    if (c < 32 || c > 127) c = '?';
    if (x < 0 || x + 4 >= OLED_W || row < 0 || row >= OLED_PAGES)
        return;
    const uint8_t *g = font5x8[(uint8_t)c - 32];
    for (int col = 0; col < 5; col++)
        fb[row][x + col] = g[col];
    if (x + 5 < OLED_W)
        fb[row][x + 5] = 0x00;  // colonne d'espacement
}

void oled_draw_string(int x, int row, const char *s) {
    while (*s && x + 5 < OLED_W) {
        oled_draw_char(x, row, *s++);
        x += 6;  // 5 px + 1 px d'espace
    }
}

void oled_draw_int(int x, int row, int32_t n) {
    char buf[13];
    int  i = 0;

    if (n < 0) {
        buf[i++] = '-';
        uint32_t u = (n == (int32_t)0x80000000u) ? 0x80000000u : (uint32_t)(-n);
        char tmp[11];
        int  j = 0;
        do { tmp[j++] = '0' + (char)(u % 10); u /= 10; } while (u);
        for (int k = j - 1; k >= 0; k--) buf[i++] = tmp[k];
    } else {
        uint32_t u = (uint32_t)n;
        if (u == 0) {
            buf[i++] = '0';
        } else {
            char tmp[11];
            int  j = 0;
            do { tmp[j++] = '0' + (char)(u % 10); u /= 10; } while (u);
            for (int k = j - 1; k >= 0; k--) buf[i++] = tmp[k];
        }
    }
    buf[i] = '\0';
    oled_draw_string(x, row, buf);
}
