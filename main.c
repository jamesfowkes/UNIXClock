/*
 * main.c
 *
 *  Startup file for a "UNIX" clock
 *  Uses ChronoDot RTC over i2c bus
 *
 *  Created on: 27 Aug 2012
 *      Author: james
 *
 *		Target: ATMEGA168 on Arduino Pro Mini board
 */

/*
 * Standard Library Includes
 */

#include <stdbool.h>
#include <string.h>
#include <stdint.h>

/*
 * Utility Includes
 */

#include "util_macros.h"
#include "util_time.h"

/*
 * AVR Includes (Defines and Primitives)
 */
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

/*
 * Device Includes
 */

#include "lib_ds3231.h"
#include "lib_shiftregister_common.h"
#include "lib_tlc5916.h"

/*
 * Generic Library Includes
 */

#include "button.h"
#include "ringbuf.h"
#include "statemachinemanager.h"
#include "statemachine.h"
#include "seven_segment_map.h"

/*
 * AVR Library Includes
 */

#include "lib_io.h"
#include "lib_i2c_common.h"
#include "lib_i2c_config.h"
#include "lib_clk.h"
#include "lib_tmr8_tick.h"
#include "lib_shiftregister.h"
#include "lib_pcint.h"
#include "lib_uart.h"

#include "util_bcd.h"

/*
 * Local Application Includes
 */

#include "unix_clock.h"
#include "compiletime.h"

/*
 * Private Defines and Datatypes
 */

#define APP_TICK_MS BUTTON_SCAN_PERIOD_MS
#define BLINK_TICK_MS 300

// Button input
#define eUP_PORT IO_PORTB
#define UP_PINS PINB
#define UP_PIN 0

#define eDIGIT_PORT IO_PORTB
#define DIGIT_PINS PINB
#define DIGIT_PIN 1

#define HB_PORT IO_PORTC
#define HB_PIN 0

// i2c pins
#define I2C_SCL_PORT	IO_PORTC
#define I2C_SCL_PIN		5
#define I2C_SDA_PORT	IO_PORTC
#define I2C_SDA_PIN		4

// Shift register clock and data
#define TLC_DATA_PORT PORTD
#define eTLC_DATA_PORT IO_PORTD
#define TLC_DATA_PIN 3
#define TLC_CLK_PORT PORTD
#define eTLC_CLK_PORT IO_PORTD
#define TLC_CLK_PIN 2
#define TLC_LATCH_PORT PORTD
#define eTLC_LATCH_PORT IO_PORTD
#define TLC_LATCH_PIN 4
#define TLC_OE_PORT PORTD
#define eTLC_OE_PORT IO_PORTD
#define TLC_OE_PIN 5

#define SECOND_TICK_PORT IO_PORTD
#define SECOND_TICK_PIN 6
#define SECOND_TICK_PCINT 22

enum states
{
	DISPLAY, EDIT, WRITING, MAX_STATES
};
typedef enum states STATES;

enum events
{
	BTN_DIGIT_SELECT,
	BTN_UP,
	BTN_IDLE,
	WRITE_COMPLETE,
	MAX_EVENTS
};
typedef enum events EVENTS;

/*
 * Function Prototypes
 */

static void setupTimer(void);
static void setupIO(void);
static void initialiseMap(void);
static void setupStateMachine(void);

static void applicationTick(void);

static void onChronodotUpdate(bool write);

static void updateUnixTimeDigits(void);
static void updateDisplay(void);

void putTimeToUART(void);

static void startWrite(SM_STATEID old, SM_STATEID new, SM_EVENT e);
static void incDigit(SM_STATEID old, SM_STATEID new, SM_EVENT e);

static void tlcOEFn(bool on);
static void tlcLatchFn(bool on);

/*
 * Private Variables
 */

static TMR8_TICK_CONFIG appTick;
static TMR8_TICK_CONFIG heartbeatTick;

static uint8_t unixTimeDigits[10] = COMPILE_TIME_DIGITS;

static int8_t sm_index = 0;

static bool s_displayDirty = false;
static bool s_BlinkState = false;

static const SM_STATE s_stateDisplay = {DISPLAY, NULL, NULL};
static const SM_STATE s_stateEdit = {EDIT, NULL, NULL};
static const SM_STATE s_stateWriting = {WRITING, NULL, NULL};

static const SM_ENTRY sm[] = {
	{ &s_stateDisplay, BTN_UP, incDigit, &s_stateEdit },
	{ &s_stateDisplay, BTN_DIGIT_SELECT, NULL, &s_stateEdit },

	{ &s_stateEdit, BTN_UP, incDigit, &s_stateEdit },
	{ &s_stateEdit, BTN_DIGIT_SELECT, NULL, &s_stateEdit },
	{ &s_stateEdit, BTN_IDLE, startWrite, &s_stateWriting },

	{ &s_stateWriting, WRITE_COMPLETE, NULL, &s_stateDisplay },
};

static UNIX_TIMESTAMP s_unixtime = COMPILE_TIME_INT;

static SEVEN_SEGMENT_MAP map = {
	0, // A
	1, // B
	3, // C
	4, // D
	5, // E
	7, // F
	6, // G
	2, // DP
};

static uint8_t displayMap[10];

static TLC5916_CONTROL tlc;

PCINT_VECTOR_ENUM secondTickVector;
static bool s_bTick;

static TM tm;
static TM compile_time = COMPILE_TIME_STRUCT;
int main(void)
{

	/* Disable watchdog: not required for this application */

	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	setupStateMachine();

	setupTimer();

	setupIO();

	initialiseMap();

	UART_Init(UART0, 9600, 32, 32, false);

	I2C_SetPrescaler(64);
	DS3231_Init();

	UC_BTN_Init(APP_TICK_MS);

	TLC5916_Init(&tlc, SR_ShiftOut, tlcLatchFn, tlcOEFn);

	TLC5916_OutputEnable(&tlc, true);

	uint8_t displayBytes[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	TLC5916_ClockOut(displayBytes, 10, &tlc);

	/* All processing interrupt based from here */
	
	sei();

	/* First get the time from the DS3231.
	 * Don't worry about the state machine or any events, just get the time
	 * and see if it's greater than the compile time
	 */

	DS3231_ReadDeviceDateTime(NULL);

	while ( !DS3231_IsIdle() ) { I2C_Task(); }
	DS3231_GetDateTime(&tm);

	DS3231_SetRate(DS3231_RATE_1HZ);
	DS3231_SQWINTControl(DS3231_SQW);
	DS3231_UpdateControl();

	while ( !DS3231_IsIdle() ) { I2C_Task(); }

	s_unixtime = time_to_unix_seconds(&tm);

	if (s_unixtime < COMPILE_TIME_INT)
	{
		TM compile_time = COMPILE_TIME_STRUCT;
		DS3231_SetDeviceDateTime(&compile_time, false, NULL);

		while ( !DS3231_IsIdle() ) { I2C_Task(); }

		s_unixtime = COMPILE_TIME_INT;
	}
	
	updateUnixTimeDigits();
	
	//putTimeToUART();
	
	s_displayDirty = true;
	
	while (true)
	{
		
		if (TMR8_Tick_TestAndClear(&appTick))
		{
			applicationTick();
		}

		if (TMR8_Tick_TestAndClear(&heartbeatTick))
		{
			s_BlinkState = !s_BlinkState;
			s_displayDirty |= true;
		}

		if (PCINT_TestAndClear(secondTickVector))
		{
			s_bTick = !s_bTick;
			if (s_bTick && (SM_GetState(sm_index) == (SM_STATEID)DISPLAY))
			{
				s_unixtime++;
				updateUnixTimeDigits();
				s_displayDirty |= true;
			}
			
			for(uint8_t i = 0; i < (uint8_t)SM_GetState(sm_index); ++i)
			{
				//IO_Control(HB_PORT, HB_PIN, IO_OFF);
				//IO_Control(HB_PORT, HB_PIN, IO_ON);
			}
		}
		
		if (s_displayDirty)
		{
			updateDisplay();
			s_displayDirty |= false;
		}

		I2C_Task();
	}
}

/*
 * Private Functions
 */

static void initialiseMap(void)
{
	uint8_t i;

	for (i = 0; i < 10; ++i)
	{
		displayMap[i] = SSEG_CreateDigit(i, &map, true);
		SSEG_AddDecimal(&displayMap[i], &map, false);
	}
}

static void setupIO(void)
{
	IO_SetMode(HB_PORT, HB_PIN, IO_MODE_OUTPUT);

	IO_SetMode(eUP_PORT, UP_PIN, IO_MODE_PULLUPINPUT);
	IO_SetMode(eDIGIT_PORT, DIGIT_PIN, IO_MODE_PULLUPINPUT);

	IO_SetMode(eTLC_DATA_PORT, TLC_DATA_PIN, IO_MODE_OUTPUT);
	IO_SetMode(eTLC_CLK_PORT, TLC_CLK_PIN, IO_MODE_OUTPUT);
	IO_SetMode(eTLC_OE_PORT, TLC_OE_PIN, IO_MODE_OUTPUT);
	IO_SetMode(eTLC_LATCH_PORT, TLC_LATCH_PIN, IO_MODE_OUTPUT);

	IO_SetMode(I2C_SCL_PORT, I2C_SCL_PIN, IO_MODE_I2C_PULLUP);
	IO_SetMode(I2C_SDA_PORT, I2C_SDA_PIN, IO_MODE_I2C_PULLUP);

	IO_SetMode(SECOND_TICK_PORT, SECOND_TICK_PIN, IO_MODE_PULLUPINPUT);
	secondTickVector = PCINT_EnableInterrupt(SECOND_TICK_PCINT, true);

	SR_Init(SFRP(TLC_DATA_PORT), TLC_DATA_PIN, SFRP(TLC_CLK_PORT), TLC_CLK_PIN);
}

static void setupTimer(void)
{
	CLK_Init(0);
	TMR8_Tick_Init(3, 0);

	appTick.reload = APP_TICK_MS;
	appTick.active = true;
	TMR8_Tick_AddTimerConfig(&appTick);

	heartbeatTick.reload = BLINK_TICK_MS;
	heartbeatTick.active = true;
	TMR8_Tick_AddTimerConfig(&heartbeatTick);
}

static void setupStateMachine(void)
{
	SMM_Config(1, 3);
	sm_index = SM_Init(&s_stateDisplay, (SM_EVENT)MAX_EVENTS, (SM_STATEID)MAX_STATES, sm);
	SM_SetActive(sm_index, true);
}

static void applicationTick(void)
{

	BTN_STATE_ENUM up = IO_Read(UP_PINS, UP_PIN); // Read up button state;
	BTN_STATE_ENUM digit = IO_Read(DIGIT_PINS, DIGIT_PIN); // Read digit button state;

	UC_BTN_Tick(up, digit);
}

static void onChronodotUpdate(bool write)
{
	if (write)
	{
		SM_Event(sm_index, WRITE_COMPLETE);
	}
}

static void updateUnixTimeDigits(void)
{
	uint32_t time = s_unixtime;
	int8_t i;
	uint32_t divisor = 1000000000;

	for (i = 0; i < 10; ++i)
	{
		unixTimeDigits[i] = time / divisor;
		time -= unixTimeDigits[i] * divisor;
		divisor = divisor / 10;
	}
}

static void updateDisplay(void)
{
	uint8_t place = 0;
	uint8_t displayBytes[10] =
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	for (place = 0; place < 10; ++place)
	{
		displayBytes[place] = displayMap[unixTimeDigits[place]];
		if ((int8_t) place == UC_SelectedDigit())
		{
			if (s_BlinkState)
			{
				displayBytes[place] = 0;
			}
		}
	}

	TLC5916_ClockOut(displayBytes, 10, &tlc);
}

static void startWrite(SM_STATEID old, SM_STATEID new, SM_EVENT e)
{
	(void)old; (void)new; (void)e;

	UNIX_TIMESTAMP time = 0;
	int8_t i;
	uint32_t multiplier = 1000000000;

	for (i = 0; i < 10; ++i)
	{
		time += (unixTimeDigits[i] * multiplier);
		multiplier = multiplier / 10;
	}

	s_unixtime = time;

	unix_seconds_to_time(time, &tm);

	DS3231_SetDeviceDateTime(&tm, false, onChronodotUpdate);
}

static void incDigit(SM_STATEID old, SM_STATEID new, SM_EVENT e)
{
	(void)old; (void)new; (void)e;
	int8_t thisDigit = UC_SelectedDigit();

	if (thisDigit == NO_DIGIT)
	{
		return;
	}

	incrementwithrollover(unixTimeDigits[thisDigit], 9);
	s_displayDirty |= true;
}

void putTimeToUART(void)
{
	UART_PutChar(UART0, compile_time.tm_sec);
	UART_PutChar(UART0, tm.tm_sec);
	UART_PutChar(UART0, compile_time.tm_min);
	UART_PutChar(UART0, tm.tm_min);
	UART_PutChar(UART0, compile_time.tm_hour);
	UART_PutChar(UART0, tm.tm_hour);
	UART_PutChar(UART0, compile_time.tm_mday);
	UART_PutChar(UART0, tm.tm_mday);
	UART_PutChar(UART0, compile_time.tm_mon+1);
	UART_PutChar(UART0, tm.tm_mon+1);
	UART_PutChar(UART0, compile_time.tm_year);
	UART_PutChar(UART0, tm.tm_year);	
}

/* Button Functions */

void UC_SelectDigit(int8_t selectedDigit)
{
	if (selectedDigit != NO_DIGIT)
	{
		SM_Event(sm_index, BTN_DIGIT_SELECT);
	}
	else
	{
		SM_Event(sm_index, BTN_IDLE);
	}
	s_displayDirty |= true;
}

void UC_IncrementDigit(int8_t selectedDigit)
{
	(void) selectedDigit;
	SM_Event(sm_index, BTN_UP);
}

/* IO functions for TLC5916 */

static void tlcOEFn(bool on)
{
	on ? IO_On(TLC_OE_PORT, TLC_OE_PIN) : IO_Off(TLC_OE_PORT, TLC_OE_PIN);
}
static void tlcLatchFn(bool on)
{
	on ? IO_On(TLC_LATCH_PORT, TLC_LATCH_PIN) :
			IO_Off(TLC_LATCH_PORT, TLC_LATCH_PIN);
}

