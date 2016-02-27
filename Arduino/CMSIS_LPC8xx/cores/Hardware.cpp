#include "Arduino.h"

void Board::initSwitchMatrix()
{	/* Generated by the Switch Matrix Tool */

	/* Enable SWM clock (already enabled in system_LPC8xx.c */
	//LPC_SYSCON->SYSAHBCLKCTRL |= (1<<7);

	/* Pin Assign 8 bit Configuration */
	/* U0_TXD @ PIO0_4 */
	/* U0_RXD @ PIO0_0 */
	LPC_SWM->PINASSIGN0 = 0xffff0004UL;

	/* Pin Assign 1 bit Configuration */
	/* XTALIN  @ PIO0_8 */
	/* XTALOUT @ PIO0_9 */
	/* RESET   @ PIO0_5 */
	LPC_SWM->PINENABLE0 = 0xffffff8fUL;

	/* Disable the clock to the Switch Matrix to save power */
	LPC_SYSCON->SYSAHBCLKCTRL &= ~(1<<7);
}

void Board::initIOCON()
{	/* Generated by the Switch Matrix Tool */

	/* Enable IOCON clock (already enabled in system_LPC8xx.c) */
	//LPC_SYSCON->SYSAHBCLKCTRL |= (1<<18);

	/* Pin I/O Configuration */
	// Enable XTALIN and XTALOUT on PIO0_8 and PIO0_9 & remove the pull-up/down resistors
	LPC_IOCON->PIO0_8 = 0x80;
	LPC_IOCON->PIO0_9 = 0x80;

	/* Disable the clock to the IOCON to save power */
	LPC_SYSCON->SYSAHBCLKCTRL &= ~(1<<18);
}

void Board::setupSysClock()
{	/* Reconfigure 30MHz system clock derived from System oscillator */
	/* Set up PLL: */
	//   Power up the crystal oscillator & system PLL in the PDRUNCFG register
	LPC_SYSCON->PDRUNCFG &= ~((1 << 5) | (1 << 7));
	//   Select the PLL input in the SYSPLLCLKSEL register
	LPC_SYSCON->SYSPLLCLKSEL = 1;	/* SYSOSC */
	//   Update the PLL clock source in the SYSPLLCLKUEN register
	LPC_SYSCON->SYSPLLCLKUEN = 0;
	LPC_SYSCON->SYSPLLCLKUEN = 1;
	//   Configure the PLL M and N dividers
	LPC_SYSCON->SYSPLLCTRL = (4 | (1 << 5));
	//   Wait for the PLL to lock by monitoring the PLL lock status
	while (!(LPC_SYSCON->SYSPLLSTAT & 1));

	/* Configure the main clock and system clock: */
	//   Select the main clock
	LPC_SYSCON->MAINCLKSEL = 3;		/* PLL output */
	//   Update the main clock source
	LPC_SYSCON->MAINCLKUEN = 0;
	LPC_SYSCON->MAINCLKUEN = 1;
	//   Select the divider value for the system clock to core, memories, and peripherals
	LPC_SYSCON->SYSAHBCLKDIV = 2;

	// Disable the BYPASS bit and select the oscillator frequency range in SYSOSCCTRL register
	LPC_SYSCON->SYSOSCCTRL = 0;
}

void Board::setupSysTick()
{
	SystemCoreClockUpdate();
	SysTick_Config(SystemCoreClock / 1000);
}

void Board::init()
{
	initSwitchMatrix();
	initIOCON();
	setupSysClock();
	setupSysTick();
	GPIOInit();
}