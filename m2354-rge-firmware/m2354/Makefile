# Copyright 2026 STARGA, Inc. / Dollarchip — XRM-SSD.
#
# M2354 RGE firmware build.
#
#   make host        -> build + link the host demo (runs on your laptop, prints
#                       the evidence-chained epochs). Software SHA-256 fallback.
#   make run         -> build host, then run it.
#   make device      -> cross-compile for the NuMaker-M2354 (Cortex-M23) using
#                       the Nuvoton BSP hardware SHA engine (-DM2354_BSP).
#                       Requires arm-none-eabi-gcc + M2354_BSP pointing at the
#                       Nuvoton BSP checkout.
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

.PHONY: all host run device clean
all: host

host: $(HOST_BIN)

$(HOST_BIN): $(COMMON_SRCS) $(MAIN_SRC) | $(BUILD)
	$(HOST_CC) $(HOST_CFLAGS) $(COMMON_SRCS) $(MAIN_SRC) -o $@

run: host
	./$(HOST_BIN)

# ---- Device build (NuMaker-M2354, Cortex-M23 / Armv8-M) -------------------
# Point M2354_BSP at your Nuvoton BSP checkout (the one with Library/StdDriver
# and Library/Device/Nuvoton/M2354/Include providing NuMicro.h).
ARM_CC       ?= arm-none-eabi-gcc
M2354_BSP    ?=
DEV_CFLAGS   := -std=c11 -Os -Wall -Wextra \
                -mcpu=cortex-m23 -mthumb -mfloat-abi=soft \
                -ffunction-sections -fdata-sections \
                -I$(INC) -DM2354_BSP
DEV_INCS     := -I$(M2354_BSP)/Library/StdDriver/inc \
                -I$(M2354_BSP)/Library/Device/Nuvoton/M2354/Include \
                -I$(M2354_BSP)/Library/CMSIS/Include
DEV_OBJ      := $(BUILD)/rge_m2354.elf

device: | $(BUILD)
ifeq ($(strip $(M2354_BSP)),)
	@echo "ERROR: set M2354_BSP=/path/to/nuvoton-bsp (needs NuMicro.h + StdDriver)."
	@echo "       e.g. make device M2354_BSP=~/m2354-bsp"
	@exit 1
endif
	$(ARM_CC) $(DEV_CFLAGS) $(DEV_INCS) \
	  $(COMMON_SRCS) $(MAIN_SRC) -o $(DEV_OBJ) -Wl,--gc-sections
	@echo "Built $(DEV_OBJ). Flash with your usual M2354 tool (Nu-Link / pyOCD)."

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
