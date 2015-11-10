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


int main(void) {

    // Configure the switch matrix
	// (connecting SCT outputs to the RGB led)
	SwitchMatrix_Init();
    //LPC_IOCON->PIO0_7 = 0x90;
    /* LPC_IOCON->PIO0_8 = 0x90; */
    /* LPC_IOCON->PIO0_9 = 0x90; */
    /* LPC_IOCON->PIO0_10 = 0x80; */
    /* LPC_IOCON->PIO0_11 = 0x80; */
    /* LPC_IOCON->PIO0_12 = 0x90; */
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

	// unhalt it: - clearing bit 2 of the CTRL register
	LPC_SCT->CTRL_L &= ~( 1<< 2  );

    // Force the counter to be placed into memory
    volatile static int i = 0 ;
    // Enter an infinite loop, just incrementing a counter
    while(1) {
        i++ ;
    }
    return 0 ;
}
