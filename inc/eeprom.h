#ifndef __EEPROM_H__
#define __EEPROM_H__

#include <stm32f0xx_hal.h>

#include <stdbool.h>
/* Bitcraze Loco Node Rev-D U6:24AA64FT-E/OT,8KiB with32-byte pages.
 * The legacy configuration region remains100 bytes; this is a driver bound. */
#define EEPROM_SIZE_BYTES 8192u
#define EEPROM_PAGE_BYTES 32u

void eepromInit(I2C_HandleTypeDef * i2c);

bool eepromRead(int address, void* data, size_t length);

bool eepromWrite(int address, void* data, size_t length);

bool eepromTest();

#endif
