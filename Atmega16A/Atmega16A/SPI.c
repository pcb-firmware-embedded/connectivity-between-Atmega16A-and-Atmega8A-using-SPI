/*
 * SPI.c
 *
 * Created: 8/4/2026 5:00:04 PM
 *  Author: Administrator
 */ 
#include "SPI.h"


/* =========================================================
   SPI MASTER FUNCTIONS
   ========================================================= */

static void SPI_MasterInit(void)
{
    /*
     * SS, MOSI and SCK outputs.
     * MISO input.
     */
    DDRB |=
        (1 << SPI_SS) |
        (1 << SPI_MOSI) |
        (1 << SPI_SCK);

    DDRB &= ~(1 << SPI_MISO);

    /*
     * Slave initially disabled.
     */
    PORTB |= (1 << SPI_SS);

    /*
     * SPI enabled
     * Master mode
     * Mode 0
     * Clock = F_CPU / 16
     */
    SPCR =
        (1 << SPE) |
        (1 << MSTR) |
        (1 << SPR0);

    SPSR &= ~(1 << SPI2X);
}

static uint8_t SPI_TransferByte(uint8_t data)
{
    SPDR = data;

    while (!(SPSR & (1 << SPIF)))
    {
        /* Wait */
    }

    return SPDR;
}

static uint8_t CalculateChecksum(
    uint8_t header,
    uint8_t highByte,
    uint8_t lowByte
)
{
    return header ^ highByte ^ lowByte;
}

/*
 * Sends the ATmega16A confirmed number.
 * At the same time, receives the ATmega8A number.
 */
static uint8_t SPI_ExchangeNumber(
    uint16_t localNumber,
    uint16_t *remoteNumber
)
{
    uint8_t transmitFrame[4];
    uint8_t receiveFrame[4];
    uint8_t i;

    transmitFrame[0] = FRAME_HEADER;
    transmitFrame[1] = (uint8_t)(localNumber >> 8);
    transmitFrame[2] = (uint8_t)(localNumber & 0xFF);

    transmitFrame[3] = CalculateChecksum(
        transmitFrame[0],
        transmitFrame[1],
        transmitFrame[2]
    );

    PORTB &= ~(1 << SPI_SS);
    _delay_us(2);

    for (i = 0; i < 4; i++)
    {
        receiveFrame[i] = SPI_TransferByte(transmitFrame[i]);
    }

    _delay_us(2);
    PORTB |= (1 << SPI_SS);

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
        *remoteNumber =
            ((uint16_t)receiveFrame[1] << 8) |
            receiveFrame[2];

        return 1;
    }

    return 0;
}