#include "mul_antenna.h"
#include "mul_pose.h"
#include "mul_geometry.h"
#include "mul_clock_fit.h"
#include "mul_clock.h"
/* Actual legacy EEPROM TLV + additive role migration + console parser. */
#include "unity.h"
#include "cfg.h"
#include "mul_plan_store.h"
#include "mul_ranging_plan.h"
#include "mul_common.h"
#include "mul_schedule.h"
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
static uint8_t memory[8192];static unsigned writes;static int budget,byteBudget;static bool badRead,reenter,failCalAck;
bool eepromRead(int address,void *data,size_t n) {if(badRead)return false;if(address<0||(size_t)address+n>sizeof(memory))return false;memcpy(data,memory+address,n);return true;}
bool eepromWrite(int address,void *data,size_t n) {writes++;TEST_ASSERT_EQUAL(0,critical);if(reenter){reenter=false;TEST_ASSERT_FALSE(cfgWriteU8(cfgMode,0));TEST_ASSERT_FALSE(cfgStorageReady());TEST_ASSERT_TRUE(cfgPendingRestart());}if(budget==0)return false;if(budget>0)budget--;if(address<0||(size_t)address+n>sizeof(memory))return false;if(byteBudget>=0){size_t take=(size_t)byteBudget<n?(size_t)byteBudget:n;memcpy(memory+address,data,take);byteBudget-=take;return take==n;}memcpy(memory+address,data,n);if(failCalAck&&address>=384&&address<448&&address%32==20&&n==4&&((uint8_t *)data)[0]){failCalAck=false;return false;}return true;}
void HAL_Delay(uint32_t ms) {(void)ms;}
static void checksum(void) {unsigned size=memory[3]|memory[4]<<8;uint8_t sum=0;for(unsigned i=0;i<5+size;i++)sum+=memory[i];memory[5+size]=sum;}
static void legacy(uint8_t address) {
 memset(memory,0,sizeof(memory));uint8_t record[]={0xbc,1,0,15,0,cfgAddress,1,address,cfgMode,1,5,cfgTxPower,4,0x12,0x34,0x56,0x78,cfgSmartPower,1,1};
 memcpy(memory,record,sizeof(record));checksum();cfgInit();writes=0;boot.mode=5;boot.address[0]=address;uwbControlInit(&boot);mulConsoleReset();
}
void setUp(void){budget=byteBudget=-1;badRead=reenter=failCalAck=false;stops=clears=inits=model=0;owner=false;txReady=true;output[0]=0;legacy(0x84);}
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
 memory[20]^=1;uint8_t damaged[sizeof(memory)];memcpy(damaged,memory,sizeof(memory));writes=0;
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
 memory[18]=255;checksum();uint8_t damaged[sizeof(memory)];memcpy(damaged,memory,sizeof(memory));writes=0;
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
 send(":mode tag\n");TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));
#if MUL_ONLY
 TEST_ASSERT_EQUAL(255,mode);
#else
 TEST_ASSERT_EQUAL(1,mode);
#endif
 TEST_ASSERT_FALSE(uwbControlRequested());
}
void test_lineSessionInvalidSyntaxAndBackpressureDoNotLeakOrMutate(void) {
 send(":console\n");send("ShOw\r\n");unsigned before=writes;
 send("StAtUs\ncal show\n");TEST_ASSERT_NOT_NULL(strstr(output,"OVERRIDE phy=0 diag=0 ads=0"));
 TEST_ASSERT_NOT_NULL(strstr(output,"RF requested=1 running=0 restart=0"));
 TEST_ASSERT_NOT_NULL(strstr(output,"CAL EEPROM/default"));TEST_ASSERT_EQUAL(before,writes);
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
  legacy(0x84);
#if MUL_ONLY
  unsigned before=writes;TEST_ASSERT_FALSE(cfgWriteU8(cfgMode,mode));TEST_ASSERT_EQUAL(before,writes);
  memory[10]=mode;checksum();cfgInit();boot.mode=mode;uwbControlInit(&boot);
  TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_FALSE(uwbControlCanEnable());
#else
  TEST_ASSERT_TRUE(cfgWriteU8(cfgMode,mode));cfgInit();boot.mode=mode;uwbControlInit(&boot);
  TEST_ASSERT_TRUE(uwbControlRequested());TEST_ASSERT_TRUE(uwbControlCanEnable());
#endif
 }
 legacy(0x84);memory[9]=0;memory[10]=200;checksum();cfgInit();boot.mode=0;uwbControlInit(&boot);
 TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_FALSE(uwbControlCanEnable());
 // An invalid explicit passive roster must not fall back to a stock transmitter.
 legacy(0x84);memory[10]=0;checksum();cfgInit();
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

void test_multiRosterPersistenceRfOffAndInvalidTransactions(void) {
 mulSchedule_t schedule;TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&schedule));
 TEST_ASSERT_EQUAL(MUL_PROFILE_LEGACY,schedule.profile);TEST_ASSERT_EQUAL(500,schedule.slotMs);
 unsigned before=writes;send(":cfg roster set 0x84 0x85 0x86\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");apply();send(":cfg set slot_ms 10\n");
 const unsigned counts[]={2,3,4,8};
 for(unsigned n=0;n<4;n++) {
  TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&schedule));schedule.count=counts[n];
  schedule.profile=schedule.count==2?MUL_PROFILE_LEGACY:MUL_PROFILE_MULTI;
  memset(schedule.ids,0,sizeof(schedule.ids));for(unsigned i=0;i<schedule.count;i++)schedule.ids[i]=0x84+i;
  TEST_ASSERT_TRUE(cfgMulScheduleWrite(0x84,&schedule));before=writes;cfgInit();
  mulSchedule_t restored;TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&restored));TEST_ASSERT_EQUAL(before,writes);
  TEST_ASSERT_EQUAL_MEMORY(&schedule,&restored,sizeof(schedule));
  mulSchedule_t bad=schedule;bad.ids[1]=bad.ids[0];TEST_ASSERT_FALSE(cfgMulScheduleWrite(0x84,&bad));TEST_ASSERT_EQUAL(before,writes);
  TEST_ASSERT_FALSE(cfgMulScheduleWrite(0x42,&schedule));TEST_ASSERT_EQUAL(before,writes);
 }
 send(":cfg roster show\n");TEST_ASSERT_NOT_NULL(strstr(output,"count=8"));
 // Multi ALR activation derives its pair/role from the ordered roster.
 uwbControlInit(&boot);send(":on\n");apply();TEST_ASSERT_TRUE(uwbControlRunning());
 TEST_ASSERT_EQUAL(MUL_PROFILE_MULTI,boot.mulSchedule.profile);
 TEST_ASSERT_EQUAL(mulAlrReference,boot.mulRole);
 send(":off\n");apply();
 // LPS mode6 has no multi receiver adapter; never run the two-node RLR against it.
 boot.mode=MODE_MUL_RLR;uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlCanEnable());
}

void test_multiScheduleScalarCliAndShadowBindingGuards(void) {
 unsigned before=writes;send(":cfg set schedule_revision 9\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");apply();send(":cfg set slot_ms 10\n:cfg roster set 0x84 0x85 0x86\n");
 send(":cfg set schedule_revision 4294967295\n");mulSchedule_t spec;
 TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&spec));TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,spec.revision);
 before=writes;
 send(":cfg set schedule_revision 4294967296\n:cfg set schedule_revision 0\n:cfg set cycle_ms 500\n:cfg set profile legacy\n:reference 0x42\n:role follower\n:addr 0x42\n");
 TEST_ASSERT_EQUAL(before,writes);cfgInit();TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&spec));
 TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,spec.revision);TEST_ASSERT_EQUAL(3,spec.count);
 send(":cfg roster set 0x85 0x84\n:cfg set profile multi\n:cfg set cycle_ms 1000\n:cfg set schedule_revision 9\n:on\n");apply();
 TEST_ASSERT_TRUE(uwbControlRunning());TEST_ASSERT_EQUAL(mulAlrFollower,boot.mulRole);
 TEST_ASSERT_EQUAL(0x85,boot.mulNetwork.addresses[0][0]);TEST_ASSERT_EQUAL(0x84,boot.mulNetwork.addresses[1][0]);
 TEST_ASSERT_EQUAL(2,boot.mulSchedule.count);TEST_ASSERT_EQUAL(MUL_PROFILE_MULTI,boot.mulSchedule.profile);
 send(":off\n");apply();send(":cfg set profile legacy\n:on\n");apply();
 TEST_ASSERT_TRUE(uwbControlRunning());TEST_ASSERT_EQUAL(MUL_PROFILE_LEGACY,boot.mulSchedule.profile);
}
void test_clockEstimatorSettingsPersistenceAndRfOffGuards(void) {
 unsigned before=writes;send(":cfg set clock_estimator kalman\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");apply();send(":cfg set slot_ms 10\n:cfg roster set 0x84 0x85 0x86 0x87\n");
 send(":cfg set clock_estimator kalman\n:cfg set clock_compensate on\n");
 mulClockFitConfig_t c;TEST_ASSERT_TRUE(cfgMulClockRead(&c));TEST_ASSERT_EQUAL(2,c.estimator);TEST_ASSERT_EQUAL(5,c.window);TEST_ASSERT_EQUAL(1,c.compensate);
 before=writes;cfgInit();TEST_ASSERT_TRUE(cfgMulClockRead(&c));TEST_ASSERT_EQUAL(before,writes);
 send(":cfg set clock_window 7\n:cfg set clock_window 33\n:cfg set clock_estimator quadratic\n");TEST_ASSERT_EQUAL(before,writes);
 send(":clock show\n");TEST_ASSERT_NOT_NULL(strstr(output,"method=2 history=5"));
 send(":on\n");apply();TEST_ASSERT_TRUE(uwbControlRunning());TEST_ASSERT_EQUAL(5,boot.mulClockConfig.window);
 send(":cfg set clock_estimator median\n");TEST_ASSERT_TRUE(cfgMulClockRead(&c));TEST_ASSERT_EQUAL(2,c.estimator);
 send(":off\n");apply();send(":cfg set clock_estimator median\n");TEST_ASSERT_TRUE(cfgMulClockRead(&c));TEST_ASSERT_EQUAL(0,c.estimator);
}
void test_oldAndUnknownClockSelectorsMigrateWithoutRetargetingTheDevice(void) {
 send(":off\n");apply();uint8_t address=0;TEST_ASSERT_TRUE(cfgReadU8(cfgAddress,&address));
 uint8_t old[12]={1,1,24,8,0xe8,3,0,1,0,0,0,0};
 TEST_ASSERT_TRUE(cfgWriteU8list(cfgMulClock,old,sizeof(old)));
 cfgInit();mulClockFitConfig_t c;TEST_ASSERT_TRUE(cfgMulClockRead(&c));TEST_ASSERT_EQUAL(0,c.estimator);TEST_ASSERT_EQUAL(5,c.window);
 uint8_t retained=255;TEST_ASSERT_TRUE(cfgReadU8(cfgAddress,&retained));TEST_ASSERT_EQUAL(address,retained);
 old[0]=2;old[1]=255;old[2]=old[3]=5;
 unsigned at=5;while(at<99&&memory[at]!=cfgMulClock)at+=2+memory[at+1];TEST_ASSERT_TRUE(at+14<100);
 memcpy(memory+at+2,old,sizeof(old));checksum();
 cfgInit();TEST_ASSERT_TRUE(cfgMulClockRead(&c));TEST_ASSERT_EQUAL(0,c.estimator);
 TEST_ASSERT_TRUE(cfgReadU8(cfgAddress,&retained));TEST_ASSERT_EQUAL(address,retained);
}
void test_experimentalNumericalTimingRequiresExplicitBudgetsAndRfOff(void) {
 send(":off\n");apply();send(":cfg set slot_ms 10\n:cfg roster set 0x84 0x85 0x86 0x87\n:cfg set profile experimental\n");
 mulSchedule_t spec;TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&spec));TEST_ASSERT_EQUAL(MUL_PROFILE_EXPERIMENTAL,spec.profile);
 unsigned before=writes;send(":cfg set slot_ms 3\n");TEST_ASSERT_EQUAL(before,writes);
 send(":cfg set tx_lead_us 1000\n:cfg set rx_budget_us 500\n:cfg set slot_ms 3\n");
 TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&spec));TEST_ASSERT_EQUAL(3,spec.slotMs);TEST_ASSERT_EQUAL(1000,spec.txLeadUs);
 before=writes;send(":cfg set cycle_ms 200\n");TEST_ASSERT_EQUAL(before,writes);
 send(":cfg set alr_alr_twr off\n:cfg set cycle_ms 200\n");cfgInit();
 TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&spec));TEST_ASSERT_EQUAL(200,spec.periodMs);TEST_ASSERT_FALSE(mulScheduleAlrRanging(&spec));
 send(":cfg roster show\n");TEST_ASSERT_NOT_NULL(strstr(output,"experimental=1"));TEST_ASSERT_NOT_NULL(strstr(output,"process=500"));
 send(":clock show\n");TEST_ASSERT_NOT_NULL(strstr(output,"CLOCK period=200"));
#if !MUL_HYBRID_CH5
 boot.lowBitrate=true;TEST_ASSERT_FALSE(uwbControlCanEnable());boot.lowBitrate=false;
#endif
 before=writes;send(":cfg set tx_lead_us 499\n:cfg set guard_us 13\n:cfg set cycle_ms 201\n:cfg set profile legacy\n");TEST_ASSERT_EQUAL(before,writes);
 send(":on\n");apply();TEST_ASSERT_TRUE(uwbControlRunning());TEST_ASSERT_EQUAL(3,boot.mulNetwork.slotMs);TEST_ASSERT_EQUAL(200,boot.mulSchedule.periodMs);
 before=writes;send(":cfg set slot_ms 5\n:cfg set tx_lead_us 2000\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");apply();send(":cfg set slot_ms 10\n:cfg set cycle_ms 1000\n:cfg set profile multi\n");
 TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&spec));TEST_ASSERT_EQUAL(MUL_PROFILE_MULTI,spec.profile);TEST_ASSERT_EQUAL(0,spec.txLeadUs);
}

void test_mulOnlyRemovedModeCommandsNeverMutateOrEnableRadio(void) {
#if MUL_ONLY
 const char *commands[]={":mode anchor\n",":mode tag\n",":mode sniffer\n",":mode tdoa2\n",":mode tdoa3\n"};
 unsigned before=writes;
 for(unsigned i=0;i<5;i++){send(commands[i]);TEST_ASSERT_EQUAL(before,writes);}
 uint8_t mode=0;TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&mode));TEST_ASSERT_EQUAL(5,mode);
 for(unsigned i=0;i<7;i++){memory[10]=i<5?i:i==5?200:255;checksum();cfgInit();boot.mode=memory[10];uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlRequest(true));}
#endif
}

void test_experimentalReplySettingsPersistValidateBudgetsAndRequireRfOff(void) {
 send(":off\n");apply();send(":cfg set profile experimental\n");
 unsigned before=writes;send(":cfg set twr_reply_ms 5\n");TEST_ASSERT_EQUAL(before,writes);
 send(":cfg set tx_lead_us 1000\n:cfg set rx_budget_us 500\n:cfg set twr_reply_ms 5\n");
 mulSchedule_t s;TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&s));TEST_ASSERT_EQUAL(5,mulScheduleReplyMs(&s));
 before=writes;send(":cfg set twr_reply_ms 7\n");TEST_ASSERT_EQUAL(before,writes);
 cfgInit();TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&s));TEST_ASSERT_EQUAL(5,mulScheduleReplyMs(&s));
 send(":cfg set alr_alr_twr off\n");TEST_ASSERT_TRUE(cfgMulScheduleRead(0x84,&s));TEST_ASSERT_EQUAL(5,mulScheduleReplyMs(&s));TEST_ASSERT_FALSE(mulScheduleAlrRanging(&s));
 send(":cfg set alr_alr_twr on\n:on\n");apply();before=writes;
 send(":cfg set twr_reply_ms 10\n");TEST_ASSERT_EQUAL(before,writes);
}

void test_replyFlagsAreFingerprintBoundAndReservedCodesFailClosed(void) {
 mulSchedule_t s,decoded;mulScheduleLegacy(&s,132,133,10);mulScheduleExperimental(&s);
 uint32_t baseline=mulScheduleFingerprint(&s);s.rangingFlags=3;
 uint8_t bytes[32];TEST_ASSERT_TRUE(mulScheduleEncode(bytes,&s));TEST_ASSERT_TRUE(mulScheduleDecode(bytes,&decoded));
 TEST_ASSERT_EQUAL(10,mulScheduleReplyMs(&decoded));TEST_ASSERT_NOT_EQUAL(baseline,mulScheduleFingerprint(&decoded));
 s.rangingFlags=5;TEST_ASSERT_NOT_NULL(mulScheduleValidate(&s));s.txLeadUs=500;s.rxBudgetUs=500;
 TEST_ASSERT_TRUE(mulScheduleEncode(bytes,&s));TEST_ASSERT_TRUE(mulScheduleDecode(bytes,&decoded));TEST_ASSERT_EQUAL(5,mulScheduleReplyMs(&decoded));
 s.rangingFlags=7;TEST_ASSERT_FALSE(mulScheduleEncode(bytes,&s));s.rangingFlags=8;TEST_ASSERT_FALSE(mulScheduleEncode(bytes,&s));
}

void test_fullPlanJournalBackendRequiresAcknowledgedOffAndPreservesLegacyBytes(void) {
 mulRangingPlan_t p={.replyMs=20,.maxUtilization=70};
 mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
 uint32_t generation=0;mulStoredPlan_t loaded;
 TEST_ASSERT_FALSE(cfgMulPlanSave(&p,true,&generation));
 TEST_ASSERT_EQUAL(mulPlanIoError,cfgMulPlanLoad(&loaded));
 send(":off\n");apply();uint8_t prefix[100];memcpy(prefix,memory,sizeof(prefix));
 TEST_ASSERT_EQUAL(mulPlanAbsent,cfgMulPlanLoad(&loaded));
 TEST_ASSERT_TRUE(cfgMulPlanSave(&p,true,&generation));TEST_ASSERT_EQUAL(1,generation);
 TEST_ASSERT_EQUAL_MEMORY(prefix,memory,sizeof(prefix));
 TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanLoad(&loaded));TEST_ASSERT_TRUE(loaded.selected);
 unsigned before=writes;TEST_ASSERT_TRUE(cfgMulPlanSave(&p,true,&generation));TEST_ASSERT_EQUAL(before,writes);
 budget=1;TEST_ASSERT_FALSE(cfgMulPlanSave(&p,false,&generation));TEST_ASSERT_FALSE(cfgStorageReady());budget=-1;
 cfgInit();TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanLoad(&loaded));TEST_ASSERT_TRUE(loaded.selected);
 TEST_ASSERT_EQUAL_MEMORY(prefix,memory,sizeof(prefix));
 TEST_ASSERT_TRUE(cfgMulPlanSave(&p,false,&generation));TEST_ASSERT_EQUAL(2,generation);
 TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanLoad(&loaded));TEST_ASSERT_FALSE(loaded.selected);
}

void test_selectedPlanBootCacheNeverEnablesLegacyOwnerAndResetDisablesJournal(void) {
 send(":off\n");apply();
 mulRangingPlan_t p={.replyMs=20,.maxUtilization=70};mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
 uint32_t generation=0;TEST_ASSERT_TRUE(cfgMulPlanSave(&p,true,&generation));
 const mulStoredPlan_t *cached=NULL;TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&cached));TEST_ASSERT_TRUE(cached->selected);
 cfgInit();TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&cached));TEST_ASSERT_TRUE(cached->selected);
 uwbControlInit(&boot);TEST_ASSERT_TRUE(uwbControlCanEnable());apply();TEST_ASSERT_NOT_NULL(boot.mulPlan);
 send(":off\n");apply();TEST_ASSERT_TRUE(cfgReset());TEST_ASSERT_TRUE(cfgPendingRestart());cfgInit();
 TEST_ASSERT_EQUAL(mulPlanAbsent,cfgMulPlanCached(&cached));TEST_ASSERT_NULL(cached);
}

void test_planConsoleEditsPersistsPreviewsSelectsAndRejectsUnsupportedRadio(void) {
 send(":off\n");apply();uint8_t prefix[100];memcpy(prefix,memory,sizeof(prefix));
 send(":plan init\n:plan set lead_us 1000\n:plan set process_us 500\n:plan set slot_ms 3\n");
 send(":plan alrs 132 135 138 141\n:plan rlrs 144 145\n:plan set reply_ms 5\n:plan set aa on\n:plan set ar on\n:plan use on\n");
 const mulStoredPlan_t *stored=NULL;TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&stored));TEST_ASSERT_TRUE(stored->selected);
 TEST_ASSERT_EQUAL(4,stored->plan.broadcast.count);TEST_ASSERT_EQUAL(2,stored->plan.rlrCount);TEST_ASSERT_EQUAL(5,stored->plan.replyMs);
 TEST_ASSERT_EQUAL_MEMORY(prefix,memory,sizeof(prefix));TEST_ASSERT_TRUE(uwbControlCanEnable());
 output[0]=0;send(":plan show\n");TEST_ASSERT_NOT_NULL(strstr(output,"PLAN selected=1"));TEST_ASSERT_NOT_NULL(strstr(output,"RLR 0 id=144"));TEST_ASSERT_NOT_NULL(strstr(output,"POLL op="));
 TEST_ASSERT_TRUE(uwbControlCanEnable());send(":on\n");apply();TEST_ASSERT_TRUE(uwbControlRunning());TEST_ASSERT_NOT_NULL(boot.mulPlan);
 unsigned before=writes;send(":plan set reply_ms 10\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");apply();before=writes;
 send(":cfg set slot_ms 10\n:plan set aa_millihz 10000\n:plan rlrs 144 144\n:plan set util_percent 100\n");TEST_ASSERT_EQUAL(before,writes);
 send(":cfg set clock_window 30\n");mulClockFitConfig_t clock;TEST_ASSERT_TRUE(cfgMulClockRead(&clock));TEST_ASSERT_EQUAL(5,clock.window);
 cfgInit();TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&stored));TEST_ASSERT_TRUE(stored->selected);
 send(":plan use off\n");apply();TEST_ASSERT_NULL(boot.mulPlan);uint16_t slot;TEST_ASSERT_TRUE(cfgMulSlotRead(&slot));TEST_ASSERT_EQUAL(500,slot);
}

void test_resetInterruptedAtEveryWriteRespectsProfileWithoutRestoringMulOwner(void) {
 for(int cut=0;cut<=3;cut++) {
  setUp();send(":off\n");apply();
  mulRangingPlan_t p={.replyMs=20,.maxUtilization=70};mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
  uint32_t generation=0;TEST_ASSERT_TRUE(cfgMulPlanSave(&p,true,&generation));
  budget=cut;TEST_ASSERT_EQUAL(cut==3,cfgReset());budget=-1;
  // Every interrupted/unconfirmed reset blocks activation until reboot/recovery.
  TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_FALSE(uwbControlRunning());TEST_ASSERT_FALSE(uwbControlCanEnable());
  cfgInit();TEST_ASSERT_TRUE(cfgReadU8(cfgMode,&boot.mode));TEST_ASSERT_TRUE(cfgReadU8(cfgAddress,&boot.address[0]));
  uwbControlInit(&boot);TEST_ASSERT_FALSE(uwbControlRunning());
  const mulStoredPlan_t *stored=NULL;
  TEST_ASSERT_EQUAL(cut<2?mulPlanStored:mulPlanAbsent,cfgMulPlanCached(&stored));
  if(cut<2){TEST_ASSERT_NOT_NULL(stored);TEST_ASSERT_TRUE(stored->selected);}
  else {TEST_ASSERT_NULL(stored);}
  if(!cut){TEST_ASSERT_EQUAL(MODE_MUL_ALR,boot.mode);TEST_ASSERT_EQUAL(132,boot.address[0]);}
  else {TEST_ASSERT_EQUAL(0,boot.address[0]);}
#if MUL_ONLY
  // Production factory defaults are idle, even if an old plan commit survives.
  TEST_ASSERT_FALSE(uwbControlRequested());
  if(cut){TEST_ASSERT_EQUAL(255,boot.mode);TEST_ASSERT_FALSE(uwbControlCanEnable());}
#else
  // Legacy factory defaults allow the stock anchor only after the plan clears.
  if(cut){TEST_ASSERT_EQUAL(MODE_ANCHOR,boot.mode);}
  TEST_ASSERT_EQUAL(cut>=2,uwbControlRequested());
  TEST_ASSERT_EQUAL(cut!=1,uwbControlCanEnable());
#endif
 }
}

void test_committedPlanCorruptionOrBootReadFailureCannotFallBackToLegacyRadio(void) {
 send(":off\n");apply();
 mulRangingPlan_t p={.replyMs=20,.maxUtilization=70};mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);p.broadcast.rangingFlags=0;
 uint32_t generation=0;TEST_ASSERT_TRUE(cfgMulPlanSave(&p,true,&generation));
 unsigned before=writes;TEST_ASSERT_FALSE(cfgMulChangeAddress(135));TEST_ASSERT_EQUAL(before,writes);
 memory[MUL_PLAN_STORE_BASE+12]^=1;cfgInit();const mulStoredPlan_t *cached=NULL;
 TEST_ASSERT_EQUAL(mulPlanCorrupt,cfgMulPlanCached(&cached));TEST_ASSERT_NULL(cached);TEST_ASSERT_FALSE(uwbControlCanEnable());
 badRead=true;cfgInit();badRead=false;TEST_ASSERT_EQUAL(mulPlanIoError,cfgMulPlanCached(&cached));TEST_ASSERT_FALSE(uwbControlCanEnable());
}

void test_freshnessPlanPrioritiesPersistAndFairSelectionRestoresCodec2(void) {
 send(":off\n");apply();send(":plan init\n:plan set policy freshness\n:plan set aa_priority 200\n:plan set ar_priority 9\n");
 const mulStoredPlan_t *p=NULL;cfgInit();TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&p));
 TEST_ASSERT_EQUAL(1,p->plan.policy);TEST_ASSERT_EQUAL(200,p->plan.aaPriority);TEST_ASSERT_EQUAL(9,p->plan.arPriority);
 TEST_ASSERT_EQUAL(3,mulRangingPlanVersion(&p->plan));unsigned before=writes;
 send(":plan set aa_priority 256\n:plan set policy unknown\n");TEST_ASSERT_EQUAL(before,writes);
 send(":plan set policy fair\n");TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&p));
 TEST_ASSERT_EQUAL(0,p->plan.aaPriority);TEST_ASSERT_EQUAL(0,p->plan.arPriority);TEST_ASSERT_EQUAL(2,mulRangingPlanVersion(&p->plan));
}

TickType_t xTaskGetTickCount(void){return 0;}

void test_geometrySourceIsAdditivePersistentAndRequiresAcknowledgedRfOff(void) {
 mulDelaySource_t source;TEST_ASSERT_TRUE(cfgGeometrySourceRead(&source));TEST_ASSERT_EQUAL(mulDelayTwr,source);
 unsigned before=writes;send(":geometry source external\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");apply();send(":geometry source external\n");TEST_ASSERT_TRUE(cfgGeometrySourceRead(&source));
 TEST_ASSERT_EQUAL(mulDelayExternal,source);TEST_ASSERT_FALSE(cfgPendingRestart());cfgInit();uwbControlInit(&boot);
 TEST_ASSERT_EQUAL(mulDelayExternal,mulGeometrySource());before=writes;
 send(":geometry source truth\n");TEST_ASSERT_EQUAL(before,writes);
 send(":geometry source none\n");TEST_ASSERT_EQUAL(mulDelayNone,mulGeometrySource());
 send(":geometry source twr\n");TEST_ASSERT_EQUAL(mulDelayTwr,mulGeometrySource());
}

void test_expiredGeometryCannotReviveAfterFullUptimeWrapOrPairRetarget(void) {
 mulNetwork_t network;TEST_ASSERT_TRUE(mulNetworkInit(&network,132,133));mulGeometryPair(&network);
 TEST_ASSERT_TRUE(mulGeometryBegin(1,1,1,0));
 mulPose_t a={.node=132,.flags=1,.clock=1,.epoch=1,.frame=1,.sequence=1,.sourceUs=1000000,.ttlMs=1000};
 mulPose_t b=a;b.node=133;b.positionMm[0]=10000;
 TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&a));TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&b));mulGeometryPoll(0);
 TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometrySnapshot(0,NULL,NULL));mulGeometryPoll(1001);
 TEST_ASSERT_EQUAL(mulPoseStale,mulGeometrySnapshot(0,NULL,NULL));
 mulGeometryPair(&network);mulGeometryPoll(0);TEST_ASSERT_EQUAL(mulPoseStale,mulGeometrySnapshot(0,NULL,NULL));
 a.sequence=b.sequence=2;a.sourceUs++;b.sourceUs++;
 TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&a));TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometryPut(&b));mulGeometryPoll(0);
 TEST_ASSERT_EQUAL(mulPoseAvailable,mulGeometrySnapshot(0,NULL,NULL));
}

void test_planAaDelayConsolePersistsAndRejectsUnsafeEditsWithoutWrites(void) {
 send(":off\n");apply();
 mulRangingPlan_t p={.replyMs=20,.maxUtilization=70,.enabled=3,.aaMilliHz=1000,.arMilliHz=50,.rlrCount=1,.rlrs={141}};
 mulScheduleLegacy(&p.broadcast,132,133,10);mulScheduleExperimental(&p.broadcast);
 p.broadcast.rangingFlags=0;p.broadcast.txLeadUs=1000;p.broadcast.rxBudgetUs=5000;
 TEST_ASSERT_EQUAL_HEX32(0x826414cc,mulRangingPlanFingerprint(&p));
 uint32_t generation;TEST_ASSERT_TRUE(cfgMulPlanSave(&p,true,&generation));
 send(":plan set aa_start_delay_us 50000\n");
 const mulStoredPlan_t *cached=NULL;cfgInit();TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&cached));
 TEST_ASSERT_TRUE(cached->selected);TEST_ASSERT_EQUAL(50000,cached->plan.aaStartDelayUs);
 output[0]=0;send(":plan show\n");
 TEST_ASSERT_NOT_NULL(strstr(output,"aa_start_delay_us=50000"));
 TEST_ASSERT_NOT_NULL(strstr(output,"66100..159200us"));TEST_ASSERT_NOT_NULL(strstr(output,"POLL op=0 at=73100 us"));
 unsigned before=writes;send(":plan set aa_start_delay_us 388851\n:plan set aa_start_delay_us 4294967295\n:plan set aa_start_delay_us -1\n");TEST_ASSERT_EQUAL(before,writes);
 send(":plan set aa_start_delay_us 100000\n"); // Must not truncate to uint16.
 TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&cached));TEST_ASSERT_EQUAL(100000,cached->plan.aaStartDelayUs);
 send(":plan set aa_start_delay_us 0\n");TEST_ASSERT_EQUAL(mulPlanStored,cfgMulPlanCached(&cached));
 TEST_ASSERT_EQUAL_HEX32(0x826414cc,mulRangingPlanFingerprint(&cached->plan));
}

void test_antennaCalibrationAtomicPairSurvivesEveryByteCutWithoutTouchingLegacy(void){
 send(":off\n");apply();
 uint8_t unrelated[384];memcpy(unrelated,memory,sizeof(unrelated));
 TEST_ASSERT_TRUE(cfgAntennaWrite(14000,18000));
 uint8_t image[8192];memcpy(image,memory,sizeof(image));
 for(int cut=0;cut<=28;cut++){
  memcpy(memory,image,sizeof(image));byteBudget=-1;cfgInit();
  byteBudget=cut;bool ok=cfgAntennaWrite(15000,19000);byteBudget=-1;
  if(!ok){TEST_ASSERT_FALSE(uwbControlCanEnable());}
  cfgInit();uint16_t tx,rx;mulAntennaGet(&tx,&rx);
  TEST_ASSERT_TRUE((tx==14000&&rx==18000)||(tx==15000&&rx==19000));
  if(ok){TEST_ASSERT_EQUAL(15000,tx);TEST_ASSERT_EQUAL(19000,rx);}
  TEST_ASSERT_EQUAL_MEMORY(unrelated,memory,sizeof(unrelated));
 }
}
void test_antennaConsoleValidationRfOffPersistenceAndReset(void){
 unsigned before=writes;send(":cal set tx 15000 rx 19000\n");TEST_ASSERT_EQUAL(before,writes);
 send(":off\n");apply();before=writes;
 send(":cal set tx 65536 rx 1\n:cal set tx -1 rx 2\n:cal set tx 1 rx invalid\n");TEST_ASSERT_EQUAL(before,writes);
 send(":cal set tx 15000 rx 19000\n");uint16_t tx,rx;mulAntennaGet(&tx,&rx);TEST_ASSERT_EQUAL(15000,tx);TEST_ASSERT_EQUAL(19000,rx);
 before=writes;cfgInit();TEST_ASSERT_EQUAL(before,writes);mulAntennaGet(&tx,&rx);TEST_ASSERT_EQUAL(15000,tx);
 TEST_ASSERT_TRUE(cfgAntennaWrite(0,65535));cfgInit();mulAntennaGet(&tx,&rx);TEST_ASSERT_EQUAL(0,tx);TEST_ASSERT_EQUAL(65535,rx);
 send(":cal reset\n");cfgInit();mulAntennaGet(&tx,&rx);TEST_ASSERT_EQUAL(MUL_ANTENNA_TX_DTU,tx);TEST_ASSERT_EQUAL(MUL_ANTENNA_RX_DTU,rx);
}
void test_antennaCorruptionBlocksRadioWithoutErasingConfiguration(void){
 send(":off\n");apply();TEST_ASSERT_TRUE(cfgAntennaWrite(14000,18000));
 uint8_t original[8192];memory[384+12]^=1;memcpy(original,memory,sizeof(original));
 cfgInit();TEST_ASSERT_FALSE(cfgStorageReady());TEST_ASSERT_FALSE(uwbControlCanEnable());TEST_ASSERT_EQUAL_MEMORY(original,memory,sizeof(original));
}

void test_firstCalibrationInterruptedAtEveryByteKeepsDefaultsOrCommittedPair(void){
 send(":off\n");apply();uint8_t image[8192];memcpy(image,memory,sizeof(image));
 for(int cut=0;cut<=28;cut++){
  memcpy(memory,image,sizeof(image));byteBudget=-1;cfgInit();byteBudget=cut;
  (void)cfgAntennaWrite(15000,19000);byteBudget=-1;cfgInit();
  uint16_t tx,rx;mulAntennaGet(&tx,&rx);
  TEST_ASSERT_TRUE((tx==MUL_ANTENNA_TX_DTU&&rx==MUL_ANTENNA_RX_DTU)||(tx==15000&&rx==19000));
  TEST_ASSERT_EQUAL_MEMORY(image,memory,384);
 }
}
void test_calibrationGenerationWrapAndReadFailureFailClosed(void){
 send(":off\n");apply();TEST_ASSERT_TRUE(cfgAntennaWrite(14000,18000));
 memset(memory+384+8,255,4);uint32_t crc=mulScheduleCrc(memory+384,16);
 for(unsigned i=0;i<4;i++)memory[384+16+i]=crc>>(8*i);
 cfgInit();TEST_ASSERT_TRUE(cfgAntennaWrite(15000,19000));cfgInit();
 uint16_t tx,rx;mulAntennaGet(&tx,&rx);TEST_ASSERT_EQUAL(15000,tx);TEST_ASSERT_EQUAL(19000,rx);
 badRead=true;TEST_ASSERT_FALSE(cfgAntennaWrite(1,2));badRead=false;
 TEST_ASSERT_FALSE(cfgStorageReady());TEST_ASSERT_FALSE(uwbControlCanEnable());
}

void test_calibrationLostCommitAcknowledgementRequiresRebootAndUsesNewPair(void){
 send(":off\n");apply();TEST_ASSERT_TRUE(cfgAntennaWrite(14000,18000));
 failCalAck=true;TEST_ASSERT_FALSE(cfgAntennaWrite(15000,19000));
 TEST_ASSERT_FALSE(cfgStorageReady());TEST_ASSERT_FALSE(uwbControlCanEnable());
 cfgInit();uint16_t tx,rx;mulAntennaGet(&tx,&rx);
 TEST_ASSERT_TRUE(cfgStorageReady());TEST_ASSERT_EQUAL(15000,tx);TEST_ASSERT_EQUAL(19000,rx);
}
