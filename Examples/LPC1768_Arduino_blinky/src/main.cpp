#include <cr_section_macros.h>
#include "Arduino.h"

TargetBoard hw;
Gpio gpioMapper;

/***** ARDUINO SKETCH CODE BEGIN *****/
// the setup function runs once when you press reset or power the board
void setup()
{
	Serial.begin(115200);
	// initialize digital pin 13 as an output.
	pinMode(13, OUTPUT);
}

// the loop function runs over and over again forever
void loop()
{
	digitalWrite(13, HIGH);   // turn the LED on (HIGH is the voltage level)
	delay(1000);              // wait for a second
	digitalWrite(13, LOW);    // turn the LED off by making the voltage LOW
	delay(1000);              // wait for a second
}
/***** ARDUINO SKETCH CODE END *****/

int main(void)
{
	init();
	setup();
	while(1) {
		loop();
		if (serialEventRun) serialEventRun();
	}
	return 0;
}
