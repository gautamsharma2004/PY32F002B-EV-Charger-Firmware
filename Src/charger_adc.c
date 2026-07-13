#include "charger_adc.h"
#include <string.h>

/* =====================================================
   Static Module State
   ===================================================== */

static ADCConfig_t g_config;
static ADCMeasurement_t g_measurement;
static bool g_has_valid_vbat = false;

// Circular buffers for filtering (3-sample median filter)
static uint16_t g_vbat_buffer[3] = {0};
static uint16_t g_ibat_buffer[3] = {0};
static uint16_t g_temp_mosfet_buffer[3] = {0};
static uint16_t g_temp_ntc_buffer[3] = {0};

static uint8_t g_buffer_index = 0;

/* =====================================================
   NTC Temperature Lookup Table
   ===================================================== 
   
   10kΩ NTC thermistor (typically B=3950K)
   ADC reference: 3.3V, 12-bit resolution (0-4095 counts)
   10k pullup resistor
   
   Table: ADC count → Temperature in 0.1°C units
   
   Typical NTC characteristics:
   - At 25°C: ~2000 ADC counts (1.6V across NTC)
   - At 80°C: ~800 ADC counts (0.64V across NTC)
   - At 90°C: ~600 ADC counts (0.48V across NTC)
   
   This is a simplified lookup. For production, use Steinhart-Hart equation.
   
   ===================================================== */

static const struct {
    uint16_t adc;    // ADC count
    uint16_t temp;   // Temperature in 0.1°C (e.g., 250 = 25.0°C)
} ntc_lookup[] = {
    // Format: { ADC_count, Temp_x10 }
    // Decreasing ADC → Increasing Temperature
    {3800, 0},       // -40°C (below practical range)
    {3500, 100},     // 10°C
    {3000, 250},     // 25°C (typical room temp)
    {2500, 400},     // 40°C
    {2000, 550},     // 55°C
    {1500, 700},     // 70°C
    {1000, 800},     // 80°C
    {800,  900},     // 90°C (shutdown threshold)
    {600,  1000},    // 100°C
    {400,  1100},    // 110°C
};

#define NTC_LOOKUP_SIZE (sizeof(ntc_lookup) / sizeof(ntc_lookup[0]))

/* =====================================================
   Helper: Median of 3 values
   ===================================================== */

static uint16_t Median3(uint16_t a, uint16_t b, uint16_t c)
{
    if (a > b) {
        if (b > c) return b;           // a > b > c
        else if (a > c) return c;      // a > c >= b
        else return a;                  // c >= a > b
    } else {
        if (a > c) return a;           // b >= a > c
        else if (b > c) return c;      // b >= c >= a
        else return b;                  // c >= b >= a
    }
}

/* =====================================================
   Helper: NTC Temperature Conversion
   ===================================================== */

static uint16_t ADC_NTCToTemperature_Internal(uint16_t adc_raw)
{
    // Find bracket in lookup table
    for (int i = 0; i < NTC_LOOKUP_SIZE - 1; i++) {
        uint16_t adc_lower = ntc_lookup[i].adc;
        uint16_t adc_upper = ntc_lookup[i+1].adc;
        uint16_t temp_lower = ntc_lookup[i].temp;
        uint16_t temp_upper = ntc_lookup[i+1].temp;
        
        // ADC values decrease as temp increases
        if (adc_raw <= adc_lower && adc_raw > adc_upper) {
            // Linear interpolation
            if (adc_lower == adc_upper)
                return temp_lower;
            
            uint16_t temp = temp_lower +
                ((adc_lower - adc_raw) * (temp_upper - temp_lower)) /
                (adc_lower - adc_upper);
            return temp;
        }
    }
    
    // Clamp to bounds
    if (adc_raw >= ntc_lookup[0].adc)
        return ntc_lookup[0].temp;
    return ntc_lookup[NTC_LOOKUP_SIZE-1].temp;
}

/* =====================================================
   Public API Implementation
   ===================================================== */

void ADC_Init(ADCConfig_t *cfg)
{
    memcpy(&g_config, cfg, sizeof(ADCConfig_t));
    memset(&g_measurement, 0, sizeof(ADCMeasurement_t));
    memset(g_vbat_buffer, 0, sizeof(g_vbat_buffer));
    memset(g_ibat_buffer, 0, sizeof(g_ibat_buffer));
    memset(g_temp_mosfet_buffer, 0, sizeof(g_temp_mosfet_buffer));
    memset(g_temp_ntc_buffer, 0, sizeof(g_temp_ntc_buffer));
    g_buffer_index = 0;
    g_has_valid_vbat = false;

    // Keep a sane fallback on the display until real ADC samples arrive.
    g_measurement.vbat_mv = 67200;
}

void ADC_ProcessRawSample(ADCChannel_t channel, uint16_t raw_adc_value)
{
    // Store raw value in buffer
    switch (channel) {
        case ADC_VBAT:
            g_vbat_buffer[g_buffer_index] = raw_adc_value;
            g_measurement.vbat_adc = raw_adc_value;
            
            // Convert ADC to mV using scale factor
            if (raw_adc_value > g_config.vbat_offset) {
                g_measurement.vbat_mv = (uint32_t)(raw_adc_value - g_config.vbat_offset) 
                                        * g_config.vbat_scale / 100;
                g_has_valid_vbat = true;
            } else {
                g_measurement.vbat_mv = 0;
            }
            break;
            
        case ADC_IBAT:
            g_ibat_buffer[g_buffer_index] = raw_adc_value;
            g_measurement.ibat_adc = raw_adc_value;
            
            // Convert ADC to mA
            // Subtract offset, then scale
            if (raw_adc_value > g_config.ibat_offset) {
                g_measurement.ibat_ma = (raw_adc_value - g_config.ibat_offset) 
                                        * g_config.ibat_scale / 100;
            } else {
                g_measurement.ibat_ma = 0;
            }
            break;
            
        case ADC_TEMP_MOSFET:
            g_temp_mosfet_buffer[g_buffer_index] = raw_adc_value;
            g_measurement.temp_mosfet_adc = raw_adc_value;
            g_measurement.temp_mosfet = ADC_NTCToTemperature_Internal(raw_adc_value);
            break;
            
        case ADC_TEMP_NTC:
            g_temp_ntc_buffer[g_buffer_index] = raw_adc_value;
            g_measurement.temp_ntc_adc = raw_adc_value;
            g_measurement.temp_ntc = ADC_NTCToTemperature_Internal(raw_adc_value);
            break;
            
        case ADC_VAUX:
            // Not used, but could store here if needed
            break;
            
        default:
            break;
    }
    
    // Advance buffer index (round-robin, 3-sample median filter)
    g_buffer_index++;
    if (g_buffer_index >= 3)
        g_buffer_index = 0;
}

ADCMeasurement_t ADC_GetMeasurement(void)
{
    // Return latest filtered measurement
    // (Filtering is done incrementally in ProcessRawSample)

    if (!g_has_valid_vbat) {
        g_measurement.vbat_mv = 67200;
    }

    return g_measurement;
}

void ADC_StartConversion(void)
{
    // This would trigger a new ADC conversion cycle
    // For now, this is a placeholder for hardware integration
    // In real code, this sets ADC_CR2 START bit or similar
}

uint16_t ADC_NTCToTemperature(uint16_t adc_raw)
{
    return ADC_NTCToTemperature_Internal(adc_raw);
}

uint16_t ADC_GetRawValue(ADCChannel_t channel)
{
    switch (channel) {
        case ADC_VBAT:
            return g_measurement.vbat_adc;
        case ADC_IBAT:
            return g_measurement.ibat_adc;
        case ADC_TEMP_MOSFET:
            return g_measurement.temp_mosfet_adc;
        case ADC_TEMP_NTC:
            return g_measurement.temp_ntc_adc;
        default:
            return 0;
    }
}

uint16_t ADC_GetACVoltage(void)
{
    // AC voltage sensing is not yet wired into the ADC channel map.
    // Return a safe nominal value until the actual divider channel is connected.
    return 230;
}