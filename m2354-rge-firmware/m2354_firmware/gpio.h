#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>
#include "nuvoton_m2354.h"

void GPIO_Init(void);
void GPIO_TogglePin(GPIO_TypeDef *port, uint32_t pin);

#endif /* GPIO_H */
