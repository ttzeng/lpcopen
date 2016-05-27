#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "common.h"
#include "motor.h"

#define ioPwmL					IOMUX(2,3)
#define ioPwmR					IOMUX(2,5)
#define ioDirL					IOMUX(2,7)
#define ioDirR					IOMUX(2,9)

STATIC const PINMUX_GRP_T IoMuxConfig[] = {
	{ IOGRP(ioPwmL), IONUM(ioPwmL), IOCON_MODE_INACT | IOCON_FUNC1},	/* PWM1.4 (PWM-L) */
	{ IOGRP(ioPwmR), IONUM(ioPwmR), IOCON_MODE_INACT | IOCON_FUNC1},	/* PWM1.6 (PWM-R) */
	{ IOGRP(ioDirL), IONUM(ioDirL), IOCON_MODE_INACT | IOCON_FUNC0},	/* DIR-L */
	{ IOGRP(ioDirR), IONUM(ioDirR), IOCON_MODE_INACT | IOCON_FUNC0},	/* DIR-R */
};

int32_t MotorRun(MotorChannel channel, int32_t duty)
{
	bool forward = true;
	if (duty < 0) {
		forward = false;
		duty = 0 - duty;
	}
	int32_t last_duty;
	if (duty <= MAX_PWM_CYCLE) {
		if (channel == MOTOR_LEFT) {
			last_duty = Chip_GPIO_ReadPortBit(LPC_GPIO, IOGRP(ioDirL), IONUM(ioDirL))?
						LPC_PWM1->MR4 : 0 - LPC_PWM1->MR4;
			LPC_PWM1->MR4  = duty;
			LPC_PWM1->LER |= (1 << 4);
			while (LPC_PWM1->LER & (1 << 4));
			Chip_GPIO_WritePortBit(LPC_GPIO, IOGRP(ioDirL), IONUM(ioDirL), forward);
		} else {
			last_duty = Chip_GPIO_ReadPortBit(LPC_GPIO, IOGRP(ioDirR), IONUM(ioDirR))?
						LPC_PWM1->MR6 : 0 - LPC_PWM1->MR6;
			LPC_PWM1->MR6  = duty;
			LPC_PWM1->LER |= (1 << 6);
			while (LPC_PWM1->LER & (1 << 6));
			Chip_GPIO_WritePortBit(LPC_GPIO, IOGRP(ioDirR), IONUM(ioDirR), forward);
		}
	}
	return last_duty;
}

static portBASE_TYPE xSetMotor(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	portBASE_TYPE len;
	const char* arg = FreeRTOS_CLIGetParameter(pcCmdStr, 1, &len);
	MotorChannel channel = MOTOR_LEFT;
	switch (toupper(*arg)) {
	case 'R':
		channel = MOTOR_RIGHT;
	case 'L':
		arg = FreeRTOS_CLIGetParameter(pcCmdStr, 2, &len);
		MotorRun(channel, atoi(arg));
	}
	*pcOutBuf = 0;
	return pdFALSE;
}

static const CLI_Command_Definition_t xCmdSetMotor =
{
    "motor",
    "motor <L/R> <duty>\tSet motor PWM\r\n",
	xSetMotor,
    2
};

void MotorInit(void)
{
	// Select hardware functions of I/O pins
	Chip_IOCON_SetPinMuxing(LPC_IOCON, IoMuxConfig, sizeof(IoMuxConfig) / sizeof(PINMUX_GRP_T));

	// Power on the PWM peripheral
    Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_PWM1);
    LPC_PWM1->TCR = 2;                      // reset TC & PC
    LPC_PWM1->IR  = 0x7ff;                  // clear any pending interrupts
    LPC_PWM1->CCR = 0;						// TC is incremented by the PR matches
    // Set prescale so we have a resolution of 1us (1000000Hz)
    LPC_PWM1->PR  = Chip_Clock_GetPeripheralClockRate(SYSCTL_PCLK_PWM1) / 1000000 - 1;
    LPC_PWM1->MR0 = MAX_PWM_CYCLE;			// set the 50Hz period (20000us)
    LPC_PWM1->MR4 = 0;						// duty time in usec (0..20000)
    LPC_PWM1->MR6 = 0;
    LPC_PWM1->LER = (1 << 6) | (1 << 4) | (1 << 0);
    LPC_PWM1->MCR = (2 << 0);				// reset on MR0
    LPC_PWM1->PCR = (1 << 14) | (1 << 12);	// enable PWM4, PWM6 outputs
    LPC_PWM1->TCR = (1 << 3) | 1;           // enable PWM mode and counting

    // Set GPIO[DIR-L, DIR-R] as output pins
    Chip_GPIO_WriteDirBit(LPC_GPIO, IOGRP(ioDirL), IONUM(ioDirL), true);
    Chip_GPIO_WriteDirBit(LPC_GPIO, IOGRP(ioDirR), IONUM(ioDirR), true);
    Chip_GPIO_WritePortBit(LPC_GPIO, IOGRP(ioDirL), IONUM(ioDirL), false);
    Chip_GPIO_WritePortBit(LPC_GPIO, IOGRP(ioDirR), IONUM(ioDirR), false);

	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdSetMotor);
}
