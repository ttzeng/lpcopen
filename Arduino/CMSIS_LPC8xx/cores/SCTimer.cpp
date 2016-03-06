#include "Arduino.h"

#define N_CHANNEL			4
#define SCT_LIMIT_50HZ		20000	// Set SCT outputs every 20ms (50Hz)

int8_t SCTimer::outputMap[N_CHANNEL] = { -1, -1, -1, -1 };

SCTimer::SCTimer()
{
	// Enable the clock to the SCT register	interface and peripheral clock
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 8);
	// Clear the SCT peripheral reset
	LPC_SYSCON->PRESETCTRL |= (1 << 8);

	// Set up the SCT as one unified 32-bit reload timer, no auto limit
	LPC_SCT->CONFIG = 0x00000001;
	// Set the prescaled factor, clear and halt the counter
	LPC_SCT->CTRL_U = ((SystemCoreClock/1000000UL - 1) << 5) | (1 << 3) | (1 << 2);
	// Optional preload the unified timer with a count value to LPC_SCT->COUNT_U
	// Configure match/limit registers
	// Create match events and optional interrupts on those match events
	// Connect CTOUTn pins to match events
	LPC_SCT->REGMODE_L  = 0;
	LPC_SCT->LIMIT_L    = (1 << 4);		// Use MAT4 as count limit
	configMatchEvent(4, SCT_LIMIT_50HZ - 1);

	int8_t ch;
	for (ch = 0; ch < N_CHANNEL; ch++) {
		configMatchEvent(ch, 0);
		LPC_SCT->OUT[ch].SET = (1 << 4);
		LPC_SCT->OUT[ch].CLR = (1 << ch);
	}

	// Start the counter
	LPC_SCT->CTRL_U &= ~(1 << 2);
}

void SCTimer::configMatchEvent(uint8_t ch, uint32_t match)
{
	LPC_SCT->MATCH[ch].U = LPC_SCT->MATCHREL[ch].U = match;
	LPC_SCT->EVENT[ch].STATE = 1;
	LPC_SCT->EVENT[ch].CTRL  = 0x1000 | ch;
}

int8_t SCTimer::attach(uint8_t pin, uint32_t duty_in_usec)
{
	int8_t ch = channel(pin);
	if (ch < 0)
		ch = channel(-1);
	if (ch >= 0) {
		outputMap[ch] = pin;
		Board::assignMovablePin(6+((ch+3)>>2), ((ch+3)&3)<<3, pin);
		LPC_SCT->MATCHREL[ch].U = duty_in_usec;
	}
	return ch;
}

int8_t SCTimer::attach(uint8_t pin, float duty_ratio)
{
	return attach(pin, (uint32_t)((SCT_LIMIT_50HZ-1)*duty_ratio));
}

void SCTimer::detach(uint8_t pin)
{
	int8_t ch = channel(pin);
	if (ch >= 0) {
		outputMap[ch] = -1;
		Board::assignMovablePin(6+((ch+3)>>2), ((ch+3)&3)<<3, 0xff);
	}
}

int8_t SCTimer::channel(int8_t pin)
{
	int8_t ch;
	for (ch = N_CHANNEL; --ch >= 0; )
		if (outputMap[ch] == pin)
			break;
	return ch;
}

SCTimer SCT;
