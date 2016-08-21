#ifndef Ultrasonic_h
#define Ultrasonic_h

#include "Arduino.h"

class Ultrasonic {
public:
	const uint8_t pinTrigger = 12,
	              pinEcho    = 11;
	Ultrasonic();
	float ping();
};

#endif
