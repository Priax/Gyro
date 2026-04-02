#ifndef OLED_H
#define OLED_H

#include <stdint.h>

// Driver SSD1306 128x64 via SPI2
//
// Brochage (GPIOB) :
//   PB0  → RES  (reset, actif bas)
//   PB1  → DC   (0 = commande, 1 = données)
//   PB2  → CS   (actif bas)
//   PB13 → SPI2_SCK  (AF5)
//   PB15 → SPI2_MOSI (AF5)
//
// Utilisation typique :
//   oled_init();
//   oled_clear();
//   oled_draw_string(0, 0, "Bonjour !");
//   oled_flush();

// Initialise SPI2, GPIOB et le contrôleur SSD1306
void oled_init(void);

// Efface le framebuffer (RAM, pas encore envoyé à l'écran)
void oled_clear(void);

// Envoie le framebuffer complet vers l'écran (1024 octets)
void oled_flush(void);

// Allume ou éteint un pixel. x ∈ [0,127], y ∈ [0,63], on ∈ {0,1}
void oled_draw_pixel(int x, int y, int on);

// Dessine un caractère ASCII. x ∈ [0,127], row ∈ [0,7] (une row = 8 px)
void oled_draw_char(int x, int row, char c);

// Dessine une chaîne de caractères. Chaque caractère occupe 6 px en largeur.
void oled_draw_string(int x, int row, const char *s);

// Dessine un entier signé (int32_t) sous forme décimale.
void oled_draw_int(int x, int row, int32_t n);

#endif
