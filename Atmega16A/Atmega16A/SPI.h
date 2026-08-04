/*
* SPI.h
*
* Created: 8/4/2026 5:00:16 PM
*  Author: Administrator
*/


#ifndef SPI_H_
#define SPI_H_

#include "MAIN.h"
/* =========================================================
SPI: ATmega16A
PB4 = SS
PB5 = MOSI
PB6 = MISO
PB7 = SCK
========================================================= */

static void SPI_MasterInit(void);
static uint8_t SPI_TransferByte(uint8_t data);
static uint8_t CalculateChecksum(
uint8_t header,
uint8_t highByte,
uint8_t lowByte
);

static uint8_t SPI_ExchangeNumber(
uint16_t localNumber,
uint16_t *remoteNumber
)


#endif /* SPI_H_ */