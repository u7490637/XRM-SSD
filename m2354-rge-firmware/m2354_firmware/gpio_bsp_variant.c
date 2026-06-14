/*
 * REAL-HARDWARE VARIANT — use this ONLY if you switch gpio.h to include the
 * official Nuvoton BSP "M2354.h" instead of the placeholder nuvoton_m2354.h.
 *
 * The BSP world is DIFFERENT from the placeholder:
 *   - GPIO ports are PA/PB/PC/... of type GPIO_T* (NOT GPIO_PORT_B / GPIO_TypeDef*)
 *   - The GPIO clock MUST be enabled (CLK->AHBCLK0) or the port is dead silicon
 *   - Pin mode is set via the BSP helper GPIO_SetMode()
 *
 * If you adopt this, also change gpio.h to:
 *     #include "M2354.h"
 *     void GPIO_TogglePin(GPIO_T *port, uint32_t pin);
 * and drop the placeholder nuvoton_m2354.h.
 */
#include "M2354.h"

void GPIO_Init(void)
{
    /* Enable the GPIO Port B clock — without this the port does nothing. */
    CLK->AHBCLK0 |= CLK_AHBCLK0_GPBCKEN_Msk;

    /* PB.0 = push-pull output, via the BSP helper. */
    GPIO_SetMode(PB, BIT0, GPIO_MODE_OUTPUT);
}

void GPIO_TogglePin(GPIO_T *port, uint32_t pin)
{
    port->DOUT ^= (1UL << pin);
}
