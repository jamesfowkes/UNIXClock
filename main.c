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
 * AVR Library Includes
 */

#include "lib_i2c_common.h"
#include "lib_clk.h"
#include "lib_tmr8_tick.h"

/*
 * Device Includes
 */

#include "lib_ds3231.h"

/*
 * Generic Library Includes
 */
 
#include "button.h"
#include "ringbuf.h"
#include "statemachine.h"
#include "statemachine_common.h"

/*
 * Local Application Includes
 */

#include "unix_clock.h"

/*
 * Private Defines and Datatypes
 */

#define APP_TICK_MS 10
#define BLINK_TICK_MS 300
#define SYNC_TICK_MS (60UL * 1000)

#define UP_PORT PORTB
#define UP_PIN 0
#define DN_PORT PORTB
#define DN_PIN 1
#define DIGIT_PORT PORTB
#define DIGIT_PIN 2

enum states
{
	INIT,
	DISPLAY,
	EDIT,
	WRITING,
	READING
};
typedef enum states STATES;

enum events
{
	BTN_DIGIT_SELECT,
	BTN_UP,
	BTN_DN,
	BTN_IDLE,
	READ_START,
	READ_COMPLETE,
	WRITE_START,
	WRITE_COMPLETE
};
typedef enum events EVENTS;

/*
 * Function Prototypes
 */

static void setupTimer(void);

static void applicationTick(void);

static void onChronodotUpdate(bool write);

static void updateUnixTimeDigits(void);
static void updateDisplay(void);

static void startRead(void);
static void startWrite(void);
static void incDigit(void);
static void decDigit(void);

/*
 * Private Variables
 */

static TMR8_TICK_CONFIG appTick;
static TMR8_TICK_CONFIG heartbeatTick;
static TMR8_TICK_CONFIG timeSyncTick;

static uint8_t unixTimeDigits[10];

static uint8_t sm_index = 0;

static bool s_BlinkState = false;

static SM_ENTRY sm[] = {
	{INIT,		READ_START,			startRead,		READING},
	
	{DISPLAY,	READ_START,			startRead,		READING},
	{DISPLAY,	BTN_UP,				incDigit,		EDIT},
	{DISPLAY,	BTN_DN,				decDigit,		EDIT},
	
	{EDIT,		BTN_UP,				incDigit,		EDIT},
	{EDIT,		BTN_DN,				decDigit,		EDIT},
	{EDIT,		BTN_IDLE,			startWrite,		WRITING},
	
	{WRITING,	WRITE_COMPLETE,		startRead,		READING},
	
	{READING,	READ_COMPLETE,		NULL,			DISPLAY},
};

int main(void)
{

	/* Disable watchdog: not required for this application */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	sm_index = SM_Init((SM_STATE)INIT, (SM_EVENT)WRITE_COMPLETE, (SM_STATE)READING, sm);
	
	setupTimer();
	
	DS3231_Init();
	
	UC_BTN_Init(APP_TICK_MS);
	
	/* All processing interrupt based from here*/
	sei();

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

		if (TMR8_Tick_TestAndClear(&timeSyncTick))
		{
			SM_Event(sm_index, READ_START);
		}
	}

	return 0;
}

/*
 * Private Functions
 */
 
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

	timeSyncTick.reload = SYNC_TICK_MS;
	timeSyncTick.active = true;
	TMR8_Tick_AddTimerConfig(&timeSyncTick);
}

static void applicationTick(void)
{
	BTN_STATE_ENUM up = IO_Read(UP_PORT, UP_PIN); // Read up button state;
	BTN_STATE_ENUM dn = IO_Read(DN_PORT, DN_PIN); // Read dn button state;
	BTN_STATE_ENUM digit = IO_Read(DIGIT_PORT, DIGIT_PIN); // Read digit button state;
	
	UC_BTN_Tick(up, dn, digit);
	
	if (SM_GetState(sm_index) == DISPLAY)
	{
		updateUnixTimeDigits();
	}
	
	updateDisplay();

}

static void onChronodotUpdate(bool write)
{
	if (write)
	{
		SM_Event(sm_index, READ_COMPLETE);
	}
	else
	{
		SM_Event(sm_index, WRITE_COMPLETE);
	}
}

static void updateUnixTimeDigits(void)
{
	// Read from ChronoDot registers
	TM tm;
	DS3231_GetDateTime(&tm);

	uint32_t time = time_to_unix_seconds(&tm);
	int8_t i;
	uint32_t divisor = 1000000000;
	
	for (i = 0; i < 10; ++i)
	{
		unixTimeDigits[i] = time / divisor;
		time -= unixTimeDigits[i] * divisor;
		divisor = divisor / 10;
	}
	
	SM_Event(sm_index, READ_COMPLETE);
	
}

static void updateDisplay(void)
{
	uint8_t digit = 0;
	
	for (digit = 0; digit < 10; ++digit)
	{
		if ((int8_t)digit == UC_SelectedDigit())
		{
			if (s_BlinkState)
			{
				//TODO: Blank this digit
			}
		}
		//TODO: write out to shift registers
	}
}

static void startRead(void)
{
	DS3231_ReadDateTime(onChronodotUpdate);
}

static void startWrite(void)
{
	TM tm;
	
	uint32_t time = 0;
	int8_t i;
	uint32_t multiplier = 1000000000;
	
	for (i = 0; i < 10; ++i)
	{
		time += (unixTimeDigits[i] * multiplier);
		multiplier = multiplier / 10;
	}
	
	unix_seconds_to_time(time, &tm);
	
	DS3231_SetDateTime(&tm, false, onChronodotUpdate);
}

static void incDigit(void)
{
	int8_t thisDigit = UC_SelectedDigit();
	
	if (thisDigit == NO_DIGIT) { return; }
	
	incrementwithrollover(unixTimeDigits[thisDigit], 9);
}

static void decDigit(void)
{
	int8_t thisDigit = UC_SelectedDigit();
	
	if (thisDigit == NO_DIGIT) { return; }
	
	decrementwithrollover(unixTimeDigits[thisDigit], 9);
}

/* Button Functions */
void UC_SelectDigit(int8_t selectedDigit)
{
	if (selectedDigit == NO_DIGIT)
	{
		SM_Event(sm_index, BTN_IDLE);
	}
}

void UC_IncrementDigit(int8_t selectedDigit)
{
	(void)selectedDigit;
	SM_Event(sm_index, BTN_UP);
}

void UC_DecrementDigit(int8_t selectedDigit)
{
	(void)selectedDigit;
	SM_Event(sm_index, BTN_DN);
}
