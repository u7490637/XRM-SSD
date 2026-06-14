#ifndef NUVOTON_M2354_H
#define NUVOTON_M2354_H

#include <stdint.h>

/* Memory map (placeholder addresses) */
#define GPIO_BASE      0x40000000UL
#define UART0_BASE     0x40001000UL

/* GPIO register structure (simplified) */
typedef struct {
    volatile uint32_t MODE;   /* Pin mode */
    volatile uint32_t DOUT;   /* Data output */
    volatile uint32_t DIN;    /* Data input */
    volatile uint32_t PULLEN; /* Pull-up/down enable */
} GPIO_TypeDef;

#define GPIO_PORT_A ((GPIO_TypeDef *) (GPIO_BASE + 0x0000))
#define GPIO_PORT_B ((GPIO_TypeDef *) (GPIO_BASE + 0x0020))
#define GPIO_PORT_C ((GPIO_TypeDef *) (GPIO_BASE + 0x0040))
#define GPIO_PORT_D ((GPIO_TypeDef *) (GPIO_BASE + 0x0060))

/* UART register structure (simplified) */
typedef struct {
    volatile uint32_t DATA;   /* Data register */
    volatile uint32_t STATUS; /* Status register */
    volatile uint32_t BAUD;   /* Baud rate divisor */
} UART_TypeDef;

#define UART0 ((UART_TypeDef *) UART0_BASE)

#endif /* NUVOTON_M2354_H */
