/*
* IncFile1.h
*
* Created: 8/4/2026 4:57:29 PM
*  Author: Administrator
*/


#ifndef INCFILE1_H_
#define INCFILE1_H_


#include "MAIN.h"
/* =========================================================
4x4 KEYPAD: ATmega16A PORTA
Rows: PA0-PA3
Columns: PA4-PA7
========================================================= */

static void Keypad_Init(void);
static char Keypad_ScanRaw(void);
static char Keypad_GetKey(void);

static void ProcessKey(
char key,
uint16_t *inputNumber,
uint16_t *sendNumber,
uint8_t *digitCount
);





#endif /* INCFILE1_H_ */