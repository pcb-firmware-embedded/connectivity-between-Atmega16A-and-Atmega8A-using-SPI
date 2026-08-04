/*
* keyboard.c
*
* Created: 8/4/2026 4:57:53 PM
*  Author: Administrator
*/

#include "keyboard.h"


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
	* PA0-PA3: row outputs
	* PA4-PA7: column inputs
	*/
	KEYPAD_DDR = 0x0F;

	/*
	* Rows initially HIGH.
	* Enable pull-ups on columns.
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
		* All rows HIGH, then selected row LOW.
		*/
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
			/*
			* Wait until the key is released.
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
NUMBER ENTRY
========================================================= */

static void ProcessKey(
char key,
uint16_t *inputNumber,
uint16_t *sendNumber,
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
		* Confirm number for transmission.
		*/
		*sendNumber = *inputNumber;
	}
}

