#include "Arduino.h"

void pinMode(uint8_t pin, uint8_t mode)
{
	GPIOSetDir(PORT0, pin, mode);
}

void digitalWrite(uint8_t pin, uint8_t val)
{
	GPIOSetBitValue(PORT0, pin, val);
}

int digitalRead(uint8_t pin)
{
	return GPIOGetPinValue(PORT0, pin);
}
