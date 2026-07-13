#ifndef __TM1637_H
#define __TM1637_H

#include "main.h"

#define TM1637_DIO_PORT GPIOB
#define TM1637_DIO_PIN  GPIO_PIN_1

#define TM1637_CLK_PORT GPIOB
#define TM1637_CLK_PIN  GPIO_PIN_2

void TM1637_Init(void);

void TM1637_Clear(void);

void TM1637_SetBrightness(uint8_t brightness);

void TM1637_DisplayNumber(uint16_t number);

void TM1637_DisplayRaw(uint8_t data[4]);

void TM1637_DisplayVoltage(uint16_t value);

void TM1637_DisplayCurrent(uint16_t value);

#endif