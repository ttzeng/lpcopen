#include <stdexcept>
#include "Arduino.h"

#ifdef __cplusplus
extern "C" {
#endif
static void __empty() {
	// Empty
}
void init(void) __attribute__ ((weak, alias("__empty")));
void yield(void) __attribute__ ((weak, alias("__empty")));

static volatile uint32_t _ulMilliSecond = 0;

void RIT_IRQHandler(void)
{
	Chip_RIT_ClearInt(LPC_RITIMER);
	_ulMilliSecond++;
}

unsigned long millis(void)
{
	return _ulMilliSecond;
}

void delay(unsigned long ms)
{
	if (ms > 0) {
		uint32_t start = _ulMilliSecond;
		do {
			__WFI();
		} while (_ulMilliSecond - start < ms);
	}
}

unsigned long micros(void)
{
	return (_ulMilliSecond * 1000) + (Chip_RIT_GetCounter(LPC_RITIMER) / 100);
}

void delayMicroseconds(unsigned int us)
{
	unsigned long t = micros() + us;
	while (micros() < t);
}
#ifdef __cplusplus
}
#endif

void pinMode(uint8_t pin, uint8_t mode)
{
	int mux = gpioMapper.toGpioPortAndBit(pin);
	if (mux >= 0)
		Chip_GPIO_SetPinDIR(LPC_GPIO, IOGRP(mux), IONUM(mux), mode);
}

void digitalWrite(uint8_t pin, uint8_t val)
{
	int mux = gpioMapper.toGpioPortAndBit(pin);
	if (mux >= 0)
		Chip_GPIO_SetPinState(LPC_GPIO, IOGRP(mux), IONUM(mux), val);
}

int digitalRead(uint8_t pin)
{
	int mux = gpioMapper.toGpioPortAndBit(pin);
	if (mux >= 0)
		return Chip_GPIO_GetPinState(LPC_GPIO, IOGRP(mux), IONUM(mux));
}

GPIO_CFG_T gpioCfgDefault[] = {
		{ 0,  2, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* TXD0 */
		{ 0,  3, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* RXD0 */
		{ 2,  0, IOCON_MODE_INACT | IOCON_FUNC0, 13 }, /* LED0 */
		{ 2, 10, IOCON_MODE_INACT | IOCON_FUNC0,  2 }, /* INT Button */
		{ GPIO_CFG_UNASSIGNED }
};

Gpio::Gpio(GPIO_CFG_T* cfg)
{
	for (; cfg->port != GPIO_CFG_UNASSIGNED; cfg++) {
		Chip_IOCON_PinMuxSet(LPC_IOCON, cfg->port, cfg->bit, cfg->modefunc);
		if (cfg->pin != GPIO_CFG_UNASSIGNED) {
			gpioMapper.mapGpio(cfg->pin, IOMUX(cfg->port, cfg->bit));
		}
	}
}

void Gpio::mapGpio(uint8_t pin, uint8_t portAndBit)
{
	if (map.count(pin)) {
		/* pin already mapped */
		map.at(pin) = portAndBit;
	} else {
		/* insert a new mapping */
		map.insert(std::pair<uint8_t, uint16_t>(pin, portAndBit));
	}
}

void Gpio::unmapGpio(uint8_t pin)
{
	if (map.count(pin)) map.erase(pin);
}

int Gpio::toGpioPortAndBit(uint8_t pin)
{
	int portAndBit;
	try {
		portAndBit = map.at(pin);
	}
	catch (std::out_of_range& oor) {
		portAndBit = -1;
	}
	return portAndBit;
}

void Board::init()
{
#if defined(__USE_LPCOPEN)
#if !defined(NO_BOARD_LIB)
	// Read clock settings and update SystemCoreClock variable
	SystemCoreClockUpdate();
	// Set up and initialize all required blocks and
	// functions related to the board hardware
	Board_Init();
#endif
#endif
	setupTimer();
}

void Board::setupTimer()
{
	/* Initialize RITimer for delay APIs */
	Chip_RIT_Init(LPC_RITIMER);
	Chip_RIT_SetCOMPVAL(LPC_RITIMER, Chip_Clock_GetPeripheralClockRate(SYSCTL_PCLK_RIT) / 1000);
	Chip_RIT_EnableCTRL(LPC_RITIMER, RIT_CTRL_ENCLR);
	Chip_RIT_TimerDebugDisable(LPC_RITIMER);
	NVIC_EnableIRQ(RITIMER_IRQn);
}
