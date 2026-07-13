/**
 ******************************************************************************
 * @file    charger_control.h
 * @brief   ADC feedback loop, thermal management, and soft-start logic
 ******************************************************************************
 */

#ifndef CHARGER_CONTROL_H
#define CHARGER_CONTROL_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* States for the Charging State Machine */
typedef enum {
    CHARGE_STATE_IDLE = 0,
    CHARGE_STATE_PRECHARGE,   /* 0.5A for deep discharge */
    CHARGE_STATE_SOFT_START,  /* Ramping current */
    CHARGE_STATE_CC_CV,       /* Normal Constant Current / Constant Voltage */
    CHARGE_STATE_COMPLETE,    /* Fully Charged Lock */
    CHARGE_STATE_FAULT        /* Latched Fault (e.g., Thermal > 90C) */
} ChargeState_t;

/* Charger Target Parameters Structure */
typedef struct {
    float vset_target;      /* User configured Voltage (e.g., 67.2V) */
    float cset_target;      /* User configured Current (e.g., 6.2A) */
    float current_limit;    /* Active limit (changes during soft-start/derating) */
} ChargerConfig_t;

/* Function Prototypes */
void ChargerCtrl_Init(void);
void ChargerCtrl_SetTargets(float vset, float cset);
void ChargerCtrl_Task(void);
ChargeState_t ChargerCtrl_GetState(void);
float ChargerCtrl_GetActualVoltage(void);
float ChargerCtrl_GetVsetTarget(void);
#endif /* CHARGER_CONTROL_H */