#include <cr_section_macros.h>
#include "Arduino.h"
#include "FreeRTOS.h"
#include "task.h"
#include "CommandLine.h"
#include "Motor2WD.h"

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

Motor2WD Motor2;

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
	/* Console thread */
	xTaskCreate(vConsole, (signed char*) "Console",
				256, NULL, (tskIDLE_PRIORITY + 0UL),
				(xTaskHandle *) NULL);

	/* Start the scheduler */
	vTaskStartScheduler();
	return 0;
}
