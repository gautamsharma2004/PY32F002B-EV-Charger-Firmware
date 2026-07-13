/**
 ******************************************************************************
 * @file    charger_control.c
 * @brief   DANGER: OPEN-LOAD TEST FIRMWARE. Bypasses start-up safety checks.
 ******************************************************************************
 */

#include "charger_control.h"
#include "main.h"

/* Thermal Limits (Celsius) */
#define TEMP_FAULT_THRESHOLD      90.0f
#define TEMP_RECOVERY_THRESHOLD   60.0f

static ChargeState_t currentState = CHARGE_STATE_IDLE;
static ChargerConfig_t config = {0};

/* ADC Filtered Readings */
static float actual_voltage = 0.0f;
static float actual_current = 0.0f;
static float actual_temp = 25.0f;

/* Internal hardware control abstraction */
static void Update_TL494_Reference(float voltage_ref, float current_ref);
static void Read_ADC_Channels(void);

/**
 * @brief Enables the TL494 controller by pulling the Dead Time Control (DTC) pin LOW.
 */
void ChargerCtrl_EnableOutput(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
}

/**
 * @brief Disables the TL494 controller by pulling the Dead Time Control (DTC) pin HIGH.
 */
void ChargerCtrl_DisableOutput(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
}

void ChargerCtrl_Init(void)
{
    currentState = CHARGE_STATE_IDLE;
    config.current_limit = 0.0f;
    
    /* Start safe, then override in Task */
    ChargerCtrl_DisableOutput();
    Update_TL494_Reference(0.0f, 0.0f);
}

void ChargerCtrl_SetTargets(float vset, float cset)
{
    config.vset_target = vset;
    config.cset_target = cset;
}

void ChargerCtrl_Task(void)
{
    /* 1. Fetch live hardware data from ADC */
    Read_ADC_Channels();

    /* 2. Critical Hardware Protections (Kept active for test safety) */
    if (actual_temp >= TEMP_FAULT_THRESHOLD) 
    {
        currentState = CHARGE_STATE_FAULT;
        ChargerCtrl_DisableOutput(); 
    }

    if (currentState == CHARGE_STATE_FAULT)
    {
        if (actual_temp <= TEMP_RECOVERY_THRESHOLD)
        {
            currentState = CHARGE_STATE_IDLE; 
        }
        return; 
    }

    /* 3. Main State Machine - TEST OVERRIDE */
    switch (currentState)
    {
        case CHARGE_STATE_IDLE:
            /* ⚠️ DANGER: FORCING OUTPUT ON IMMEDIATELY ⚠️ */
            /* Bypassing battery detection (Vset Wake) and Soft Start */
            config.current_limit = config.cset_target; 
            currentState = CHARGE_STATE_CC_CV;
            ChargerCtrl_EnableOutput(); /* Turns on the TL494 */
            break;

        case CHARGE_STATE_CC_CV:
            /* Output is live. Continously update references if using DAC/PWM */
            Update_TL494_Reference(config.vset_target, config.current_limit);
            break;

        default:
            break;
    }
}

/**
 * @brief  Updates the reference voltages fed to the TL494 error amplifiers.
 */
static void Update_TL494_Reference(float voltage_ref, float current_ref)
{
    /* To get EXACTLY 67.0V instead of the hardware default (e.g. 67.2V), 
     * you must output a PWM/DAC signal here to adjust the TL494 Error Amp.
     * If this is empty, the output relies purely on your PCB's resistor divider.
     */
}

/**
 * @brief  Reads ADC to get the REAL voltage for the TM1637 display.
 */
static void Read_ADC_Channels(void)
{
    /* TODO: You MUST map your PY32 ADC register here for the display to work. */
    /* Example: 
     * uint32_t raw_adc = HAL_ADC_GetValue(&hadc);
     * actual_voltage = (float)raw_adc * (3.3f / 4095.0f) * VOLTAGE_DIVIDER_RATIO;
     */
     
    /* TEMPORARY MOCK VALUE: Remove this when ADC is configured */
    actual_voltage = 67.0f; 
}

/* --- Global Module Getters --- */

float ChargerCtrl_GetActualVoltage(void)
{
    /* Returns the live ADC voltage. main.c calls this to update the TM1637 */
    return actual_voltage;
}

float ChargerCtrl_GetVsetTarget(void)
{
    return config.vset_target;
}

ChargeState_t ChargerCtrl_GetState(void)
{
    return currentState;
}