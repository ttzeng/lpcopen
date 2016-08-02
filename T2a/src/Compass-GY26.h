#ifndef Compass_GY26_h
#define Compass_GY26_h

#include "Arduino.h"

class CompassGY26 {
public:
	static const byte address = 0x70;
	typedef enum {
		getAngle = 0x31,
		calibration = 0xc0,
		finishCalibration = 0xc1,
	} gy26Command;
	CompassGY26();
	float getHeading();
};

#endif
