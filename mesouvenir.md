# Explication du firmware Gyro — STM32F3 Discovery

## Vue d'ensemble

Ce projet est un firmware **bare-metal** pour la carte **STM32F3 Discovery**.

Il fait trois choses :
1. Lit la vitesse de rotation du gyroscope **I3G4250D** via le bus SPI.
2. Intègre cette vitesse pour calculer un angle, et allume la LED correspondante sur la carte.
3. Envoie les données brutes vers un terminal via UART (USB → ST-Link).

---

## Structure des fichiers

```
stm32f3.ld          → Linker script : décrit la mémoire physique
startup/startup.c   → Point d'entrée réel, table d'interruptions, SysTick
drivers/spi.c       → Driver SPI (communication avec le gyro)
drivers/gyro.c      → Driver du gyroscope I3G4250D
drivers/uart.c      → Driver UART (envoi de texte vers terminal)
kernel/scheduler.c  → Ordonnanceur de tâches
app/main.c          → Logique applicative (LEDs, intégration d'angle)
```

---

## 1. Le linker script — `stm32f3.ld`

Ce fichier indique au compilateur **où placer chaque partie du programme** en mémoire.

### La mémoire physique du STM32F303

```
FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 40K
```

- **FLASH** (256 Ko à `0x08000000`) : mémoire non-volatile. Contient le code et les constantes. Survit à un reset ou une coupure d'alimentation.
- **RAM** (40 Ko à `0x20000000`) : mémoire volatile. Contient les variables. Effacée au reset.

### Les sections

| Section        | Contenu                          | Emplacement       |
|----------------|----------------------------------|-------------------|
| `.isr_vector`  | Table des vecteurs d'interruption | Flash (en premier)|
| `.text`        | Code compilé                     | Flash             |
| `.rodata`      | Constantes (ex: chaînes de texte)| Flash             |
| `.data`        | Variables initialisées            | Flash → copiées en RAM au boot |
| `.bss`         | Variables non-initialisées        | RAM (mises à zéro au boot) |

### Les symboles exportés

Le linker script génère des adresses utilisées par le code de démarrage :

- `_sdata` / `_edata` : début et fin de `.data` en RAM.
- `_sidata` : adresse source de `.data` en Flash (pour la copie).
- `_sbss` / `_ebss` : début et fin de `.bss` en RAM.
- `_estack` : sommet de la RAM = `0x20000000 + 40K = 0x2000A000` → valeur initiale du stack pointer.

---

## 2. Le démarrage — `startup/startup.c`

Ce fichier est exécuté **avant `main()`**. Il configure l'horloge, prépare la RAM et configure le timer système.

### Initialisation de l'horloge à 72 MHz — `clock_72mhz`

Le STM32F303 démarre par défaut à 8 MHz (HSI). Pour atteindre 72 MHz, on configure le PLL depuis le cristal externe (HSE = 8 MHz) :

```c
static void clock_72mhz(void) {
    FLASH_ACR = 0x12;               // 2 wait states + prefetch (requis à 72 MHz)
    RCC_CR |= (1u << 16);           // HSEON : active le cristal externe
    while (!(RCC_CR & (1u << 17))); // attend HSERDY : stabilisation du cristal
    RCC_CFGR = (1u << 16)           // PLLSRC = HSE (8 MHz)
             | (0x7u << 18);        // PLLMUL = ×9 → 8 MHz × 9 = 72 MHz
    RCC_CR |= (1u << 24);           // PLLON : active le PLL
    while (!(RCC_CR & (1u << 25))); // attend PLLRDY : PLL verrouillé
    RCC_CFGR |= 0x2u;               // SW = PLL : bascule le sysclk sur le PLL
    while (((RCC_CFGR >> 2) & 0x3u) != 0x2u); // attend SWS = PLL
}
```

Les **2 wait states Flash** sont obligatoires au-dessus de 48 MHz : la Flash ne peut pas répondre aussi vite que le CPU, il faut lui laisser 2 cycles supplémentaires par lecture.

Cette fonction est appelée **en tout premier** dans `Reset_Handler`, avant même la copie de `.data` en RAM. Elle ne dépend d'aucune variable globale.

### La table des vecteurs d'interruption

```c
__attribute__((section(".isr_vector")))
uint32_t vector_table[] = {
    [0]  = (uint32_t)&_estack,         // Valeur initiale du Stack Pointer
    [1]  = (uint32_t)&Reset_Handler,   // Exécuté au démarrage / reset
    [2]  = (uint32_t)&Default_Handler, // NMI (interruption non-masquable)
    [3]  = (uint32_t)&Default_Handler, // HardFault
    ...
    [15] = (uint32_t)&SysTick_Handler, // Timer système (chaque ms)
    [16 ... 98] = (uint32_t)&Default_Handler, // IRQ périphériques
};
```

Quand le microcontrôleur démarre, il lit ce tableau en Flash :
- L'entrée `[0]` initialise le registre SP (stack pointer) avec `_estack`.
- L'entrée `[1]` donne l'adresse de la première fonction à exécuter : `Reset_Handler`.

`Default_Handler` est une boucle infinie — si une interruption non-gérée se déclenche, le programme se bloque (signale un bug).

### `Reset_Handler` — Le vrai point d'entrée

```c
void Reset_Handler(void) {
    clock_72mhz(); // Configure le PLL → 72 MHz

    // 1. Copier les variables initialisées de Flash vers RAM
    // 2. Mettre les variables non-initialisées à zéro
    // 3. Appeler main()
}
```

### Le SysTick — `systick_init` et `SysTick_Handler`

```c
void systick_init(void) {
    SYST_RVR = 72000 - 1;  // Registre Reload : compte de 71999 à 0
    SYST_CVR = 0;           // Reset le compteur courant
    SYST_CSR = 0x7;         // Active le timer + l'interruption + source = CPU
}
```

Le **SysTick** est un timer intégré dans tout processeur ARM Cortex-M. Il compte à rebours depuis `SYST_RVR` jusqu'à 0, puis recharge la valeur et déclenche une interruption.

- Le CPU tourne à **72 MHz** (PLL configuré dans `clock_72mhz`).
- Avec `SYST_RVR = 71999` : 72 000 000 cycles/s ÷ 72 000 cycles = **1 000 interruptions/s = 1 interruption par ms**.

```c
void SysTick_Handler(void) {
    sys_tick++;
}
```

Chaque milliseconde, `sys_tick` est incrémenté. C'est l'horloge globale du système, utilisée par le scheduler.

---

## 3. Le driver SPI — `drivers/spi.c`

SPI (Serial Peripheral Interface) est le bus de communication entre le STM32 et le gyroscope.

### Les broches utilisées

| Broche | Signal       | Description                              |
|--------|--------------|------------------------------------------|
| PA5    | SPI1_SCK     | Horloge (générée par le maître)          |
| PA6    | SPI1_MISO    | Données : gyro → STM32                  |
| PA7    | SPI1_MOSI    | Données : STM32 → gyro                  |
| PE3    | CS (actif bas) | Chip Select : active le gyro           |

### Activation des horloges (`spi_init`)

Avant d'utiliser un périphérique, il faut activer son horloge dans le registre **RCC** (Reset and Clock Control) :

```c
RCC_AHBENR  |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOEEN;  // Active GPIOA et GPIOE
RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;                        // Active SPI1
```

Sans cela, les registres des périphériques ne répondent pas.

### Configuration des broches GPIO

Chaque broche a 2 bits dans le registre **MODER** :
- `0b00` = entrée
- `0b01` = sortie
- `0b10` = alternate function (contrôlée par un périphérique)
- `0b11` = analogique

```c
// PA5, PA6, PA7 → Alternate Function (0b10)
GPIOA_MODER &= ~((0x3 << (5*2)) | (0x3 << (6*2)) | (0x3 << (7*2)));
GPIOA_MODER |=   (0x2 << (5*2)) | (0x2 << (6*2)) | (0x2 << (7*2));

// Choisir quelle AF : AF5 = SPI1 sur ces broches
GPIOA_AFRL |= (0x5 << (5*4)) | (0x5 << (6*4)) | (0x5 << (7*4));

// PE3 → sortie (0b01) pour le Chip Select
GPIOE_MODER |= (0x1 << (3*2));
```

### Le Chip Select

```c
#define CS_LOW()   (GPIOE_BSRR = (1 << (3 + 16)))  // Reset bit 3 → PE3 = 0
#define CS_HIGH()  (GPIOE_BSRR = (1 << 3))          // Set bit 3  → PE3 = 1
```

`BSRR` est un registre atomique : les bits 0–15 mettent une broche à 1, les bits 16–31 la mettent à 0. Pas besoin de lire-modifier-écrire, l'opération est instantanée et sûre.

Une transaction SPI commence toujours par **CS bas** et finit par **CS haut**.

### Configuration de SPI1

```c
SPI1_CR1 = (1 << 1)    // CPOL=1 : horloge idle à niveau haut
         | (1 << 0)    // CPHA=1 : données capturées sur front descendant → Mode SPI 3
         | (1 << 2)    // MSTR : on est le maître (on génère l'horloge)
         | (0x2 << 3)  // BR[2:0] = 010 : diviseur 8 → 8 MHz / 8 = 1 MHz
         | (1 << 9)    // SSM : gestion du Slave Select par logiciel
         | (1 << 8);   // SSI : forcer NSS interne à 1 (évite le mode multimaître)

SPI1_CR2 = (1 << 12);  // FRXTH : le flag RXNE est levé dès 1 octet (8 bits) reçu

SPI1_CR1 |= (1 << 6);  // SPE : active SPI1
```

Le gyro I3G4250D requiert le **mode SPI 3** (CPOL=1, CPHA=1) — c'est indiqué dans sa datasheet.

### `spi_transfer` — Envoyer et recevoir un octet

```c
static uint8_t spi_transfer(uint8_t data) {
    while (!(SPI1_SR & SPI_SR_TXE));   // Attendre que le buffer TX soit vide
    SPI1_DR = data;                     // Écrire l'octet à envoyer
    while (!(SPI1_SR & SPI_SR_RXNE));  // Attendre qu'un octet soit reçu
    return SPI1_DR;                     // Lire l'octet reçu
}
```

En SPI, l'envoi et la réception sont **simultanés** (full-duplex). À chaque octet envoyé, le périphérique renvoie un octet en même temps. C'est pourquoi on lit toujours `SPI1_DR` après chaque envoi.

### Protocole de registre du gyroscope

Le gyro suit un protocole simple : le premier octet d'une transaction indique le registre et la direction.

- **Écriture** (`spi_write`) : bit7 = 0
  ```
  CS bas → [0|reg 7 bits] → [data] → CS haut
  ```

- **Lecture simple** (`spi_read`) : bit7 = 1
  ```
  CS bas → [1|reg 7 bits] → [0x00 dummy] → CS haut (renvoie l'octet du 2e transfert)
  ```

- **Lecture double atomique** (`spi_read_multi`) : bit7 = 1, bit6 = 1 (auto-incrément)
  ```
  CS bas → [1|1|reg 6 bits] → [0x00] → [0x00] → CS haut
  ```
  CS reste bas pendant les deux octets → les deux octets appartiennent garantiment au même sample.

---

## 4. Le driver gyroscope — `drivers/gyro.c`

Le gyroscope est un **I3G4250D** de ST Microelectronics.

### `gyro_init` — Initialisation

```c
if (spi_read(WHO_AM_I) != 0xD3) return 0;
```
**WHO_AM_I** (registre `0x0F`) contient l'identifiant du circuit. La valeur `0xD3` est fixée en usine. Si on ne lit pas `0xD3`, le gyro n'est pas présent ou pas alimenté.

```c
spi_write(CTRL_REG1, 0xFF);
```
**CTRL_REG1** (`0x20`) configure le fonctionnement général :
- `DR[1:0] = 11` → **ODR = 800 Hz** : le gyro produit 800 nouvelles mesures par seconde (une toutes les 1.25 ms).
- `BW[1:0] = 11` → **Bandwidth = 110 Hz** : filtre passe-bas interne.
- `PD = 1` → sortie du mode power-down (le gyro est actif).
- `Zen = Yen = Xen = 1` → les trois axes X, Y, Z sont actifs.

```c
spi_write(CTRL_REG4, 0x80);
```
**CTRL_REG4** (`0x23`) configure la plage et la protection des données :
- `BDU = 1` → **Block Data Update** : les registres OUT_Z_H et OUT_Z_L ne sont mis à jour qu'après que les deux aient été lus. Sans ça, on risquerait de lire l'octet bas d'une mesure et l'octet haut d'une mesure suivante.
- `FS[1:0] = 00` → **plage ±245 dps**, sensibilité = **8.75 mdps/LSB**.

### `gyro_data_ready` — Nouvelle mesure disponible ?

```c
return (spi_read(STATUS_REG) & STATUS_ZRDY) != 0;
```
**STATUS_REG** (`0x27`) contient des flags. Le bit 2 (`ZRDY`) passe à 1 quand une nouvelle mesure Z est prête. Il est effacé automatiquement à la lecture de OUT_Z_H.

### `gyro_read_z` — Lire la mesure

```c
spi_read_multi(OUT_Z_L, &low, &high);
return (int16_t)((high << 8) | low);
```
Les registres **OUT_Z_L** (`0x2C`) et **OUT_Z_H** (`0x2D`) contiennent la mesure en **little-endian** (octet faible en premier). On les assemble pour former un entier signé 16 bits (plage : −32 768 à +32 767).

---

## 5. Le driver UART — `drivers/uart.c`

L'UART envoie du texte vers un terminal sur PC via le câble USB (ST-Link).

### `uart_init`

```c
RCC_AHBENR  |= RCC_AHBENR_GPIOCEN;    // Active GPIOC
RCC_APB2ENR |= RCC_APB2ENR_USART1EN;  // Active USART1
```

```c
// PC4 en Alternate Function, AF7 = USART1_TX
GPIOC_MODER &= ~(0x3 << (4 * 2));
GPIOC_MODER |=  (0x2 << (4 * 2));
GPIOC_AFRL  &= ~(0xF << (4 * 4));
GPIOC_AFRL  |=  (0x7 << (4 * 4));
```

```c
USART1_BRR = 625;  // 72 000 000 / 115 200 ≈ 625 → baudrate 115 200
USART1_CR1 = USART_CR1_TE | USART_CR1_UE;  // Active TX et USART
```

L'UART est accessible via le **ST-Link embarqué** sur la carte (câble USB principal) sans matériel supplémentaire. Sur Linux, le port apparaît sous `/dev/ttyACM0`. Pour lire les données : `picocom -b 115200 /dev/ttyACM0`.

### `uart_putc` — Envoyer un caractère

```c
while (!(USART1_ISR & USART_ISR_TXE));  // Attendre que TX soit prêt
USART1_TDR = (uint32_t)c;              // Envoyer le caractère
```

`TXE` (Transmit Data Register Empty) passe à 1 quand le registre de transmission est prêt à recevoir un nouvel octet.

### `uart_print_int` — Envoyer un entier

Convertit un entier signé 32 bits en chaîne de chiffres en divisant successivement par 10, puis envoie les chiffres en sens inverse.

Un cas spécial : `INT32_MIN` (`0x80000000 = -2 147 483 648`) ne peut pas être nié directement en `int32_t` (dépassement), donc il est casté directement en `uint32_t`.

---

## 6. Le scheduler — `kernel/scheduler.c`

Un ordonnanceur coopératif minimal basé sur le temps.

### La structure `task_t`

```c
typedef struct {
    task_func_t func;    // Pointeur de fonction : void (*)(void)
    uint32_t period;     // Période en ms
    uint32_t last_run;   // Valeur de sys_tick au dernier appel
} task_t;
```

### `scheduler_add_task`

Enregistre une tâche avec sa période. Maximum 8 tâches (`MAX_TASKS = 8`).

### `scheduler_run`

```c
void scheduler_run(void) {
    for (int i = 0; i < task_count; i++) {
        if ((sys_tick - tasks[i].last_run) >= tasks[i].period) {
            tasks[i].last_run = sys_tick;
            tasks[i].func();
        }
    }
}
```

Pour chaque tâche, on vérifie si le temps écoulé depuis le dernier appel est supérieur ou égal à la période. Si oui, on l'exécute.

La soustraction `sys_tick - last_run` utilise l'arithmétique entière non-signée (modulo 2³²), ce qui la rend **résistante au débordement** de `sys_tick` (overflow naturel après ~49 jours).

---

## 7. L'application — `app/main.c`

### Les LEDs

La carte STM32F3 Discovery possède 8 LEDs connectées aux broches **PE8 à PE15**.

```c
static const int led_pins[8] = {9, 10, 11, 12, 13, 14, 15, 8};
```
Cet ordre correspond à l'ordre **physique circulaire** sur la carte (sens des aiguilles d'une montre).

```c
static void leds_init(void) {
    RCC_AHBENR |= RCC_AHBENR_GPIOEEN;  // Active GPIOE
    for (int i = 8; i <= 15; i++) {
        GPIOE_MODER &= ~(0x3 << (i * 2));  // Efface les 2 bits de mode
        GPIOE_MODER |=  (0x1 << (i * 2));  // Met en mode sortie (0b01)
    }
    GPIOE_ODR &= ~(0xFF << 8);  // Éteint toutes les LEDs
}
```

```c
static void led_show(int index) {
    GPIOE_ODR &= ~(0xFF << 8);             // Éteint toutes les LEDs
    GPIOE_ODR |= (1 << led_pins[index & 7]); // Allume la LED à l'index
}
```

`index & 7` est équivalent à `index % 8` mais plus rapide — garde l'index dans [0, 7].

### Les paramètres d'intégration

```c
#define DEAD_ZONE        100
#define ANGLE_FULL_TURN  360000  // Un tour = 360 000 milli-degrés
```

**DEAD_ZONE = 100** : si la valeur brute du gyro est entre -100 et +100, on la considère comme du bruit et on la force à zéro. Sans ça, la carte en position stable dériverait lentement.

**ANGLE_FULL_TURN = 360 000** : on travaille en milli-degrés pour conserver la précision des calculs entiers.

### `gyro_task` — Intégration de la vitesse en angle

```c
void gyro_task(void) {
    if (!gyro_data_ready()) return;  // Pas de nouvelle mesure → rien à faire

    gz = gyro_read_z();

    if (gz > -DEAD_ZONE && gz < DEAD_ZONE) gz = 0;  // Zone morte

    angle_mdeg += (int32_t)gz * 875 / 80000;
    // = gz × 8.75 [mdps/LSB] × 1.25 [ms/sample] = gz × 10.9375 µdeg/sample
    // = gz × 875/80000 mdeg/sample

    angle_mdeg %= ANGLE_FULL_TURN;
    if (angle_mdeg < 0) angle_mdeg += ANGLE_FULL_TURN;  // Correction modulo négatif
}
```

**Pourquoi `875 / 80000` ?**

| Paramètre | Valeur |
|-----------|--------|
| Sensibilité du gyro (plage ±245 dps) | 8.75 mdps/LSB |
| Période d'échantillonnage (ODR 800 Hz) | 1/800 s = 1.25 ms |
| Angle par sample | 8.75 × 1.25 = 10.9375 µdeg = **875/80 000 mdeg** |

La division entière `875 / 80000` tronque, mais c'est acceptable ici : l'erreur est < 0.001° par sample.

### `led_task` — Affichage de l'angle

```c
void led_task(void) {
    int index = angle_mdeg / (ANGLE_FULL_TURN / 8);  // 360000 / 8 = 45000 mdeg = 45°
    led_show(index);
}
```

Le cercle est divisé en **8 secteurs de 45°**. La LED allumée indique dans quel secteur se trouve l'angle actuel. Comme les LEDs sont disposées physiquement en cercle, cela visualise la rotation de la carte.

### `uart_task` — Débogage

```c
void uart_task(void) {
    uart_print_int(gz);              // Vitesse angulaire brute (LSB)
    uart_puts("\t");
    uart_print_int(angle_mdeg / 1000); // Angle en degrés entiers
    uart_puts("\r\n");
}
```

Envoie une ligne toutes les 100 ms. Format : `gz<TAB>angle\r\n`

- **Colonne 1 — `gz`** : vitesse angulaire instantanée sur l'axe Z, en LSB brut. Positif = rotation sens horaire, négatif = anti-horaire. Zéro si dans la dead zone (|gz| < 100).
- **Colonne 2 — angle** : rotation cumulée depuis le démarrage, en **degrés entiers** (0–359). Fait le tour complet.

Exemple de sortie :
```
0    359
4632    0
-11746    353
9003    250
```

Pour lire : `picocom -b 115200 /dev/ttyACM0`, puis RESET sur la carte.

### `main` — L'orchestration

```c
int main(void) {
    leds_init();   // Configure PE8-PE15 en sorties
    spi_init();    // Configure SPI1 (PA5/PA6/PA7 + PE3)
    uart_init();   // Configure USART1 (PC4)

    uart_puts("\r\nGyro Scheduler\r\n");

    if (!gyro_init()) {
        uart_puts("ERREUR: WHO_AM_I != 0xD3, gyro non detecte\r\n");
        leds_blink_error();  // Clignote toutes les LEDs 6 fois
        while (1);           // Bloque → erreur fatale
    }

    systick_init();   // SysTick à 1 kHz (sys_tick++ chaque ms)
    scheduler_init(); // Réinitialise le scheduler

    scheduler_add_task(gyro_task,    1);  // Toutes les 1 ms
    scheduler_add_task(led_task,    10);  // Toutes les 10 ms
    scheduler_add_task(uart_task,  100);  // Toutes les 100 ms

    while (1) {
        scheduler_run();  // Boucle infinie, exécute les tâches à temps
    }
}
```

---

## Flux complet au démarrage

```
Power-on / Reset
│
├─ ARM lit vector_table[0] → initialise SP = _estack (0x2000A000)
├─ ARM lit vector_table[1] → saute à Reset_Handler
│
└─ Reset_Handler
   ├─ clock_72mhz() : HSE → PLL ×9 → 72 MHz
   ├─ Copie .data (Flash → RAM)
   ├─ Met .bss à zéro
   └─ Appelle main()
      │
      ├─ Initialise LEDs, SPI, UART, gyro
      ├─ Configure SysTick (1 ms)
      ├─ Enregistre 3 tâches dans le scheduler
      │
      └─ Boucle infinie : scheduler_run()
         │
         ├─ Toutes les 1 ms  → gyro_task  : lit gz, intègre angle_mdeg
         ├─ Toutes les 10 ms → led_task   : allume la LED du bon secteur
         └─ Toutes les 100 ms → uart_task : envoie gz et angle via UART

         En parallèle, toutes les 1 ms :
         └─ SysTick_Handler (interruption) : sys_tick++
```

---

## Récapitulatif des valeurs importantes

| Valeur | Signification |
|--------|---------------|
| `0x08000000` | Adresse de départ de la Flash |
| `0x20000000` | Adresse de départ de la RAM |
| `0x2000A000` | Sommet de la RAM (= `_estack`) |
| `0x40021000` | Adresse du bloc RCC |
| `0x48000000` | Adresse de GPIOA |
| `0x48000800` | Adresse de GPIOC |
| `0x48001000` | Adresse de GPIOE |
| `0x40013000` | Adresse de SPI1 |
| `0x40013800` | Adresse de USART1 |
| `0xE000E010` | Adresse du SysTick (SYST_CSR) |
| `0xD3` | WHO_AM_I du gyro I3G4250D |
| `72000 - 1` | Valeur reload du SysTick → 1 ms à 72 MHz |
| `625` | BRR de l'UART → 115 200 bauds à 72 MHz |
| `8.75 mdps/LSB` | Sensibilité du gyro en mode ±245 dps |
| `800 Hz` | ODR (Output Data Rate) du gyro |
| `875 / 80000` | Facteur de conversion raw → milli-degrés/sample |
| `100` | Dead zone : bruit en dessous duquel gz est ignoré |
| `360 000` | Un tour complet en milli-degrés |
