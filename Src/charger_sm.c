#include "charger_sm.h"
#include "charger_adc.h"
#include <string.h>

/* =====================================================
   Static Module State
   ===================================================== */

static ChargerConfig_t g_config;
static ChargerRuntime_t g_runtime;

/* =====================================================
   Lookup Tables
   ===================================================== */

// SOC vs VSET% table (from firmware requirements)
static const uint16_t soc_lookup_table[][2] = {
    {0,   650},   // 0% SOC = 65.0% VSET
    {5,   803},   // 5% SOC = 80.3% VSET
    {10,  826},
    {15,  849},
    {20,  872},
    {25,  895},
    {30,  900},
    {35,  905},
    {40,  910},
    {45,  915},
    {50,  920},
    {55,  925},
    {60,  930},
    {65,  935},
    {70,  940},
    {75,  945},
    {80,  956},
    {85,  967},
    {90,  978},
    {95,  989},
    {100, 1000},  // 100% SOC = 100.0% VSET
};

#define SOC_TABLE_SIZE (sizeof(soc_lookup_table) / sizeof(soc_lookup_table[0]))

/* =====================================================
   Helper Functions
   ===================================================== */



static uint8_t CalculateSOC(uint32_t vbat_mv)
{
    uint16_t percent_x10 = (vbat_mv * 1000) / g_config.vset;
    
    for (int i = 0; i < SOC_TABLE_SIZE - 1; i++) {
        uint16_t lower_vset = soc_lookup_table[i][1];
        uint16_t upper_vset = soc_lookup_table[i+1][1];
        uint16_t lower_soc = soc_lookup_table[i][0];
        uint16_t upper_soc = soc_lookup_table[i+1][0];
        
        if (percent_x10 >= lower_vset && percent_x10 < upper_vset) {
            uint16_t interp = lower_soc +
                ((percent_x10 - lower_vset) * (upper_soc - lower_soc)) /
                (upper_vset - lower_vset);
            return (uint8_t)interp;
        }
    }
    
    if (percent_x10 >= soc_lookup_table[SOC_TABLE_SIZE-1][1])
        return 100;
    return 0;
}

static FaultCode_t CheckFaults(uint32_t vbat_mv, uint16_t ibat_ma,
                                uint16_t temp_mosfet, uint16_t temp_ntc)
{
    if (vbat_mv > g_config.adc_ov_threshold)
        return FAULT_OVPT;
    
    if (vbat_mv < g_config.adc_uv_threshold && g_runtime.charge_session_active)
        return FAULT_UNVP;
    
    if (temp_mosfet > g_config.adc_temp_shutdown || temp_ntc > g_config.adc_temp_shutdown)
        return FAULT_HITP;
    
    if (ibat_ma > (g_config.cset * 150 / 100) && vbat_mv < (g_config.vset * 30 / 100))
        return FAULT_SCPT;
    
    return FAULT_NONE;
}

/* =====================================================
   State Machine Core
   ===================================================== */

static void State_PowerOn(void)
{
    if (g_runtime.state_timer == 0) {
        g_runtime.pwm_enabled = false; 
        g_runtime.fan_enabled = true;
        g_runtime.charge_complete_locked = false;
    }
    
    if (g_runtime.state_timer >= 100) { 
        g_runtime.pwm_enabled = true;
        g_runtime.next_state = STATE_VSET_WAKE;
    }
}

static void State_VsetWake(void)
{
    // Regulate output for exactly 60 seconds
    if (g_runtime.state_timer == 0) {
        g_runtime.pwm_enabled = true; // PA2 LOW -> TL494 ON
        g_runtime.fan_enabled = false;
        g_runtime.charge_session_active = false;
    }
    
    // Exit condition after 60 seconds (600 deciseconds)
    if (g_runtime.state_timer >= 600) {
        uint32_t percent = (g_runtime.vbat_adc * 100) / g_config.vset;
        
        if (percent >= 65) {
            g_runtime.charge_session_active = true;
            g_runtime.next_state = STATE_SOFTSTART;
        } else if (percent >= 40) {
            g_runtime.charge_session_active = true;
            g_runtime.active_fault = FAULT_DPDC; // Deep Discharge
            g_runtime.next_state = STATE_DEEP_DISCHARGE;
        } else if (percent >= 20) {
            g_runtime.charge_session_active = true;
            g_runtime.active_fault = FAULT_BTRM; // Battery Recovery
            g_runtime.next_state = STATE_PRECHARGE;
        } else {
            // < 20% Vset
            g_runtime.active_fault = FAULT_BTNG; // Battery Not Good
            g_runtime.next_state = STATE_STANDBY;
        }
    }
}

static void State_Precharge(void)
{
    if (g_runtime.state_timer == 0) {
        g_runtime.current_ref = 0;
        g_runtime.current_target = 500;  
    }
    
    g_runtime.fan_enabled = true;
    g_runtime.pwm_enabled = true;
    
    if (g_runtime.vbat_adc >= g_config.vset_40) {
        g_runtime.next_state = STATE_DEEP_DISCHARGE;
    }
}

static void State_DeepDischarge(void)
{
    if (g_runtime.state_timer == 0) {
        g_runtime.current_ref = 0;
        g_runtime.current_target = 1000;  
    }
    
    g_runtime.fan_enabled = true;
    g_runtime.pwm_enabled = true;
    
    if (g_runtime.vbat_adc >= g_config.vset_65) {
        g_runtime.next_state = STATE_SOFTSTART;
    }
}

static void State_Softstart(void)
{
    if (g_runtime.state_timer == 0) {
        g_runtime.current_ref = 0;
        g_runtime.current_target = g_config.cset;
        g_runtime.softstart_timer = 0;
        g_runtime.total_charge_timer = 0;  
    }
    
    g_runtime.fan_enabled = true;
    g_runtime.pwm_enabled = true;
    
    if (g_runtime.current_ref < g_runtime.current_target) {
        g_runtime.current_ref += 3;
        if (g_runtime.current_ref > g_runtime.current_target)
            g_runtime.current_ref = g_runtime.current_target;
    }
    
    if (g_runtime.current_ref >= g_runtime.current_target) {
        g_runtime.next_state = STATE_CC;
    }
}

static void State_CC(void)
{
    g_runtime.fan_enabled = true;
    g_runtime.pwm_enabled = true;
    g_runtime.current_ref = g_config.cset;
    
    if (g_runtime.vbat_adc >= g_config.vset) {
        g_runtime.cv_timeout_timer = 0; 
        g_runtime.next_state = STATE_CV;
    }
}

static void State_CV(void)
{
    g_runtime.fan_enabled = true;
    g_runtime.pwm_enabled = true;
    
    // CV Termination Logic (Must be in active session)
    if (g_runtime.charge_session_active) {
        if (g_runtime.vbat_adc >= g_config.vset && 
            g_runtime.ibat_adc <= g_config.cc_termination) {
            g_runtime.active_fault = FAULT_100P; // Fully Charged
            g_runtime.next_state = STATE_CHARGE_COMPLETE;
        }
        
        // 60 Minute CV Safety Timer (36000 deciseconds)
        if (g_runtime.vbat_adc >= (g_config.vset * 99 / 100)) {
            g_runtime.cv_timeout_timer++;
            if (g_runtime.cv_timeout_timer >= 36000) {
                g_runtime.active_fault = FAULT_CHTO;
                g_runtime.next_state = STATE_FAULT;
            }
        }
    }
}

static void State_ChargeComplete(void)
{
    if (g_runtime.state_timer == 0) {
        g_runtime.pwm_enabled = false;
        g_runtime.fan_enabled = false;
        g_runtime.charge_complete_locked = true;
    }
}

static void State_Standby(void)
{
    if (g_runtime.state_timer == 0) {
        g_runtime.pwm_enabled = false;
        g_runtime.fan_enabled = false;
        g_runtime.charge_session_active = false;
    }
}

static void State_Fault(void)
{
    if (g_runtime.state_timer == 0) {
        g_runtime.pwm_enabled = false;
        g_runtime.fan_enabled = false;
        g_runtime.charge_session_active = false;
    }
}

/* =====================================================
   Public API Implementation
   ===================================================== */

void ChargerSM_Init(ChargerConfig_t *cfg)
{
    memcpy(&g_config, cfg, sizeof(ChargerConfig_t));
    memset(&g_runtime, 0, sizeof(ChargerRuntime_t));
    
    g_runtime.current_state = STATE_POWER_ON;
    g_runtime.next_state = STATE_POWER_ON;
    g_runtime.soc_percent = CalculateSOC(g_config.vset * 65 / 100); 
    g_config.cc_termination = (g_config.cset * 40) / 100;
    g_config.vset_min = (g_config.vset * 20) / 100;
    g_config.vset_40 = (g_config.vset * 40) / 100;
    g_config.vset_65 = (g_config.vset * 65) / 100;
}

static void CheckACInputProtection(void)
{
    uint16_t ac_voltage = ADC_GetACVoltage(); 
    
    if (ac_voltage >= 280) {
        g_runtime.active_fault = FAULT_OVPT;
        return;
    }
    if (ac_voltage <= 270 && g_runtime.active_fault == FAULT_OVPT) {
        g_runtime.active_fault = FAULT_NONE;
    }
    
    if (ac_voltage < 150) {
        g_runtime.active_fault = FAULT_UNVP;
        return;
    }
    if (ac_voltage >= 160 && !g_runtime.charge_session_active) {
        g_runtime.active_fault = FAULT_NONE;
    }
}

static void ApplyThermalDerating(uint16_t temp_mosfet, uint16_t temp_ntc)
{
    uint16_t temp_tenths = (temp_mosfet > temp_ntc) ? temp_mosfet : temp_ntc;
    
    if (temp_tenths >= 900) {  
        g_runtime.active_fault = FAULT_HITP;
        g_runtime.next_state = STATE_FAULT;
        return;
    }
    
    if (temp_tenths >= 800) {  
        uint16_t temp_over_80 = temp_tenths - 800;  
        uint8_t derate_percent = (temp_over_80 * 100) / 100;  
        uint16_t derated_current = (g_config.cset * (100 - derate_percent)) / 100;
        g_runtime.current_target = derated_current;
    } else {
        g_runtime.current_target = g_config.cset;
    }
}

void ChargerSM_Task(uint32_t vbat_mv, uint16_t ibat_ma,
                    uint16_t temp_mosfet, uint16_t temp_ntc)
{
    g_runtime.vbat_adc = vbat_mv;
    g_runtime.ibat_adc = ibat_ma;
    g_runtime.temp_mosfet_adc = temp_mosfet;
    g_runtime.temp_ntc_adc = temp_ntc;

    CheckACInputProtection();
    ApplyThermalDerating(temp_mosfet, temp_ntc);

    FaultCode_t fault = CheckFaults(vbat_mv, ibat_ma, temp_mosfet, temp_ntc);
    if (fault != FAULT_NONE) {
        g_runtime.active_fault = fault;
        g_runtime.next_state = STATE_FAULT;
    }
    
    if (g_runtime.charge_session_active) {
        if (g_runtime.total_charge_timer > 252000) {
            g_runtime.active_fault = FAULT_CHTO;
            g_runtime.next_state = STATE_FAULT;
        }
    }
    
    if (g_runtime.charge_session_active && g_runtime.current_state != STATE_VSET_WAKE) {
        if (g_runtime.soc_qualification_timer >= 600) {
            uint8_t new_soc = CalculateSOC(vbat_mv);
            if (new_soc > g_runtime.soc_percent) {
                uint8_t delta = new_soc - g_runtime.soc_percent;
                if (delta > 5)
                    delta = 5;
                g_runtime.soc_percent += delta;
            }
            g_runtime.soc_qualification_timer = 0;
        }
    }
    
    switch (g_runtime.current_state) {
        case STATE_POWER_ON:        State_PowerOn(); break;
        case STATE_VSET_WAKE:       State_VsetWake(); break;
        case STATE_BATTERY_DETECT:  State_Standby(); break;
        case STATE_PRECHARGE:       State_Precharge(); break;
        case STATE_DEEP_DISCHARGE:  State_DeepDischarge(); break;
        case STATE_SOFTSTART:       State_Softstart(); break;
        case STATE_CC:              State_CC(); break;
        case STATE_CV:              State_CV(); break;
        case STATE_CHARGE_COMPLETE: State_ChargeComplete(); break;
        case STATE_STANDBY:         State_Standby(); break;
        case STATE_FAULT:           State_Fault(); break;
    }
    
    if (g_runtime.next_state != g_runtime.current_state) {
        g_runtime.current_state = g_runtime.next_state;
        g_runtime.state_timer = 0;
    } else {
        g_runtime.state_timer++;
    }
    
    g_runtime.vset_wake_timer++;
    // Note: cv_timeout_timer is now handled specifically inside State_CV
    g_runtime.total_charge_timer++;
    g_runtime.softstart_timer++;
    g_runtime.soc_qualification_timer++;
}

void ChargerSM_SetVset(uint32_t voltage_mv)
{
    g_config.vset = voltage_mv;
    g_config.vset_min = voltage_mv * 20 / 100;
    g_config.vset_40 = voltage_mv * 40 / 100;
    g_config.vset_65 = voltage_mv * 65 / 100;
}

void ChargerSM_SetCset(uint16_t current_ma)
{
    g_config.cset = current_ma;
    g_config.cc_termination = current_ma * 40 / 100; 
}

void ChargerSM_ACOnEvent(void)
{
    if (g_runtime.current_state != STATE_POWER_ON) {
        g_runtime.current_state = STATE_POWER_ON;
        g_runtime.next_state = STATE_POWER_ON;
        g_runtime.state_timer = 0;
        g_runtime.charge_complete_locked = false;
        g_runtime.active_fault = FAULT_NONE;
    }
}

void ChargerSM_ACOffEvent(void)
{
    g_runtime.pwm_enabled = false;
    g_runtime.fan_enabled = false;
}

/* =====================================================
   Accessors
   ===================================================== */

ChargerState_t ChargerSM_GetState(void)
{
    return g_runtime.current_state;
}

FaultCode_t ChargerSM_GetFault(void)
{
    return g_runtime.active_fault;
}

uint8_t ChargerSM_GetPWMDuty(void)
{
    return g_runtime.pwm_enabled ? g_runtime.pwm_duty : 0;
}

bool ChargerSM_IsFanEnabled(void)
{
    return g_runtime.fan_enabled;
}

uint8_t ChargerSM_GetSOC(void)
{
    return g_runtime.soc_percent;
}

bool ChargerSM_IsCharging(void)
{
    return g_runtime.charge_session_active &&
           g_runtime.current_state != STATE_STANDBY &&
           g_runtime.current_state != STATE_FAULT;
}

bool ChargerSM_IsChargingComplete(void)
{
    return g_runtime.charge_complete_locked;
}

bool ChargerSM_IsPWMEnabled(void) 
{
    return g_runtime.pwm_enabled;
}

/* =====================================================
   Debug Helpers
   ===================================================== */

const char* ChargerSM_StateToString(ChargerState_t state)
{
    switch (state) {
        case STATE_POWER_ON:        return "POWER_ON";
        case STATE_VSET_WAKE:       return "VSET_WAKE";
        case STATE_BATTERY_DETECT:  return "BATTERY_DETECT";
        case STATE_PRECHARGE:       return "PRECHARGE";
        case STATE_DEEP_DISCHARGE:  return "DEEP_DISCHARGE";
        case STATE_SOFTSTART:       return "SOFTSTART";
        case STATE_CC:              return "CC";
        case STATE_CV:              return "CV";
        case STATE_CHARGE_COMPLETE: return "COMPLETE";
        case STATE_STANDBY:         return "STANDBY";
        case STATE_FAULT:           return "FAULT";
        default:                    return "UNKNOWN";
    }
}

const char* ChargerSM_FaultToString(FaultCode_t fault)
{
    switch (fault) {
        case FAULT_NONE:            return "NONE";
        case FAULT_SCPT:            return "SCPT";
        case FAULT_RPPT:            return "RPPT";
        case FAULT_HITP:            return "HITP";
        case FAULT_OVPT:            return "OVPT";
        case FAULT_UNVP:            return "UNVP";
        case FAULT_DPDC:            return "DPDC";
        case FAULT_BTRM:            return "BTRM";
        case FAULT_BTNG:            return "BTNG";
        case FAULT_CHTO:            return "CHTO";
        case FAULT_100P:            return "100P";
        default:                    return "UNKNOWN";
    }
}