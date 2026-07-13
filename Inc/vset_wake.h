/**
 ******************************************************************************
 * @file    vset_wake.h
 * @brief   Header for 60-second Vset wake timer and battery detection
 ******************************************************************************
 */

#ifndef VSET_WAKE_H
#define VSET_WAKE_H

#include "main.h"
#include <stdbool.h>

/* States for the Vset Wake State Machine */
typedef enum {
    WAKE_STATE_INIT = 0,
    WAKE_STATE_ACTIVE,
    WAKE_STATE_EVALUATE,
    WAKE_STATE_COMPLETE
} VsetWakeState_t;

/* Function Prototypes */
void VsetWake_Init(void);
void VsetWake_Task(void);
VsetWakeState_t VsetWake_GetState(void);

#endif /* VSET_WAKE_H */