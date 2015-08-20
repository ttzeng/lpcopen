#if defined (__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif
#include <string.h>
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"
#include "common.h"
#include "compass.h"

#define I2CBUS_100KHZ			100000

#define ioI2cSda				IOMUX(0,19)
#define ioI2cScl				IOMUX(0,20)

STATIC const PINMUX_GRP_T IoMuxConfig[] = {
	{ IOGRP(ioI2cSda), IONUM(ioI2cSda), IOCON_MODE_INACT | IOCON_FUNC3},	/* SDA1 */
	{ IOGRP(ioI2cScl), IONUM(ioI2cScl), IOCON_MODE_INACT | IOCON_FUNC3},	/* SCL1 */
};

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
		/* Address 0x48 points to LM75AIM device which needs 2 bytes be read */
		int found = (Chip_I2C_MasterRead(I2C1, addr, (uint8_t*)buf1, 1 + (addr == 0x48)) > 0);
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

void I2C1_IRQHandler(void)
{
	if (Chip_I2C_IsMasterActive(I2C1)) {
		Chip_I2C_MasterStateHandler(I2C1);
	} else {
		Chip_I2C_SlaveStateHandler(I2C1);
	}
}

#define GY26_ADDR					0x70
#define GY26_CMD_GETANGLE			0x31
#define GY26_CMD_CALIBRATE_START	0xC0
#define GY26_CMD_CALIBRATE_FINISH	0xC1

float CompassGetHeading(void)
{
	float heading = 0.0;
	uint8_t buf[8];
	buf[0] = 0x00;
	buf[1] = GY26_CMD_GETANGLE;
	if (Chip_I2C_MasterSend(I2C1, GY26_ADDR, buf, 2) == 2) {
		vTaskDelay(configTICK_RATE_HZ / 30);	// Response frequency 30Hz
		Chip_I2C_MasterRead(I2C1, GY26_ADDR, buf, 8);
		heading = (float)((uint32_t)buf[1]<< 8 | buf[2]) / 10;
	}
	return heading;
}

void CompassCalibrate(bool finish)
{
	uint8_t buf[2];
	buf[0] = 0x00;
	buf[1] = finish? GY26_CMD_CALIBRATE_FINISH : GY26_CMD_CALIBRATE_START;
	if (Chip_I2C_MasterSend(I2C1, GY26_ADDR, buf, 2) == 2) {
		vTaskDelay(configTICK_RATE_HZ / 30);	// Response frequency 30Hz
	}
}

static portBASE_TYPE xGetHeading(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	sprintf((char*)pcOutBuf, "Heading: %.1f\r\n", CompassGetHeading());
	return pdFALSE;
}

static const CLI_Command_Definition_t xCmdI2cProbe =
{
    "i2c",
    "i2c\t\tProbes all available slaves on I2C1\r\n",
	xI2cProbe,
    0
};
static const CLI_Command_Definition_t xCmdGetHeading =
{
    "heading",
    "heading\t\tGet compass angle\r\n",
	xGetHeading,
    0
};

void CompassInit(void)
{
	// Select hardware functions of I/O pins
	Chip_IOCON_SetPinMuxing(LPC_IOCON, IoMuxConfig, sizeof(IoMuxConfig) / sizeof(PINMUX_GRP_T));
	Chip_IOCON_EnableOD(LPC_IOCON, IOGRP(ioI2cSda), IONUM(ioI2cSda));
	Chip_IOCON_EnableOD(LPC_IOCON, IOGRP(ioI2cScl), IONUM(ioI2cScl));

	/* Initialize I2C */
	Chip_I2C_Init(I2C1);
	Chip_I2C_SetClockRate(I2C1, I2CBUS_100KHZ);

	/* Set interrupt mode */
	Chip_I2C_SetMasterEventHandler(I2C1, Chip_I2C_EventHandler);
	NVIC_EnableIRQ(I2C1_IRQn);

	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdI2cProbe);
	FreeRTOS_CLIRegisterCommand(&xCmdGetHeading);
}
