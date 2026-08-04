/*
 * ATmega16A SPI Master
 * Bidirectional communication with ATmega8A
 * 4x4 keypad + LCD 16x2
 * Atmel Studio 6 / AVR-GCC
 */

#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdlib.h>

/* =========================================================
   LCD CONNECTIONS — ATmega16A PORTC

   RS -> PC0
   EN -> PC1
   D4 -> PC4
   D5 -> PC5
   D6 -> PC6
   D7 -> PC7
   RW -> GND
   ========================================================= */

#define LCD_PORT PORTC
#define LCD_DDR  DDRC

#define LCD_RS PC0
#define LCD_EN PC1
#define LCD_D4 PC4
#define LCD_D5 PC5
#define LCD_D6 PC6
#define LCD_D7 PC7

/* =========================================================
   4x4 KEYPAD — ATmega16A PORTA

   R1 -> PA0
   R2 -> PA1
   R3 -> PA2
   R4 -> PA3

   C1 -> PA4
   C2 -> PA5
   C3 -> PA6
   C4 -> PA7
   ========================================================= */

#define KEYPAD_PORT PORTA
#define KEYPAD_DDR  DDRA
#define KEYPAD_PIN  PINA

/* =========================================================
   SPI — ATmega16A

   PB4 -> SS
   PB5 -> MOSI
   PB6 -> MISO
   PB7 -> SCK
   ========================================================= */

#define SPI_SS   PB4
#define SPI_MOSI PB5
#define SPI_MISO PB6
#define SPI_SCK  PB7

#define FRAME_HEADER 0xA5
#define FRAME_SIZE   4

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
    while (*text != '\0')
    {
        LCD_Character(*text);
        text++;
    }
}

static void LCD_PrintNumber(uint16_t number)
{
    char buffer[6];

    /*
     * Maximum value in this project is 9999.
     */
    itoa((int)number, buffer, 10);
    LCD_Print(buffer);

    /*
     * Clear the remaining positions so old digits disappear.
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
     * PA0–PA3: output rows.
     * PA4–PA7: input columns with pull-ups.
     */
    KEYPAD_DDR = 0x0F;
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
                return keypadMap[row][column];
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
                _delay_ms(5);

            return key;
        }
    }

    return 0;
}

/* =========================================================
   NUMBER ENTRY
   ========================================================= */

static void ProcessKey(
    char key,
    uint16_t *inputNumber,
    uint16_t *confirmedNumber,
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
         * Confirm the number that will be transmitted.
         */
        *confirmedNumber = *inputNumber;
    }
}

/* =========================================================
   SPI MASTER FUNCTIONS
   ========================================================= */

static uint8_t CalculateChecksum(
    uint8_t header,
    uint8_t highByte,
    uint8_t lowByte
)
{
    return header ^ highByte ^ lowByte;
}

static void BuildFrame(uint16_t number, uint8_t *frame)
{
    frame[0] = FRAME_HEADER;
    frame[1] = (uint8_t)(number >> 8);
    frame[2] = (uint8_t)(number & 0xFF);

    frame[3] = CalculateChecksum(
        frame[0],
        frame[1],
        frame[2]
    );
}

static void SPI_MasterInit(void)
{
    /*
     * SS, MOSI and SCK outputs.
     */
    DDRB |=
        (1 << SPI_SS) |
        (1 << SPI_MOSI) |
        (1 << SPI_SCK);

    /*
     * MISO input.
     */
    DDRB &= ~(1 << SPI_MISO);

    /*
     * Keep slave deselected initially.
     */
    PORTB |= (1 << SPI_SS);

    /*
     * SPI enabled.
     * Master mode.
     * Mode 0: CPOL = 0, CPHA = 0.
     * Clock = F_CPU / 128.
     *
     * At 8 MHz:
     * SPI clock = 62.5 kHz.
     *
     * This slower speed is reliable in Proteus.
     */
    SPCR =
        (1 << SPE) |
        (1 << MSTR) |
        (1 << SPR1) |
        (1 << SPR0);

    SPSR &= ~(1 << SPI2X);
}

static uint8_t SPI_TransferByte(uint8_t transmitByte)
{
    SPDR = transmitByte;

    while (!(SPSR & (1 << SPIF)))
    {
        /*
         * Wait for transfer completion.
         */
    }

    return SPDR;
}

/*
 * Sends the ATmega16A number and simultaneously receives
 * the ATmega8A number.
 *
 * Returns:
 * 1 = valid frame received
 * 0 = invalid frame
 */
static uint8_t SPI_ExchangeNumbers(
    uint16_t masterNumber,
    uint16_t *slaveNumber
)
{
    uint8_t txFrame[FRAME_SIZE];
    uint8_t rxFrame[FRAME_SIZE];

    uint8_t expectedChecksum;
    uint8_t i;

    BuildFrame(masterNumber, txFrame);

    /*
     * Start a new SPI transaction.
     *
     * The falling SS edge causes INT2 on the ATmega8A.
     */
    PORTB &= ~(1 << SPI_SS);

    /*
     * Allow the ATmega8A INT2 ISR to reset its indexes
     * and preload the first response byte.
     */
    _delay_us(100);

    for (i = 0; i < FRAME_SIZE; i++)
    {
        rxFrame[i] =
            SPI_TransferByte(txFrame[i]);

        /*
         * Allow the ATmega8A SPI ISR to preload the next byte.
         */
        _delay_us(50);
    }

    /*
     * End the transaction.
     */
    PORTB |= (1 << SPI_SS);

    _delay_us(100);

    expectedChecksum = CalculateChecksum(
        rxFrame[0],
        rxFrame[1],
        rxFrame[2]
    );

    if (
        (rxFrame[0] == FRAME_HEADER) &&
        (rxFrame[3] == expectedChecksum)
    )
    {
        *slaveNumber =
            ((uint16_t)rxFrame[1] << 8) |
            (uint16_t)rxFrame[2];

        return 1;
    }

    return 0;
}

/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    char key;

    uint16_t inputNumber = 0;
    uint16_t confirmedNumber = 0;
    uint16_t numberFromATmega8 = 0;

    uint8_t digitCount = 0;

    LCD_Init();
    Keypad_Init();
    SPI_MasterInit();

    LCD_Goto(0, 0);
    LCD_Print("M16:");

    LCD_Goto(1, 0);
    LCD_Print("M8 :");

    while (1)
    {
        key = Keypad_GetKey();

        if (key != 0)
        {
            ProcessKey(
                key,
                &inputNumber,
                &confirmedNumber,
                &digitCount
            );
        }

        /*
         * The master continuously generates SPI transactions.
         *
         * If a frame is invalid, numberFromATmega8 keeps the
         * last successfully received value.
         */
        SPI_ExchangeNumbers(
            confirmedNumber,
            &numberFromATmega8
        );

        LCD_Goto(0, 5);
        LCD_PrintNumber(inputNumber);

        LCD_Goto(1, 5);
        LCD_PrintNumber(numberFromATmega8);

        _delay_ms(50);
    }

    return 0;
}