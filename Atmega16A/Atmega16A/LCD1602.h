/*
 * LCD1602.h
 *
 * Created: 8/4/2026 4:55:06 PM
 *  Author: Administrator
 */ 


#ifndef LCD1602_H_
#define LCD1602_H_


#include "MAIN.h"
/* =========================================================
   LCD: ATmega16A PORTC
   ========================================================= */


static void LCD_EnablePulse(void);
static void LCD_SendNibble(uint8_t nibble);

static void LCD_SendByte(uint8_t value, uint8_t dataMode);
static void LCD_Command(uint8_t command);
static void LCD_Character(char character);
static void LCD_Init(void);
static void LCD_Goto(uint8_t row, uint8_t column);
static void LCD_Print(const char *text);
static void LCD_PrintNumber(uint16_t number);






#endif /* LCD1602_H_ */