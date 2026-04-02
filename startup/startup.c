#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sdata, _edata, _sidata;
extern uint32_t _sbss, _ebss;

int main(void);
void systick_init(void);

#define SYST_CSR (*(volatile uint32_t *)0xE000E010)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018)

extern volatile uint32_t sys_tick;

void SysTick_Handler(void) {
    sys_tick++;
}

void systick_init(void) {
    SYST_RVR = 72000 - 1;  // 72 MHz / 72000 = 1 kHz → 1 ms par tick
    SYST_CVR = 0;
    SYST_CSR = 0x7;        // CLKSOURCE=CPU, TICKINT=1, ENABLE=1
}

#define RCC_CR    (*(volatile uint32_t *)0x40021000)
#define RCC_CFGR  (*(volatile uint32_t *)0x40021004)
#define FLASH_ACR (*(volatile uint32_t *)0x40022000)

static void clock_72mhz(void) {
    FLASH_ACR = 0x12;               // 2 wait states + prefetch (requis à 72 MHz)
    RCC_CR |= (1u << 16);           // HSEON
    while (!(RCC_CR & (1u << 17))); // attendre HSERDY
    RCC_CFGR = (1u << 16)           // PLLSRC = HSE
             | (0x7u << 18);        // PLLMUL = ×9 → 8 MHz × 9 = 72 MHz
    RCC_CR |= (1u << 24);           // PLLON
    while (!(RCC_CR & (1u << 25))); // attendre PLLRDY
    RCC_CFGR |= 0x2u;               // SW = PLL
    while (((RCC_CFGR >> 2) & 0x3u) != 0x2u); // attendre SWS = PLL
}

void Reset_Handler(void) {
    clock_72mhz();

    // 1. Copier .data de Flash vers RAM
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // 2. Mettre .bss à zéro
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // 3. Lancer main
    main();

    // Ne devrait jamais arriver
    while (1);
}

void Default_Handler(void) { while (1); }

// Table des vecteurs — STM32F303xC : 16 exceptions noyau + 83 IRQ périphériques = 99 entrées
// Les slots réservés (indices 7-10 et 13) sont implicitement à zéro.
__attribute__((section(".isr_vector")))
uint32_t vector_table[] = {
    [0]  = (uint32_t)&_estack,           // Stack pointer initial
    [1]  = (uint32_t)&Reset_Handler,     // Reset
    [2]  = (uint32_t)&Default_Handler,   // NMI
    [3]  = (uint32_t)&Default_Handler,   // HardFault
    [4]  = (uint32_t)&Default_Handler,   // MemManage
    [5]  = (uint32_t)&Default_Handler,   // BusFault
    [6]  = (uint32_t)&Default_Handler,   // UsageFault
    // [7..10] : réservé (zéro)
    [11] = (uint32_t)&Default_Handler,   // SVCall
    [12] = (uint32_t)&Default_Handler,   // DebugMon
    // [13] : réservé (zéro)
    [14] = (uint32_t)&Default_Handler,   // PendSV
    [15] = (uint32_t)&SysTick_Handler,   // SysTick
    [16 ... 98] = (uint32_t)&Default_Handler, // IRQ0-IRQ82 (périphériques STM32F303xC)
};
