/**
 ******************************************************************************
 * @file    vset_wake.c
 * @brief   60-second Vset wake timer implementation
 ******************************************************************************
 */

#include "vset_wake.h"

/* 60 seconds in milliseconds */
#define VSET_WAKE_DURATION_MS 60000 

static VsetWakeState_t currentState = WAKE_STATE_INIT;
static uint32_t wakeStartTime = 0;

void VsetWake_Init(void)
{
    currentState = WAKE_STATE_INIT;
    wakeStartTime = 0;
}

void VsetWake_Task(void)
{
    switch(currentState) 
    {
        case WAKE_STATE_INIT:
            /* 1. Turn ON Output (Regulate to Vset) */
            /* Assuming pulling PA2 (DTC) LOW enables the TL494 output */
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET); 
            
            /* Record the start time using the SysTick timer */
            wakeStartTime = HAL_GetTick(); 
            currentState = WAKE_STATE_ACTIVE;
            break;

        case WAKE_STATE_ACTIVE:
            /* 2. Wait for 60 seconds non-blocking */
            if ((HAL_GetTick() - wakeStartTime) >= VSET_WAKE_DURATION_MS) 
            {
                currentState = WAKE_STATE_EVALUATE;
            }
            break;

        case WAKE_STATE_EVALUATE:
            /* 3. 60 Seconds is up. Battery Detection Logic goes here. */
            /* TODO: Read ADC Battery Voltage */
            /* If Voltage < 20% Vset -> BTNG Fault -> Turn OFF PA2 */
            /* If Voltage >= 65% Vset -> Normal Charge Phase */
            
            /* Disable output for now as a safety default until ADC is mapped */
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET); 
            
            currentState = WAKE_STATE_COMPLETE;
            break;

        case WAKE_STATE_COMPLETE:
            /* Timer is done. Awaiting AC Reset to trigger again. */
            break;
    }
}

VsetWakeState_t VsetWake_GetState(void)
{
    return currentState;
}