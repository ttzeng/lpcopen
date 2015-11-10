/****************************************************************************
 *   $Id:: blinky.c 3634 2012-10-31 00:09:55Z usb00423                      $
 *   Project: NXP LPC8xx Blinky example
 *
 *   Description:
 *     This file contains LED blink code example which include timer,
 *     GPIO initialization, and clock monitoring.
 *
 ****************************************************************************
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * products. This software is supplied "AS IS" without any warranties.
 * NXP Semiconductors assumes no responsibility or liability for the
 * use of the software, conveys no license or title under any patent,
 * copyright, or mask work right to the product. NXP Semiconductors
 * reserves the right to make changes in the software without
 * notification. NXP Semiconductors also make no representation or
 * warranty that such application will be suitable for the specified
 * use without further testing or modification.

 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors'
 * relevant copyright in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers. This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.

****************************************************************************/
#ifdef __USE_CMSIS
#include "LPC8xx.h"
#endif


#include "lpc8xx_clkconfig.h"
#include "lpc8xx_gpio.h"
#include "lpc8xx_mrt.h"

extern uint32_t mrt_counter;



/* Main Program */

int main (void) {
  uint32_t regVal;


  SystemCoreClockUpdate();

  /* Config CLKOUT, mostly used for debugging. */
  regVal = LPC_SWM->PINASSIGN8 & ~( 0xFF << 16 );
  LPC_SWM->PINASSIGN8 = regVal | ( 12 << 16 );	/* P0.12 is CLKOUT, ASSIGN(23:16). */
  CLKOUT_Setup( CLKOUTCLK_SRC_MAIN_CLK );

	#if 0
	regVal = LPC_SWM->PINASSIGN0 & ~( (0xFF << 0) | (0xFF << 8) );
	LPC_SWM->PINASSIGN0 = regVal | ( (2 << 0) | (3 << 8) );	/* P0.2 is UART0 TX, ASSIGN(7:0); P0.3 is UART0 RX. ASSIGN(15:8). */
#endif

  /* Enable AHB clock to the GPIO domain. */
  LPC_SYSCON->SYSAHBCLKCTRL |= (1<<6);

  /* Set port p0.7 to output */
  GPIOSetDir( 0, 7, 1 );

	/* Set port p0.16 to output */
  GPIOSetDir( 0, 16, 1 );

 /* Set port p0.17 to output */
  GPIOSetDir( 0, 17, 1 );

	init_mrt(0x8000);
	
  while (1)                                /* Loop forever */
  {
		/* I/O configuration and LED setting pending. */
		if ( (mrt_counter > 0) && (mrt_counter <= 200) )
		{
			GPIOSetBitValue( 0, 7, 0 );
		}
		if ( (mrt_counter > 200) && (mrt_counter <= 400) )
		{
			GPIOSetBitValue( 0, 7, 1 );
		}
		if ( (mrt_counter > 400) && (mrt_counter <= 600) )
		{
			GPIOSetBitValue( 0, 16, 0 );
		}
		if ( (mrt_counter > 600) && (mrt_counter <= 800) )
		{
			GPIOSetBitValue( 0, 16, 1 );
		}

		if ( (mrt_counter > 800) && (mrt_counter <= 1000) )
		{
			GPIOSetBitValue( 0, 17, 0 );
		}
		if ( (mrt_counter > 1000) && (mrt_counter <= 1200) )
		{
			GPIOSetBitValue( 0, 17, 1 );
		}

		else if ( mrt_counter > 1200 )
		{
			mrt_counter = 0;
		}
  }
}

