#include <ctype.h>
#include "Motor2WD.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

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
		Motor2.setSpeed(channel, (float)atoi(arg) / 20000);
	}
	*pcOutBuf = 0;
	return pdFALSE;
}

static const CLI_Command_Definition_t xCmdSetMotor = {
		"motor",
		"motor <L/R> <duty>\tSet motor PWM\r\n",
		xSetMotor,
		2
};
#ifdef __cplusplus
}
#endif

Motor2WD::Motor2WD()
{
	// Set pin4 as DIR-L control on P2.7
	GPIO_CFG_T gpioDirL = { 2, 7, IOCON_MODE_INACT | IOCON_FUNC0, 4 };
	gpioMapper.add(&gpioDirL);
	pinMode(4, OUTPUT);
	// Set pin5 as PWM1.4 on P2.3
	GPIO_CFG_T gpioPwmL = { 2, 3, IOCON_MODE_INACT | IOCON_FUNC1, 5 };
	gpioMapper.add(&gpioPwmL, 4);
	gpioMapper.pwmSetDutyCycle(5, 0);

	// Set pin7 as DIR-R control on P2.9
	GPIO_CFG_T gpioDirR = { 2, 9, IOCON_MODE_INACT | IOCON_FUNC0, 7 };
	gpioMapper.add(&gpioDirR);
	pinMode(7, OUTPUT);
	// Set pin6 as PWM1.6 on P2.5
	GPIO_CFG_T gpioPwmR = { 2, 5, IOCON_MODE_INACT | IOCON_FUNC1, 6 };
	gpioMapper.add(&gpioPwmR, 6);
	gpioMapper.pwmSetDutyCycle(6, 0);

	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdSetMotor);
}

void Motor2WD::setSpeed(MotorChannel channel, float ratio)
{
	bool forward = true;
	if (ratio < 0) {
		forward = false;
		ratio = 0 - ratio;
	}
	if (ratio > 1.f) ratio = 1.f;
	if (channel == MOTOR_LEFT) {
		gpioMapper.pwmSetDutyCycle(5, ratio);
		digitalWrite(4, forward);
	} else {
		gpioMapper.pwmSetDutyCycle(6, ratio);
		digitalWrite(7, forward);
	}
}
