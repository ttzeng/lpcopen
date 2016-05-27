#include "common.h"

int angle_subtract(int from, int to)
{
	from %= 360, to %= 360;
	int diff = to - from;
	diff += (diff > 180) ? -360 : (diff < -180) ? 360 : 0;
	return diff;
}
