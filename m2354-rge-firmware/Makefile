# XRM-SSD M2354 firmware — build entry.
#
#   make host         # build + run on your laptop (portable SHA, HOST_BUILD)
#   make test         # host build + compare against the Python reference
#   make M2354_BSP=1  # cross-compile for the M2354 (needs arm-none-eabi-gcc + BSP)

M2354_DIR := m2354
INC       := -I$(M2354_DIR)/include
SRC       := $(M2354_DIR)/src/rge.c $(M2354_DIR)/src/crypto_accel.c \
             $(M2354_DIR)/src/sha256.c $(M2354_DIR)/src/main.c

# ---- Host build (x86-64 / your dev box) ----
HOST_CC     := cc
HOST_CFLAGS := -std=c11 -O2 -Wall -Wextra -DHOST_BUILD $(INC)

.PHONY: host test cross clean
host:
	$(HOST_CC) $(HOST_CFLAGS) $(SRC) -o rge_demo_host
	@echo "built rge_demo_host"

test: host
	@echo "=== firmware (C) ===" && ./rge_demo_host
	@echo "=== host reference (Python) ===" && python3 host/rge_host.py
	@echo "If the per-epoch hashes match line-for-line, host<->device agree."

# ---- Cross build for the M2354 (Cortex-M23 / Armv8-M baseline) ----
# Supply BSP_DIR=/path/to/Nuvoton/M2354/BSP and a startup/linker setup.
CROSS_CC     := arm-none-eabi-gcc
CROSS_CFLAGS := -mcpu=cortex-m23 -mthumb -Os -ffreestanding -DM2354_BSP \
                $(INC) -I$(BSP_DIR)/Library/Device/Nuvoton/M2354/Include \
                -I$(BSP_DIR)/Library/StdDriver/inc
cross:
	@test -n "$(BSP_DIR)" || (echo "set BSP_DIR=/path/to/M2354/BSP"; exit 1)
	$(CROSS_CC) $(CROSS_CFLAGS) $(SRC) -c
	@echo "compiled objects for cortex-m23 (link with your BSP startup + linker script)"

clean:
	rm -f rge_demo_host *.o
