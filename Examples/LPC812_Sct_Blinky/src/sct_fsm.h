#ifndef __SCT_FSM_H__
#define __SCT_FSM_H__

/* Generated by fzmparser version 2.2 --- DO NOT EDIT! */

#include "sct_user.h"

extern void sct_fsm_init (void);

/* Output assignments (and their defaults if specified) */
#define SCT_OUTPUT_Output_pin_0 (0)
#define SCT_OUTPUTPRELOAD_Output_pin_0 (1)


/* Match register reload macro definitions */
#define reload_matchOnDelay(value) LPC_SCT->MATCHREL[0].U = value;

#endif