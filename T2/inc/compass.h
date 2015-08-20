#ifndef COMPASS_H_
#define COMPASS_H_

#ifdef __cplusplus
extern "C" {
#endif

void CompassInit(void);
float CompassGetHeading(void);
void CompassCalibrate(bool finish);

#ifdef __cplusplus
}
#endif
#endif /* COMPASS_H_ */
