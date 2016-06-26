#include <stdio.h>
#include "Wire.h"
#include "FreeRTOS.h"
#include "task.h"
#include "Compass-GY26.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS_CLI.h"
#ifdef __cplusplus
}
#endif

static portBASE_TYPE xGetHeading(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	sprintf((char*)pcOutBuf, "Heading: %.1f\r\n", GY26.getHeading());
	return pdFALSE;
}

static const CLI_Command_Definition_t xCmdGetHeading = {
		"heading",
		"heading\t\tGet compass angle\r\n",
		xGetHeading,
		0
};

CompassGY26::CompassGY26()
{
	/* Register CLI commands */
	FreeRTOS_CLIRegisterCommand(&xCmdGetHeading);
}

float CompassGY26::getHeading()
{
	float heading = 0.f;
	Wire.beginTransmission(address);
	uint8_t buf[8] = { 0, getAngle };
	Wire.write(buf, 2);
	if (Wire.endTransmission() == 0) {
		 vTaskDelay(configTICK_RATE_HZ / 30);	// Response frequency 30Hz
		 if (Wire.requestFrom(address, 8) == 8) {
			 Wire.readBytes(buf, 8);
			 heading = (float)((uint16_t)buf[1]<< 8 | buf[2]) / 10;
		 }
	}
	return heading;
}
