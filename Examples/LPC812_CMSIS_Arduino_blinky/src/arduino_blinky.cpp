#include <cr_section_macros.h>

#include "Arduino.h"

class TargetBoard hw;

/***** ARDUINO SKETCH CODE BEGIN *****/
// the setup function runs once when you press reset or power the board
void setup() {
	pinMode(17, OUTPUT);
	Serial.begin(115200);
}

// the loop function runs over and over again forever
void loop() {
	digitalWrite(17, HIGH);   // turn the LED on (HIGH is the voltage level)
	delay(1000);              // wait for a second
	digitalWrite(17, LOW);    // turn the LED off by making the voltage LOW
	delay(1000);              // wait for a second
}
/***** ARDUINO SKETCH CODE END *****/

int main(void)
{
	init();
	setup();
	Serial.println(SystemCoreClock);
	while(1) {
		loop();
		if (serialEventRun) serialEventRun();
	}
	return 0;
}
