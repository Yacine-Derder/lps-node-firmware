/* Actual legacy EEPROM TLV + additive role migration + console parser. */
#include "unity.h"
#include "cfg.h"
#include "mul_common.h"
#include "mul_console.h"
#include <string.h>
#include <stddef.h>
static uint8_t memory[100];static unsigned writes;static int budget;static bool badRead;
bool eepromRead(int address,void *data,size_t n) {if(badRead)return false;if(address<0||(size_t)address+n>sizeof(memory))return false;memcpy(data,memory+address,n);return true;}
bool eepromWrite(int address,void *data,size_t n) {writes++;if(budget==0)return false;if(budget>0)budget--;if(address<0||(size_t)address+n>sizeof(memory))return false;memcpy(memory+address,data,n);return true;}
void HAL_Delay(uint32_t ms) {(void)ms;}
static void checksum(void) {unsigned size=memory[3]|memory[4]<<8;uint8_t sum=0;for(unsigned i=0;i<5+size;i++)sum+=memory[i];memory[5+size]=sum;}
static void legacy(uint8_t address) {
 memset(memory,0,sizeof(memory));uint8_t record[]={0xbc,1,0,15,0,cfgAddress,1,address,cfgMode,1,5,cfgTxPower,4,0x12,0x34,0x56,0x78,cfgSmartPower,1,1};
 memcpy(memory,record,sizeof(record));checksum();cfgInit();writes=0;
}
void setUp(void){budget=-1;badRead=false;legacy(0x84);}
void tearDown(void){}
static void binding(uint8_t node,uint8_t role,uint8_t ref,uint8_t fol) {
 uint8_t actualRole=99,a=0,b=0;TEST_ASSERT_TRUE(cfgMulRead(node,&actualRole,&a,&b));TEST_ASSERT_EQUAL(role,actualRole);TEST_ASSERT_EQUAL(ref,a);TEST_ASSERT_EQUAL(fol,b);
 uint32_t power=0;TEST_ASSERT_TRUE(cfgReadU32(cfgTxPower,&power));TEST_ASSERT_EQUAL_HEX32(0x78563412,power);
}
void test_legacyRoleMigratesOnlyOnEditAndSurvivesAddressAndReboot(void) {
 binding(0x84,0,0x84,0x85);TEST_ASSERT_EQUAL(0,writes);
 TEST_ASSERT_TRUE(cfgMulChangeAddress(0x42));TEST_ASSERT_EQUAL(2,writes);cfgInit();binding(0x42,0,0x42,0x85);
 uint8_t node;TEST_ASSERT_TRUE(cfgReadU8(cfgAddress,&node));TEST_ASSERT_EQUAL(0x42,node);
 TEST_ASSERT_TRUE(cfgMulWrite(1,0x23,0x42));cfgInit();binding(0x42,1,0x23,0x42);
 unsigned before=writes;TEST_ASSERT_TRUE(cfgMulWrite(1,0x23,0x42));TEST_ASSERT_EQUAL(before,writes);
 legacy(0x85);TEST_ASSERT_TRUE(cfgMulChangeAddress(0x41));cfgInit();binding(0x41,1,0x84,0x41);
}
void test_selfBroadcastMalformedAndWriteFailuresDoNotReportSuccess(void) {
 TEST_ASSERT_FALSE(cfgMulChangeAddress(0x85));TEST_ASSERT_FALSE(cfgMulWrite(1,0x84,0x85));TEST_ASSERT_FALSE(cfgMulWrite(255,0x84,0x85));TEST_ASSERT_FALSE(cfgMulWrite(0,0x84,255));TEST_ASSERT_EQUAL(0,writes);
 budget=0;TEST_ASSERT_FALSE(cfgMulChangeAddress(0x42));
 uint8_t role,ref,fol;TEST_ASSERT_FALSE(cfgMulRead(0x84,&role,&ref,&fol));cfgInit();binding(0x84,0,0x84,0x85);
 budget=1;TEST_ASSERT_FALSE(cfgMulChangeAddress(0x42));cfgInit();binding(0x84,0,0x84,0x85);
 badRead=true;TEST_ASSERT_FALSE(cfgMulWrite(0,0x84,0x23));badRead=false;
}
void test_capacityAndMalformedNewFieldAreRejectedWithoutOverwritingLegacy(void) {
 unsigned at=5+memory[3];memory[at]=200;memory[at+1]=74;memory[3]+=76;checksum();cfgInit();writes=0;
 TEST_ASSERT_FALSE(cfgMulWrite(0,0x84,0x23));TEST_ASSERT_EQUAL(0,writes);binding(0x84,0,0x84,0x85);
 legacy(0x84);at=5+memory[3];memory[at]=cfgMulNetwork;memory[at+1]=2;memory[at+2]=0;memory[at+3]=0x84;memory[3]+=4;checksum();cfgInit();
 uint8_t role,ref,fol;TEST_ASSERT_FALSE(cfgMulRead(0x84,&role,&ref,&fol));
}
static void send(const char *text) {for(;*text;text++)TEST_ASSERT_TRUE(mulConsoleByte(*text));}
void test_fragmentedPrefixedConsoleCannotLeakIntoLegacyOrMutateOnOverflow(void) {
 TEST_ASSERT_FALSE(mulConsoleByte('a'));send(":mul show\n");TEST_ASSERT_EQUAL(0,writes);
 send(":MUL ADDRESS 0x42\r\n");cfgInit();binding(0x42,0,0x42,0x85);
 send(":mul setup follower 0x23 0x42\n");cfgInit();binding(0x42,1,0x23,0x42);
 unsigned before=writes;send(":mul address 0xGG\n");TEST_ASSERT_EQUAL(before,writes);
 send(":");for(unsigned i=0;i<100;i++)TEST_ASSERT_TRUE(mulConsoleByte('a'));send("\n");TEST_ASSERT_EQUAL(before,writes);
 TEST_ASSERT_FALSE(mulConsoleByte('h'));
}

void test_tornExistingConfigurationIsRetainedAndRadioIdlesWithoutDefaultErase(void) {
 memory[20]^=1;uint8_t damaged[100];memcpy(damaged,memory,sizeof(memory));writes=0;
 cfgInit();TEST_ASSERT_EQUAL(0,writes);TEST_ASSERT_EQUAL_MEMORY(damaged,memory,sizeof(memory));
 uint8_t mode=0;TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(255,mode);
 uint8_t role,reference,follower;TEST_ASSERT_FALSE(cfgMulRead(0,&role,&reference,&follower));
 TEST_ASSERT_FALSE(cfgMulChangeAddress(0x42));TEST_ASSERT_FALSE(cfgWriteU8(cfgMode,5));
 TEST_ASSERT_FALSE(cfgWriteU32(cfgTxPower,0));TEST_ASSERT_EQUAL(0,writes);
}

void test_unreadableExistingConfigurationSelectsIdleWithoutWriting(void) {
 badRead=true;unsigned before=writes;cfgInit();TEST_ASSERT_EQUAL(before,writes);
 uint8_t mode=0;TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(255,mode);
}

void test_checksumValidTruncatedTlvCannotEscapeEepromImage(void) {
 memory[18]=255;checksum();uint8_t damaged[100];memcpy(damaged,memory,sizeof(memory));writes=0;
 cfgInit();TEST_ASSERT_EQUAL(0,writes);TEST_ASSERT_EQUAL_MEMORY(damaged,memory,sizeof(memory));
 uint8_t mode=0;TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(255,mode);
 uint8_t role,reference,follower;TEST_ASSERT_FALSE(cfgMulRead(0,&role,&reference,&follower));
}
