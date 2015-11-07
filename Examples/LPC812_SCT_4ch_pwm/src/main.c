/****************************************************************************
 *   $Id:: main.c 3634 2012-10-31 00:09:55Z usb00423                      $
 *   Project: NXP LPC8xx SCT example
 *
 *   Description:
 *     This file configures the switch matrix, and the SCT (configured as timer)
 *     using Red State.
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

#include <cr_section_macros.h>
#include <NXP/crp.h>

#include "swm.h"
#include "sct_fsm.h"


// Variable to store CRP value in. Will be placed automatically
// by the linker when "Enable Code Read Protect" selected.
// See crp.h header for more information
__CRP const unsigned int CRP_WORD = CRP_NO_CRP ;



volatile uint32_t numPwmCycles;
volatile int pwmAborted;
volatile int pwmPhase;


void SCT_IRQHandler (void)
{
	uint32_t status = LPC_SCT->EVFLAG;

	if (status & (1u << SCT_IRQ_EVENT_IRQ_cycle)) {
		/* Interrupt once per PWM cycle */
		++numPwmCycles;
	}

	if (status & (1u << SCT_IRQ_EVENT_IRQ_abort)) {
		/* Abort interrupt */
		pwmAborted = 1;
	}

	/* Acknowledge interrupts */
	LPC_SCT->EVFLAG = status;
}




int main(void)
{
	uint32_t lastCycles;

    // Configure the switch matrix
	// (connecting SCT outputs to the RGB led)
	SwitchMatrix_Init();
    LPC_IOCON->PIO0_7 = 0x90;
    /* LPC_IOCON->PIO0_8 = 0x90; */
    /* LPC_IOCON->PIO0_9 = 0x90; */
    /* LPC_IOCON->PIO0_10 = 0x80; */
    /* LPC_IOCON->PIO0_11 = 0x80; */
    LPC_IOCON->PIO0_12 = 0x90;
    /* LPC_IOCON->PIO0_13 = 0x90; */
    /* LPC_IOCON->PIO0_14 = 0x90; */
    /* LPC_IOCON->PIO0_15 = 0x90; */
    LPC_IOCON->PIO0_16 = 0x90;
    LPC_IOCON->PIO0_17 = 0x90;
	
	// enable the SCT clock
	LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 8);

	// clear peripheral reset the SCT:
	LPC_SYSCON->PRESETCTRL |= ( 1<< 8);


	// Initialize it:
	sct_fsm_init();

	/* Conflict resolution: Inactive state takes precedence.
	 * CTOUT_0, CTOUT_1: Inactive state 0
	 * CTOUT_2, CTOUT_3: Inactive state 1
     */
	LPC_SCT->RES = 0x0000005A;

	NVIC_EnableIRQ(SCT_IRQn);

	// unhalt it: - clearing bit 2 of the CTRL register
	LPC_SCT->CTRL_L &= ~( 1<< 2  );

    // Enter an infinite loop
	lastCycles = numPwmCycles;
    while(1) {
    	if (numPwmCycles != lastCycles) {
    		lastCycles = numPwmCycles;

    		/* Every few PWM cycles change the duty cycles */
			if ((lastCycles % 5) == 0) {
				/* TODO: Access to match registers will change in future tool release */

				/* Prevent the reload registers from being used before we have updated
				 * all PWM channels.
				 */
				LPC_SCT->CONFIG |= (1u << 7);	/* NORELOAD_L (U) */
				if (pwmPhase == 0) {
					reload_pwm_val1(200000);
					reload_pwm_val3(700000);
				}
				else {
					reload_pwm_val1(950000);
					reload_pwm_val3(LPC_SCT->MATCHREL[0].U);	// duty cycle 0 (test conflict resolution)
				}
				/* Update done */
				LPC_SCT->CONFIG &= ~(1u << 7);	/* NORELOAD_L (U) */

				++pwmPhase;
				if (pwmPhase > 1) {
					pwmPhase = 0;
				}
			}
        }

    	if (pwmAborted) {
    		/* Demo: Poll ABORT input, and restart timer if abort condition has gone. */
    		while (!(LPC_SCT->INPUT & (1u << 0)))
    			;
    		LPC_SCT->CTRL_U &= ~(1u << 2);	/* HALT_L (U) */

    		pwmAborted = 0;
    	}
    }
    return 0 ;
}
