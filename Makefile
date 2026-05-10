CC=arm-none-eabi-gcc
TARGET=usb_cdc

MACH=-mcpu=cortex-m4
FLOAT=-mfloat-abi=hard -mfpu=fpv4-sp-d16

BUILD_DIR = build
ROOT_DIR = .


LINKER= -T $(ROOT_DIR)/drivers/Device/linkers/STM32F411.ld
MAP_FILE= -Wl,-Map=$(BUILD_DIR)/$(TARGET).map

CFLAGS= -c $(MACH) -mthumb $(FLOAT) -std=gnu99 -Wall -O0 -g \
		--specs=nano.specs -ffunction-sections -fdata-sections

LDFLAGS = $(MACH) -mthumb $(FLOAT) --specs=nano.specs $(LINKER) $(MAP_FILE) \
			-Wl,--gc-sections -Wl,--print-memory-usage \
			-Wl,--start-group -lc -lm -Wl,--end-group

OBJCOPY=arm-none-eabi-objcopy

###########################################
#				 INCLUDES
###########################################

INCLUDES+= -I $(ROOT_DIR)/drivers/Inc/

############################################
# 				SOURCE FILES
############################################

OBJS		+= $(BUILD_DIR)/main.o
OBJS		+= $(BUILD_DIR)/syscalls.o
OBJS		+= $(BUILD_DIR)/startup.o

DRIVERS		+= $(BUILD_DIR)/driver_clock.o
DRIVERS		+= $(BUILD_DIR)/usb_hw.o

PREREQ = $(BUILD_DIR)

all: $(PREREQ) $(BUILD_DIR)/$(TARGET).elf

$(PREREQ):
	mkdir -p $(PREREQ)

$(BUILD_DIR)/%.o: $(ROOT_DIR)/app/Src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<

$(BUILD_DIR)/%.o: $(ROOT_DIR)/drivers/Src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<

$(BUILD_DIR)/%.o: $(ROOT_DIR)/drivers/Device/system/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<

$(BUILD_DIR)/%.o: $(ROOT_DIR)/drivers/Device/startup/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $<

# TARGET ELF
$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(DRIVERS)
	$(CC) $(LDFLAGS) -o $@ $^
	$(OBJCOPY) -O binary $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin
	arm-none-eabi-size $(BUILD_DIR)/$(TARGET).elf

load: all
	jlink flash.jlink

clean:
	rm -rf $(BUILD_DIR)
