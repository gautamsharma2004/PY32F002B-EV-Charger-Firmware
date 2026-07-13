#ifndef CHARGER_SM_H
#define CHARGER_SM_H

#include <stdint.h>
#include <stdbool.h>

#include "battery.h"

/* =====================================================
   Charger State Machine - Core State Definitions
   ===================================================== */

typedef enum {
    STATE_POWER_ON,          // Initial state - FAN test, display VSET/CSET
    STATE_VSET_WAKE,         // 60 sec output at VSET for battery detection
    STATE_BATTERY_DETECT,    // Classify battery health
    STATE_PRECHARGE,         // 0.5A for very weak batteries (20%-40% VSET)
    STATE_DEEP_DISCHARGE,    // 1.0A for discharged batteries (40%-65% VSET)
    STATE_SOFTSTART,         // Ramp 0A → CSET @ 0.3A/sec
    STATE_CC,                // Constant Current until VSET reached
    STATE_CV,                // Constant Voltage (current tapers naturally)
    STATE_CHARGE_COMPLETE,   // Charge complete (locked until AC cycle)
    STATE_STANDBY,           // No battery or battery invalid
    STATE_FAULT,             // Protection triggered
} ChargerState_t;

typedef enum {
    FAULT_NONE = 0,
    FAULT_SCPT,    // Short Circuit Protection
    FAULT_RPPT,    // Reverse Polarity Protection
    FAULT_HITP,    // High Temperature Protection
    FAULT_OVPT,    // AC Over Voltage Protection
    FAULT_UNVP,    // AC Under Voltage Protection
    FAULT_DPDC,    // Deep Discharge Charging
    FAULT_BTRM,    // Battery Recovery Mode
    FAULT_BTNG,    // Battery Not Good
    FAULT_CHTO,    // Charge Time Out
    FAULT_100P     // Fully Charged
} FaultCode_t;

/* =====================================================
   Configuration Structure
   ===================================================== */

typedef struct {
    // Voltage thresholds (mV)
    uint32_t vset;           // Selected output voltage (e.g. 67200 = 67.2V)
    uint32_t vset_min;       // 20% VSET threshold (precharge entry)
    uint32_t vset_40;        // 40% VSET threshold
    uint32_t vset_65;        // 65% VSET threshold (normal charge entry)
    
    // Current thresholds (mA)
    uint16_t cset;           // Selected charging current (e.g. 6200 = 6.2A)
    uint16_t cc_termination; // 0.4 * CSET (CV termination current)
    
    // ADC raw thresholds for protection
    uint32_t adc_ov_threshold;       // Over-voltage ADC threshold
    uint32_t adc_uv_threshold;       // Under-voltage ADC threshold
    uint16_t adc_temp_warn;          // Temperature warning (80°C)
    uint16_t adc_temp_shutdown;      // Temperature shutdown (90°C)
    
    // Time constants (seconds)
    uint8_t vset_wake_duration;      // 60 seconds
    uint8_t cv_timeout_minutes;      // 60 minutes
    uint8_t total_timeout_hours;     // 7 hours
    uint8_t softstart_ramp_rate;     // 0.3A/sec
} ChargerConfig_t;

/* =====================================================
   Runtime State Structure
   ===================================================== */

typedef struct {
    ChargerState_t current_state;
    ChargerState_t next_state;
    
    // Measured values (latest ADC readings)
    uint32_t vbat_adc;       // Battery voltage ADC value
    uint16_t ibat_adc;       // Battery current ADC value
    uint16_t temp_mosfet_adc;
    uint16_t temp_ntc_adc;
    
    // Control outputs
    uint8_t pwm_duty;        // PWM duty cycle 0-100
    bool pwm_enabled;        // PWM enable flag
    bool fan_enabled;        // Fan control
    
    // Timers (in deciseconds, updated every 100ms)
    uint32_t state_timer;    // Time in current state
    uint32_t vset_wake_timer;
    uint32_t cv_timeout_timer;
    uint32_t total_charge_timer;
    uint32_t softstart_timer;
    
    // Current references (for soft start ramp)
    uint16_t current_ref;    // Current reference in mA (ramping)
    uint16_t current_target; // Target current (CSET)
    
    // Display and UI
    uint8_t soc_percent;     // State of charge display (0-100%)
    uint8_t soc_qualification_timer; // 60-sec timer for SOC advance
    uint16_t last_soc_voltage;       // Voltage at last SOC evaluation
    
    // Fault tracking
    FaultCode_t active_fault;
    bool charge_session_active;      // True after battery detection until complete
    bool charge_complete_locked;     // True after CV termination (locked)
    
    // Plug-in/out tracking
    uint8_t ac_on_count;     // AC state transition counter
} ChargerRuntime_t;

/* =====================================================
   Public API
   ===================================================== */

void ChargerSM_Init(ChargerConfig_t *cfg);
void ChargerSM_Task(uint32_t vbat_mv, uint16_t ibat_ma,
                    uint16_t temp_mosfet, uint16_t temp_ntc);
void ChargerSM_SetVset(uint32_t voltage_mv);
void ChargerSM_SetCset(uint16_t current_ma);
void ChargerSM_ACOnEvent(void);
void ChargerSM_ACOffEvent(void);

// Accessors
ChargerState_t ChargerSM_GetState(void);
FaultCode_t ChargerSM_GetFault(void);
uint8_t ChargerSM_GetPWMDuty(void);
bool ChargerSM_IsFanEnabled(void);
uint8_t ChargerSM_GetSOC(void);
bool ChargerSM_IsCharging(void);
bool ChargerSM_IsChargingComplete(void);
bool ChargerSM_IsPWMEnabled(void);
// Debug / info
const char* ChargerSM_StateToString(ChargerState_t state);
const char* ChargerSM_FaultToString(FaultCode_t fault);

#endif // CHARGER_SM_H