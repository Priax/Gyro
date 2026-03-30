CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

TARGET = gyro

APP_DIR     = app
DRIVER_DIR  = drivers
KERNEL_DIR  = kernel
STARTUP_DIR = startup

SRCS = \
$(wildcard $(APP_DIR)/*.c) \
$(wildcard $(DRIVER_DIR)/*.c) \
$(wildcard $(KERNEL_DIR)/*.c) \
$(wildcard $(STARTUP_DIR)/*.c) \

INCLUDES = \
-I$(DRIVER_DIR)/include \
-I$(KERNEL_DIR)/include \

CFLAGS  = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS += -O0 -g -Wall
CFLAGS += -nostdlib -nostartfiles
CFLAGS += $(INCLUDES)

LDFLAGS = -T stm32f3.ld -nostdlib

all: $(TARGET).bin

$(TARGET).elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: $(TARGET).bin
	openocd -f interface/stlink.cfg \
	        -f target/stm32f3x.cfg \
	        -c "program $(TARGET).bin verify reset exit 0x08000000"

openocd:
	openocd -f interface/stlink.cfg -f target/stm32f3x.cfg

gdb: $(TARGET).elf
	gdb $(TARGET).elf

size: $(TARGET).elf
	arm-none-eabi-size -A $(TARGET).elf

clean:
	rm -f *.elf *.bin

.PHONY: all flash clean
