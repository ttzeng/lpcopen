#ifndef SCTimer_h
#define SCTimer_h

#include <inttypes.h>

class SCTimer {
public:
	SCTimer();
	int8_t attach(uint8_t pin, uint32_t duty_in_usec = 0);
	int8_t attach(uint8_t pin, float duty_ratio);
	void detach(uint8_t pin);
private:
	void configMatchEvent(uint8_t ch, uint32_t match);
	static int8_t channel(int8_t pin);
	static int8_t outputMap[];
};

extern SCTimer SCT;

#endif
