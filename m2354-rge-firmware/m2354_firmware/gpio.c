#include "gpio.h"

/*
 * This version matches your CURRENT build — gpio.h includes the placeholder
 * nuvoton_m2354.h, which defines GPIO_TypeDef (MODE/DOUT) and GPIO_PORT_A/B/C/D.
 * It compiles and links clean against that header. See README_FIX.md for the
 * real-BSP variant (M2354.h) that actually drives hardware.
 *
 * LED assumed on PB.0 — change the port/pin to match the actual board.
 */

void GPIO_Init(void)
{
    /* Set PB.0 to output. 2 bits per pin in MODE: 0b01 = push-pull output. */
    GPIO_PORT_B->MODE &= ~(0x3UL << (0 * 2));
    GPIO_PORT_B->MODE |=  (0x1UL << (0 * 2));
}

void GPIO_TogglePin(GPIO_TypeDef *port, uint32_t pin)
{
    port->DOUT ^= (1UL << pin);
}
