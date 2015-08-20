/*
 * @brief LPC17xx/40xx PWM driver
 *
 * @note
 * Copyright(C) NXP Semiconductors, 2014
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

#ifndef PWM_17XX_40XX_H_
#define PWM_17XX_40XX_H_

typedef struct {
	__IO uint32_t IR;			/*!< Offset: 0x00 (R/W)  PWM Interrupt Register */
	__IO uint32_t TCR;			/*!< Offset: 0x04 (R/W)  PWM Timer Control Register */
	__IO uint32_t TC;			/*!< Offset: 0x08 (R/W)  PWM Timer Register */
	__IO uint32_t PR;			/*!< Offset: 0x0c (R/W)  PWM Prescale Register */
	__IO uint32_t PC;			/*!< Offset: 0x10 (R/W)  PWM Prescale Counter */
	__IO uint32_t MCR;			/*!< Offset: 0x14 (R/W)  PWM Match Control Register */
	__IO uint32_t MR0;			/*!< Offset: 0x18 (R/W)  PWM Match Register 0 */
	__IO uint32_t MR1;
	__IO uint32_t MR2;
	__IO uint32_t MR3;
	__IO uint32_t CCR;			/*!< Offset: 0x28 (R/W)  PWM Capture Control Register */
	__I  uint32_t CR0;			/*!< Offset: 0x2c (RO)   PWM Capture Register 0 */
	__I  uint32_t CR1;
	__I  uint32_t CR2;
	__I  uint32_t CR3;
	     uint32_t RESERVED0;
	__IO uint32_t MR4;
	__IO uint32_t MR5;
	__IO uint32_t MR6;
	__IO uint32_t PCR;			/*!< Offset: 0x4c (R/W)  PWM Control Register */
	__IO uint32_t LER;			/*!< Offset: 0x50 (R/W)  PWM Latch Enable Register */
	     uint32_t RESERVED1[7];
	__IO uint32_t CTCR;			/*!< Offset: 0x70 (R/W)  PWM Count Control Register */
} LPC_PWM_T;

#endif /* PWM_17XX_40XX_H_ */
