/*
 * ATmega8A SPI Slave
 * 4x4 keypad + LCD 16x2
 * Atmel Studio 6 / AVR-GCC
 */

#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/atomic.h>
#include <stdint.h>
#include <stdlib.h>

/* =========================================================
   LCD: ATmega8A PORTC
   ========================================================= */

#define LCD_PORT PORTC
#define LCD_DDR  DDRC

#define LCD_RS PC0
#define LCD_EN PC1
#define LCD_D4 PC2
#define LCD_D5 PC3
#define LCD_D6 PC4
#define LCD_D7 PC5

/* =========================================================
   4x4 KEYPAD: ATmega8A PORTD
   Rows: PD0-PD3
   Columns: PD4-PD7
   ========================================================= */

#define KEYPAD_PORT PORTD
#define KEYPAD_DDR  DDRD
#define KEYPAD_PIN  PIND

#define FRAME_HEADER 0xA5

static volatile uint16_t sendNumber = 0;
static volatile uint16_t remoteNumber = 0;

static volatile uint8_t receiveFrame[4];
static volatile uint8_t transmitFrame[4];

static volatile uint8_t spiIndex = 0;

/* =========================================================
   LCD FUNCTIONS
   ========================================================= */

static void LCD_EnablePulse(void)
{
    LCD_PORT |= (1 << LCD_EN);
    _delay_us(1);

    LCD_PORT &= ~(1 << LCD_EN);
    _delay_us(100);
}

static void LCD_SendNibble(uint8_t nibble)
{
    LCD_PORT &= ~(
        (1 << LCD_D4) |
        (1 << LCD_D5) |
        (1 << LCD_D6) |
        (1 << LCD_D7)
    );

    if (nibble & 0x01)
        LCD_PORT |= (1 << LCD_D4);

    if (nibble & 0x02)
        LCD_PORT |= (1 << LCD_D5);

    if (nibble & 0x04)
        LCD_PORT |= (1 << LCD_D6);

    if (nibble & 0x08)
        LCD_PORT |= (1 << LCD_D7);

    LCD_EnablePulse();
}

static void LCD_SendByte(uint8_t value, uint8_t dataMode)
{
    if (dataMode)
        LCD_PORT |= (1 << LCD_RS);
    else
        LCD_PORT &= ~(1 << LCD_RS);

    LCD_SendNibble(value >> 4);
    LCD_SendNibble(value & 0x0F);

    _delay_us(50);
}

static void LCD_Command(uint8_t command)
{
    LCD_SendByte(command, 0);

    if ((command == 0x01) || (command == 0x02))
        _delay_ms(2);
}

static void LCD_Character(char character)
{
    LCD_SendByte((uint8_t)character, 1);
}

static void LCD_Init(void)
{
    LCD_DDR |=
        (1 << LCD_RS) |
        (1 << LCD_EN) |
        (1 << LCD_D4) |
        (1 << LCD_D5) |
        (1 << LCD_D6) |
        (1 << LCD_D7);

    LCD_PORT &= ~(
        (1 << LCD_RS) |
        (1 << LCD_EN)
    );

    _delay_ms(40);

    LCD_SendNibble(0x03);
    _delay_ms(5);

    LCD_SendNibble(0x03);
    _delay_us(150);

    LCD_SendNibble(0x03);
    LCD_SendNibble(0x02);

    LCD_Command(0x28);
    LCD_Command(0x0C);
    LCD_Command(0x06);
    LCD_Command(0x01);
}

static void LCD_Goto(uint8_t row, uint8_t column)
{
    uint8_t address;

    if (row == 0)
        address = column;
    else
        address = 0x40 + column;

    LCD_Command(0x80 | address);
}

static void LCD_Print(const char *text)
{
    while (*text)
    {
        LCD_Character(*text);
        text++;
    }
}

static void LCD_PrintNumber(uint16_t number)
{
    char buffer[6];

    itoa(number, buffer, 10);
    LCD_Print(buffer);

    if (number < 10000)
        LCD_Character(' ');

    if (number < 1000)
        LCD_Character(' ');

    if (number < 100)
        LCD_Character(' ');

    if (number < 10)
        LCD_Character(' ');
}

/* =========================================================
   KEYPAD FUNCTIONS
   ========================================================= */

static const char keypadMap[4][4] =
{
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

static void Keypad_Init(void)
{
    KEYPAD_DDR = 0x0F;
    KEYPAD_PORT = 0xFF;
}

static char Keypad_ScanRaw(void)
{
    uint8_t row;
    uint8_t column;

    for (row = 0; row < 4; row++)
    {
        KEYPAD_PORT |= 0x0F;
        KEYPAD_PORT &= ~(1 << row);

        _delay_us(5);

        for (column = 0; column < 4; column++)
        {
            if (!(KEYPAD_PIN & (1 << (column + 4))))
            {
                return keypadMap[row][column];
            }
        }
    }

    return 0;
}

static char Keypad_GetKey(void)
{
    char key;

    key = Keypad_ScanRaw();

    if (key != 0)
    {
        _delay_ms(20);

        if (Keypad_ScanRaw() == key)
        {
            while (Keypad_ScanRaw() != 0)
            {
                _delay_ms(5);
            }

            return key;
        }
    }

    return 0;
}

/* =========================================================
   SPI SLAVE FUNCTIONS
   ========================================================= */

static uint8_t CalculateChecksum(
    uint8_t header,
    uint8_t highByte,
    uint8_t lowByte
)
{
    return header ^ highByte ^ lowByte;
}

static void PrepareTransmitFrame(uint16_t number)
{
    transmitFrame[0] = FRAME_HEADER;
    transmitFrame[1] = (uint8_t)(number >> 8);
    transmitFrame[2] = (uint8_t)(number & 0xFF);

    transmitFrame[3] = CalculateChecksum(
        transmitFrame[0],
        transmitFrame[1],
        transmitFrame[2]
    );
}

static void SPI_SlaveInit(void)
{
    /*
     * ATmega8A SPI:
     *
     * PB2 = SS input
     * PB3 = MOSI input
     * PB4 = MISO output
     * PB5 = SCK input
     */

    DDRB &= ~(
        (1 << PB2) |
        (1 << PB3) |
        (1 << PB5)
    );

    DDRB |= (1 << PB4);

    /*
     * Pull-up on SS.
     */
    PORTB |= (1 << PB2);

    PrepareTransmitFrame(0);

    spiIndex = 0;

    /*
     * Put the first response byte into SPDR.
     */
    SPDR = transmitFrame[0];

    /*
     * Enable SPI and SPI interrupt.
     * Mode 0.
     */
    SPCR =
        (1 << SPE) |
        (1 << SPIE);

    sei();
}

ISR(SPI_STC_vect)
{
    uint8_t receivedByte;
    uint16_t localCopy;

    receivedByte = SPDR;

    receiveFrame[spiIndex] = receivedByte;

    spiIndex++;

    if (spiIndex >= 4)
    {
        spiIndex = 0;

        /*
         * Validate frame received from ATmega16A.
         */
        if (
            receiveFrame[0] == FRAME_HEADER &&
            receiveFrame[3] ==
            CalculateChecksum(
                receiveFrame[0],
                receiveFrame[1],
                receiveFrame[2]
            )
        )
        {
            remoteNumber =
                ((uint16_t)receiveFrame[1] << 8) |
                receiveFrame[2];
        }

        /*
         * Prepare the slave number for the next frame.
         */
        localCopy = sendNumber;
        PrepareTransmitFrame(localCopy);
    }

    /*
     * Load the next response byte.
     */
    SPDR = transmitFrame[spiIndex];
}

/* =========================================================
   NUMBER ENTRY
   ========================================================= */

static void ProcessKey(
    char key,
    uint16_t *inputNumber,
    uint8_t *digitCount
)
{
    if ((key >= '0') && (key <= '9'))
    {
        if (*digitCount < 4)
        {
            *inputNumber =
                (*inputNumber * 10) +
                (uint16_t)(key - '0');

            (*digitCount)++;
        }
    }
    else if (key == '*')
    {
        *inputNumber = 0;
        *digitCount = 0;
    }
    else if (key == 'A')
    {
        *inputNumber /= 10;

        if (*digitCount > 0)
            (*digitCount)--;
    }
    else if (key == 'B')
    {
        if (*inputNumber < 9999)
            (*inputNumber)++;
    }
    else if (key == 'C')
    {
        if (*inputNumber > 0)
            (*inputNumber)--;
    }
    else if (key == 'D')
    {
        *inputNumber = 0;
        *digitCount = 0;
    }
    else if (key == '#')
    {
        /*
         * Confirm the slave number.
         */
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            sendNumber = *inputNumber;
        }
    }
}

/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    char key;

    uint16_t inputNumber = 0;
    uint16_t receivedNumberCopy = 0;

    uint8_t digitCount = 0;

    LCD_Init();
    Keypad_Init();
    SPI_SlaveInit();

    LCD_Goto(0, 0);
    LCD_Print("M8 :");

    LCD_Goto(1, 0);
    LCD_Print("M16:");

    while (1)
    {
        key = Keypad_GetKey();

        if (key != 0)
        {
            ProcessKey(
                key,
                &inputNumber,
                &digitCount
            );
        }

        /*
         * remoteNumber is changed inside the SPI ISR,
         * so copy it atomically.
         */
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            receivedNumberCopy = remoteNumber;
        }

        LCD_Goto(0, 5);
        LCD_PrintNumber(inputNumber);

        LCD_Goto(1, 5);
        LCD_PrintNumber(receivedNumberCopy);

        _delay_ms(50);
    }
}