/*
 * Standard Library Includes
 */

#include <stdbool.h>
#include <stdint.h>

/*
 * Generic Library Includes
 */

#include "button.h"

/*
 * Local Application Includes
 */

#include "unix_clock.h"

/*
 * Defines and Typedefs
 */

#define BUTTON_DEBOUNCE_MS (100)
#define BUTTON_REPEAT_MS (1000)
#define BUTTON_REPEAT_COUNT (BUTTON_REPEAT_MS / BUTTON_SCAN_PERIOD_MS)
#define BUTTON_DEBOUNCE_COUNT (BUTTON_DEBOUNCE_MS / BUTTON_SCAN_PERIOD_MS)

#define IDLE_MS_COUNT (2000)

/*
 * Private Function Prototypes
 */

void upBtnRepeat(void);
void upBtnChange(BTN_STATE_ENUM state);

void dnBtnRepeat(void);
void dnBtnChange(BTN_STATE_ENUM state);

void digitBtnRepeat(void);
void digitBtnChange(BTN_STATE_ENUM state);

/*
 * Local Variables
 */
 
static BTN upButton = 
{
	.current_state = BTN_STATE_INACTIVE,
	.change_state_callback = upBtnChange,
	.repeat_callback = upBtnRepeat,
	.max_repeat_count = BUTTON_REPEAT_COUNT,
	.max_debounce_count = BUTTON_DEBOUNCE_COUNT
};

static BTN dnButton = 
{
	.current_state = BTN_STATE_INACTIVE,
	.change_state_callback = dnBtnChange,
	.repeat_callback = dnBtnRepeat,
	.max_repeat_count = BUTTON_REPEAT_COUNT,
	.max_debounce_count = BUTTON_DEBOUNCE_COUNT
};

static BTN digitButton = 
{
	.current_state = BTN_STATE_INACTIVE,
	.change_state_callback = digitBtnChange,
	.repeat_callback = digitBtnRepeat,
	.max_repeat_count = BUTTON_REPEAT_COUNT,
	.max_debounce_count = BUTTON_DEBOUNCE_COUNT
};

static int8_t s_selectedDigit = NO_DIGIT;
static uint16_t s_idleCount = 0;

static uint8_t s_scanPeriodMs;

/*
 * Public Functions
 */
 
bool UC_BTN_Init(uint8_t scanPeriodMs)
{
	bool success = true;
	
	s_scanPeriodMs = scanPeriodMs;
	
	success &= BTN_InitHandler(&upButton);
	success &= BTN_InitHandler(&dnButton);
	success &= BTN_InitHandler(&digitButton);
	
	return success;
}
 
void UC_BTN_Tick(BTN_STATE_ENUM up, BTN_STATE_ENUM dn, BTN_STATE_ENUM digit)
{
	BTN_Update(&upButton, up);
	BTN_Update(&dnButton, dn);
	BTN_Update(&digitButton, digit);

	if ((up == BTN_STATE_INACTIVE) && (dn == BTN_STATE_INACTIVE) && (digit == BTN_STATE_INACTIVE))
	{
		if (s_selectedDigit != NO_DIGIT) // Selected digit != NO_DIGIT indicates non-idle state
		{
			s_idleCount += s_scanPeriodMs;
			if (s_idleCount == IDLE_MS_COUNT)
			{
				s_idleCount = 0;
				s_selectedDigit = NO_DIGIT;
			}
		}
	}
}

int8_t UC_SelectedDigit(void)
{
	return s_selectedDigit;
}

/*
 * Private Functions
 */

void upBtnRepeat(void)
{
	UC_IncrementDigit(s_selectedDigit);
}

void upBtnChange(BTN_STATE_ENUM state)
{
	if (state == BTN_STATE_ACTIVE)
	{
		if (s_selectedDigit == NO_DIGIT) { s_selectedDigit = 0; }
		upBtnRepeat();
	}
}

void dnBtnRepeat(void)
{
	UC_DecrementDigit(s_selectedDigit);
}

void dnBtnChange(BTN_STATE_ENUM state)
{
	if (state == BTN_STATE_ACTIVE)
	{
		if (s_selectedDigit == NO_DIGIT) { s_selectedDigit = 0; }
		dnBtnRepeat();
	}
}

void digitBtnRepeat(void)
{
	s_selectedDigit = (s_selectedDigit < MAX_DIGIT_INDEX) ? s_selectedDigit + 1 : 0;
}

void digitBtnChange(BTN_STATE_ENUM state)
{
	if (state == BTN_STATE_ACTIVE) { digitBtnRepeat(); }
}
