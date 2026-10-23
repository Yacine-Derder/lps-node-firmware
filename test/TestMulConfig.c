/* Actual legacy EEPROM TLV + additive role migration + console parser. */
#include "unity.h"
#include "cfg.h"
#include "mul_common.h"
#include "mul_console.h"
#include "uwb_control.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>
static uwbConfig_t boot;
static unsigned critical;
void vPortEnterCritical(void){TEST_ASSERT_EQUAL(0,critical);critical++;}
void vPortExitCritical(void){TEST_ASSERT_EQUAL(1,critical);critical--;}
static unsigned stops,clears,inits,model;static bool owner,txReady=true;
static char output[4096];
uwbConfig_t *uwbGetConfig(void){return &boot;}
bool uwbSetEnabled(bool enabled){return uwbControlRequest(enabled);}
void dwIdle(dwDevice_t *dev){(void)dev;TEST_ASSERT_TRUE(owner);stops++;}
void dwClearAllStatus(dwDevice_t *dev){(void)dev;TEST_ASSERT_TRUE(owner);clears++;}
bool usbcommTryWrite(const char *data,unsigned len){TEST_ASSERT_TRUE(len<=48);if(!txReady)return false;TEST_ASSERT_TRUE(strlen(output)+len<sizeof(output));strncat(output,data,len);return true;}
static uint8_t memory[100];static unsigned writes;static int budget;static bool badRead,reenter;
bool eepromRead(int address,void *data,size_t n) {if(badRead)return false;if(address<0||(size_t)address+n>sizeof(memory))return false;memcpy(data,memory+address,n);return true;}
bool eepromWrite(int address,void *data,size_t n) {writes++;TEST_ASSERT_EQUAL(0,critical);if(reenter){reenter=false;TEST_ASSERT_FALSE(cfgWriteU8(cfgMode,0));TEST_ASSERT_FALSE(cfgStorageReady());TEST_ASSERT_TRUE(cfgPendingRestart());}if(budget==0)return false;if(budget>0)budget--;if(address<0||(size_t)address+n>sizeof(memory))return false;memcpy(memory+address,data,n);return true;}
void HAL_Delay(uint32_t ms) {(void)ms;}
static void checksum(void) {unsigned size=memory[3]|memory[4]<<8;uint8_t sum=0;for(unsigned i=0;i<5+size;i++)sum+=memory[i];memory[5+size]=sum;}
static void legacy(uint8_t address) {
 memset(memory,0,sizeof(memory));uint8_t record[]={0xbc,1,0,15,0,cfgAddress,1,address,cfgMode,1,5,cfgTxPower,4,0x12,0x34,0x56,0x78,cfgSmartPower,1,1};
 memcpy(memory,record,sizeof(record));checksum();cfgInit();writes=0;boot.mode=5;boot.address[0]=address;uwbControlInit(&boot);mulConsoleReset();
}
void setUp(void){budget=-1;badRead=reenter=false;stops=clears=inits=model=0;owner=false;txReady=true;output[0]=0;legacy(0x84);}
void tearDown(void){TEST_ASSERT_EQUAL(0,critical);}
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
 unsigned at=5+memory[3];memory[at]=200;memory[at+1]=74;memory[3]+=76;checksum();cfgInit();writes=0;boot.mode=5;boot.address[0]=0x84;uwbControlInit(&boot);mulConsoleReset();
 TEST_ASSERT_FALSE(cfgMulWrite(0,0x84,0x23));TEST_ASSERT_EQUAL(0,writes);binding(0x84,0,0x84,0x85);
 legacy(0x84);at=5+memory[3];memory[at]=cfgMulNetwork;memory[at+1]=2;memory[at+2]=0;memory[at+3]=0x84;memory[3]+=4;checksum();cfgInit();
 uint8_t role,ref,fol;TEST_ASSERT_FALSE(cfgMulRead(0x84,&role,&ref,&fol));
}
static void send(const char *text) {for(;*text;text++){TEST_ASSERT_TRUE(mulConsoleByte(*text));while(!mulConsolePoll()){};}}
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

static void initialize(uwbConfig_t *c,dwDevice_t *dev) {
 (void)dev;TEST_ASSERT_TRUE(owner);TEST_ASSERT_TRUE(c==&boot);inits++;model=0;
}
static void apply(void) {
 uwbAlgorithm_t algorithm={.init=initialize};owner=true;uwbControlApply(NULL,&algorithm);owner=false;
}
void test_rfLifecycleIsOwnerOnlyAndFailedOffNeverRearms(void) {
 TEST_ASSERT_TRUE(uwbControlRequested());TEST_ASSERT_FALSE(uwbControlRunning());
 TEST_ASSERT_EQUAL(0,stops);apply();TEST_ASSERT_EQUAL(1,inits);TEST_ASSERT_TRUE(uwbControlRunning());
 TEST_ASSERT_TRUE(uwbControlEvent(eventPacketSent));TEST_ASSERT_EQUAL(1,uwbControlCount(eventPacketSent));
 budget=0;send(":off\n");TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_TRUE(uwbControlRunning());
 TEST_ASSERT_EQUAL(1,stops); // Console has done NO SPI, including on failure.
 TEST_ASSERT_FALSE(uwbControlEvent(eventPacketSent));apply();TEST_ASSERT_FALSE(uwbControlRunning());
 TEST_ASSERT_EQUAL(1,inits);unsigned before=writes;send(":on\n");apply();
 TEST_ASSERT_EQUAL(before,writes);TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_EQUAL(1,inits);
 TEST_ASSERT_NOT_NULL(strstr(output,"EEPROM unconfirmed"));
 budget=-1;cfgInit();uwbControlInit(&boot);apply(); // Old durable enabled value, explicitly rebooted.
 send(":off\n");apply();TEST_ASSERT_FALSE(uwbControlRunning());
 before=writes;send(":off\n:save\n");TEST_ASSERT_EQUAL(before,writes);
 cfgInit();uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlRequested());
 unsigned beforeStops=stops;send(":on\n");TEST_ASSERT_EQUAL(beforeStops,stops);apply();TEST_ASSERT_TRUE(uwbControlRunning());
 model=123;send(":off\n");apply();send(":on\n");apply();TEST_ASSERT_EQUAL(0,model);
 TEST_ASSERT_EQUAL(stops,clears);
}
void test_pendingRestartAndPassiveRoleCannotEnableOldTransmitter(void) {
 apply();send(":mul setup disabled 0x23 0x85\n");TEST_ASSERT_TRUE(cfgPendingRestart());
 send(":role rlr\n");TEST_ASSERT_FALSE(uwbControlRequested());apply();
 uint8_t mode;TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(6,mode);
 binding(0x84,3,0x23,0x85);unsigned before=writes;send(":on\n:mode anchor\n:mode hybrid_static\n");
 TEST_ASSERT_EQUAL(before,writes);TEST_ASSERT_FALSE(uwbControlRequested());
 cfgInit();boot.mode=6;uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlRequested());
 send(":on\n");apply();TEST_ASSERT_TRUE(uwbControlRunning());TEST_ASSERT_EQUAL(6,boot.mode);
 send(":role standalone\n");apply();TEST_ASSERT_FALSE(uwbControlRunning());
 TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(255,mode);
 send(":mode tag\n");TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(1,mode);
 TEST_ASSERT_FALSE(uwbControlRequested());
}
void test_lineSessionInvalidSyntaxAndBackpressureDoNotLeakOrMutate(void) {
 send(":console\n");send("ShOw\r\n");unsigned before=writes;
 send("StAtUs\ncal show\n");TEST_ASSERT_NOT_NULL(strstr(output,"OVERRIDE phy=0 diag=0 ads=0"));
 TEST_ASSERT_NOT_NULL(strstr(output,"RF requested=1 running=0 restart=0"));
 TEST_ASSERT_NOT_NULL(strstr(output,"CAL runtime=UNSUPPORTED"));TEST_ASSERT_EQUAL(before,writes);
 send("role wrong\noff extra\non extra\nmode xyz\nwifi on\npeer del 1\ncal on\n");
 TEST_ASSERT_EQUAL(before,writes);TEST_ASSERT_TRUE(uwbControlRequested());
 send("diag reset\n");TEST_ASSERT_EQUAL(before,writes);TEST_ASSERT_NOT_NULL(strstr(output,"requires RF OFF"));
 output[0]=0;send("help\n");TEST_ASSERT_NOT_NULL(strstr(output,"off retains mode"));
 txReady=false;send("show");TEST_ASSERT_TRUE(mulConsoleByte('\n'));TEST_ASSERT_FALSE(mulConsolePoll());
 txReady=true;while(!mulConsolePoll()){};send("legacy\n");TEST_ASSERT_FALSE(mulConsoleByte('h'));
 send(":console\n");mulConsoleReset();TEST_ASSERT_FALSE(mulConsoleByte('o'));
}
void test_committedButUnreadableOffStaysOffAfterRebootAndInvalidRfIsRejected(void) {
 apply();badRead=true;send(":off\n");TEST_ASSERT_FALSE(uwbControlRequested());apply();badRead=false;
 cfgInit();uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlRequested());
 TEST_ASSERT_TRUE(cfgWriteU8(cfgRfEnable,2));cfgInit();bool enabled=true;
 TEST_ASSERT_FALSE(cfgRfEnabled(&enabled));uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlRequested());
}

void test_everyStockModeRetainsLegacyEnabledBootAndMalformedRequiredFieldsFailClosed(void) {
 for(uint8_t mode=0;mode<5;mode++) {
  legacy(0x84);TEST_ASSERT_TRUE(cfgWriteU8(cfgMode,mode));cfgInit();boot.mode=mode;uwbControlInit(&boot);
  TEST_ASSERT_TRUE(uwbControlRequested());TEST_ASSERT_TRUE(uwbControlCanEnable());
 }
 legacy(0x84);memory[9]=0;memory[10]=200;checksum();cfgInit();boot.mode=0;uwbControlInit(&boot);
 TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_FALSE(uwbControlCanEnable());
 // An invalid explicit passive roster must not fall back to a stock transmitter.
 legacy(0x84);TEST_ASSERT_TRUE(cfgWriteU8(cfgMode,0));
 unsigned at=5+memory[3];memory[at]=cfgMulNetwork;memory[at+1]=3;memory[at+2]=3;memory[at+3]=0x84;memory[at+4]=0x84;memory[3]+=5;checksum();
 cfgInit();boot.mode=0;uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_FALSE(uwbControlCanEnable());
}
void test_counterResetRetainsProtocolIdentityAndRejectsActiveRadio(void) {
 apply();TEST_ASSERT_TRUE(uwbControlEvent(eventPacketReceived));TEST_ASSERT_FALSE(uwbControlResetCounters());
 send(":off\n");TEST_ASSERT_FALSE(uwbControlResetCounters());apply();
 TEST_ASSERT_TRUE(uwbControlResetCounters());TEST_ASSERT_EQUAL(0,uwbControlCount(eventPacketReceived));
 binding(0x84,0,0x84,0x85);
}

void test_concurrentLppStyleWriterIsRejectedWithoutInterleavingEeprom(void) {
 reenter=true;unsigned before=writes;TEST_ASSERT_TRUE(cfgWriteU8(cfgRfEnable,0));
 TEST_ASSERT_EQUAL(before+1,writes);uint8_t mode=0;TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(5,mode);
 TEST_ASSERT_TRUE(cfgStorageReady());TEST_ASSERT_FALSE(cfgPendingRestart());
}

void test_roleModeAddressEditsRetainExistingAnchorPositionAndPower(void) {
 float position[3]={1.25f,-2.0f,3.0f},actual[3]={0};
 TEST_ASSERT_TRUE(cfgWriteFP32list(cfgAnchorPos,position,3));cfgInit();
 send(":mul setup disabled 0x23 0x85\n:role rlr\n:addr 0x42\n");
 TEST_ASSERT_TRUE(cfgReadFP32list(cfgAnchorPos,actual,3));TEST_ASSERT_EQUAL_MEMORY(position,actual,sizeof(position));
 binding(0x42,3,0x23,0x85);
}

void test_runtimeSlotRequiresStoppedOwnerPersistsAndResetsOnResume(void) {
 uint16_t slot=0;TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(500,slot);TEST_ASSERT_EQUAL(0,writes);
 apply();unsigned before=writes;send(":cfg set slot_ms 10\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");send(":cfg set slot_ms 10\n");TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(500,slot); // OFF still pending
 apply();TEST_ASSERT_FALSE(uwbControlRunning());
 const unsigned slots[]={10,50,500,10};
 for(unsigned i=0;i<4;i++) {
  char command[40];snprintf(command,sizeof(command),":cfg set slot_ms %u\n",slots[i]);send(command);
  TEST_ASSERT_FALSE(cfgPendingRestart());TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(slots[i],slot);
  send(":on\n");apply();TEST_ASSERT_TRUE(uwbControlRunning());TEST_ASSERT_EQUAL(slots[i],boot.mulNetwork.slotMs);
  send(":off\n");apply();cfgInit();uwbControlInit(&boot);TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(slots[i],slot);
 }
 before=writes;send(":cfg set slot_ms 20\n:cfg set slot_ms -1\n:cfg set slot_ms 1000\n");TEST_ASSERT_EQUAL(before,writes);
 TEST_ASSERT_FALSE(cfgMulSlotWrite(20));send(":cfg show\n:status\n");
 TEST_ASSERT_NOT_NULL(strstr(output,"SAVED slot_ms=10"));
}
void test_malformedSlotTlvFailsClosedWithoutErasingOtherFields(void) {
 TEST_ASSERT_TRUE(cfgWriteU8(cfgMulSlotMs,10));cfgInit();uint16_t slot;
 TEST_ASSERT_FALSE(cfgMulSlotRead(&slot));uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlCanEnable());
 binding(0x84,0,0x84,0x85);
}
void test_freshEepromUses10msAndOldEepromMigrationDoesNotWrite(void) {
 memset(memory,255,sizeof(memory));cfgInit();uint16_t slot=0;
 TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(10,slot);
 unsigned before=writes;cfgInit();TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(10,slot);TEST_ASSERT_EQUAL(before,writes);
 legacy(0x84);TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(500,slot);TEST_ASSERT_EQUAL(0,writes);
}
