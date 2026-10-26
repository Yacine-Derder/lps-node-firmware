/*
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * LPS node firmware.
 *
 * Copyright 2016, Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Foobar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Foobar.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stm32f0xx_hal.h>
#include <stdio.h>

#include <stdbool.h>
#include <string.h>

#include "eeprom.h"

#include "FreeRTOS.h"
#include "task.h"

static I2C_HandleTypeDef * hi2c;
static int devAddr = 0xA0;

void eepromInit(I2C_HandleTypeDef * i2c)
{
  hi2c = i2c;
}

bool eepromTest()
{
  uint8_t test;

  return eepromRead(0, &test, 1);
}

static bool withinDevice(int address,const void *data,size_t length) {
  return address>=0&&(unsigned)address<=EEPROM_SIZE_BYTES&&
    length<=EEPROM_SIZE_BYTES-(unsigned)address&&(!length||data!=NULL);
}

bool eepromRead(int address,void *data,size_t length)
{
  if(!withinDevice(address,data,length))return false;
  if(!length)return true;
  return HAL_I2C_Mem_Read(hi2c,devAddr,(uint16_t)address,I2C_MEMADD_SIZE_16BIT,
                         data,(uint16_t)length,100)==HAL_OK;
}

bool eepromWrite(int address,void *data,size_t length)
{
  if(!withinDevice(address,data,length))return false;
  const uint8_t *bytes=data;
  while(length) {
    size_t chunk=EEPROM_PAGE_BYTES-(unsigned)address%EEPROM_PAGE_BYTES;
    if(chunk>length)chunk=length;
    // Stop on the first failed page: a later success cannot validate this image.
    if(HAL_I2C_Mem_Write(hi2c,devAddr,(uint16_t)address,I2C_MEMADD_SIZE_16BIT,
                        (uint8_t *)bytes,(uint16_t)chunk,100)!=HAL_OK)return false;
    vTaskDelay(10);
    address+=(int)chunk;bytes+=chunk;length-=chunk;
  }
  return true;
}
