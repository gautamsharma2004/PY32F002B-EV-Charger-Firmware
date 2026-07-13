#ifndef PWM_CTRL_H
#define PWM_CTRL_H

#include <stdint.h>
#include <stdbool.h>

void PWM_Init(void);
void PWM_RegulateVoltage(uint16_t mv);
void PWM_RegulateCurrent(uint16_t ma);
void PWM_Disable(void);
void Fan_Control(bool state);

#endif // PWM_CTRL_H