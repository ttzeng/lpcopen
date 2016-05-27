#ifndef VEHICLE_H_
#define VEHICLE_H_

#ifdef __cplusplus
extern "C" {
#endif

void VehicleInit(void);
void VehicleSetHorsePower(float percent);
void VehicleSetSteeringWheel(int32_t adj);

#ifdef __cplusplus
}
#endif
#endif /* VEHICLE_H_ */
