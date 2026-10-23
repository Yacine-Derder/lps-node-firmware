OPENOCD           ?= openocd
OPENOCD_INTERFACE ?= interface/stlink.cfg
OPENOCD_CMDS      ?=
REV               ?= B
PYTHON           ?= python3
# CFLAGS          += -fdiagnostics-color=auto
# CFLAGS += -DUSE_FTDI_UART

BOOTLOAD          ?= 0

ifeq ($(strip $(REV)),A)
$(error Rev.A not supported anymore)
else ifeq ($(strip $(REV)),B)
HAL_ROOT=hal/stm32f0xx
CPU=f0
PROCESSOR=-mthumb -mcpu=cortex-m0 -DHSI48_VALUE="((uint32_t)48000000)" -DSTM32F072xB
OPENOCD_TARGET    ?= target/stm32f0x.cfg
else
$(error Rev.$(REV) unknown)
endif

INCLUDES=-Iinc -Iinc/$(CPU) -I$(HAL_ROOT)/Inc -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc

# FreeRTOS
FREERTOS_OBJS=list queue timers tasks port event_groups
OBJS+=$(foreach mod, $(FREERTOS_OBJS), lib/freertos/src/$(mod).o)
INCLUDES+=-Ilib/freertos/inc

# Platform specific files
OBJS+=src/f0/startup_stm32f072xb.o src/f0/system_stm32f0xx.o src/f0/stm32f0xx_it.o src/f0/stm32f0xx_hal_msp.o
OBJS+=src/f0/gpio.o src/f0/i2c.o src/f0/spi.o src/f0/system.o src/f0/usart.o
OBJS+=src/f0/usbd_conf.o src/eeprom.o src/bootmode.o
HALS+=i2c_ex

OBJS+=src/main.o
OBJS+=src/usb_device.o src/usbd_cdc_if.o src/usbd_desc.o src/lps25h.o src/led.o src/button.o
OBJS+=src/mul_console.o src/cfg.o src/usbcomm.o src/test_support.o src/production_test.o
OBJS+=src/uwb_control.o src/uwb.o src/uwb_twr_anchor.o src/uwb_sniffer.o src/uwb_twr_tag.o
OBJS+=src/lpp.o src/uwb_tdoa_anchor2.o src/uwb_tdoa_anchor3.o
OBJS+=src/mul_ads.o src/mul_ads_dw.o
OBJS+=src/phy_interop_dw.o src/mul_core.o src/mul_diag_dw.o
OBJS+=src/mul_common.o src/mul_alr.o src/mul_rlr.o
OBJS+=src/mul_rx.o src/mul_power.o
OBJS+=src/mul_clock.o src/mul_relation.o src/mul_role_state.o src/mul_downlink.o src/mul_localization.o
OBJS+=src/mul_twr.o src/mul_observation.o

HALS+=gpio rcc cortex i2c pcd dma pcd_ex rcc_ex spi uart pwr
OBJS+=$(foreach mod, $(HALS), $(HAL_ROOT)/Src/stm32$(CPU)xx_hal_$(mod).o)
OBJS+=$(HAL_ROOT)/Src/stm32$(CPU)xx_hal.o

USB_CORES=core ctlreq ioreq
USB_CDC=cdc
OBJS+=$(foreach mod, $(USB_CORES), Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_$(mod).o)
OBJS+=$(foreach mod, $(USB_CDC), Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_$(mod).o)

#libdw1000
INCLUDES+=-Ivendor/libdw1000/inc
OBJS+=vendor/libdw1000/src/libdw1000.o vendor/libdw1000/src/libdw1000Spi.o

OBJS+=src/dwOps.o

CFLAGS+=$(PROCESSOR) $(INCLUDES) -O3 -g3 -Wall -Wno-pointer-sign -std=gnu11
CFLAGS+=$(EXTRA_CFLAGS)
LDFLAGS+=$(PROCESSOR) --specs=nano.specs --specs=nosys.specs -lm -lc -u _printf_float

ifeq ($(strip $(BOOTLOAD)),0)
LDFLAGS+=-Ttools/make/stm32f072.ld
LOAD_ADDRESS = 0x8000000
else
LDFLAGS+=-Ttools/make/stm32f072_bootload.ld
LOAD_ADDRESS = 0x8005000
endif

# Remove un-used functions and global variables from output file
CFLAGS += -ffunction-sections -fdata-sections
# Console/configuration are not radio timing paths. Keep the complete command
# interface inside128KB without reducing stacks, queues or protocol state.
# Bounded codecs and passive association/localization use -Os. Transmit
# scheduling, hardware drivers and clock estimation retain -O3.
src/main.o src/mul_console.o src/cfg.o src/usbcomm.o src/uwb_control.o src/mul_common.o src/mul_rx.o src/mul_relation.o src/mul_localization.o src/mul_observation.o src/mul_downlink.o src/mul_rlr.o: CFLAGS += -Os
LDFLAGS+=-Wl,-Map=bin/$(PROG).map,--cref,--gc-sections


PREFIX=arm-none-eabi-

CC=$(PREFIX)gcc
LD=$(PREFIX)gcc
AS=$(PREFIX)as
OBJCOPY=$(PREFIX)objcopy
SIZE=$(PREFIX)size

all: check_submodules bin/lps-node-firmware.elf bin/lps-node-firmware.dfu

# The compatibility translation units include the canonical .c files. This
# Makefile has no compiler-generated header dependencies: invalidate objects on
# any portable core input change so incremental builds cannot keep a stale core.
MUL_CORE_INPUTS := $(wildcard portable/mul_core/*.[ch])
$(OBJS): $(MUL_CORE_INPUTS)

bin/lps-node-firmware.elf: $(OBJS)
	$(LD) -o $@ $^ $(LDFLAGS)
	$(SIZE) $@
	@echo BOOTLOADER Support: $(BOOTLOAD)

clean:
	rm -f bin/lps-node-firmware.elf bin/lps-node-firmware.dfu bin/.map $(OBJS)

flash:
	$(OPENOCD) -d2 -f $(OPENOCD_INTERFACE) $(OPENOCD_CMDS) -f $(OPENOCD_TARGET) -c init -c targets -c "reset halt" \
	           -c "flash write_image erase bin/lps-node-firmware.elf" -c "verify_image bin/lps-node-firmware.elf" -c "reset run" -c shutdown
erase:
	$(OPENOCD) -d2 -f $(OPENOCD_INTERFACE) $(OPENOCD_CMDS) -f $(OPENOCD_TARGET) -c init -c targets -c "reset halt" \
	           -c "stm32f1x mass_erase 0" -c shutdown

openocd:
	$(OPENOCD) -d2 -f $(OPENOCD_INTERFACE) $(OPENOCD_CMDS) -f $(OPENOCD_TARGET) -c init -c targets

dfu:
	dfu-util -d 0483:df11 -a 0 -D bin/lps-node-firmware.dfu -s :leave

reset_and_dfu:
	tools/make/reset-to-dfu.py
	dfu-util -d 0483:df11 -a 0 -D bin/lps-node-firmware.dfu -s :leave

# Generic rules
%.bin: %.elf
	$(OBJCOPY) $^ -O binary $@

%.dfu: %.bin
	$(PYTHON) tools/make/dfu-convert.py -b $(LOAD_ADDRESS):$^ $@

check_submodules:
	$(PYTHON) tools/make/check-for-submodules.py
