#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "common.h"
#include "vehicle.h"
#include "compass.h"
#include "bluebooth.h"

#define BT_UART					LPC_UART1
#define BT_IRQ					UART1_IRQn

#define ioBtTxD					IOMUX(0,15)
#define ioBtRxD					IOMUX(0,16)

STATIC const PINMUX_GRP_T IoMuxConfig[] = {
	{ IOGRP(ioBtTxD), IONUM(ioBtTxD), IOCON_MODE_INACT | IOCON_FUNC1},	/* TXD1 */
	{ IOGRP(ioBtRxD), IONUM(ioBtRxD), IOCON_MODE_INACT | IOCON_FUNC1},	/* RXD1 */
};

static QueueHandle_t xInQ;

void UART1_IRQHandler(void)
{
	uint8_t ch;
	if (Chip_UART_Read(BT_UART, &ch, 1) > 0)
		xQueueSendFromISR(xInQ, &ch, NULL);
}

#define CMD_SetPower			0xF0
#define CMD_PowerLevelMask		0x0F
#define CMD_PowerTuning			0xC0
#define CMD_TuningLevelMask		0x1F
#define CMD_CalibrationStart	0xA0
#define CMD_CalibrationFinish	0xA1

static void vTaskBTControl(void *pvParameters)
{
	uint8_t ch;
	while (1) {
		__WFI();
		if (xQueueReceive(xInQ, &ch, portMAX_DELAY) != pdTRUE)
			continue;
		if ((ch & ~CMD_PowerLevelMask) == CMD_SetPower) {
			VehicleSetHorsePower((float)(ch & CMD_PowerLevelMask) / CMD_PowerLevelMask);
		} else if ((ch & ~CMD_TuningLevelMask) == CMD_PowerTuning) {
			VehicleSetSteeringWheel((ch & CMD_TuningLevelMask) - 16);
		} else
			switch (ch) {
			case CMD_CalibrationStart:
				CompassCalibrate(false);
				break;
			case CMD_CalibrationFinish:
				CompassCalibrate(true);
			}
	}
}

void BluetoothInit(void)
{
	// Select hardware functions of I/O pins
	Chip_IOCON_SetPinMuxing(LPC_IOCON, IoMuxConfig, sizeof(IoMuxConfig) / sizeof(PINMUX_GRP_T));

	if ((xInQ = xQueueCreate(1, sizeof(uint8_t))) != NULL) {
		/* Initialize UART */
		Chip_UART_Init(BT_UART);
		Chip_UART_SetBaud(BT_UART, 115200);
		Chip_UART_ConfigData(BT_UART, UART_LCR_WLEN8 | UART_LCR_SBS_1BIT | UART_LCR_PARITY_DIS);
		Chip_UART_TXEnable(BT_UART);

		/* Enable interrupts from the Bluetooth serial port */
		Chip_UART_IntEnable(BT_UART, UART_IER_RBRINT);
		NVIC_EnableIRQ(BT_IRQ);

		/* Bluetooth control thread */
		xTaskCreate(vTaskBTControl, (signed char*) "vTaskBTControl",
					configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 1UL),
					(xTaskHandle *) NULL);
	}
}
