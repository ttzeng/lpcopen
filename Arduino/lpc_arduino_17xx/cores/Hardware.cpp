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
	int prop = gpioMapper.getProp(pin);
	if (prop >= 0 && !IOPWM(prop))
		Chip_GPIO_SetPinDIR(LPC_GPIO, IOGRP(prop), IONUM(prop), mode);
}

void digitalWrite(uint8_t pin, uint8_t val)
{
	int prop = gpioMapper.getProp(pin);
	if (prop >= 0) {
		if (!IOPWM(prop))
			Chip_GPIO_SetPinState(LPC_GPIO, IOGRP(prop), IONUM(prop), val);
		else
			gpioMapper.pwmSetDutyCycle(pin, val);
	}
}

int digitalRead(uint8_t pin)
{
	int prop = gpioMapper.getProp(pin);
	if (prop >= 0 && !IOPWM(prop))
		return Chip_GPIO_GetPinState(LPC_GPIO, IOGRP(prop), IONUM(prop));
}

void analogWrite(uint8_t pin, uint8_t val)
{
	int prop = gpioMapper.getProp(pin);
	if (prop >= 0 && IOPWM(prop))
		gpioMapper.pwmSetDutyCycle(pin, val / 255.f);
}

GPIO_CFG_T gpioCfgDefault[] = {
		{ 0,  2, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* TXD0 */
		{ 0,  3, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* RXD0 */
		{ 2,  0, IOCON_MODE_INACT | IOCON_FUNC0, 13 }, /* LED0 */
		{ 2, 10, IOCON_MODE_INACT | IOCON_FUNC0,  2 }, /* INT Button */
		{ GPIO_CFG_UNASSIGNED }
};

volatile uint32_t* Gpio::pwm_mr[] = {
		&LPC_PWM1->MR1, &LPC_PWM1->MR2, &LPC_PWM1->MR3,
		&LPC_PWM1->MR4, &LPC_PWM1->MR5, &LPC_PWM1->MR6,
};

Gpio::Gpio(const GPIO_CFG_T* cfg)
{
	for (; cfg && cfg->port != GPIO_CFG_UNASSIGNED; cfg++)
		add(cfg);
}

void Gpio::add(const GPIO_CFG_T* cfg, byte pwm_ch)
{
	if (cfg && cfg->port != GPIO_CFG_UNASSIGNED) {
		Chip_IOCON_PinMuxSet(LPC_IOCON, cfg->port, cfg->bit, cfg->modefunc);
		if (cfg->pin != GPIO_CFG_UNASSIGNED) {
			gpioMapper.mapGpio(cfg->pin,
					((uint32_t)pwm_ch << 12) | ((uint32_t)cfg->modefunc << 8) | IOMUX(cfg->port, cfg->bit));
			if (0 < pwm_ch && pwm_ch <= N_PWM) {
				*pwm_mr[pwm_ch - 1] = LPC_PWM1->MR0;
				LPC_PWM1->LER |= (1 << pwm_ch);         // Update match register on next reset
				LPC_PWM1->PCR |= (1 << (pwm_ch + 8));   // Enable PWM.1n output
			}
		}
	}
}

void Gpio::mapGpio(uint8_t pin, uint32_t prop)
{
	if (map.count(pin)) {
		/* pin already mapped */
		map.at(pin) = prop;
	} else {
		/* insert a new mapping */
		map.insert(std::pair<uint8_t, uint32_t>(pin, prop));
	}
}

void Gpio::unmapGpio(uint8_t pin)
{
	if (map.count(pin)) map.erase(pin);
}

int Gpio::getProp(uint8_t pin)
{
	return map.count(pin)? map.at(pin) : -1;
}

void Gpio::pwmSetDutyCycle(uint8_t pin, float ratio)
{
	byte pwm_ch;
	int prop = getProp(pin);
	if (prop >= 0 && (pwm_ch = IOPWM(prop)) > 0 && pwm_ch <= N_PWM) {
		*pwm_mr[pwm_ch - 1] = ratio * LPC_PWM1->MR0;
		LPC_PWM1->LER |= (1 << pwm_ch);
	}
}

void Board::init()
{
#if defined(__USE_LPCOPEN)
#if !defined(NO_BOARD_LIB)
	// Read clock settings and update SystemCoreClock variable
	SystemCoreClockUpdate();
	// Turn off all peripheral clocks
	LPC_SYSCTL->PCONP = 0;
	// Set up and initialize all required blocks and
	// functions related to the board hardware
	Board_Init();
#endif
#endif
	setupTimer();
	setupPWM();
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

void Board::setupPWM(uint32_t cycle_in_usec)
{
	// Power on the PWM peripheral
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_PWM1);

	LPC_PWM1->TCR = 2;              // reset the Timer Counter and the Prescale Counter
	LPC_PWM1->IR  = 0x7ff;          // clear any pending interrupts

	// Set prescale to 1 usec resolution (1000000Hz)
	LPC_PWM1->PR  = Chip_Clock_GetPeripheralClockRate(SYSCTL_PCLK_PWM1) / 1000000 - 1;
	LPC_PWM1->MR0 = cycle_in_usec;
	LPC_PWM1->LER = 1;
	LPC_PWM1->MCR = 2;       		// reset on MR0
	LPC_PWM1->TCR = (1 << 3) | 1;   // enable PWM mode and counting
}
