/*
 * MAIN.h
 *
 * Created: 8/4/2026 4:55:51 PM
 *  Author: Administrator
 */ 


#ifndef MAIN_H_
#define MAIN_H_



#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdlib.h>

#include "keyboard.h"
#include "LCD1602.h"
#include "SPI.h"


#define KEYPAD_PORT PORTA
#define KEYPAD_DDR  DDRA
#define KEYPAD_PIN  PINA


#define LCD_PORT PORTC
#define LCD_DDR  DDRC

#define LCD_RS PC0
#define LCD_EN PC1
#define LCD_D4 PC4
#define LCD_D5 PC5
#define LCD_D6 PC6
#define LCD_D7 PC7


#define SPI_SS   PB4
#define SPI_MOSI PB5
#define SPI_MISO PB6
#define SPI_SCK  PB7

#define FRAME_HEADER 0xA5

#endif /* MAIN_H_ */