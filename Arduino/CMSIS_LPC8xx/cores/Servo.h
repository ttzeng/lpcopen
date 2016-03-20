#ifndef Servo_h
#define Servo_h

#include <inttypes.h>

#define MIN_PULSE_WIDTH		544		// the shortest pulse sent to a servo
#define MAX_PULSE_WIDTH		2400	// the longest pulse sent to a servo
#define DEFAULT_PULSE_WIDTH	1500	// default pulse width when servo is attached

#define MAX_SERVOS	4

class Servo {
public:
	Servo() : srvPin(-1) {}
	int8_t attach(uint8_t pin, uint16_t min = MIN_PULSE_WIDTH, uint16_t max = MAX_PULSE_WIDTH);
	void detach();
	int read();
	void write(int angle);
	void writeMicroseconds(int usec);
	bool attached() { return (srvPin >= 0); }
protected:
	int8_t srvPin;
	uint16_t srvCurPulse;
	uint16_t srvMinPulse;
	uint16_t srvMaxPulse;
};

#endif
