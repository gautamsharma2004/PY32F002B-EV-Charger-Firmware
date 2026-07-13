/*
 * main.c - EV Charger Firmware
 * * Modular architecture with state machine control.
 * Calls to menu/button/display still occur, but display manager
 * prioritizes what gets shown based on charger state.
 */

#include <stdint.h>
#include <stdbool.h>

// Hardware headers (assuming CMSIS-style)
#include "py32f0xx.h"

// Application modules
#include "charger_sm.h"
#include "charger_adc.h"
#include "button.h"
#include "menu.h"
#include "tm1637.h"
#include "battery.h"
#include "soc.h"

/* =====================================================
   Configuration & Hardware Setup
   ===================================================== */

// Timer ISR counter (10 ms tick)
static volatile uint32_t g_systick_ms = 0;

// ADC ISR counter (triggers conversion every 100 ms)
static volatile uint8_t g_adc_interval_counter = 0;
static bool g_menu_was_active = false;

// Display state (what to show)
typedef enum {
    DISPLAY_VOLTAGE,
    DISPLAY_CURRENT,
    DISPLAY_SOC,
    DISPLAY_FAULT_CODE,
    DISPLAY_MENU,
} DisplayMode_t;

static DisplayMode_t g_display_mode = DISPLAY_VOLTAGE;
static uint32_t g_display_timer = 0;  // Alternates every 100 deciseconds (10 sec)

/* =====================================================
   Hardware Initialization
   ===================================================== */

static void SystemInit_Clock(void)
{
    // Configure system clock (HSI 8 MHz, no PLL assumed for simplicity)
    // In real code, use HAL or CMSIS clock configuration
}

static void SystemInit_GPIO(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // PA1: DTC Control (Output) -> HIGH = OFF, LOW = ON
    GPIOA->MODER &= ~(0x3 << (1 * 2)); 
    GPIOA->MODER |=  (0x1 << (1 * 2)); 
    GPIOA->BSRR = GPIO_PIN_1; // Set High (TL494 OFF) by default

    // PA2: FAN Control (Output) -> HIGH = ON, LOW = OFF
    GPIOA->MODER &= ~(0x3 << (2 * 2));
    GPIOA->MODER |=  (0x1 << (2 * 2));
    GPIOA->BRR = GPIO_PIN_2;  // Set Low (Fan OFF) by default
    
    // PA3: AC Power Sense (Input from Q5 Optocoupler)
    GPIOA->MODER &= ~(0x3 << (3 * 2)); // Input mode
    GPIOA->PUPDR &= ~(0x3 << (3 * 2));
    GPIOA->PUPDR |=  (0x1 << (3 * 2)); // Enable Internal Pull-up

    // TM1637 (PB1, PB2)
    GPIOB->MODER &= ~(0x3 << (1 * 2));
    GPIOB->MODER &= ~(0x3 << (2 * 2));
    GPIOB->MODER |= (0x1 << (1 * 2));
    GPIOB->MODER |= (0x1 << (2 * 2));
}

static void SystemInit_Timer(void)
{
    // Setup SysTick for 10ms tick
    SysTick_Config(8000000 / 100);  // 10 ms for 8 MHz
}

static void SystemInit_ADC(void)
{
    // Configure ADC for channels: VBAT, IBAT, TEMP_MOSFET, TEMP_NTC
    ADCConfig_t adc_cfg = {
        .vbat_scale = 20,           // 20 mV per ADC count (adjust per divider)
        .ibat_scale = 3,            // 3 mA per ADC count (adjust per sense R)
        .vbat_offset = 0,
        .ibat_offset = 2000,        // ~1.64V offset for 0A (mid-range sensor)
        .adc_ov_limit = 4000,       // ~80V (over-voltage)
        .adc_uv_limit = 500,        // ~10V (under-voltage / reverse polarity)
        .adc_temp_warn = 1200,      // 80°C
        .adc_temp_shutdown = 1000,  // 90°C
    };
    
    ADC_Init(&adc_cfg);
}

/* =====================================================
   Charger Configuration
   ===================================================== */

static void ChargerInit(void)
{
    ChargerConfig_t cfg = {
        .vset = 67200,           // Default 67.2V
        .cset = 6200,            // Default 6.2A
        .adc_ov_threshold = 75000,  // 75V over-voltage
        .adc_uv_threshold = 10000,  // 10V under-voltage
        .adc_temp_warn = 1200,      // 80°C warning (ADC units)
        .adc_temp_shutdown = 1000,  // 90°C shutdown
        .vset_wake_duration = 60,
        .cv_timeout_minutes = 60,
        .total_timeout_hours = 7,
        .softstart_ramp_rate = 1,   // ADC will handle the 0.3A/sec ramp
    };
    
    ChargerSM_Init(&cfg);
    Battery_Init(&(BatteryConfig_t){
        .vset = 67200,
        .threshold_20 = 13440,
        .threshold_40 = 26880,
        .threshold_65 = 43680,
        .debounce_count = 0,
        .debounce_samples = 5,
    });
    
    SOC_Init(&(SOCConfig_t){
        .vset = 67200,
        .hysteresis_pct = 2,
    });
}

/* =====================================================
   Main Loop - 10 ms Tick
   ===================================================== */

static void Task_10ms(void)
{
    // Process button input
    Button_Task();
    
    // Process menu (only if in menu mode)
    if (g_display_mode == DISPLAY_MENU) {
        Menu_Task();
    }

    if (Menu_IsActive()) {
        g_menu_was_active = true;
        g_display_mode = DISPLAY_MENU;
        g_display_timer = 0;
    } else if (g_menu_was_active) {
        g_menu_was_active = false;
        g_display_mode = DISPLAY_VOLTAGE;
        g_display_timer = 0;
    }
}

/* =====================================================
   ADC Sampling - 100 ms Tick
   ===================================================== */

static void Task_100ms(void)
{
    // Get latest ADC measurements
    ADCMeasurement_t meas = ADC_GetMeasurement();
    
    // Update battery health classifier
    Battery_Update(meas.vbat_mv);
    
    // Update SOC calculator
    SOC_Update(meas.vbat_mv);
    
    // Call charger state machine (every 100ms = 1 decisecond)
    ChargerSM_Task(meas.vbat_mv, meas.ibat_ma,
                   meas.temp_mosfet, meas.temp_ntc);
    
    // Trigger next ADC conversion
    ADC_StartConversion();
}

/* =====================================================
   Display Manager - 1 Second Tick
   ===================================================== */

static void Task_1sec(void)
{
    ChargerState_t state = ChargerSM_GetState();
    
    
    // If in fault state, show fault code
    if (state == STATE_FAULT) {
        g_display_mode = DISPLAY_FAULT_CODE;
    }
    // If menu is active, show menu
    else if (Menu_IsActive()) {
        g_display_mode = DISPLAY_MENU;
    }
    // During charging, alternate between voltage and SOC
    else if (ChargerSM_IsCharging()) {
        g_display_timer++;
        if (g_display_timer >= 10) {  // Every 10 seconds
            g_display_timer = 0;
            g_display_mode = (g_display_mode == DISPLAY_VOLTAGE) 
                           ? DISPLAY_SOC : DISPLAY_VOLTAGE;
        }
    }
    // Otherwise show voltage
    else {
        g_display_mode = DISPLAY_VOLTAGE;
    }
}

/* =====================================================
   Display Update - Synchronized with Display Manager
   ===================================================== */

static void UpdateDisplay(void)
{
    ADCMeasurement_t meas = ADC_GetMeasurement();
    uint8_t soc = SOC_GetDisplaySOC();
    
    switch (g_display_mode) {
        case DISPLAY_VOLTAGE: {
            // Display battery voltage (TM1637_DisplayVoltage expects 0.01V units)
            TM1637_DisplayVoltage((uint16_t)(meas.vbat_mv / 10));
            break;
        }
        case DISPLAY_CURRENT: {
            uint16_t amps = meas.ibat_ma / 1000;
            TM1637_DisplayNumber(amps);
            break;
        }
        case DISPLAY_SOC: {
            if (SOC_IsQualified()) {
                TM1637_DisplayNumber(soc);
            } else {
                TM1637_DisplayNumber(soc);  // Keep showing old value during qualification
            }
            break;
        }
        case DISPLAY_FAULT_CODE: {
            FaultCode_t fault = ChargerSM_GetFault();
            TM1637_DisplayNumber((uint16_t)fault);
            break;
        }
        case DISPLAY_MENU: {
            Menu_UpdateDisplay();
            break;
        }
    }
}

/* =====================================================
   Hardware Output Controls
   ===================================================== */




static void UpdateLED(void)
{
    ChargerState_t state = ChargerSM_GetState();
    // Assuming LED is on PA5 (Example)
    if (state == STATE_POWER_ON || ChargerSM_IsCharging()) {
        // GPIOA->BSRR = GPIO_PIN_5;
    } else if (state == STATE_FAULT) {
        // Toggle logic
    } else {
        // GPIOA->BRR = GPIO_PIN_5;
    }
}

static void UpdateFan(void)
{
    // Fan_CTRL is on PA2. Q6 is an N-Channel MOSFET, so HIGH = ON.
    if (ChargerSM_IsFanEnabled()) {
        GPIOA->BSRR = GPIO_PIN_2;  // PA2 High = Fan ON
    } else {
        GPIOA->BRR = GPIO_PIN_2;   // PA2 Low = Fan OFF
    }
}

static void UpdateTL494(void)
{
    // DTC is on PA1. 
    if (ChargerSM_IsPWMEnabled()) {
        GPIOA->BRR = GPIO_PIN_1;  // PA1 LOW = TL494 ON (Outputs Voltage)
    } else {
        GPIOA->BSRR = GPIO_PIN_1; // PA1 HIGH = TL494 OFF (0V Output)
    }
}

static void CheckACPower(void)
{
    static bool ac_was_on = false;
    
    // Optocoupler Q5 pulls PA3 LOW when 230V AC is present
    bool ac_is_on = ((GPIOA->IDR & GPIO_PIN_3) == 0); 
    
    if (ac_is_on && !ac_was_on) {
        ChargerSM_ACOnEvent();
    } else if (!ac_is_on && ac_was_on) {
        ChargerSM_ACOffEvent();
    }
    
    ac_was_on = ac_is_on;
}

void App_SysTickHandler(void)
{
    g_systick_ms += 10;

    Task_10ms();

    static uint8_t count_100ms = 0;
    count_100ms++;
    if (count_100ms >= 10) {
        count_100ms = 0;
        Task_100ms();
    }

    static uint16_t count_1sec = 0;
    count_1sec++;
    if (count_1sec >= 100) {
        count_1sec = 0;
        Task_1sec();
    }

    UpdateDisplay();
    UpdateLED();
    UpdateFan();
    UpdateTL494();
    CheckACPower();
}

/* =====================================================
   Main Entry Point
   ===================================================== */

int main(void)
{
    // System initialization
    SystemInit_Clock();
    SystemInit_GPIO();
    SystemInit_Timer();
    SystemInit_ADC();
    
    // Application initialization
    Button_Init();
    Menu_Init();
    TM1637_Init();
    ChargerInit();

    __enable_irq();

    TM1637_DisplayVoltage(6720);
    
    // Main loop: background tasks and idle
    while (1) {
        __WFI();  // Wait for interrupt
    }
    
    return 0;
}

/* =====================================================
   Stub Implementations (for compilation if missing)
   ===================================================== */

__weak void Button_Init(void) { }
__weak void Button_Task(void) { }
__weak void Menu_Init(void) { }
__weak void Menu_Task(void) { }
__weak void Menu_Enter(void) { }
__weak void Menu_ToggleMode(void) { }
__weak bool Menu_IsActive(void) { return false; }
__weak void Menu_OnButtonPress(void) { }
__weak void Menu_UpdateDisplay(void) { }
__weak void TM1637_Init(void) { }
__weak void TM1637_DisplayNumber(uint16_t num) { }