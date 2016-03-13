#include "Arduino.h"

void Tone::attach(uint8_t pin, unsigned int frequency, unsigned long duration_in_msec)
{
	if (toneOutput < 0 || toneOutput == pin) {
		GPIOSetDir(PORT0, toneOutput = pin, OUTPUT);
		toneToggleCount = (duration_in_msec << 1) * frequency / 1000;
		LPC_MRT->Channel[2].CTRL   = MRT_REPEATED_MODE | MRT_INT_ENA;
		LPC_MRT->Channel[2].INTVAL = (1 << 31) | (SystemCoreClock / (frequency << 1));
	}
}

void Tone::detach(uint8_t pin)
{
	if (toneOutput == pin)
		LPC_MRT->Channel[2].INTVAL = (1 << 31);
}

void Tone::handler()
{
	if (toneOutput >= 0) {
		LPC_GPIO_PORT->NOT0 = (1 << toneOutput);
		if (toneToggleCount > 0 && --toneToggleCount == 0)
			detach(toneOutput);
	}
}

void tone_handler(void)
{
	toneGenerator.handler();
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration_in_msec)
{
	toneGenerator.attach(pin, frequency, duration_in_msec);
}

void noTone(uint8_t pin)
{
	toneGenerator.detach(pin);
}

Tone toneGenerator;
