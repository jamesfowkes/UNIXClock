#ifndef _UNIX_CLOCK_H_
#define _UNIX_CLOCK_H_

/*
 * Defines and Typedefs
 */

#define BUTTON_SCAN_PERIOD_MS (25)

#define NUM_DIGITS (10)
#define MAX_DIGIT_INDEX (NUM_DIGITS-1)

#define NO_DIGIT (-1)

bool UC_BTN_Init(uint8_t scanPeriodMs);
void UC_BTN_Tick(BTN_STATE_ENUM up, BTN_STATE_ENUM digit);

int8_t UC_SelectedDigit(void);

void UC_SelectDigit(int8_t selectedDigit);
void UC_IncrementDigit(int8_t selectedDigit);

#endif
