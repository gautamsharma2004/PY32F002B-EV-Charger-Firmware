#ifndef CHARGER_ADC_H
#define CHARGER_ADC_H

#include <stdint.h>
#include <stdbool.h>

/* =====================================================
   ADC Channel Definitions
   ===================================================== */

typedef enum {
    ADC_VBAT,        // Battery voltage measurement
    ADC_IBAT,        // Battery current (via sense resistor)
    ADC_TEMP_MOSFET, // MOSFET temperature (NTC)
    ADC_TEMP_NTC,    // Auxiliary temperature sensor
    ADC_VAUX,        // Auxiliary voltage (not used now)
    ADC_CHANNEL_COUNT
} ADCChannel_t;

/* =====================================================
   Scaling Configuration
   =====================================================
   
   For a PY32F002AF15P6TU with 12-bit ADC (0-4095):
   - Reference: 3.3V
   - Resolution: 3.3V / 4096 = 0.8056 mV per count
   
   Example configurations (adjust based on actual hardware):
   
   VBAT: Scaled 0-100V with 1:15 resistor divider
   - ADC reads: 0-4095 counts
   - Maps to: 0-100V (no, maps to ~24.4V full scale)
   - Actual: Need voltage divider ratio in schematic
   
   IBAT: 0.08Ω sense resistor → 0-1.5V for 0-20A
   - ADC: 0-4095 → 0-3.3V
   - Sense: 0-1.5V
   - Current: 0-20A (or 0-10A depending on CSET max)
   
   TEMP: NTC thermistor with 10k pullup
   - ADC: 0-4095 → 0-3.3V
   - Convert via Steinhart-Hart or lookup
   
   ===================================================== */

typedef struct {
    // Scaling factors (output mV or mA per ADC count)
    // e.g., vbat_scale = 24 means each count = 24mV
    uint16_t vbat_scale;        // mV per ADC count (typically 15-25)
    uint16_t ibat_scale;        // mA per ADC count (typically 2-5)
    uint16_t temp_scale;        // Depends on thermistor (will use lookup)
    
    // Offsets (raw ADC values for filtering baseline)
    uint16_t vbat_offset;       // ADC offset for Vbat
    uint16_t ibat_offset;       // ADC offset for Ibat (for current sense)
    
    // Hardware limits (ADC raw thresholds for protection)
    uint16_t adc_ov_limit;      // ADC count for over-voltage
    uint16_t adc_uv_limit;      // ADC count for under-voltage
    uint16_t adc_oc_limit;      // ADC count for over-current (if used)
    uint16_t adc_temp_warn;     // ADC count for 80°C warning
    uint16_t adc_temp_shutdown; // ADC count for 90°C shutdown
} ADCConfig_t;

/* =====================================================
   ADC Measured Values (filtered)
   ===================================================== */

typedef struct {
   uint32_t vbat_mv;           // Battery voltage in mV
    uint16_t ibat_ma;           // Battery current in mA
    uint16_t temp_mosfet;       // MOSFET temperature in 0.1°C (e.g., 800 = 80.0°C)
    uint16_t temp_ntc;          // NTC temperature in 0.1°C
    
    // Raw ADC values (unscaled, for debug)
    uint16_t vbat_adc;
    uint16_t ibat_adc;
    uint16_t temp_mosfet_adc;
    uint16_t temp_ntc_adc;
} ADCMeasurement_t;

/* =====================================================
   Public API
   ===================================================== */

void ADC_Init(ADCConfig_t *cfg);

// Called from ISR or main loop when new ADC sample is available
void ADC_ProcessRawSample(ADCChannel_t channel, uint16_t raw_adc_value);

// Retrieve latest filtered readings
ADCMeasurement_t ADC_GetMeasurement(void);

// Low-level ADC trigger (if using software-triggered ADC)
void ADC_StartConversion(void);

// Optional AC input voltage estimate used by charger state protection
uint16_t ADC_GetACVoltage(void);

// Temperature conversion: NTC lookup (simplified)
uint16_t ADC_NTCToTemperature(uint16_t adc_raw);

// Debug / monitoring
uint16_t ADC_GetRawValue(ADCChannel_t channel);

#endif // CHARGER_ADC_H