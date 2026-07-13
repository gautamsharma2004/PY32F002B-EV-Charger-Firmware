#include "battery.h"
#include <string.h>

/* =====================================================
   Static Module State
   ===================================================== */

static BatteryConfig_t g_config;
static BatteryRuntime_t g_runtime;

/* =====================================================
   Classification Logic
   ===================================================== */

static BatteryHealth_t ClassifyBattery(uint32_t vbat_mv)
{
    if (vbat_mv >= g_config.threshold_65)
        return BATTERY_NORMAL;
    else if (vbat_mv >= g_config.threshold_40)
        return BATTERY_DEEP_DISCHARGE;
    else if (vbat_mv >= g_config.threshold_20)
        return BATTERY_PRECHARGE;
    else
        return BATTERY_INVALID;
}

/* =====================================================
   Public API Implementation
   ===================================================== */

void Battery_Init(BatteryConfig_t *cfg)
{
    if (!cfg) return;
    
    memcpy(&g_config, cfg, sizeof(BatteryConfig_t));
    
    // Initialize runtime state
    g_runtime.current_health = BATTERY_INVALID;
    g_runtime.previous_health = BATTERY_INVALID;
    g_runtime.health_changed = false;
    g_runtime.latest_voltage = 0;
    g_runtime.detection_timer = 0;
    g_runtime.is_qualified = false;
    g_runtime.qualification_timer = 0;
    
    // Default debounce: 5 samples (500ms at 100ms update rate)
    if (g_config.debounce_samples == 0)
        g_config.debounce_samples = 5;
}

void Battery_Update(uint32_t vbat_mv)
{
    // Store latest voltage
    g_runtime.latest_voltage = vbat_mv;
    
    // Classify battery health
    BatteryHealth_t new_health = ClassifyBattery(vbat_mv);
    
    // Check if health classification has changed
    if (new_health != g_runtime.current_health) {
        // Start debounce counter
        g_config.debounce_count++;
        
        // After enough stable samples, confirm transition
        if (g_config.debounce_count >= g_config.debounce_samples) {
            g_runtime.previous_health = g_runtime.current_health;
            g_runtime.current_health = new_health;
            g_runtime.health_changed = true;
            g_runtime.detection_timer = 0;
            g_runtime.is_qualified = false;        // Reset qualification
            g_runtime.qualification_timer = 0;
            g_config.debounce_count = 0;
        }
    } else {
        // Classification stable; reset debounce counter
        g_config.debounce_count = 0;
    }
    
    // Increment detection timer (called every 100ms = 1 decisecond)
    g_runtime.detection_timer++;
    
    // After 600 deciseconds (60 seconds) in stable state, qualify health
    if (g_runtime.detection_timer >= 600 && !g_runtime.is_qualified) {
        g_runtime.is_qualified = true;
        g_runtime.qualification_timer = 0;
    }
}

void Battery_SetVset(uint32_t vset_mv)
{
    g_config.vset = vset_mv;
    
    // Recalculate thresholds as percentages of new VSET
    g_config.threshold_20 = (vset_mv * 20) / 100;   // 20% of VSET
    g_config.threshold_40 = (vset_mv * 40) / 100;   // 40% of VSET
    g_config.threshold_65 = (vset_mv * 65) / 100;   // 65% of VSET
    
    // Force re-qualification when VSET changes
    g_runtime.is_qualified = false;
    g_runtime.qualification_timer = 0;
    g_runtime.detection_timer = 0;
}

BatteryHealth_t Battery_GetHealth(void)
{
    return g_runtime.current_health;
}

bool Battery_IsHealthQualified(void)
{
    return g_runtime.is_qualified;
}

uint16_t Battery_GetVoltage(void)
{
    return g_runtime.latest_voltage;
}

bool Battery_HealthChanged(void)
{
    bool changed = g_runtime.health_changed;
    g_runtime.health_changed = false;  // Clear flag after reading
    return changed;
}

const char* Battery_HealthToString(BatteryHealth_t health)
{
    switch (health) {
        case BATTERY_INVALID:
            return "INVALID";
        case BATTERY_PRECHARGE:
            return "PRECHARGE";
        case BATTERY_DEEP_DISCHARGE:
            return "DEEP_DISCHARGE";
        case BATTERY_NORMAL:
            return "NORMAL";
        default:
            return "UNKNOWN";
    }
}