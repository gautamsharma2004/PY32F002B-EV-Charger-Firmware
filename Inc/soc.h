#ifndef SOC_H
#define SOC_H

#include <stdint.h>
#include <stdbool.h>

/* =====================================================
   SOC Display Configuration
   
   The SOC display is qualified by a 60-second timer:
   The battery voltage must remain within the same SOC
   bracket for 60 seconds before the display updates.
   
   This prevents flickering on noisy measurements.
   ===================================================== */

typedef struct {
   uint32_t vset;           // Current selected voltage (mV)
    uint8_t hysteresis_pct;  // Hysteresis as % of VSET (default: 2%)
} SOCConfig_t;

/* =====================================================
   SOC Voltage Lookup Table
   
   Maps battery voltage percentage (of VSET) to SOC display.
   
   Format: { voltage_percent_x10, soc_percent }
   Example: { 650, 0 } means 65.0% of VSET = 0% SOC
   
   These values are derived from lithium battery discharge curves.
   For your 67.2V system, this represents a 3-cell series pack
   where each cell follows similar V-Q curves.
   
   The table is interpolated to provide sub-integer SOC values,
   then rounded to nearest integer for display.
   ===================================================== */

typedef enum {
    SOC_LOOKUP_SIZE = 21
} SOCConstants_t;

/* =====================================================
   Public API
   ===================================================== */

/* Initialize SOC module */
void SOC_Init(SOCConfig_t *cfg);

/* Update SOC based on battery voltage */
void SOC_Update(uint32_t vbat_mv);

/* Set VSET (recalculates thresholds) */
void SOC_SetVset(uint32_t vset_mv);

/* Get current displayed SOC (0-100%) */
uint8_t SOC_GetDisplaySOC(void);

/* Get raw calculated SOC (higher resolution for debug) */
uint16_t SOC_GetRawSOC(void);

/* Check if current SOC is qualified (stable for 60sec) */
bool SOC_IsQualified(void);

/* Force SOC qualification (for testing / manual override) */
void SOC_ForceQualify(void);

/* Get qualification timer status (deciseconds remaining) */
uint16_t SOC_GetQualificationTimer(void);

/* Debug: Get voltage percentage at last update */
uint16_t SOC_GetLastVoltagePercent(void);

#endif // SOC_H