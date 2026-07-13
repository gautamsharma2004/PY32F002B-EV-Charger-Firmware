#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include <stdbool.h>

/* =====================================================
   Battery Health Classification
   ===================================================== */

typedef enum {
    BATTERY_INVALID,         // < 20% VSET (too weak to charge safely)
    BATTERY_PRECHARGE,       // 20%-40% VSET (needs 0.5A precharge)
    BATTERY_DEEP_DISCHARGE,  // 40%-65% VSET (needs 1.0A soft charge)
    BATTERY_NORMAL,          // >= 65% VSET (ready for normal CC/CV)
} BatteryHealth_t;

/* =====================================================
   Battery Detection Configuration
   
   Thresholds are calculated as percentages of VSET:
   
   Example for VSET = 67.2V:
   - 20% VSET = 13.44V (precharge entry)
   - 40% VSET = 26.88V (deep discharge entry)
   - 65% VSET = 43.68V (normal charge entry)
   
   These are stored as absolute millivolt values and
   updated whenever VSET changes.
   ===================================================== */

typedef struct {
   uint32_t vset;              // Current selected output voltage (mV)
   uint32_t threshold_20;      // 20% VSET threshold (mV)
   uint32_t threshold_40;      // 40% VSET threshold (mV)
   uint32_t threshold_65;      // 65% VSET threshold (mV)
    
    uint8_t debounce_count;     // Number of samples to confirm transition
    uint8_t debounce_samples;   // Samples collected so far
} BatteryConfig_t;

/* =====================================================
   Battery State Tracking
   ===================================================== */

typedef struct {
    BatteryHealth_t current_health;
    BatteryHealth_t previous_health;
    bool health_changed;        // Set to true on transition
    
   uint32_t latest_voltage;    // Last measured voltage (mV)
    uint32_t detection_timer;   // Time in current health state (deciseconds)
    
    // Qualification timer: must stay stable for 60 seconds
    // before changing display/charging state
    bool is_qualified;          // Health stable for 60+ seconds
    uint32_t qualification_timer;
} BatteryRuntime_t;

/* =====================================================
   Public API
   ===================================================== */

/* Initialize battery detection module */
void Battery_Init(BatteryConfig_t *cfg);

/* Update battery health based on voltage reading */
void Battery_Update(uint32_t vbat_mv);

/* Notify module that VSET has changed */
void Battery_SetVset(uint32_t vset_mv);

/* Get current battery health classification */
BatteryHealth_t Battery_GetHealth(void);

/* Check if battery health is qualified (stable for 60sec) */
bool Battery_IsHealthQualified(void);

/* Get latest measured voltage */
uint16_t Battery_GetVoltage(void);

/* Check if battery transitioned to new health state */
bool Battery_HealthChanged(void);

/* Debug string representation */
const char* Battery_HealthToString(BatteryHealth_t health);

#endif // BATTERY_H