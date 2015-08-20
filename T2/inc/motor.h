#ifndef MOTOR_H_
#define MOTOR_H_

#define MAX_PWM_CYCLE			20000		// 50Hz (20000us)

typedef enum {
	MOTOR_LEFT = 0,
	MOTOR_RIGHT,
} MotorChannel;

#ifdef __cplusplus
extern "C" {
#endif

void MotorInit(void);
int32_t MotorRun(MotorChannel channel, int32_t duty);

#ifdef __cplusplus
}
#endif
#endif /* MOTOR_H_ */
