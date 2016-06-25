#ifndef Motor_2WD_h
#define Motor_2WD_h

#include "Arduino.h"

typedef enum {
	MOTOR_LEFT = 0,
	MOTOR_RIGHT,
} MotorChannel;

class Motor2WD {
public:
	Motor2WD();
	void setSpeed(MotorChannel channel, float ratio);
};

extern Motor2WD Motor2;

#endif
