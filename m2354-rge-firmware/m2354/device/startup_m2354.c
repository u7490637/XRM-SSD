/* Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
 *
 * Minimal Cortex-M23 (Armv8-M baseline) startup for the Nuvoton M2354.
 *
 * Self-contained: depends only on arm-none-eabi-gcc + newlib — NO Nuvoton BSP.
 * Output is via SEMIHOSTING (run the image under a SWD debugger: pyOCD, Nu-Link
 * GDB server, or OpenOCD), so no UART pin-mux or clock configuration is needed
 * to see the evidence-chained epochs — the riskiest board-specific bits are
 * sidestepped for the first bring-up. The on-chip hardware SHA engine + real
 * UART are the v2 (BSP) upgrade; this v1 uses the portable SHA-256 so the digest
 * is byte-identical to the host build.
 *
 * The M2354 boots in the Armv8-M SECURE state from flash at 0x00000000; this
 * firmware runs entirely in secure world (no TrustZone split for the v1 demo).
 */
#include <stdint.h>

extern uint32_t _sidata;   /* .data init image, in flash (LMA)            */
extern uint32_t _sdata;    /* .data start in SRAM (VMA)                   */
extern uint32_t _edata;    /* .data end in SRAM                           */
extern uint32_t _sbss;     /* .bss start in SRAM                          */
extern uint32_t _ebss;     /* .bss end in SRAM                            */
extern uint32_t _estack;   /* top of SRAM (initial MSP), from the linker  */

extern int  main(void);
extern void __libc_init_array(void);

void Reset_Handler(void);
void Default_Handler(void);

void Reset_Handler(void)
{
    /* Copy initialised data from its flash load image into SRAM. */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
    /* Zero the .bss segment. */
    for (dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0U;
    }
    /* Run C library / static constructor init, then the application. */
    __libc_init_array();
    (void)main();
    for (;;) {
        /* main() returned — park. */
    }
}

void Default_Handler(void)
{
    for (;;) {
        /* Unexpected exception/IRQ — park so a debugger can catch it. */
    }
}

/* Weak aliases: any handler the app does not define falls back to park-forever.
 * The demo enables no interrupts, so none of these fire in normal operation. */
#define ALIAS(target) __attribute__((weak, alias(#target)))
void NMI_Handler(void)       ALIAS(Default_Handler);
void HardFault_Handler(void) ALIAS(Default_Handler);
void SVC_Handler(void)       ALIAS(Default_Handler);
void PendSV_Handler(void)    ALIAS(Default_Handler);
void SysTick_Handler(void)   ALIAS(Default_Handler);

/* Armv8-M baseline (Cortex-M23) vector table. Layout:
 *   [0]   initial MSP            [11]  SVCall
 *   [1]   Reset                  [14]  PendSV
 *   [2]   NMI                    [15]  SysTick
 *   [3]   HardFault              [16+] external IRQs (none used by the demo)
 *   [4..10], [12..13] reserved.
 * A generous run of Default_Handler IRQ slots is appended for safety; the demo
 * enables no NVIC interrupts so none are ever fetched. */
__attribute__((section(".isr_vector"), used))
void (*const g_pfnVectors[])(void) = {
    (void (*)(void))(&_estack),  /* 0  initial stack pointer */
    Reset_Handler,               /* 1  reset                 */
    NMI_Handler,                 /* 2  NMI                   */
    HardFault_Handler,           /* 3  HardFault             */
    0, 0, 0, 0,                  /* 4..7   reserved          */
    0, 0, 0,                     /* 8..10  reserved          */
    SVC_Handler,                 /* 11 SVCall                */
    0, 0,                        /* 12..13 reserved          */
    PendSV_Handler,              /* 14 PendSV                */
    SysTick_Handler,             /* 15 SysTick               */
    /* 16.. external IRQs (unused) */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
};
