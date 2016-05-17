#ifndef Hardware_h
#define Hardware_h

#include <map>

#define IOMUX(pingrp,pinnum)	(((pingrp)<<5)|(pinnum))
#define IOGRP(mux)				((mux)>>5)
#define IONUM(mux)				((mux)&0x1f)

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
	Gpio(GPIO_CFG_T* cfg = gpioCfgDefault);
	void mapGpio(uint8_t pin, uint8_t portAndBit);
	void unmapGpio(uint8_t pin);
	int toGpioPortAndBit(uint8_t pin);
private:
	std::map<uint8_t, uint8_t> map;
};

extern Gpio gpioMapper;

class Board {
public:
	virtual void init();
	virtual void setupTimer();
};

class TargetBoard {
public:
	TargetBoard(Board* board = new Board) {
		board->init();
	}
};

#endif
