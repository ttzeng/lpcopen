#include "Arduino.h"
#include "Servo.h"

int8_t Servo::attach(uint8_t pin, uint16_t min, uint16_t max)
{
	int8_t rc = SCT.attach(pin, (uint32_t)(srvCurPulse = DEFAULT_PULSE_WIDTH));
	if (rc >= 0) {
		srvPin = pin;
		srvMinPulse = min, srvMaxPulse = max;
	}
	return rc;
}

void Servo::detach()
{
	if (attached()) {
		SCT.detach(srvPin);
		srvPin = -1;
	}
}

int Servo::read()
{
	return attached()? map(srvCurPulse, srvMinPulse, srvMaxPulse, 0, 180) : -1;
}

void Servo::write(int angle)
{
	angle = constrain(angle, 0, 180);
	writeMicroseconds((uint32_t)map(angle, 0, 180, srvMinPulse, srvMaxPulse));
}

void Servo::writeMicroseconds(int usec)
{
	usec = constrain(usec, srvMinPulse, srvMaxPulse);
	if (attached())
		SCT.attach(srvPin, (uint32_t)(srvCurPulse = usec));
}
