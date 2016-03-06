#include "Arduino.h"

void pinMode(uint8_t pin, uint8_t mode)
{
	GPIOSetDir(PORT0, pin, mode);
}

void digitalWrite(uint8_t pin, uint8_t val)
{
	analogDetach(pin);
	GPIOSetBitValue(PORT0, pin, val);
}

int digitalRead(uint8_t pin)
{
	analogDetach(pin);
	return GPIOGetPinValue(PORT0, pin);
}
