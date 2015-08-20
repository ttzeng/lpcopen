#include "board.h"
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"
#include "queue.h"

#define CONSOLE_UART		DEBUG_UART
#define CONSOLE_IRQ			UART0_IRQn
#define MAX_INPUT_LENGTH	50
#define MAX_OUTPUT_LENGTH	100

enum {
	CHAR_BS = 8,
	CHAR_LF = 10,
	CHAR_CR = 13,
};

static QueueHandle_t xInQ;

void UART0_IRQHandler(void)
{
	int ch;
	if ((ch = Board_UARTGetChar()) != EOF)
		xQueueSendFromISR(xInQ, &ch, NULL);
}

static void vDisplayPrompt(void)
{
	Board_UARTPutSTR("\r\nT2> ");
}

static void vUARTConsoleTask(void *pvParameters)
{
	int ch, index = 0;
	char pcInStr[MAX_INPUT_LENGTH], pcOutStr[MAX_OUTPUT_LENGTH];
	BaseType_t more;
	while (1) {
		__WFI();
		if (xQueueReceive(xInQ, &ch, portMAX_DELAY) != pdTRUE)
			continue;

		switch (ch) {
		case CHAR_CR :
			Board_UARTPutSTR("\r\n");
			do {
				more = FreeRTOS_CLIProcessCommand(pcInStr, pcOutStr, sizeof(pcOutStr));
				Board_UARTPutSTR(pcOutStr);
			} while (more == pdTRUE);
			index = 0;
			vDisplayPrompt();
		case CHAR_LF :
			break;
		case CHAR_BS :
			if (index > 0) {
				pcInStr[--index] = 0;
				Board_UARTPutChar(ch);
			}
			break;
		default :
			if (index < MAX_INPUT_LENGTH - 1) {
				pcInStr[index++] = ch, pcInStr[index] = 0;
				Board_UARTPutChar(ch);
			}
		}
	}
}

void ConsoleInit(void)
{
	if ((xInQ = xQueueCreate(1, sizeof(int))) != NULL) {
		Chip_UART_ABCmd(CONSOLE_UART, UART_ACR_MODE0, true, ENABLE);
		Chip_UART_IntEnable(CONSOLE_UART, UART_IER_RBRINT);
		NVIC_EnableIRQ(CONSOLE_IRQ);

		/* UART console thread */
		xTaskCreate(vUARTConsoleTask, "vTaskUartConsole",
					256, NULL, (tskIDLE_PRIORITY + 1UL),
					(xTaskHandle*)NULL);
	}
}
