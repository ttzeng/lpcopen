#include "Ultrasonic.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "FreeRTOS.h"
#include "queue.h"
#include "FreeRTOS_CLI.h"

static Ultrasonic* hc_sr04;
static LPC_TIMER_T *pingTimer = LPC_TIMER1;
static const CHIP_SYSCTL_PCLK_T pingSysCtlPclk = SYSCTL_PCLK_TIMER1;
static const LPC175X_6X_IRQn_Type pingIRQn = TIMER1_IRQn;
static const uint8_t captureRegister = 1;
static QueueHandle_t xQueueEchoDurationInUsec;

void TIMER1_IRQHandler()
{
	if (Chip_TIMER_CapturePending(pingTimer, captureRegister)) {
		if (digitalRead(hc_sr04->pinEcho)) {
			Chip_TIMER_Reset(pingTimer);
		} else {
			// The falling edge of Echo marks the end of duration
			xQueueSendFromISR(xQueueEchoDurationInUsec, &pingTimer->CR[captureRegister], NULL);
		}
		Chip_TIMER_ClearCapture(pingTimer, captureRegister);
	}
}

static portBASE_TYPE xSonarPing(char* pcOutBuf, size_t xOutBufLen, const char* pcCmdStr)
{
	sprintf(pcOutBuf,"Distance: %.1fcm\r\n", hc_sr04->ping());
	return pdFALSE;
}

static const CLI_Command_Definition_t xCmdSonarPing = {
		"ping",
		"ping\t\tUltrasonar ping\r\n",
		xSonarPing,
		0
};
#ifdef __cplusplus
}
#endif

Ultrasonic::Ultrasonic()
{
	// Set pin12 as sonar trigger on P0.22
	GPIO_CFG_T gpioTrig = { 0, 22, IOCON_MODE_INACT | IOCON_FUNC0, pinTrigger };
	gpioMapper.add(&gpioTrig);
	pinMode(pinTrigger, OUTPUT);
	digitalWrite(pinTrigger, LOW);
	delay(100);	// ignore the echo caused by initializing the trigger pin

	// Set pin11 as PCAP1.1 on P1.19 to measure sonar echo duration
	GPIO_CFG_T gpioEcho = { 1, 19, IOCON_MODE_INACT | IOCON_FUNC3, pinEcho };
	gpioMapper.add(&gpioEcho);

	Chip_TIMER_Init(pingTimer);
	Chip_TIMER_Reset(pingTimer);    // reset TC & PC
	pingTimer->IR   = 0x3f;         // clear any pending interrupts
	pingTimer->CTCR = 0;            // TC is incremented by the PR matches
	// Set prescaler to a resolution of 1 usec (1000000Hz)
	uint32_t prescale = Chip_Clock_GetPeripheralClockRate(pingSysCtlPclk) / 1000000 - 1;
	Chip_TIMER_PrescaleSet(pingTimer, prescale);
	// Timer mode
	Chip_TIMER_TIMER_SetCountClockSrc(pingTimer, TIMER_CAPSRC_RISING_PCLK, captureRegister);
	// Enables capture on the rising edge of sonar echo signal
	Chip_TIMER_CaptureRisingEdgeEnable(pingTimer, captureRegister);
	// Enables capture on the falling edge of sonar echo signal
	Chip_TIMER_CaptureFallingEdgeEnable(pingTimer, captureRegister);
	// Enable interrupts on capture register loaded
	Chip_TIMER_CaptureEnableInt(pingTimer, captureRegister);
	NVIC_EnableIRQ(pingIRQn);

	Chip_TIMER_Enable(pingTimer);

	if ((xQueueEchoDurationInUsec = xQueueCreate(1, sizeof(uint32_t))) != NULL) {
		hc_sr04 = this;
		/* Register CLI commands */
		FreeRTOS_CLIRegisterCommand(&xCmdSonarPing);
	}
}

float Ultrasonic::ping()
{
	// Send a 10uS pulse to the Trigger input to start the ranging
	digitalWrite(pinTrigger, HIGH);
	delayMicroseconds(10);
	digitalWrite(pinTrigger, LOW);

	// HC-SR04 ultrasonic ranging module will send out an 8 cycle burst of ultrasound at 40 kHz and
	// raise the Echo output, the Echo pulse width will be proportioned to distance to the obstacle
	uint32_t duration_in_usec;
	return (xQueueReceive(xQueueEchoDurationInUsec, &duration_in_usec, portMAX_DELAY) == pdTRUE)?
			(float)duration_in_usec / 58 : -1.f;
}
