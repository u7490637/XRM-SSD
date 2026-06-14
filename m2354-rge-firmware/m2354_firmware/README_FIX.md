# M2354 Firmware Fix — Why it was dead, and how to fix it

## Diagnosis (from firmware.map + main.o symbols)

The project **built and linked clean but the firmware was a no-op** — two structural bugs:

1. **Empty function bodies.** `GPIO_Init`, `GPIO_TogglePin`, `UART_Init` were each
   only **4 bytes** in the map — a single `bx lr` (empty `return`). They were
   declared and called but did nothing. `--gc-sections` even *discarded*
   `UART_SendChar` / `UART_ReceiveChar` as unreferenced.

2. **`main` had no loop.** `main` was 60 bytes: it called `SystemInit`,
   `GPIO_Init`, `UART_Init`, toggled the pin **once**, then fell off the end.
   On Cortex-M23 with no RTOS, returning from `main` drops into the startup trap
   loop — so the LED toggled once and froze. There was no `while(1)` and no delay.

## Two header worlds — DON'T MIX THEM

Your `gpio.h` includes the **placeholder** `nuvoton_m2354.h`, which defines:
- `GPIO_TypeDef` (fields `MODE`, `DOUT`, `DIN`, `PULLEN`)
- `GPIO_PORT_A/B/C/D`
- **placeholder addresses** (`GPIO_BASE 0x40000000`) and **no `CLK` register**

You also sent the **official Nuvoton BSP** `M2354.h`, which is a completely
different API: ports are `PA/PB/PC...` of type `GPIO_T*`, there's a real `CLK`
controller, real peripheral addresses, and helpers like `GPIO_SetMode()`.

**These two are mutually exclusive.** Code written for one will not compile
against the other. Your current build uses the placeholder, so the default fix
below targets that. The BSP variant is provided separately.

## What's in this zip

```
fixed/
  gpio.c               <- fix for your CURRENT build (placeholder header)
  main.c               <- adds the missing while(1) blink loop
  gpio_bsp_variant.c   <- REAL-HARDWARE version (only if you switch to M2354.h)
original/
  gpio.c, gpio.h, nuvoton_m2354.h, M2354.h   <- your files, unchanged
```

## Path A — keep the placeholder header (compiles today, good for simulator)

Drop in `fixed/gpio.c` + `fixed/main.c` and rebuild. This **compiles and links
clean** and fixes both bugs. But because the placeholder has fake addresses and
no clock register, it **will NOT physically blink an LED on real M2354 silicon**
— it's correct logic against a stub. Fine for a simulator or as a structural fix.

## Path B — go to real hardware (the official BSP)

To actually drive the chip:
1. Change `gpio.h` to `#include "M2354.h"` and declare
   `void GPIO_TogglePin(GPIO_T *port, uint32_t pin);`
2. Drop the placeholder `nuvoton_m2354.h`.
3. Use `fixed/gpio_bsp_variant.c` (enables the GPB clock via `CLK->AHBCLK0` and
   sets the mode via `GPIO_SetMode`).
4. In `main.c`, change `GPIO_TogglePin(GPIO_PORT_B, 0)` to
   `GPIO_TogglePin(PB, 0)`.
5. Make sure the rest of the BSP (`system_M2354.c`, `clk.c`, `gpio.c` from the
   BSP, the BSP startup + linker script) is in the build — `M2354.h` pulls in
   dozens of `*_reg.h` and driver headers that must be on the include path.

The critical real-silicon point: **the GPIO port clock must be enabled** before
the port responds. The placeholder header has no way to express that; the BSP
does, and `gpio_bsp_variant.c` does it.

## LED pin

Both versions assume **PB.0**. If the board LED is on a different port/pin,
change `GPIO_PORT_B`/`PB` and the pin number in both `gpio.c` and `main.c`.
