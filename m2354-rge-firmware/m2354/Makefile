# Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
#
# M2354 RGE firmware build.
#
#   make host        -> build + link the host demo (runs on your laptop, prints
#                       the evidence-chained epochs). Software SHA-256 fallback.
#   make run         -> build host, then run it.
#   make device      -> cross-compile for the NuMaker-M2354 (Cortex-M23).
#                       Output via SEMIHOSTING — needs a SWD debugger attached.
#                       Requires arm-none-eabi-gcc.
#   make uart        -> same as device, but stdio over UART0 -> Nu-Link2-Me
#                       USB-VCOM. Flash it, open any serial terminal @115200 8N1,
#                       reset — epochs scroll live, NO debugger needed.
#   make clean
#
# The host and device builds compile the SAME rge.c / sha256.c so the digest
# the laptop computes is byte-identical to what the silicon's SHA engine emits
# — that identity is the whole point of the evidence chain.

INC      := include
SRC      := src
BUILD    := build

COMMON_SRCS := $(SRC)/rge.c $(SRC)/sha256.c $(SRC)/crypto_accel.c
MAIN_SRC    := $(SRC)/main.c

# ---- Host build (default) -------------------------------------------------
HOST_CC      ?= cc
HOST_CFLAGS  := -std=c11 -O2 -Wall -Wextra -Werror -I$(INC) -DHOST_BUILD
HOST_BIN     := $(BUILD)/rge_host

.PHONY: all host run device uart flash clean
all: host

host: $(HOST_BIN)

$(HOST_BIN): $(COMMON_SRCS) $(MAIN_SRC) | $(BUILD)
	$(HOST_CC) $(HOST_CFLAGS) $(COMMON_SRCS) $(MAIN_SRC) -o $@

run: host
	./$(HOST_BIN)

# ---- Device build (NuMaker-M2354, Cortex-M23 / Armv8-M) -------------------
# Self-contained: needs ONLY arm-none-eabi-gcc — NO Nuvoton BSP. Output is via
# SEMIHOSTING: run the flashed image under a SWD debugger (Nu-Link GDB server,
# pyOCD, OpenOCD, or Keil) and the evidence-chained epochs print to the debugger
# console — no UART pin-mux or clock bring-up needed. Uses the portable SHA-256,
# so the on-device digest is byte-identical to `make run` on the host.
# (v2 upgrade: -DM2354_BSP routes SHA through the on-chip engine — needs the BSP.)
ARM_CC      ?= arm-none-eabi-gcc
ARM_OBJCOPY ?= arm-none-eabi-objcopy
ARM_SIZE    ?= arm-none-eabi-size
DEV_DIR     := device
DEV_CFLAGS  := -std=c11 -Os -Wall -Wextra -mcpu=cortex-m23 -mthumb \
               -ffunction-sections -fdata-sections -ffreestanding -I$(INC)
DEV_LDFLAGS := -mcpu=cortex-m23 -mthumb -T $(DEV_DIR)/m2354.ld \
               --specs=nano.specs --specs=rdimon.specs \
               -Wl,--gc-sections -Wl,-Map=$(BUILD)/rge_m2354.map
DEV_SRCS    := $(COMMON_SRCS) $(MAIN_SRC) $(DEV_DIR)/startup_m2354.c
DEV_ELF     := $(BUILD)/rge_m2354.elf
DEV_BIN     := $(BUILD)/rge_m2354.bin
DEV_HEX     := $(BUILD)/rge_m2354.hex

device: $(DEV_BIN) $(DEV_HEX)

$(DEV_ELF): $(DEV_SRCS) $(DEV_DIR)/m2354.ld | $(BUILD)
	$(ARM_CC) $(DEV_CFLAGS) $(DEV_LDFLAGS) $(DEV_SRCS) -o $@
	@$(ARM_SIZE) $@

$(DEV_BIN): $(DEV_ELF)
	$(ARM_OBJCOPY) -O binary $< $@
	@echo "Built $@ (raw binary — Nu-Link / pyOCD / NuMaker drag-drop)."

$(DEV_HEX): $(DEV_ELF)
	$(ARM_OBJCOPY) -O ihex $< $@

# ---- Device build, UART variant (no debugger needed) ----------------------
# Identical to `device` but routes stdio over UART0 -> the Nu-Link2-Me USB-VCOM
# bridge instead of semihosting. Flash the .bin, open ANY serial terminal
# (NuConsole / PuTTY / screen) on the board's COM port at 115200 8N1, reset the
# board, and the evidence-chained epochs scroll live — NO SWD debugger session
# required. Self-contained (bare UART0 register writes), no Nuvoton BSP, and the
# on-device SHA-256 digest stays byte-identical to `make run` on the host.
UART_CFLAGS  := -std=c11 -Os -Wall -Wextra -mcpu=cortex-m23 -mthumb \
                -ffunction-sections -fdata-sections -ffreestanding \
                -DUART_BUILD -I$(INC)
UART_LDFLAGS := -mcpu=cortex-m23 -mthumb -T $(DEV_DIR)/m2354.ld \
                --specs=nano.specs --specs=nosys.specs \
                -Wl,--gc-sections -Wl,-Map=$(BUILD)/rge_m2354_uart.map
UART_SRCS    := $(COMMON_SRCS) $(MAIN_SRC) $(DEV_DIR)/startup_m2354.c \
                $(DEV_DIR)/uart_m2354.c
UART_ELF     := $(BUILD)/rge_m2354_uart.elf
UART_BIN     := $(BUILD)/rge_m2354_uart.bin
UART_HEX     := $(BUILD)/rge_m2354_uart.hex

.PHONY: uart
uart: $(UART_BIN) $(UART_HEX)

$(UART_ELF): $(UART_SRCS) $(DEV_DIR)/m2354.ld | $(BUILD)
	$(ARM_CC) $(UART_CFLAGS) $(UART_LDFLAGS) $(UART_SRCS) -o $@
	@$(ARM_SIZE) $@

$(UART_BIN): $(UART_ELF)
	$(ARM_OBJCOPY) -O binary $< $@
	@echo "Built $@ (UART/VCOM — flash, open terminal @115200 8N1, reset)."

$(UART_HEX): $(UART_ELF)
	$(ARM_OBJCOPY) -O ihex $< $@

# Flash with pyOCD (pip install pyocd && pyocd pack install M2354). If pyOCD is
# absent, flash $(DEV_BIN) with Nu-Link, Keil, or by drag-dropping it onto the
# NuMaker mass-storage drive. Run the .elf under a debugger to see semihosting.
flash: $(DEV_BIN)
	@command -v pyocd >/dev/null 2>&1 && pyocd flash -t m2354 $(DEV_BIN) \
	  || echo "pyOCD not found — flash $(DEV_BIN) with Nu-Link / Keil / NuMaker drag-drop (see README)."

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
