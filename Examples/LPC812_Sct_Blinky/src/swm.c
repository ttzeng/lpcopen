/*
* @brief 
* This file is used to config SwitchMatrix module.
*
* @note
* Copyright(C) NXP Semiconductors, 2012
* All rights reserved.
*
* @par
* Software that is described herein is for illustrative purposes only
* which provides customers with programming information regarding the
* LPC products.  This software is supplied "AS IS" without any warranties of
* any kind, and NXP Semiconductors and its licensor disclaim any and
* all warranties, express or implied, including all implied warranties of
* merchantability, fitness for a particular purpose and non-infringement of
* intellectual property rights.  NXP Semiconductors assumes no responsibility
* or liability for the use of the software, conveys no license or rights under any
* patent, copyright, mask work right, or any other intellectual property rights in
* or to any products. NXP Semiconductors reserves the right to make changes
* in the software without notification. NXP Semiconductors also makes no
* representation or warranty that such application will be suitable for the
* specified use without further testing or modification.
*
* @par
* Permission to use, copy, modify, and distribute this software and its
* documentation is hereby granted, under NXP Semiconductors' and its
* licensor's relevant copyrights in the software, without fee, provided that it
* is used in conjunction with NXP Semiconductors microcontrollers.  This
* copyright, permission, and disclaimer notice must appear in all copies of
* this code.
*/
    
#include "LPC8xx.h"    /* LPC8xx Peripheral Registers */


void SwitchMatrix_Init() 
{ 
    /* Enable UART clock */
    //LPC_SYSCON->SYSAHBCLKCTRL |= (1<<7);

    /* Pin Assign 8 bit Configuration */
    /* CTOUT_0 */
    LPC_SWM->PINASSIGN6 = 0x07ffffffUL; // changed from 07 at top to 00
    /* CTOUT_1 */
    /* CTOUT_2 */
    //LPC_SWM->PINASSIGN7 = 0xffff1110UL;

    /* Pin Assign 1 bit Configuration */
    /* SWCLK */
    /* SWDIO */
    /* RESET */
    //LPC_SWM->PINENABLE0 = 0xffffffd3UL;
}

 /**********************************************************************
 **                            End Of File
 **********************************************************************/
