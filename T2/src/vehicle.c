#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "common.h"
#include "motor.h"
#include "compass.h"
#include "ultrasonic.h"
#include "vehicle.h"

static bool bVehicleAuto = false;

#define STD_HEARTBEAT			833		// 72 beats/min
static inline int beatrate(int distance)
{
	int beatrate = (((distance * distance) >> 2) + 375) >> 1;
	return (beatrate > STD_HEARTBEAT)? STD_HEARTBEAT : beatrate;
}

static int32_t iVehicleMotorDutyL = 0;
static int32_t iVehicleMotorDutyR = 0;

void VehicleSetHorsePower(float percent)
{
	if (percent == 0) bVehicleAuto = false;
	if (bVehicleAuto) return;

	iVehicleMotorDutyL = iVehicleMotorDutyR = MAX_PWM_CYCLE * percent;
	MotorRun(MOTOR_LEFT , iVehicleMotorDutyL);
	MotorRun(MOTOR_RIGHT, iVehicleMotorDutyR);
}

void VehicleSetSteeringWheel(int32_t adj)
{
	if (bVehicleAuto) return;

	int throttle = MOTOR_RIGHT;
	if (adj < 0) {
		throttle = MOTOR_LEFT;
		adj = 0 - adj;
	}
	float factor = 1.0f;
	if (adj > 8) {			/* 9, 10 */
		factor = -1.0f;
	} else if (adj > 5) {	/* 6, 7, 8 */
		factor = 0.0f;
	} else if (adj > 2) {	/* 3, 4, 5 */
		factor = 0.6f;
	} else if (adj > 0) {	/* 1, 2 */
		factor = 0.9f;
	}
	MotorRun(MOTOR_LEFT , (throttle == MOTOR_LEFT )? (iVehicleMotorDutyL * factor) : iVehicleMotorDutyL);
	MotorRun(MOTOR_RIGHT, (throttle == MOTOR_RIGHT)? (iVehicleMotorDutyR * factor) : iVehicleMotorDutyR);
}

#define THRESHOLD_AUTODRIVE		10.0
static void vTaskVehicleCtrl(void *pvParameters)
{
	float heading;
	while (1) {
		if (bVehicleAuto) {
			__WFI();
			int angle = angle_subtract(heading, CompassGetHeading());
			if (angle < 90) {
				MotorRun(MOTOR_LEFT ,  iVehicleMotorDutyL);
				MotorRun(MOTOR_RIGHT, -iVehicleMotorDutyR);
				continue;
			}
			if (angle > 100) {
				MotorRun(MOTOR_LEFT , -iVehicleMotorDutyL);
				MotorRun(MOTOR_RIGHT,  iVehicleMotorDutyR);
				continue;
			}
			MotorRun(MOTOR_LEFT , iVehicleMotorDutyL);
			MotorRun(MOTOR_RIGHT, iVehicleMotorDutyR);
			bVehicleAuto = false;
		} else {
			Board_LED_Toggle(0);
			float obstacle = UltrasonicPing();
			if (obstacle < THRESHOLD_AUTODRIVE) {
				Board_LED_Set(0, false);
				bVehicleAuto = true;
				// FIXME
				heading = CompassGetHeading();
				MotorRun(MOTOR_LEFT ,  MAX_PWM_CYCLE / 10);
				MotorRun(MOTOR_RIGHT, -MAX_PWM_CYCLE / 10);
			} else {
				/* toggle rate based on the range to the obstacle */
				vTaskDelay(200); //beatrate(UltrasonicPing()));
			}
		}
	}
}

void VehicleInit(void)
{
	/* Vehicle control thread */
	xTaskCreate(vTaskVehicleCtrl, (signed char*) "vVehicleCtrl",
				configMINIMAL_STACK_SIZE, NULL, (tskIDLE_PRIORITY + 0UL),
				(xTaskHandle *) NULL);
}
