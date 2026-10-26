#include "unity.h"
#include "eeprom.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <limits.h>
static I2C_HandleTypeDef bus;
static uint8_t memory[EEPROM_SIZE_BYTES];
static unsigned calls,waits,readCalls,failAt;
static uint16_t addresses[8],sizes[8];
void setUp(void){memset(memory,0xff,sizeof(memory));calls=waits=readCalls=failAt=0;eepromInit(&bus);}
void tearDown(void){}
void vTaskDelay(TickType_t ticks){TEST_ASSERT_EQUAL(10,ticks);waits++;}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *b,uint16_t device,uint16_t at,uint16_t mode,uint8_t *data,uint16_t n,uint32_t timeout) {
 TEST_ASSERT_EQUAL_PTR(&bus,b);TEST_ASSERT_EQUAL_HEX16(0xa0,device);
 TEST_ASSERT_EQUAL(I2C_MEMADD_SIZE_16BIT,mode);TEST_ASSERT_EQUAL(100,timeout);
 TEST_ASSERT_TRUE(n>0&&n<=EEPROM_PAGE_BYTES&&at/EEPROM_PAGE_BYTES==(at+n-1)/EEPROM_PAGE_BYTES);
 TEST_ASSERT_TRUE((unsigned)at+n<=sizeof(memory));
 TEST_ASSERT_TRUE(calls<8);addresses[calls]=at;sizes[calls]=n;calls++;
 if(failAt==calls)return HAL_ERROR;
 memcpy(memory+at,data,n);return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *b,uint16_t device,uint16_t at,uint16_t mode,uint8_t *data,uint16_t n,uint32_t timeout) {
 TEST_ASSERT_EQUAL_PTR(&bus,b);TEST_ASSERT_EQUAL_HEX16(0xa0,device);
 TEST_ASSERT_EQUAL(I2C_MEMADD_SIZE_16BIT,mode);TEST_ASSERT_EQUAL(100,timeout);
 TEST_ASSERT_TRUE((unsigned)at+n<=sizeof(memory));readCalls++;
 if(failAt)return HAL_ERROR;
 memcpy(data,memory+at,n);return HAL_OK;
}
void test_eepromWritesSplitAtActual32BytePageBoundaries(void) {
 uint8_t data[66];for(unsigned i=0;i<sizeof(data);i++)data[i]=i;
 TEST_ASSERT_TRUE(eepromWrite(31,data,sizeof(data)));TEST_ASSERT_EQUAL(4,calls);TEST_ASSERT_EQUAL(4,waits);
 const uint16_t at[]={31,32,64,96},n[]={1,32,32,1};
 TEST_ASSERT_EQUAL_MEMORY(at,addresses,sizeof(at));TEST_ASSERT_EQUAL_MEMORY(n,sizes,sizeof(n));
 TEST_ASSERT_EQUAL_MEMORY(data,memory+31,sizeof(data));TEST_ASSERT_EQUAL(255,memory[30]);TEST_ASSERT_EQUAL(255,memory[97]);
}
void test_eepromFirstFailedPageCannotBeHiddenByLaterSuccess(void) {
 uint8_t data[100];memset(data,0x55,sizeof(data));
 for(unsigned failed=1;failed<=4;failed++) {
  calls=waits=0;failAt=failed;memset(memory,0xff,sizeof(memory));
  TEST_ASSERT_FALSE(eepromWrite(0,data,sizeof(data)));
  TEST_ASSERT_EQUAL(failed,calls);TEST_ASSERT_EQUAL(failed-1,waits);
  TEST_ASSERT_EQUAL(255,memory[(failed-1)*32]);
 }
}
void test_eepromRejectsOutOfBoundsAndNullBeforeAnyBusAccess(void) {
 uint8_t data[8]={0};
 TEST_ASSERT_FALSE(eepromWrite(-1,data,1));TEST_ASSERT_FALSE(eepromRead(-1,data,1));
 TEST_ASSERT_FALSE(eepromWrite(8191,data,2));TEST_ASSERT_FALSE(eepromRead(8191,data,2));
 TEST_ASSERT_FALSE(eepromWrite(INT_MAX,data,1));TEST_ASSERT_FALSE(eepromRead(8192,data,SIZE_MAX));
 TEST_ASSERT_FALSE(eepromWrite(0,NULL,1));TEST_ASSERT_FALSE(eepromRead(0,NULL,1));
 TEST_ASSERT_EQUAL(0,calls);TEST_ASSERT_EQUAL(0,readCalls);
 TEST_ASSERT_TRUE(eepromWrite(8192,NULL,0));TEST_ASSERT_TRUE(eepromRead(8192,NULL,0));
 TEST_ASSERT_EQUAL(0,calls);TEST_ASSERT_EQUAL(0,readCalls);
}
void test_eepromReadsAndLastDeviceByteAreBoundedAndFailuresReported(void) {
 uint8_t value=42,result=0;
 TEST_ASSERT_TRUE(eepromWrite(8191,&value,1));TEST_ASSERT_TRUE(eepromRead(8191,&result,1));TEST_ASSERT_EQUAL(42,result);
 failAt=1;TEST_ASSERT_FALSE(eepromRead(0,&result,1));TEST_ASSERT_FALSE(eepromTest());
}
