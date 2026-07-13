#include "tm1637.h"

static uint8_t brightness = 7;

static const uint8_t segTable[10] =
{
    0x3F,   //0
    0x06,   //1
    0x5B,   //2
    0x4F,   //3
    0x66,   //4
    0x6D,   //5
    0x7D,   //6
    0x07,   //7
    0x7F,   //8
    0x6F    //9
};

/*------------------------------------------------------------------*/

static void Delay(void)
{
    for(volatile int i = 0; i < 20; i++);
}

/*------------------------------------------------------------------*/

static void CLK_HIGH(void)
{
    HAL_GPIO_WritePin(TM1637_CLK_PORT, TM1637_CLK_PIN, GPIO_PIN_SET);
}

static void CLK_LOW(void)
{
    HAL_GPIO_WritePin(TM1637_CLK_PORT, TM1637_CLK_PIN, GPIO_PIN_RESET);
}

static void DIO_HIGH(void)
{
    HAL_GPIO_WritePin(TM1637_DIO_PORT, TM1637_DIO_PIN, GPIO_PIN_SET);
}

static void DIO_LOW(void)
{
    HAL_GPIO_WritePin(TM1637_DIO_PORT, TM1637_DIO_PIN, GPIO_PIN_RESET);
}

/*------------------------------------------------------------------*/

static void Start(void)
{
    DIO_HIGH();
    CLK_HIGH();
    Delay();

    DIO_LOW();
    Delay();

    CLK_LOW();
}

static void Stop(void)
{
    CLK_LOW();
    Delay();

    DIO_LOW();
    Delay();

    CLK_HIGH();
    Delay();

    DIO_HIGH();
}

/*------------------------------------------------------------------*/

static void WriteByte(uint8_t data)
{
    for(uint8_t i = 0; i < 8; i++)
    {
        CLK_LOW();

        if(data & 0x01)
            DIO_HIGH();
        else
            DIO_LOW();

        Delay();

        CLK_HIGH();
        Delay();

        data >>= 1;
    }

    CLK_LOW();

    DIO_HIGH();

    Delay();

    CLK_HIGH();

    Delay();

    CLK_LOW();
}

/*------------------------------------------------------------------*/

void TM1637_Init(void)
{
    CLK_HIGH();
    DIO_HIGH();
}

/*------------------------------------------------------------------*/

void TM1637_SetBrightness(uint8_t level)
{
    if(level > 7)
        level = 7;

    brightness = level;
}

/*------------------------------------------------------------------*/

void TM1637_Clear(void)
{
    Start();
    WriteByte(0x40);
    Stop();

    Start();
    WriteByte(0xC0);

    for(uint8_t i = 0; i < 4; i++)
        WriteByte(0x00);

    Stop();

    Start();
    WriteByte(0x88 | brightness);
    Stop();
}

/*------------------------------------------------------------------*/
/* Display raw segment data                                         */
/*------------------------------------------------------------------*/

void TM1637_DisplayRaw(uint8_t data[4])
{
    Start();
    WriteByte(0x40);
    Stop();

    Start();
    WriteByte(0xC0);

    for(uint8_t i = 0; i < 4; i++)
        WriteByte(data[i]);

    Stop();

    Start();
    WriteByte(0x88 | brightness);
    Stop();
}

/*------------------------------------------------------------------*/
/* Display normal integer                                            */
/*------------------------------------------------------------------*/

void TM1637_DisplayNumber(uint16_t number)
{
    uint8_t seg[4];

    seg[0] = segTable[(number / 1000) % 10];
    seg[1] = segTable[(number / 100) % 10];
    seg[2] = segTable[(number / 10) % 10];
    seg[3] = segTable[number % 10];

    TM1637_DisplayRaw(seg);
}

/*------------------------------------------------------------------*/
/* Display Voltage                                                   */
/* Input : 5460 -> 54.6                                              */
/*         6735 -> 67.3                                              */
/*------------------------------------------------------------------*/

void TM1637_DisplayVoltage(uint16_t value)
{
    uint8_t seg[4];

    uint8_t d1 = value / 1000;
    uint8_t d2 = (value / 100) % 10;
    uint8_t d3 = (value / 10) % 10;
    uint8_t d4 = value % 10;

    seg[0] = segTable[d1];
    seg[1] = segTable[d2] | 0x80;     // Decimal point
    seg[2] = segTable[d3];
    seg[3] = segTable[d4];

    TM1637_DisplayRaw(seg);
}

/*------------------------------------------------------------------*/
/* Display Current                                                   */
/* Input : 62  -> 6.2                                                */
/*         102 ->10.2                                                */
/*------------------------------------------------------------------*/

void TM1637_DisplayCurrent(uint16_t value)
{
    uint8_t seg[4];

    if(value < 100)
    {
        seg[0] = 0x00;                            // Blank
        seg[1] = segTable[value / 10] | 0x80;     // Decimal point
        seg[2] = segTable[value % 10];
        seg[3] = 0x00;                            // Blank
    }
    else
    {
        seg[0] = segTable[1];
        seg[1] = segTable[0] | 0x80;
        seg[2] = segTable[value % 10];
        seg[3] = 0x00;
    }

    TM1637_DisplayRaw(seg);
}