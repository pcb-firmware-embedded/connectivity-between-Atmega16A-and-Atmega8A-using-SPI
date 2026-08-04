/*
 * ATmega8A SPI Slave
 * Bidirectional number communication with ATmega16A
 * 4x4 keypad + 16x2 LCD
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
   LCD CONNECTIONS — PORTC

   RS -> PC0
   EN -> PC1
   D4 -> PC2
   D5 -> PC3
   D6 -> PC4
   D7 -> PC5
   RW -> GND
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
   4x4 KEYPAD — PORTD

   R1 -> PD0
   R2 -> PD1
   R3 -> PD2
   R4 -> PD3

   C1 -> PD4
   C2 -> PD5
   C3 -> PD6
   C4 -> PD7
   ========================================================= */

#define KEYPAD_PORT PORTD
#define KEYPAD_DDR  DDRD
#define KEYPAD_PIN  PIND

/* =========================================================
   SPI FRAME
   ========================================================= */

#define FRAME_HEADER 0xA5
#define FRAME_SIZE   4

/*
 * Number confirmed on ATmega8A.
 * This value is sent to ATmega16A.
 */
static volatile uint16_t confirmedNumber = 0;

/*
 * Number received from ATmega16A.
 */
static volatile uint16_t numberFromATmega16 = 0;

/*
 * SPI buffers.
 */
static volatile uint8_t receiveFrame[FRAME_SIZE];
static volatile uint8_t transmitFrame[FRAME_SIZE];

static volatile uint8_t receiveIndex = 0;

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

static void LCD_SendByte(uint8_t value, uint8_t isData)
{
    if (isData)
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

    LCD_Command(0x28); /* 4-bit, 2 lines */
    LCD_Command(0x0C); /* Display ON */
    LCD_Command(0x06); /* Cursor increment */
    LCD_Command(0x01); /* Clear LCD */
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
    while (*text != '\0')
    {
        LCD_Character(*text);
        text++;
    }
}

static void LCD_PrintNumber(uint16_t number)
{
    char buffer[6];

    itoa((int)number, buffer, 10);
    LCD_Print(buffer);

    /*
     * Clear remaining positions.
     */
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
    /*
     * PD0-PD3: outputs for rows
     * PD4-PD7: inputs for columns
     */
    KEYPAD_DDR = 0x0F;

    /*
     * Rows HIGH, column pull-ups enabled
     */
    KEYPAD_PORT = 0xFF;
}

static char Keypad_ScanRaw(void)
{
    uint8_t row;
    uint8_t column;

    for (row = 0; row < 4; row++)
    {
        /*
         * Set all rows HIGH.
         */
        KEYPAD_PORT |= 0x0F;

        /*
         * Drive current row LOW.
         */
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
            /*
             * Wait for release.
             */
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
   NUMBER PROCESSING
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
                (*inputNumber * 10U) +
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
        *inputNumber /= 10U;

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
         * Confirm the number for transmission.
         */
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            confirmedNumber = *inputNumber;
        }
    }
}

/* =========================================================
   SPI FUNCTIONS
   ========================================================= */

static uint8_t CalculateChecksum(
    uint8_t header,
    uint8_t highByte,
    uint8_t lowByte
)
{
    return header ^ highByte ^ lowByte;
}

static void BuildTransmitFrame(uint16_t number)
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
     * ATmega8A SPI pins:
     *
     * PB2 = SS / INT2 input
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
     * Enable pull-up on SS.
     */
    PORTB |= (1 << PB2);

    receiveIndex = 0;

    BuildTransmitFrame(0);

    /*
     * Preload first outgoing byte.
     */
    SPDR = transmitFrame[0];

    /*
     * Enable SPI and SPI interrupt.
     * Slave mode, Mode 0.
     */
    SPCR =
        (1 << SPE) |
        (1 << SPIE);

    /*
     * Configure INT2 on falling edge.
     * PB2 is also INT2.
     */
    MCUCSR &= ~(1 << ISC2);

    /*
     * Clear pending INT2 interrupt flag.
     */
    GIFR |= (1 << INTF2);

    /*
     * Enable INT2.
     */
    GICR |= (1 << INT2);

    sei();
}

/*
 * Called when ATmega16A pulls SS LOW.
 */
ISR(INT2_vect)
{
    uint16_t localNumberSnapshot;

    receiveIndex = 0;

    /*
     * Copy confirmed number.
     */
    localNumberSnapshot = confirmedNumber;

    /*
     * Build the frame for this transaction.
     */
    BuildTransmitFrame(localNumberSnapshot);

    /*
     * Preload first byte before clock starts.
     */
    SPDR = transmitFrame[0];
}

/*
 * Called after every SPI byte transfer.
 */
ISR(SPI_STC_vect)
{
    uint8_t receivedByte;
    uint8_t expectedChecksum;

    /*
     * Read received byte.
     */
    receivedByte = SPDR;

    if (receiveIndex < FRAME_SIZE)
    {
        receiveFrame[receiveIndex] = receivedByte;
        receiveIndex++;
    }

    /*
     * More bytes remain.
     */
    if (receiveIndex < FRAME_SIZE)
    {
        SPDR = transmitFrame[receiveIndex];
    }
    else
    {
        /*
         * Four-byte frame received.
         */
        expectedChecksum = CalculateChecksum(
            receiveFrame[0],
            receiveFrame[1],
            receiveFrame[2]
        );

        if (
            (receiveFrame[0] == FRAME_HEADER) &&
            (receiveFrame[3] == expectedChecksum)
        )
        {
            numberFromATmega16 =
                ((uint16_t)receiveFrame[1] << 8) |
                (uint16_t)receiveFrame[2];
        }

        /*
         * Do not write SPDR here.
         * The next INT2 interrupt preloads byte zero.
         */
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
         * Copy received 16-bit number safely.
         */
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            receivedNumberCopy =
                numberFromATmega16;
        }

        /*
         * First line: local ATmega8A input.
         */
        LCD_Goto(0, 5);
        LCD_PrintNumber(inputNumber);

        /*
         * Second line: number received from ATmega16A.
         */
        LCD_Goto(1, 5);
        LCD_PrintNumber(receivedNumberCopy);

        _delay_ms(20);
    }

    return 0;
}