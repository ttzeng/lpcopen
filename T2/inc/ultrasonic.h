#ifndef ULTRASONIC_H_
#define ULTRASONIC_H_

#ifdef __cplusplus
extern "C" {
#endif

void UltrasonicInit(void);
float UltrasonicPing(void);

#ifdef __cplusplus
}
#endif
#endif /* ULTRASONIC_H_ */
