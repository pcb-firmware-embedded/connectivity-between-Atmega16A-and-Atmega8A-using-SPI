# ATmega16A ↔ ATmega8A SPI Communication

This project demonstrates bidirectional SPI communication between an **ATmega16A** and an **ATmega8A** using AVR-GCC in **Atmel Studio 6**.

The ATmega16A operates as the **SPI master**, while the ATmega8A operates as the **SPI slave**. Each microcontroller has its own:

- 4×4 matrix keypad
- 16×2 LCD
- Local numeric input
- Remote numeric display

A number entered on either keypad can be confirmed and exchanged through SPI.

---

## System Overview

```text
ATmega16A                              ATmega8A
SPI Master                             SPI Slave

4×4 Keypad                             4×4 Keypad
16×2 LCD                               16×2 LCD

PB5 MOSI  ---------------------------> PB3 MOSI
PB6 MISO  <--------------------------- PB4 MISO
PB7 SCK   ---------------------------> PB5 SCK
PB4 SS    ---------------------------> PB2 SS
GND       ---------------------------- GND
```

The ATmega16A always starts the communication because only the SPI master generates the clock.

SPI is full duplex, so both microcontrollers exchange one byte during every clocked byte transfer.

---

## Hardware Requirements

- 1 × ATmega16A
- 1 × ATmega8A
- 2 × 16×2 HD44780-compatible LCD
- 2 × 4×4 matrix keypad
- 2 × 8 MHz crystal
- 4 × 22 pF crystal capacitors
- 2 × 10 kΩ LCD contrast potentiometers
- 100 nF decoupling capacitors
- 5 V regulated supply
- Proteus for simulation
- Atmel Studio 6 with AVR-GCC

---

## Clock Configuration

Both microcontrollers use an external 8 MHz crystal.

Each source file must contain:

```c
#define F_CPU 8000000UL
```

Recommended crystal wiring:

```text
XTAL1 ---- 8 MHz crystal ---- XTAL2
  |                              |
  22 pF                          22 pF
  |                              |
 GND                            GND
```

For Proteus, set the device clock frequency to:

```text
8000000 Hz
```

---

## SPI Connections

| Signal | ATmega16A Master | ATmega8A Slave | Direction |
|---|---|---|---|
| SS | PB4 | PB2 | Master → Slave |
| MOSI | PB5 | PB3 | Master → Slave |
| MISO | PB6 | PB4 | Slave → Master |
| SCK | PB7 | PB5 | Master → Slave |
| GND | GND | GND | Common reference |

The MISO line is required for sending the ATmega8A number back to the ATmega16A.

Both microcontrollers must share the same ground.

---

## ATmega16A LCD Connections

The ATmega16A LCD is connected in 4-bit mode.

| LCD Pin | ATmega16A |
|---|---|
| RS | PC0 |
| EN | PC1 |
| D4 | PC4 |
| D5 | PC5 |
| D6 | PC6 |
| D7 | PC7 |
| RW | GND |
| VSS | GND |
| VDD | +5 V |
| V0 | Contrast potentiometer |

---

## ATmega8A LCD Connections

| LCD Pin | ATmega8A |
|---|---|
| RS | PC0 |
| EN | PC1 |
| D4 | PC2 |
| D5 | PC3 |
| D6 | PC4 |
| D7 | PC5 |
| RW | GND |
| VSS | GND |
| VDD | +5 V |
| V0 | Contrast potentiometer |

---

## ATmega16A Keypad Connections

| Keypad Pin | ATmega16A |
|---|---|
| Row 1 | PA0 |
| Row 2 | PA1 |
| Row 3 | PA2 |
| Row 4 | PA3 |
| Column 1 | PA4 |
| Column 2 | PA5 |
| Column 3 | PA6 |
| Column 4 | PA7 |

Rows are configured as outputs. Columns are configured as inputs with internal pull-up resistors.

---

## ATmega8A Keypad Connections

| Keypad Pin | ATmega8A |
|---|---|
| Row 1 | PD0 |
| Row 2 | PD1 |
| Row 3 | PD2 |
| Row 4 | PD3 |
| Column 1 | PD4 |
| Column 2 | PD5 |
| Column 3 | PD6 |
| Column 4 | PD7 |

---

## Keypad Functions

| Key | Function |
|---|---|
| `0–9` | Enter digits |
| `#` | Confirm the number for transmission |
| `*` | Clear the entered number |
| `A` | Backspace |
| `B` | Increment |
| `C` | Decrement |
| `D` | Reset to zero |

The entered number can be limited to `0–9999`.

---

## LCD Display

### ATmega16A LCD

```text
M16: 123
M8 : 456
```

- First line: number entered locally on ATmega16A
- Second line: number received from ATmega8A

### ATmega8A LCD

```text
M8 : 456
M16: 123
```

- First line: number entered locally on ATmega8A
- Second line: number received from ATmega16A

---

## SPI Operating Mode

Both devices use:

```text
SPI Mode 0
CPOL = 0
CPHA = 0
MSB first
```

The ATmega16A can use a slow SPI clock during Proteus testing, for example:

```text
F_CPU / 128 = 62.5 kHz at 8 MHz
```

A slower SPI clock makes simulation and interrupt timing more reliable.

---

## Communication Behavior

The ATmega16A periodically starts an SPI transaction.

During each byte transfer:

```text
ATmega16A sends one byte through MOSI
ATmega8A sends one byte through MISO
```

The ATmega8A cannot initiate an SPI transfer by itself. It stores its confirmed number and waits for the ATmega16A to generate the clock.

Typical communication sequence:

```text
1. User enters a number on ATmega16A.
2. User presses #.
3. ATmega16A stores the confirmed number.
4. ATmega16A starts an SPI transaction.
5. ATmega8A receives the ATmega16A number.
6. ATmega8A returns its own confirmed number through MISO.
7. Both LCDs are updated.
```

---

## Recommended Packet Format

A simple four-byte packet can be used:

| Byte | Description |
|---|---|
| 0 | Header `0xA5` |
| 1 | Number high byte |
| 2 | Number low byte |
| 3 | XOR checksum |

Checksum:

```c
checksum = header ^ highByte ^ lowByte;
```

Number reconstruction:

```c
number = ((uint16_t)highByte << 8) | lowByte;
```

The master must keep `SS` low for the entire packet.

```text
SS LOW
Transfer byte 0
Transfer byte 1
Transfer byte 2
Transfer byte 3
SS HIGH
```

---

## Atmel Studio 6 Setup

Create two separate AVR-GCC projects.

### Project 1

```text
Device: ATmega16A
Role: SPI Master
```

### Project 2

```text
Device: ATmega8A
Role: SPI Slave
```

For each project:

1. Create a GCC C Executable Project.
2. Select the correct MCU.
3. Add the corresponding source code.
4. Build the project.
5. Load the generated HEX file into the correct Proteus MCU.
6. Set both MCU clock values to 8 MHz.

---

## Proteus Setup Checklist

- Load the ATmega16A HEX into U1.
- Load the ATmega8A HEX into U2.
- Set both clock frequencies to `8000000`.
- Connect MOSI to MOSI.
- Connect MISO to MISO.
- Connect SCK to SCK.
- Connect master SS to slave SS.
- Connect both grounds.
- Connect LCD RW pins to ground.
- Set LCD contrast using potentiometers.
- Confirm both keypads use the correct row and column order.

---

## Common Problems

### ATmega16A sends successfully, but cannot receive from ATmega8A

Check:

- ATmega8A PB4 is connected to ATmega16A PB6.
- ATmega8A PB4 is configured as an output.
- ATmega16A PB6 is configured as an input.
- Both devices share a common ground.
- The ATmega8A loads its response into `SPDR` before the next byte is clocked.
- The master waits long enough between bytes in Proteus.

---

### `SPDR Write collision`

This means software writes to `SPDR` while a transfer is still active.

Fix:

- On the master, wait for `SPIF` before writing the next byte.
- On the slave, update `SPDR` only after a transfer-complete event.
- Do not write to `SPDR` repeatedly from the main loop.
- Add a small inter-byte delay during Proteus simulation.

---

### LCD displays `ERR`

This means the received packet failed header or checksum validation.

Check:

- Packet byte alignment
- SPI mode
- MISO wiring
- SS timing
- Packet length
- Checksum calculation on both devices

For initial debugging, display the last valid received number instead of repeatedly printing `ERR`.

---

### Wrong keypad characters

The physical Proteus keypad layout may not match the software matrix.

Update:

```c
static const char keypadMap[4][4]
```

to match the actual row and column order used in the schematic.

---

### LCD is blank

Check:

- VSS to GND
- VDD to +5 V
- RW to GND
- Contrast potentiometer
- Correct LCD data-pin mapping
- Correct MCU clock
- Correct HEX file

---

## Recommended Debugging Procedure

Test the project in stages.

### Stage 1: SPI byte test

Use fixed values:

```text
ATmega16A sends 0x55
ATmega8A returns 0xAA
```

Confirm the ATmega16A receives `0xAA`.

### Stage 2: One-byte number exchange

Exchange values from `0–255`.

### Stage 3: Two-byte number exchange

Exchange `uint16_t` values.

### Stage 4: Add packet header and checksum

Validate communication reliability.

### Stage 5: Add LCDs and keypads

Integrate the user interface only after SPI communication works.

---

## Important Notes

- SPI slave communication is clocked by the master.
- The slave cannot send spontaneously.
- The ATmega16A must poll the ATmega8A.
- `SS` must remain low during a complete packet.
- MOSI and MISO must not be swapped.
- A shared ground is mandatory.
- Both devices must use the same SPI mode.
- Both source files must use the correct `F_CPU`.

---

## Expected Result

When the ATmega16A user enters `123` and presses `#`:

```text
ATmega8A LCD:
M8 : local value
M16: 123
```

When the ATmega8A user enters `456` and presses `#`:

```text
ATmega16A LCD:
M16: local value
M8 : 456
```

This confirms bidirectional number exchange between the ATmega16A and ATmega8A over SPI.
