#include "Arduino.h"

static void __empty() {
	// Empty
}
void init(void) __attribute__ ((weak, alias("__empty")));
void yield(void) __attribute__ ((weak, alias("__empty")));

static volatile uint32_t _ulTickCount;

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

static volatile uint32_t mrt_counter = 0;

void MRT_IRQHandler(void)
{
	if (LPC_MRT->Channel[0].STAT & MRT_STAT_IRQ_FLAG) {
		LPC_MRT->Channel[0].STAT = MRT_STAT_IRQ_FLAG;
		mrt_counter++;
	}
	if (LPC_MRT->Channel[2].STAT & MRT_STAT_IRQ_FLAG) {
		LPC_MRT->Channel[2].STAT = MRT_STAT_IRQ_FLAG;
		tone_handler();
	}
	if (LPC_MRT->Channel[3].STAT & MRT_STAT_IRQ_FLAG) {
		LPC_MRT->Channel[3].STAT = MRT_STAT_IRQ_FLAG;
		_ulTickCount++;
	}
}

unsigned long micros(void)
{
	return (mrt_counter * 1000000UL) + ((SystemCoreClock - LPC_MRT->Channel[0].TIMER) >> 4);
}

void delayMicroseconds(unsigned int us)
{
	unsigned long t = micros() + us;
	while (micros() < t);
}

void delayNonoseconds(unsigned int ns)
{
	uint32_t ivalue;
	LPC_MRT->Channel[1].CTRL   = MRT_ONE_SHOT_INT;
	LPC_MRT->Channel[1].INTVAL = ((SystemCoreClock / 1000000) * ns / 1000) | (1 << 31);
	while ((ivalue = LPC_MRT->Channel[1].TIMER & 0x7fffffff) != 0x7fffffff);
}
