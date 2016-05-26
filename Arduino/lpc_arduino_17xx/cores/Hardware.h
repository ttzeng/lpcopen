#ifndef Hardware_h
#define Hardware_h

#include <map>

#define IOMUX(pingrp,pinnum)	(((pingrp)<<5)|(pinnum))
#define IOGRP(prop)				(((prop)>>5)&7)
#define IONUM(prop)				((prop)&0x1f)
#define IOPWM(prop)				(((prop)>>12)&7)

#define GPIO_CFG_UNASSIGNED	0xFF
typedef struct {
	byte port;
	byte bit;
	byte modefunc;
	byte pin;
} GPIO_CFG_T;

extern GPIO_CFG_T gpioCfgDefault[];

class Gpio {
public:
	static const int N_PWM = 6;
	Gpio(GPIO_CFG_T* cfg = gpioCfgDefault);
	void add(GPIO_CFG_T* cfg, byte pwm_ch = 0);
	void mapGpio(uint8_t pin, uint32_t prop);
	void unmapGpio(uint8_t pin);
	int getProp(uint8_t pin);
	void pwmSetDutyCycle(uint8_t pin, float ratio);
private:
	static volatile uint32_t* pwm_mr[N_PWM];
	std::map<uint8_t, uint32_t> map;
};

extern Gpio gpioMapper;

class Board {
public:
	virtual void init();
	virtual void setupTimer();
	virtual void setupPWM(uint32_t cycle_in_usec = 20000);
};

class TargetBoard {
public:
	TargetBoard(Board* board = new Board) {
		board->init();
	}
};

#endif
