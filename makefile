NAME=test

AVR_DIR=C:/WinAVR-20100110/bin

CC=$(AVR_DIR)/avr-gcc
MCU_TARGET=atmega328p
LIBS_DIR = $(PROJECTS_PATH)\Libs

DEL = python $(PROJECTS_PATH)\Libs\del.py

OPT_LEVEL=3

ERROR_FILE="error.txt"

INCLUDE_DIRS = \
	-I$(LIBS_DIR)/AVR \
	-I$(LIBS_DIR)/Common \
	-I$(LIBS_DIR)/Devices \
	-I$(LIBS_DIR)/Generics \
	-I$(LIBS_DIR)/Utility

CFILES = \
	main.c \
	unixclock_buttons.c \
	$(LIBS_DIR)/Utility/util_time.c \
	$(LIBS_DIR)/Utility/util_bcd.c \
	$(LIBS_DIR)/AVR/lib_clk.c \
	$(LIBS_DIR)/AVR/lib_fuses.c \
	$(LIBS_DIR)/AVR/lib_tmr8.c \
	$(LIBS_DIR)/AVR/lib_tmr8_tick.c \
	$(LIBS_DIR)/AVR/lib_i2c.c \
	$(LIBS_DIR)/AVR/lib_i2c_mt.c \
	$(LIBS_DIR)/AVR/lib_i2c_mr.c \
	$(LIBS_DIR)/Devices/lib_ds3231.c \
	$(LIBS_DIR)/Generics/button.c \
	$(LIBS_DIR)/Generics/task.c \
	$(LIBS_DIR)/Generics/ringbuf.c \
	$(LIBS_DIR)/Generics/statemachine_common.c \
	$(LIBS_DIR)/Generics/statemachine_static.c \
	$(LIBS_DIR)/Generics/statemachine.c \

OPTS = \
	-g \
	-Wall \
	-Wextra \
	-DF_CPU=8000000 \
	-DMAX_EVENT_QUEUE=2 \
	-DNUM_STATE_MACHINES=1 \
	-DUNIX_TIME_TYPE=int32_t

LDFLAGS = \
	-Wl

OBJDEPS=$(CFILES:.c=.o)

all: init $(NAME).elf errors

init:
	DEL $(ERROR_FILE)
	python $(LIBS_DIR)\compiletime.py 
	
$(NAME).elf: $(OBJDEPS)
	$(CC) $(INCLUDE_DIRS) $(OPTS) $(LDFLAGS) -O$(OPT_LEVEL) -mmcu=$(MCU_TARGET) -o $@ $^

%.o:%.c
	$(CC) $(INCLUDE_DIRS) $(OPTS) -O$(OPT_LEVEL) -mmcu=$(MCU_TARGET) -c $< -o $@ 2>>$(ERROR_FILE)

errors:
	@echo "Errors and Warnings:"
	@cat $(ERROR_FILE)
	
clean:
	DEL $(NAME).elf
	DEL $(OBJDEPS)