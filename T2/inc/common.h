#ifndef COMMON_H_
#define COMMON_H_

#define IOMUX(pingrp,pinnum)	(((pingrp)<<5)|(pinnum))
#define IOGRP(mux)				((mux)>>5)
#define IONUM(mux)				((mux)&0x1f)

int angle_subtract(int from, int to);

#endif /* COMMON_H_ */
