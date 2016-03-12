#include "Arduino.h"

#define SWM_ACOMP_I2		1

void analogDetach(uint8_t pin)
{
	SCT.detach(pin);
}

void analogWrite(uint8_t pin, uint8_t val)
{
	SCT.attach(pin, val / 255.f);
}

#define ACOMP_VOLTAGE_LADDER_STEPS			32
#define ACOMP_VOLTAGE_LADDER_SETTLE_TIME	100		// Arduino takes about 100 usec to read an analog input

int analogRead(uint8_t pin)
{
	if (pin == SWM_ACOMP_I2) {
		static bool initialized = false;
		if (!initialized) {
			/* Comparator should be powered up first; use of comparator requires BOD */
			LPC_SYSCON->PDRUNCFG &= ~((1 << 15) | (1 << 3));
			/* Enable the clock to the comparator register interface */
			LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 19) | (1 << 18) | (1 << 7);
			/* Reset the analog comparator */
			LPC_SYSCON->PRESETCTRL &= ~(1 << 12);
			LPC_SYSCON->PRESETCTRL |=  (1 << 12);
			/* Disable pull-up/pull-down resistor */
			LPC_IOCON->PIO0_1 &= ~(0x3 << 3);
			/* Enable ACOMP_I2 fixed-pin function. Require to disable CLKIN */
			LPC_SWM->PINENABLE0 = (LPC_SWM->PINENABLE0 & ~(1 << 1)) | (1 << 7);
			LPC_SYSCON->SYSAHBCLKCTRL &= ~((1 << 18) | (1 << 7));
			/* Select voltage ladder as comparator positive voltage input, and ACOMP_I2 as negative voltage input */
			LPC_CMP->CTRL = (0x0 << 8) | (0x2 << 11);

			initialized = true;
		}
		byte step;
		for (step = 0; step < ACOMP_VOLTAGE_LADDER_STEPS - 1; step++) {
			LPC_CMP->LAD = (step << 1) | 0x1;
			delayMicroseconds(ACOMP_VOLTAGE_LADDER_SETTLE_TIME);
			if (LPC_CMP->CTRL & COMPSTAT)
				break;
		}
		return map(step, 0, ACOMP_VOLTAGE_LADDER_STEPS, 0, 1023);
	}
}
