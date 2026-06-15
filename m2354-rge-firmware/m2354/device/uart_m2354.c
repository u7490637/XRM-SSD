/* Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
 *
 * Self-contained UART0 driver + newlib stdio retarget for the Nuvoton M2354
 * (Cortex-M23, Armv8-M). NO Nuvoton BSP required — bare register writes only.
 *
 * Why this exists: the default device build prints over SEMIHOSTING, which
 * only appears when a SWD debugger session is attached. This driver routes
 * printf over UART0 -> the Nu-Link2-Me USB-VCOM bridge, so the evidence-chained
 * epochs scroll live in ANY serial terminal (NuConsole, PuTTY, screen, minicom)
 * with NO debugger and NO clock/PLL bring-up.
 *
 * Default config on a powered-on M2354: the chip runs from the internal
 * 12 MHz HIRC after reset, and UART0 defaults to HIRC as its clock source.
 * We program the baud-rate generator against 12 MHz directly — no PLL, no
 * crystal, no SYS_UnlockReg dance needed for these blocks.
 *
 * Pin routing: UART0 on the NuMaker-M2354 is wired to the Nu-Link2-Me VCOM on
 * PB.12 = UART0_RXD, PB.13 = UART0_TXD (the board's documented debug-UART pins).
 * We set those two multi-function bits and leave everything else at reset.
 *
 * Terminal settings: 115200 8N1.
 */
#include <stdint.h>
#include <stddef.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---- Clock controller (CLK) ---------------------------------------------- */
#define CLK_BASE        0x40000200UL
#define CLK_APBCLK0     REG32(CLK_BASE + 0x208)  /* APB peripheral clock enable 0 */
#define CLK_CLKSEL2     REG32(CLK_BASE + 0x01C)  /* clock-source select 2 (UART0) */
#define CLK_APBCLK0_UART0CKEN  (1UL << 16)
/* UART0SEL field: CLKSEL2[25:24]. 0b11 = HIRC (12 MHz), the reset-safe source. */
#define CLK_CLKSEL2_UART0SEL_HIRC  (0x3UL << 24)
#define CLK_CLKSEL2_UART0SEL_MASK  (0x3UL << 24)

/* ---- GPIO PB multi-function (SYS) ---------------------------------------- */
#define SYS_BASE        0x40000000UL
#define SYS_GPB_MFPH    REG32(SYS_BASE + 0x038)  /* PB[15:8] multi-function high */
/* PB12 -> bits [19:16], PB13 -> bits [23:20]. UART0 alt-func code on these
 * pins is 0x6 (UART0_RXD on PB12, UART0_TXD on PB13) per the M2354 MFP table. */
#define SYS_GPB_MFPH_PB12MFP_Pos   16
#define SYS_GPB_MFPH_PB13MFP_Pos   20
#define SYS_GPB_MFPH_PB12_UART0RXD (0x6UL << SYS_GPB_MFPH_PB12MFP_Pos)
#define SYS_GPB_MFPH_PB13_UART0TXD (0x6UL << SYS_GPB_MFPH_PB13MFP_Pos)
#define SYS_GPB_MFPH_PB12_MASK     (0xFUL << SYS_GPB_MFPH_PB12MFP_Pos)
#define SYS_GPB_MFPH_PB13_MASK     (0xFUL << SYS_GPB_MFPH_PB13MFP_Pos)

/* ---- SYS register lock/unlock -------------------------------------------- */
#define SYS_REGLCTL     REG32(SYS_BASE + 0x100)

/* ---- UART0 --------------------------------------------------------------- */
#define UART0_BASE      0x40070000UL
#define UART0_DAT       REG32(UART0_BASE + 0x00)  /* TX/RX data        */
#define UART0_FIFO      REG32(UART0_BASE + 0x08)  /* FIFO control      */
#define UART0_LINE      REG32(UART0_BASE + 0x10)  /* line control      */
#define UART0_BAUD      REG32(UART0_BASE + 0x24)  /* baud-rate divider */
#define UART0_FSR       REG32(UART0_BASE + 0x18)  /* FIFO status       */

#define UART_LINE_8N1       0x00000003UL          /* WLS=11 (8-bit), 1 stop, no parity */
#define UART_FIFO_RFR_TFR   (0x06UL)              /* reset RX+TX FIFO bits */
#define UART_FSR_TXFULL     (1UL << 23)           /* TX FIFO full */
#define UART_FSR_TXEMPTY    (1UL << 28)           /* TX FIFO empty + shifter idle */

/* BAUD register: enable mode-2 (BAUDM1|BAUDM0 = 11), divider = clk/baud - 2. */
#define UART_BAUD_MODE2     (0x30000000UL)
#define HIRC_HZ             12000000UL
#define UART_BAUDRATE       115200UL

void uart_init(void)
{
    /* Unlock protected SYS registers (REGLCTL unlock sequence). */
    SYS_REGLCTL = 0x59;
    SYS_REGLCTL = 0x16;
    SYS_REGLCTL = 0x88;

    /* Clock UART0 from HIRC (12 MHz) and enable its APB clock gate. */
    CLK_CLKSEL2 = (CLK_CLKSEL2 & ~CLK_CLKSEL2_UART0SEL_MASK) | CLK_CLKSEL2_UART0SEL_HIRC;
    CLK_APBCLK0 |= CLK_APBCLK0_UART0CKEN;

    /* Route PB12/PB13 to UART0 RXD/TXD. */
    SYS_GPB_MFPH = (SYS_GPB_MFPH & ~(SYS_GPB_MFPH_PB12_MASK | SYS_GPB_MFPH_PB13_MASK))
                 | SYS_GPB_MFPH_PB12_UART0RXD | SYS_GPB_MFPH_PB13_UART0TXD;

    /* Reset the FIFOs, set 8N1, program 115200 from the 12 MHz source. */
    UART0_FIFO |= UART_FIFO_RFR_TFR;
    UART0_LINE  = UART_LINE_8N1;
    UART0_BAUD  = UART_BAUD_MODE2 | ((HIRC_HZ / UART_BAUDRATE) - 2);
}

static void uart_putc(char c)
{
    while (UART0_FSR & UART_FSR_TXFULL) { /* wait for room */ }
    UART0_DAT = (uint32_t)(uint8_t)c;
}

/* newlib retarget: every printf/putchar/fwrite to stdout/stderr lands here.
 * Translate '\n' -> "\r\n" so plain terminals show clean lines. */
int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') uart_putc('\r');
        uart_putc(buf[i]);
    }
    return len;
}

/* Drain the TX shifter so the last line is fully out before main() parks. */
void uart_flush(void)
{
    while (!(UART0_FSR & UART_FSR_TXEMPTY)) { /* wait */ }
}
