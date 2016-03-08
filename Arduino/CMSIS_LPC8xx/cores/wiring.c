#include "Arduino.h"

static void __empty() {
	// Empty
}
void init(void) __attribute__ ((weak, alias("__empty")));
void yield(void) __attribute__ ((weak, alias("__empty")));

static volatile uint32_t _ulTickCount;

void SysTick_Handler(void)
{
	_ulTickCount++;
}

unsigned long millis(void)
{
	return _ulTickCount ;
}

void delay(unsigned long ms)
{
	if (ms > 0) {
		uint32_t start = _ulTickCount;
		do {
			__WFI();
		} while (_ulTickCount - start < ms);
	}
}

unsigned long micros(void)
{
	extern volatile uint32_t mrt_counter;
	return (mrt_counter * 1000000UL) + ((SystemCoreClock - LPC_MRT->Channel[0].TIMER) >> 4);
}

void delayMicroseconds(unsigned int us)
{
	unsigned long t = micros() + us;
	while (micros() < t);
}
