/*
 * "Servo PWM on the LPC17xx" (http://openlpc.com/4e26f1/examples/pwm.lpc17xx)
 */
#include <stdio.h>
#include <math.h>

#if defined(__USE_LPCOPEN)
#if defined(NO_BOARD_LIB)
#include "chip.h"
#else
#include "board.h"
#endif
#endif

#include <cr_section_macros.h>

int main(void) {

#if defined(__USE_LPCOPEN)
#if !defined(NO_BOARD_LIB)
    // Read clock settings and update SystemCoreClock variable
    SystemCoreClockUpdate();
    // Turn off all peripheral clocks
    LPC_SYSCTL->PCONP = 0;
    // Set up and initialize all required blocks and
    // functions related to the board hardware
    Board_Init();
    // Set the LED to the state of "On"
    Board_LED_Set(0, true);
#endif
#endif

    printf("Welcome to LPC17xx PWM demo\n\r");

    // Power on the PWM peripheral (it is powered on by default)
    Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_PWM1);

    LPC_PWM1->TCR = 2;                      // reset TC & PC
    LPC_PWM1->IR = 0x7ff;                   // clear any pending interrupts

    // configure P2.0 for PWM1.1 - O - Pulse Width Modulator 1, channel 1 output, and disable pullups
    Chip_IOCON_PinMuxSet(LPC_IOCON, 2, 0, IOCON_MODE_INACT | IOCON_FUNC1);

    // Set prescale so we have a resolution of 1us (1000000Hz)
    LPC_PWM1->PR  = Chip_Clock_GetPeripheralClockRate(SYSCTL_PCLK_PWM1) / 1000000 - 1;
    // FIXME: below are hard code for testing purpose
#if 1
    LPC_PWM1->MR0 = 20000;					// set the 50Hz period (20000us)
    LPC_PWM1->MR1 = 1000;					// set duty of 1ms (1000us)
    LPC_PWM1->LER = 3;
    LPC_PWM1->MCR = 2;						// reset on MR0
    LPC_PWM1->PCR = 1 << 9;					// enable PWM1 output

    LPC_PWM1->TCR = (1 << 3) | 1;           // enable PWM mode and counting

    while (1) {
    	__WFI();
    }
#endif
#if 0
    LPC_PWM1->MR0 = 20000;                  // set the period in us. 50Hz rate
    LPC_PWM1->MR1 = 1500;                   // set duty of 1.5ms
    LPC_PWM1->MR2 = 19000;                  // set a match that occurs 1ms
                                            // before the TC resets.
    LPC_PWM1->LER = 0x7;					// set latch-enable register
    LPC_PWM1->MCR = 0x2 | (1 << 6);         // reset on MR0, interrupt on MR2
    LPC_PWM1->PCR = 1 << 9;                 // enable PWM1 with single-edge operation

    LPC_PWM1->TCR = (1 << 3) | 1;           // enable PWM mode and counting

    // modulate the PWM signal on a 0.5Hz sin wave
    // update the duty cycle synchronously with the PWM period
    // precompute duty cycle values for one full period
    // duty(t) = sin(2pi*Fm*t)
    // duty[n] = sin(2pi*Fm*n*Ts)
    // N := number of samples in 1 full period
    // n = 1:N
    // Fs = 1/Ts := PWM frequency
    // M := modulation decimation factor (the duty cycle will
    // be updated every M periods). This effectively sets the resolution
    // of the modulating wave
    const double Fs = 50.0;    // PWM frequency is 50Hz
    const double Fm = 0.5;     // frequency of modulating wave is 0.5Hz
    const int M = 1;           // update duty every cycle
    const int N = Fs / (Fm * M);  // how many pwm cycles fit into one period
                                  // of the modulating wave, taking into
                                  // account the subsampling rate?
    const double PI = 3.14159;

    // this array will contain one full period of duty cycle values
    int n, duty[N];

    printf("Computing duty cycle values:\n\r");
    for (n = 0; n < N; n++) {
    	duty[n] = 1500 + (int)(500.0 * sin(2.0*PI*Fm/Fs * n));
    	printf("duty[%d] = %d\n\r", n, duty[n]);
    }

    printf("Done computing duty cycle values.\n\r");

    n = 0;
    while (1) {
    	// wait for MR2 interrupt flag to be raised
    	while((LPC_PWM1->IR & (1 << 2)) == 0);
    	LPC_PWM1->IR = 0x7ff;	// clear interrupt flags

    	// write duty cycle value and set Latch-enable register
    	LPC_PWM1->MR1 = duty[n];
    	LPC_PWM1->LER = 1 << 1;

    	// increment n and wrap if necessary
    	n++;
    	if(n >= N)
    		n = 0;
    }
#endif
    return 0 ;
}
