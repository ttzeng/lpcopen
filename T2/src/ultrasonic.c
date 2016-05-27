#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"
#include "common.h"
#include "ultrasonic.h"

#define PING_TIMER				LPC_TIMER3
#define SYSCTL_PCLK_PING		SYSCTL_PCLK_TIMER3
#define PULSE_TRIGGER			10		// 10 usec
#define USEC_OUT_RANGE			23200	// Max. ranging = 400cm

#define ioTrig					IOMUX(0,22)
#define ioEcho					IOMUX(0,21)

STATIC const PINMUX_GRP_T IoMuxConfig[] = {
	{ IOGRP(ioTrig), IONUM(ioTrig), IOCON_MODE_INACT | IOCON_FUNC0},	/* GPIO */
	{ IOGRP(ioEcho), IONUM(ioEcho), IOCON_MODE_INACT | IOCON_FUNC0},	/* GPIO */
};

float UltrasonicPing(void)
{
	// Send a 10uS pulse to the Trigger input to start the ranging
	Chip_GPIO_WritePortBit(LPC_GPIO, IOGRP(ioTrig), IONUM(ioTrig), true);
	Chip_TIMER_Reset(PING_TIMER);
	while (Chip_TIMER_ReadCount(PING_TIMER) <= 10);
	Chip_GPIO_WritePortBit(LPC_GPIO, IOGRP(ioTrig), IONUM(ioTrig), false);
	// HC-SR04 ultrasonic ranging module will send out an 8 cycle burst of ultrasound at 40 kHz and
	// raise the Echo output, the Echo pulse width will be proportioned to distance to the obstacle
	uint32_t usec_mark = 0, usec_now;
	while ((usec_now = Chip_TIMER_ReadCount(PING_TIMER)) < USEC_OUT_RANGE) {
		if (Chip_GPIO_ReadPortBit(LPC_GPIO, IOGRP(ioEcho), IONUM(ioEcho)) == true) {
			if (!usec_mark)
				usec_mark = usec_now;
		} else {
			if (usec_mark) {
				return (float)(usec_now - usec_mark) / 58;
			}
		}
	}
	return (float)USEC_OUT_RANGE / 58;
}

static portBASE_TYPE xSonarPing(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	sprintf(pcOutBuf,"Distance: %.1fcm\r\n", UltrasonicPing());
	return pdFALSE;
}

static const CLI_Command_Definition_t xCmdSonarPing =
{
    "ping",
    "ping\t\tUltrasonar ping\r\n",
	xSonarPing,
    0
};

void UltrasonicInit(void)
{
	// Select hardware functions of I/O pins
	Chip_IOCON_SetPinMuxing(LPC_IOCON, IoMuxConfig, sizeof(IoMuxConfig) / sizeof(PINMUX_GRP_T));

	// Set Trigger and Echo as output and input respectively
	Chip_GPIO_WriteDirBit(LPC_GPIO, IOGRP(ioTrig), IONUM(ioTrig), true);
	Chip_GPIO_WriteDirBit(LPC_GPIO, IOGRP(ioEcho), IONUM(ioEcho), false);

	// Power on the Timer peripheral
    Chip_TIMER_Init(PING_TIMER);
	PING_TIMER->TCR  = TIMER_RESET;			// reset TC & PC
	PING_TIMER->IR   = 0x3f;				// clear any pending interrupts
	PING_TIMER->CTCR = 0;					// TC is incremented by the PR matches
	// Set prescaler to a resolution of 1 usec (1000000Hz)
	uint32_t prescale = Chip_Clock_GetPeripheralClockRate(SYSCTL_PCLK_PING) / 1000000 - 1;
	Chip_TIMER_PrescaleSet(PING_TIMER, prescale);
	PING_TIMER->TCR = TIMER_ENABLE;

	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdSonarPing);
}
