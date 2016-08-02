#include <string.h>
#include <cr_section_macros.h>
#include "Arduino.h"
#include "Wire.h"
#include "FreeRTOS.h"
#include "task.h"
#include "CommandLine.h"
#include "Motor2WD.h"
#include "Compass-GY26.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS_CLI.h"
#ifdef __cplusplus
}
#endif

TargetBoard hw;

GPIO_CFG_T gpioCfgT2[] = {
		{ 0,  2, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* TXD0 */
		{ 0,  3, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* RXD0 */
		{ 2,  0, IOCON_MODE_INACT | IOCON_FUNC0, 13 }, /* LED0 */
		{ 2, 10, IOCON_MODE_INACT | IOCON_FUNC0,  2 }, /* INT Button */
		{ 0, 15, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* TXD1 */
		{ 0, 16, IOCON_MODE_INACT | IOCON_FUNC1, GPIO_CFG_UNASSIGNED }, /* RXD1 */
		{ GPIO_CFG_UNASSIGNED }
};
Gpio gpioMapper(gpioCfgT2);

static GPIO_CFG_T gpioSda1 = { 0, 19, IOCON_MODE_INACT | IOCON_FUNC3, 20 };
static GPIO_CFG_T gpioScl1 = { 0, 20, IOCON_MODE_INACT | IOCON_FUNC3, 21 };
TwoWire Wire(I2C1, &gpioSda1, &gpioScl1);

static portBASE_TYPE xI2cProbe(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	BaseType_t xMore = pdTRUE;
	static int pos = 0;
	const char* szHeader = "Probing available I2C devices...\r\n"
	                       "     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\r\n"
	                       "====================================================\r\n"
	                       "00                          ";
	if (pos >= 0) {
		const char* str = szHeader + pos;
		int nbytes = strlen(str);
		if (nbytes + 1 > xOutBufLen) nbytes = xOutBufLen - 1;
		strncpy((char*)pcOutBuf, str, nbytes);
		pcOutBuf[nbytes] = 0, pos += nbytes;
		if (szHeader[pos] == 0)	pos = -9;	// Start from address 0x08
	} else {
		char buf1[10], buf2[4];
		int addr = -1 - pos;
		Wire.beginTransmission(addr);
		/* Address 0x48 points to LM75AIM device which needs 2 bytes be read */
		int found = Wire.requestFrom(addr, (addr == 0x48)? 2 : 1);
		if (!(addr & 0x0F))
			sprintf(buf1, "\r\n%02X  ", addr >> 4);
		else strcpy(buf1, "");
		sprintf(buf2, found? " %02X":" --", addr);
		strcat(buf1, buf2);
		if (--pos < -120) {
			strcat(buf1, "\r\n");
			pos = 0, xMore = pdFALSE;
		}
		strcpy((char*)pcOutBuf, buf1);
	}
	return xMore;
}

static const CLI_Command_Definition_t xCmdI2cProbe = {
		"i2c",
		"i2c\t\tProbes all available slaves on I2C1\r\n",
		xI2cProbe,
		0
};

static void vConsole(void *pvParameters)
{
	Serial0.begin(115200);
	CommandLine console0(&Serial0, "Serial> ");
	Serial1.begin(115200);
	CommandLine console1(&Serial1, "Bluetooth> ");

	while(1) {
		__WFI();
		console0.run();
		console1.run();
	}
}

int main(void)
{
	/* Initiate the Wire library and join the I2C bus as master */
	Wire.begin();

	/* Initiate resources */
	new Motor2WD;
	new CompassGY26;

	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdI2cProbe);

	/* Console thread */
	xTaskCreate(vConsole, (signed char*) "Console",
				512, NULL, (tskIDLE_PRIORITY + 0UL),
				(xTaskHandle *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();
	return 0;
}
