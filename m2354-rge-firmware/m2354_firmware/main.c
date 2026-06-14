#include "nuvoton_m2354.h"
#include "gpio.h"
#include "uart.h"

/* Crude busy-wait delay. For accurate timing, switch to a SysTick-based
   delay derived from SystemCoreClock once the real BSP clock setup is in. */
static void delay(volatile uint32_t n)
{
    while (n--) { __asm volatile ("nop"); }
}

int main(void)
{
    SystemInit();
    GPIO_Init();
    UART_Init();

    while (1) {                            /* <-- the loop that was MISSING */
        GPIO_TogglePin(GPIO_PORT_B, 0);    /* toggle PB.0 (LED) */
        delay(1000000);
    }
    /* never returns */
}
