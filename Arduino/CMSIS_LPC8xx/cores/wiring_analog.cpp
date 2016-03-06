#include "Arduino.h"

void analogDetach(uint8_t pin)
{
	SCT.detach(pin);
}

void analogWrite(uint8_t pin, uint8_t val)
{
	SCT.attach(pin, val / 255.f);
}
