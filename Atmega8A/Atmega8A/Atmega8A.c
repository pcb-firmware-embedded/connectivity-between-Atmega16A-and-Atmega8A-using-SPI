/*
 * ATmega8A SPI Slave
 * Bidirectional number exchange with ATmega16A
 * 4x4 keypad + LCD 16x2
 *
 * Atmel Studio 6 / AVR-GCC
 * Clock: 8 MHz
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

   LCD RS -> PC0
   LCD EN -> PC1
   LCD D4 -> PC2
   LCD D5 -> PC3
   LCD D6 -> PC4
   LCD D7 -> PC5
   LCD RW -> GND
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
   4x4 KEYPAD CONNECTIONS — PORTD

   Row 1 -> PD0
   Row 2 -> PD1
   Row 3 -> PD2
   Row 4 -> PD3

   Column 1 -> PD4
   Column 2 -> PD5
   Column 3 -> PD6
   Column 4 -> PD7
   ========================================================= */

#define KEYPAD_PORT PORTD
#define KEYPAD_DDR  DDRD
#define KEYPAD_PIN  PIND

/* =========================================================
   SPI FRAME FORMAT

   Byte 0: Header 0xA5
   Byte 1: Number high byte
   Byte 2: Number low byte
   Byte 3: XOR checksum
   ========================================================= */

#define FRAME_HEADER 0xA5
#define FRAME_SIZE   4

/*
 * Number entered and confirmed on ATmega8A.
 * This value is transmitted to ATmega16A.
 */
static volatile uint16_t confirmedNumber = 0;

/*
 * Number received from ATmega16A.
 */
static volatile uint16_t numberFromATmega16 = 0;

/*
 * SPI frame buffers.
 */
static volatile uint8_t receiveFrame[FRAME_SIZE];
static volatile uint8_t transmitFrame[FRAME_SIZE];

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

    /*
     * HD44780 4-bit initialization.
     */
    LCD_SendNibble(0x03);
    _delay_ms(5);

    LCD_SendNibble(0x03);
    _delay_us(150);

    LCD_SendNibble(0x03);
    LCD_SendNibble(0x02);

    LCD_Command(0x28); /* 4-bit, 2-line display */
    LCD_Command(0x0C); /* Display ON, cursor OFF */
    LCD_Command(0x06); /* Cursor moves right */
    LCD_Command(0x01); /* Clear display */
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
     * Clear unused positions so old digits disappear.
     * Four LCD positions are used for numbers 0–9999.
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
     * PD0–PD3: row outputs.
     * PD4–PD7: column inputs.
     */
    KEYPAD_DDR = 0x0F;

    /*
     * Rows initially HIGH.
     * Internal pull-ups enabled on columns.
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
         * Drive one selected row LOW.
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
             * Wait until key is released.
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
   NUMBER ENTRY FUNCTIONS
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
        /*
         * Clear entered number.
         */
        *inputNumber = 0;
        *digitCount = 0;
    }
    else if (key == 'A')
    {
        /*
         * Backspace.
         */
        *inputNumber /= 10U;

        if (*digitCount > 0)
            (*digitCount)--;
    }
    else if (key == 'B')
    {
        /*
         * Increment.
         */
        if (*inputNumber < 9999)
            (*inputNumber)++;
    }
    else if (key == 'C')
    {
        /*
         * Decrement.
         */
        if (*inputNumber > 0)
            (*inputNumber)--;
    }
    else if (key == 'D')
    {
        /*
         * Reset to zero.
         */
        *inputNumber = 0;
        *digitCount = 0;
    }
    else if (key == '#')
    {
        /*
         * Confirm the number for transmission.
         *
         * confirmedNumber is 16-bit and is also read by
         * the SPI ISR, so update it atomically.
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
     * ATmega8A hardware SPI pins:
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
     * Enable pull-up on SS.
     */
    PORTB |= (1 << PB2);

    spiIndex = 0;

    /*
     * Prepare the initial outgoing frame.
     */
    BuildTransmitFrame(0);

    /*
     * Load the first outgoing byte before the first
     * master transaction.
     */
    SPDR = transmitFrame[0];

    /*
     * Enable SPI and SPI transfer-complete interrupt.
     *
     * MSTR remains zero: slave mode.
     * CPOL = 0 and CPHA = 0: SPI Mode 0.
     */
    SPCR =
        (1 << SPE) |
        (1 << SPIE);

    sei();
}

/* =========================================================
   SPI TRANSFER-COMPLETE INTERRUPT

   The master must always send exactly four bytes while
   SS remains LOW.

   The slave:
   - receives one master byte,
   - stores it,
   - loads its next response byte into SPDR.
   ========================================================= */

ISR(SPI_STC_vect)
{
    uint8_t receivedByte;
    uint8_t expectedChecksum;
    uint16_t localNumberCopy;

    /*
     * Reading SPDR retrieves the byte sent by ATmega16A.
     */
    receivedByte = SPDR;

    receiveFrame[spiIndex] = receivedByte;

    spiIndex++;

    if (spiIndex >= FRAME_SIZE)
    {
        /*
         * A complete four-byte frame has been received.
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
         * Prepare the latest confirmed ATmega8A number
         * for the next complete transaction.
         */
        localNumberCopy = confirmedNumber;
        BuildTransmitFrame(localNumberCopy);

        /*
         * Restart the frame index.
         */
        spiIndex = 0;

        /*
         * Preload the first byte for the next transaction.
         */
        SPDR = transmitFrame[0];
    }
    else
    {
        /*
         * Preload the next response byte for the current
         * transaction.
         */
        SPDR = transmitFrame[spiIndex];
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

    /*
     * Fixed LCD labels.
     */
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
         * numberFromATmega16 is updated inside the ISR.
         * Copy it atomically because it is 16-bit.
         */
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            receivedNumberCopy = numberFromATmega16;
        }

        /*
         * Display the local ATmega8A number.
         */
        LCD_Goto(0, 5);
        LCD_PrintNumber(inputNumber);

        /*
         * Display the number received from ATmega16A.
         */
        LCD_Goto(1, 5);
        LCD_PrintNumber(receivedNumberCopy);

        _delay_ms(20);
    }

    return 0;
}