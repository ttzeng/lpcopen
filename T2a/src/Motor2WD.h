#ifndef Motor_2WD_h
#define Motor_2WD_h

#include "Arduino.h"

typedef enum {
	MOTOR_LEFT = 0,
	MOTOR_RIGHT,
} MotorChannel;

class Motor2WD {
public:
	const uint8_t pinDirL = 4;
	const uint8_t pinPwmL = 5;
	const uint8_t pinPwmR = 6;
	const uint8_t pinDirR = 7;
	Motor2WD();
	void setSpeed(MotorChannel channel, float ratio);
};

#endif
