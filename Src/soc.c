#include "soc.h"
#include "charger_sm.h"
#include <string.h>

/* =====================================================
   SOC Lookup Table
   ===================================================== */

static const uint16_t soc_voltage_table[SOC_LOOKUP_SIZE][2] = {
    {650,  0},      // SOC  0% = 65.0% of VSET
    {803,  5},
    {826,  10},
    {849,  15},
    {872,  20},
    {895,  25},
    {900,  30},
    {905,  35},
    {910,  40},
    {915,  45},
    {920,  50},
    {925,  55},
    {930,  60},
    {935,  65},
    {940,  70},
    {945,  75},
    {956,  80},
    {967,  85},
    {978,  90},
    {989,  95},
    {1000, 100},    // SOC 100% = 100.0% of VSET
};

/* =====================================================
   Static Module State
   ===================================================== */

static SOCConfig_t g_config;

typedef struct {
    uint8_t display_soc;           // Displayed SOC (0-100%, qualified)
    uint16_t raw_soc;              // Raw calculated SOC (for debug)
    uint16_t last_voltage_percent; // Voltage % at last calculation
    
    bool is_qualified;             // Display is stable for 60 seconds
    uint16_t qualification_timer;  // Deciseconds in current bracket
    uint8_t last_bracket;          // Last SOC bracket for hysteresis
    
    bool bracket_changed;          // Set when SOC bracket changes
    
    uint8_t last_stable_soc;       // Tracks confirmed SOC for increment limits
    uint8_t candidate_soc;         // Active target SOC waiting for 60s qualification
} SOCRuntime_t;

static SOCRuntime_t g_runtime;

/* =====================================================
   Interpolation Helper
   ===================================================== */

static uint8_t InterpolateSOC(
    uint16_t lower_volt_x10,
    uint8_t  lower_soc,
    uint16_t upper_volt_x10,
    uint8_t  upper_soc,
    uint16_t target_volt_x10
)
{
    if (target_volt_x10 <= lower_volt_x10) return lower_soc;
    if (target_volt_x10 >= upper_volt_x10) return upper_soc;
    
    uint16_t volt_range = upper_volt_x10 - lower_volt_x10;
    uint16_t soc_range = upper_soc - lower_soc;
    uint16_t volt_offset = target_volt_x10 - lower_volt_x10;
    
    uint16_t interp = lower_soc + (volt_offset * soc_range) / volt_range;
    return (uint8_t)interp;
}

static uint8_t CalculateRawSOC(uint32_t vbat_mv, uint32_t vset_mv)
{
    uint32_t voltage_percent_x10 = ((uint32_t)vbat_mv * 1000) / vset_mv;
    
    if (voltage_percent_x10 <= soc_voltage_table[0][0]) return 0;
    if (voltage_percent_x10 >= soc_voltage_table[SOC_LOOKUP_SIZE-1][0]) return 100;
    
    for (int i = 0; i < SOC_LOOKUP_SIZE - 1; i++) {
        uint16_t lower_volt = soc_voltage_table[i][0];
        uint16_t upper_volt = soc_voltage_table[i+1][0];
        uint8_t  lower_soc  = soc_voltage_table[i][1];
        uint8_t  upper_soc  = soc_voltage_table[i+1][1];
        
        if (voltage_percent_x10 >= lower_volt && voltage_percent_x10 <= upper_volt) {
            return InterpolateSOC(lower_volt, lower_soc,
                                 upper_volt, upper_soc,
                                 voltage_percent_x10);
        }
    }
    return 0; 
}

/* =====================================================
   Public API Implementation
   ===================================================== */

void SOC_Init(SOCConfig_t *cfg)
{
    if (!cfg) return;
    
    memcpy(&g_config, cfg, sizeof(SOCConfig_t));
    
    if (g_config.hysteresis_pct == 0)
        g_config.hysteresis_pct = 2;
    
    g_runtime.display_soc = 0;
    g_runtime.raw_soc = 0;
    g_runtime.last_voltage_percent = 0;
    g_runtime.is_qualified = false;
    g_runtime.qualification_timer = 0;
    g_runtime.last_bracket = 0;
    g_runtime.bracket_changed = false;
    
    // Initialize new fields
    g_runtime.last_stable_soc = 0;
    g_runtime.candidate_soc = 0;
}

void SOC_Update(uint32_t vbat_mv)
{
    // Calculate raw target SOC based on the defined lookup table
    uint8_t target_soc = CalculateRawSOC(vbat_mv, g_config.vset);
    
    // Require voltage to remain continuously above threshold for 60 seconds
    if (target_soc > g_runtime.last_stable_soc) {
        if (target_soc == g_runtime.candidate_soc) {
            g_runtime.qualification_timer++;
            
            // 600 deciseconds = 60 seconds
            if (g_runtime.qualification_timer >= 600) {
                
                // Maximum increment limit = 5%
                uint8_t increment = target_soc - g_runtime.last_stable_soc;
                if (increment > 5) {
                    g_runtime.last_stable_soc += 5;
                } else {
                    g_runtime.last_stable_soc = target_soc;
                }
                
                // Downward transition lock (SOC never decreases during charging)
                if (g_runtime.last_stable_soc > g_runtime.display_soc) {
                    g_runtime.display_soc = g_runtime.last_stable_soc;
                }
                
                g_runtime.qualification_timer = 0; // Reset for next bracket
            }
        } else {
            // Voltage fluctuated, reset qualification timer
            g_runtime.candidate_soc = target_soc;
            g_runtime.qualification_timer = 0;
        }
    }
}

void SOC_SetVset(uint32_t vset_mv)
{
    g_config.vset = vset_mv;
    
    g_runtime.is_qualified = false;
    g_runtime.qualification_timer = 0;
    g_runtime.bracket_changed = true;
    
    // Ensure SOC resets correctly if VSET is dropped drastically
    g_runtime.last_stable_soc = 0;
    g_runtime.candidate_soc = 0;
}

uint8_t SOC_GetDisplaySOC(void)
{
    return g_runtime.display_soc;
}

uint16_t SOC_GetRawSOC(void)
{
    return g_runtime.raw_soc;
}

bool SOC_IsQualified(void)
{
    return g_runtime.is_qualified;
}

void SOC_ForceQualify(void)
{
    g_runtime.is_qualified = true;
    g_runtime.qualification_timer = 600;
    
    uint8_t soc_increase = g_runtime.raw_soc - g_runtime.display_soc;
    if (soc_increase > 5) {
        g_runtime.display_soc += 5;  
        g_runtime.last_stable_soc += 5;
    } else {
        g_runtime.display_soc = g_runtime.raw_soc;
        g_runtime.last_stable_soc = g_runtime.raw_soc;
    }
}

uint16_t SOC_GetQualificationTimer(void)
{
    if (g_runtime.is_qualified)
        return 0;
    return 600 - g_runtime.qualification_timer;  
}

uint16_t SOC_GetLastVoltagePercent(void)
{
    return g_runtime.last_voltage_percent;
}