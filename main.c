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

#include "util_bcd.h"

/*
 * Local Application Includes
 */

#include "unix_clock.h"
#include "compiletime.h"

/*
 * Private Defines and Datatypes
 */

#define APP_TICK_MS 10
#define BLINK_TICK_MS 300

// Button input
#define eUP_PORT IO_PORTB
#define UP_PINS PINB
#define UP_PIN 0

#define eDIGIT_PORT IO_PORTB
#define DIGIT_PINS PINB
#define DIGIT_PIN 1

#define HB_PORT IO_PORTB
#define HB_PIN 5

// i2c pins
#define I2C_SCL_PORT	IO_PORTC
#define I2C_SCL_PIN		5
#define I2C_SDA_PORT	IO_PORTC
#define I2C_SDA_PIN		4

// Shift register clock and data
#define TLC_DATA_PORT PORTD
#define eTLC_DATA_PORT IO_PORTD
#define TLC_DATA_PIN 4
#define TLC_CLK_PORT PORTD
#define eTLC_CLK_PORT IO_PORTD
#define TLC_CLK_PIN 5
#define TLC_LATCH_PORT PORTD
#define eTLC_LATCH_PORT IO_PORTD
#define TLC_LATCH_PIN 3
#define TLC_OE_PORT PORTD
#define eTLC_OE_PORT IO_PORTD
#define TLC_OE_PIN 2

#define SECOND_TICK_PORT IO_PORTD
#define SECOND_TICK_PIN 6
#define SECOND_TICK_PCINT 22

enum states
{
	DISPLAY, EDIT, WRITING
};
typedef enum states STATES;

enum events
{
	BTN_DIGIT_SELECT,
	BTN_UP,
	BTN_IDLE,
	WRITE_START,
	WRITE_COMPLETE
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

static bool s_BlinkState = false;

static const SM_STATE s_stateDisplay = {DISPLAY, NULL, NULL};
static const SM_STATE s_stateEdit = {EDIT, NULL, NULL};
static const SM_STATE s_stateWriting = {WRITING, NULL, NULL};

static SM_ENTRY sm[] = {
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

int main(void)
{

	/* Disable watchdog: not required for this application */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	setupStateMachine();

	setupTimer();

	setupIO();

	initialiseMap();

	DS3231_Init();
	I2C_SetPrescaler(64);

	UC_BTN_Init(APP_TICK_MS);

	TLC5916_Init(&tlc, SR_ShiftOut, tlcLatchFn, tlcOEFn);

	TLC5916_OutputEnable(&tlc, true);

	uint8_t displayBytes[10] =
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	TLC5916_ClockOut(displayBytes, 10, &tlc);

	/* All processing interrupt based from here*/

	sei();

	DS3231_SetRate(DS3231_RATE_1HZ);
	DS3231_SQWINTControl(DS3231_SQW);
	DS3231_UpdateControl();

	while ( !DS3231_IsIdle() ) { I2C_Task(); }

	/* First get the time from the DS3231.
	 * Don't worry about the state machine or any events, just get the time
	 * and see if it's greater than the compile time
	 */

	DS3231_ReadDeviceDateTime(NULL);

	while ( !DS3231_IsIdle() ) { I2C_Task(); }

	TM tm;
	DS3231_GetDateTime(&tm);

	s_unixtime = time_to_unix_seconds(&tm);

	if (s_unixtime < COMPILE_TIME_INT)
	{
		unix_seconds_to_time(COMPILE_TIME_INT, &tm);
		DS3231_SetDeviceDateTime(&tm, false, NULL);

		while ( !DS3231_IsIdle() ) { I2C_Task(); }

		s_unixtime = COMPILE_TIME_INT;
	}

	updateUnixTimeDigits();

	while (true)
	{
		if (TMR8_Tick_TestAndClear(&appTick))
		{
			applicationTick();
		}

		if (TMR8_Tick_TestAndClear(&heartbeatTick))
		{
			s_BlinkState = !s_BlinkState;
		}

		if (PCINT_TestAndClear(secondTickVector))
		{
			s_bTick = !s_bTick;
			if (s_bTick && (SM_GetState(sm_index) == (SM_STATEID)DISPLAY))
			{
				s_unixtime++;
				IO_Control(HB_PORT, HB_PIN, IO_TOGGLE);
				updateUnixTimeDigits();
			}
		}

		I2C_Task();
	}

	return 0;
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
	sm_index = SM_Init(&s_stateDisplay, (SM_EVENT)WRITE_COMPLETE, (SM_STATEID)WRITING, sm);
	SM_SetActive(sm_index, true);
}

static void applicationTick(void)
{

	BTN_STATE_ENUM up = IO_Read(UP_PINS, UP_PIN); // Read up button state;
	BTN_STATE_ENUM digit = IO_Read(DIGIT_PINS, DIGIT_PIN); // Read digit button state;

	UC_BTN_Tick(up, digit);

	updateDisplay();
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
	static TM tm;

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
