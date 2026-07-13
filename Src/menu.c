#include "menu.h"
#include "tm1637.h"
#include "charger_sm.h"
#include "battery.h"
#include "soc.h"

typedef struct
{
    uint16_t value;
} VSET_t;

typedef struct
{
    uint16_t value;
} CSET_t;

/*---------------- VSET TABLE ----------------*/

static const VSET_t VsetTable[] =
{
    {5460},
    {5475},
    {5840},
    {5880},
    {6735},
    {6935},
    {7140},
    {7300},
    {8395},
    {8400},
    {8760}
};

/*---------------- CSET TABLE ----------------*/

static const CSET_t CsetTable[] =
{
    {62},
    {72},
    {82},
    {92},
    {102}
};

#define VSET_COUNT (sizeof(VsetTable)/sizeof(VSET_t))
#define CSET_COUNT (sizeof(CsetTable)/sizeof(CSET_t))

// 8 seconds = 8000ms. If Task runs every 10ms, timeout is 800.
#define MENU_TIMEOUT_TICKS 800 

static MenuMode_t CurrentMode = MODE_VSET;
static uint8_t VsetIndex = 4;      //67.35V
static uint8_t CsetIndex = 0;      //6.2A
static bool MenuActive = false;
static uint16_t MenuIdleTimer = 0;
static uint8_t SaveSequenceStep = 0;

static uint32_t MenuVsetToMv(uint16_t value)
{
    return (uint32_t)value * 10U;
}

static void Menu_ApplySelection(void)
{
    uint32_t vset_mv = MenuVsetToMv(VsetTable[VsetIndex].value);
    uint16_t cset_ma = (uint16_t)CsetTable[CsetIndex].value * 100U;

    ChargerSM_SetVset(vset_mv);
    ChargerSM_SetCset(cset_ma);
    Battery_SetVset(vset_mv);
    SOC_SetVset(vset_mv);
}

void Menu_Init(void)
{
    CurrentMode = MODE_VSET;
    VsetIndex = 4;
    CsetIndex = 0;
    MenuActive = false;
    MenuIdleTimer = 0;
    SaveSequenceStep = 0;
}

void Menu_Task(void)
{
    if (!MenuActive) return;

    MenuIdleTimer++;
    
    // 8 seconds of inactivity triggers auto-save sequence
    if (MenuIdleTimer >= MENU_TIMEOUT_TICKS) {
        
        // Sequence: Display VSET (2s) -> Display CSET (2s) -> Exit
        if (MenuIdleTimer == MENU_TIMEOUT_TICKS) {
            SaveSequenceStep = 1;
            TM1637_DisplayVoltage(VsetTable[VsetIndex].value);
        }
        else if (MenuIdleTimer == MENU_TIMEOUT_TICKS + 200) { // +2 seconds
            SaveSequenceStep = 2;
            TM1637_DisplayCurrent(CsetTable[CsetIndex].value);
        }
        else if (MenuIdleTimer >= MENU_TIMEOUT_TICKS + 400) { // +4 seconds total
            Menu_ApplySelection();
            // TODO: Add Flash/EEPROM write here to make non-volatile across power cycles
            MenuActive = false;
            SaveSequenceStep = 0;
            MenuIdleTimer = 0;
        }
    }
}

void Menu_UpdateDisplay(void)
{
    if(CurrentMode == MODE_VSET)
    {
        TM1637_DisplayVoltage(VsetTable[VsetIndex].value);
    }
    else
    {
        TM1637_DisplayCurrent(CsetTable[CsetIndex].value);
    }
}

void Menu_NextItem(void)
{
    if (SaveSequenceStep > 0) return; // Block input during save sequence
    
    MenuActive = true;
    MenuIdleTimer = 0; // Reset 8-second timer
    
    if(CurrentMode == MODE_VSET)
    {
        VsetIndex++;
        if(VsetIndex >= VSET_COUNT)
            VsetIndex = 0;
    }
    else
    {
        CsetIndex++;
        if(CsetIndex >= CSET_COUNT)
            CsetIndex = 0;
    }

    Menu_UpdateDisplay();
}

void Menu_ToggleMode(void)
{
    if (SaveSequenceStep > 0) return; // Block input during save sequence
    
    MenuActive = true;
    MenuIdleTimer = 0;

    if(CurrentMode == MODE_VSET)
        CurrentMode = MODE_CSET;
    else
        CurrentMode = MODE_VSET;

    Menu_UpdateDisplay();
}

void Menu_Enter(void)
{
    MenuActive = true;
    MenuIdleTimer = 0;
    SaveSequenceStep = 0;
    Menu_UpdateDisplay();
}

void Menu_OnButtonPress(void)
{
    Menu_NextItem();
}

bool Menu_IsActive(void)
{
    return MenuActive;
}

MenuMode_t Menu_GetMode(void)
{
    return CurrentMode;
}