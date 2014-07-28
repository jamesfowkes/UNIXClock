include $(PROJECTS_PATH)/Libs/standard.mk

NAME=UNIXClock

MCU_TARGET=atmega328p
AVRDUDE_PART=m328p

CFILES = \
	main.c \
	unixclock_buttons.c \
	$(LIBS_DIR)/Utility/util_time.c \
	$(LIBS_DIR)/Utility/util_bcd.c \
	$(LIBS_DIR)/AVR/lib_clk.c \
	$(LIBS_DIR)/AVR/lib_fuses.c \
	$(LIBS_DIR)/AVR/lib_io.c \
	$(LIBS_DIR)/AVR/lib_pcint.c \
	$(LIBS_DIR)/AVR/lib_tmr8.c \
	$(LIBS_DIR)/AVR/lib_tmr8_tick.c \
	$(LIBS_DIR)/Generics/memorypool.c \
	$(LIBS_DIR)/Generics/button.c \
	$(LIBS_DIR)/AVR/lib_shiftregister.c \
	$(LIBS_DIR)/AVR/lib_uart.c \
	$(LIBS_DIR)/Devices/lib_tlc5916.c \
	$(LIBS_DIR)/Generics/ringbuf.c \
	$(LIBS_DIR)/Generics/seven_segment_map.c \
	$(LIBS_DIR)/Generics/statemachinemanager.c \
	$(LIBS_DIR)/Generics/statemachine.c \
	$(LIBS_DIR)/AVR/lib_i2c.c \
	$(LIBS_DIR)/Common/lib_i2c.c \
	$(LIBS_DIR)/Common/lib_i2c_mt.c \
	$(LIBS_DIR)/Common/lib_i2c_mr.c \
	$(LIBS_DIR)/Devices/lib_ds3231.c

EXTRA_FLAGS = \
	-DF_CPU=8000000 \
	-DUNIX_TIME_TYPE=int32_t \
	-DMEMORY_POOL_BYTES=512 \
	-DMAX_EVENT_QUEUE=2 \
	-DNUM_STATE_MACHINES=1 \
	-DI2C_MT \
	-DI2C_MR

include $(LIBS_DIR)/AVR/make_avr.mk

