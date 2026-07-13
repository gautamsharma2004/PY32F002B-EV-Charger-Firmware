#ifndef __MENU_H
#define __MENU_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    MODE_VSET = 0,
    MODE_CSET
} MenuMode_t;

void Menu_Init(void);

void Menu_Task(void);

void Menu_NextItem(void);

void Menu_Enter(void);

void Menu_ToggleMode(void);

void Menu_OnButtonPress(void);

bool Menu_IsActive(void);

void Menu_UpdateDisplay(void);

MenuMode_t Menu_GetMode(void);

#endif