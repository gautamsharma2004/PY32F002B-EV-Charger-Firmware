#include "button.h"
#include "menu.h"
#include "py32f0xx.h"

/*
    Call Button_Task() every 10ms

    Long Press = 2 seconds

    10ms x 200 = 2000ms
*/

#define BUTTON_PORT GPIOB
#define BUTTON_PIN  GPIO_PIN_3

#define LONG_PRESS_TIME   200

static uint16_t PressCounter = 0;
static uint8_t LongPressDone = 0;


static uint8_t Button_Read(void)
{
    return ((BUTTON_PORT->IDR & BUTTON_PIN) == 0);
}


void Button_Init(void)
{
    PressCounter = 0;
    LongPressDone = 0;
}


void Button_Task(void)
{
    if(Button_Read())
    {
        if(PressCounter < LONG_PRESS_TIME)
            PressCounter++;

        if((PressCounter >= LONG_PRESS_TIME) &&
           (LongPressDone == 0))
        {
            LongPressDone = 1;

            Menu_ToggleMode();
        }
    }
    else
    {
        if((PressCounter > 0) &&
           (PressCounter < LONG_PRESS_TIME))
        {
            Menu_NextItem();
        }

        PressCounter = 0;
        LongPressDone = 0;
    }
}