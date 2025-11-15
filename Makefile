# --- Toolchain ---------------------------------------------------------------
CROSS   ?= aarch64-none-elf
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy

# --- Flags -------------------------------------------------------------------
CFLAGS  := -Wall -Wextra -O2 -ffreestanding -fno-builtin -fno-stack-protector \
           -nostdlib -nostartfiles -mcmodel=small -mgeneral-regs-only -MMD -MP
ASFLAGS := $(CFLAGS)
LDFLAGS := -T linker.ld -nostdlib

# --- Project structure -------------------------------------------------------
BUILD_DIR := build
SRC_DIRS  := arch/arm64 core drivers guests
EXCLUDE  :=

# --- Auto-discovery of sources ----------------------------------------------
SRCS := $(shell find $(SRC_DIRS) -type f \( -name '*.c' -o -name '*.S' \) \
         $(foreach pat,$(EXCLUDE),-not -path '$(pat)') )

OBJS := $(patsubst %.c, $(BUILD_DIR)/%.o, \
        $(patsubst %.S, $(BUILD_DIR)/%.o, $(SRCS)))

START_OBJ := $(BUILD_DIR)/arch/arm64/start.o
OBJS_LINK := $(START_OBJ) $(filter-out $(START_OBJ),$(OBJS))

DEPS := $(OBJS:.o=.d)

TARGET := $(BUILD_DIR)/schism.elf

# --- Default rules -----------------------------------------------------------
all: $(TARGET)

$(TARGET): $(OBJS_LINK) linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJS_LINK) $(LDFLAGS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -Iinclude -c $< -o $@

run: $(TARGET)
	qemu-system-aarch64 -M virt,virtualization=on,gic-version=3 \
	  -cpu max -smp 1 -m 256M -nographic -kernel $(TARGET)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean run

-include $(DEPS)
