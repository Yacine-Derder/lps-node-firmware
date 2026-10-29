/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_console.h"
#include "mul_antenna_offset.h"
#include "cfg.h"
#include "uwb_control.h"
#include "mul_rx.h"
#include "mul_localization.h"
#include "phy_interop_dw.h"
#include "mul_diag_dw.h"
#include "mul_ads_dw.h"
#include "mul_geometry.h"
#include "task.h"
#include <usbcomm.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static bool collecting,overflow,skipLf,session;
static uint8_t used,page,pageIndex;
static const char *message;
/* Up to eight hexadecimal roster IDs; output remains bounded to48 bytes. */
static char line[184]; // One80-byte pose record as160 hex digits plus command.
enum {none,answer,help,show,status,peers,diag,cal,clockPage,planPage,posePage};
static void reply(const char *text) {message=text;page=answer;pageIndex=0;}
static bool id(const char *s,uint8_t *value) {
 unsigned n=0,base=10;if(s[0]=='0'&&s[1]=='x'){base=16;s+=2;}
 if(!*s)return false;
 for(;*s;s++){unsigned d=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='f'?*s-'a'+10:99;
  if(d>=base||n>(255-d)/base)return false;
  n=n*base+d;}
 *value=n;return true;
}
static bool revision(const char *s,uint32_t *value) {
 uint32_t n=0;if(!*s)return false;
 for(;*s;s++){unsigned d=(unsigned)(*s-'0');if(d>9||n>(UINT32_MAX-d)/10)return false;n=n*10+d;}
 if(!n)return false;
 *value=n;return true;
}
static const char *roleName(uint8_t role) {return role==0?"ref":role==1?"follower":role==3?"rlr":"disabled";}
static bool saved(uint8_t *node,uint8_t *role,uint8_t *ref,uint8_t *fol) {
 if(!cfgReadU8(cfgAddress,node))return false;
 const mulStoredPlan_t *stored=NULL;
 if(cfgMulPlanCached(&stored)==mulPlanStored&&stored->selected) {
  *ref=stored->plan.broadcast.ids[0];*fol=stored->plan.broadcast.ids[1];
  int slot=mulScheduleIndex(&stored->plan.broadcast,*node);*role=slot==0?0:slot>0?1:2;return true;
 }
 if(!cfgMulRead(*node,role,ref,fol))return false;
 uint8_t bytes;if(cfgFieldSize(cfgMulSchedule,&bytes)) {
  mulSchedule_t spec;if(!cfgMulScheduleRead(*node,&spec))return false;
  *ref=spec.ids[0];*fol=spec.ids[1];int slot=mulScheduleIndex(&spec,*node);
  if(slot>=0)*role=slot?1:0;
 }
 return true;
}
static void result(bool ok) {reply(ok?"SAVED; restart to activate":"ERR EEPROM/roster unconfirmed; inspect reboot");}
static bool rfOff(void) {
 uwbSetEnabled(false);
 return cfgWriteU8(cfgRfEnable,0);
}
static void planCommand(char **w,unsigned count) {
 if(count==2&&!strcmp(w[1],"show")){page=planPage;pageIndex=0;return;}
 if(uwbControlRequested()||uwbControlRunning()){reply("ERR plan edit requires RF OFF acknowledged");return;}
 const mulStoredPlan_t *stored=NULL;mulPlanStoreResult_t state=cfgMulPlanCached(&stored);
 if(state==mulPlanIoError||state==mulPlanCorrupt){reply("ERR plan storage unconfirmed; inspect reboot");return;}
 mulRangingPlan_t p;bool selected=stored&&stored->selected;
 if(count==2&&!strcmp(w[1],"init")) {
  if(stored){reply("ERR plan exists; use plan set/show/use");return;}
  uint8_t node=0;cfgReadU8(cfgAddress,&node);memset(&p,0,sizeof(p));
  if(!cfgMulScheduleRead(node,&p.broadcast)){reply("ERR invalid broadcast configuration");return;}
  p.replyMs=mulScheduleReplyMs(&p.broadcast);
  if(p.broadcast.profile!=MUL_PROFILE_EXPERIMENTAL)mulScheduleExperimental(&p.broadcast);
  p.broadcast.rangingFlags=0;p.aaMilliHz=500;p.arMilliHz=2000;p.maxUtilization=70;
 }else {
  if(!stored){reply("ERR no plan; use plan init with RF OFF");return;}p=stored->plan;
  if(count==3&&!strcmp(w[1],"use")) {
   if(!strcmp(w[2],"on"))selected=true;else if(!strcmp(w[2],"off"))selected=false;else {reply("ERR plan use on/off");return;}
  }else if(count>=2&&(!strcmp(w[1],"alrs")||!strcmp(w[1],"rlrs"))) {
   bool alrs=!strcmp(w[1],"alrs");unsigned n=count-2;
   if(n>8||(alrs&&n<2)){reply("ERR roster ALR2..8/RLR0..8");return;}
   uint8_t *ids=alrs?p.broadcast.ids:p.rlrs;memset(ids,0,8);
   for(unsigned i=0;i<n;i++)if(!id(w[i+2],&ids[i])){reply("ERR invalid roster ID");return;}
   if(alrs)p.broadcast.count=(uint8_t)n;else p.rlrCount=(uint8_t)n;
  }else if(count==4&&!strcmp(w[1],"set")) {
   if(!strcmp(w[2],"aa")||!strcmp(w[2],"ar")) {
    unsigned flag=!strcmp(w[2],"aa")?MUL_PLAN_AA:MUL_PLAN_AR;
    if(!strcmp(w[3],"on"))p.enabled|=flag;else if(!strcmp(w[3],"off"))p.enabled&=~flag;else {reply("ERR ranging type: on/off");return;}
   }else if(!strcmp(w[2],"policy")) {
    if(!strcmp(w[3],"fair")){p.policy=MUL_PLAN_FAIR;p.aaPriority=p.arPriority=0;}
    else if(!strcmp(w[3],"freshness"))p.policy=MUL_PLAN_FRESHNESS;
    else {reply("ERR policy fair/freshness");return;}
   }else {
    uint32_t n=0;if(strcmp(w[3],"0")&&!revision(w[3],&n)){reply("ERR unsigned decimal number");return;}
    if(!strcmp(w[2],"revision"))p.broadcast.revision=n;
    else if(!strcmp(w[2],"aa_start_delay_us"))p.aaStartDelayUs=n;
    else {
     if(n>UINT16_MAX){reply("ERR timing/rate exceeds uint16");return;}
     if(!strcmp(w[2],"aa_millihz"))p.aaMilliHz=n;
     else if(!strcmp(w[2],"ar_millihz"))p.arMilliHz=n;
     else if(!strcmp(w[2],"aa_priority")||!strcmp(w[2],"ar_priority")) {
      if(n>255){reply("ERR priority0..255");return;}
      if(!strcmp(w[2],"aa_priority"))p.aaPriority=n;else p.arPriority=n;
     }
     else if(!strcmp(w[2],"slot_ms"))p.broadcast.slotMs=n;
     else if(!strcmp(w[2],"cycle_ms"))p.broadcast.periodMs=n;
     else if(!strcmp(w[2],"lead_us"))p.broadcast.txLeadUs=n;
     else if(!strcmp(w[2],"process_us"))p.broadcast.rxBudgetUs=n;
     else if(!strcmp(w[2],"air_us"))p.broadcast.airtimeUs=n;
     else if(!strcmp(w[2],"guard_us")){if(n<100||n>10000||n%100){reply("ERR guard_us100..10000 step100");return;}p.broadcast.guard100us=n/100;}
     else if(!strcmp(w[2],"reply_ms")){if(n>255){reply("ERR reply_ms5/10/20");return;}p.replyMs=n;}
     else if(!strcmp(w[2],"util_percent")){if(n>100){reply("ERR utilization10..80");return;}p.maxUtilization=n;}
     else {reply("ERR unknown plan field; see plan show/help");return;}
    }
   }
  }else {reply("ERR plan init/show/use/alrs/rlrs/set");return;}
 }
 const char *error=mulRangingPlanValidate(&p,NULL);if(error){reply(error);return;}
 uint32_t generation=0;reply(cfgMulPlanSave(&p,selected,&generation)?"SAVED plan; ON checks assigned radio support":"ERR plan write unconfirmed; inspect reboot");
}

static uint32_t poseNow(void){return (uint32_t)xTaskGetTickCount()*portTICK_PERIOD_MS;}
static bool decimal(const char *s,uint32_t *n){*n=0;return !strcmp(s,"0")||revision(s,n);}
static void poseCommand(char **w,unsigned count) {
 if(count==2&&!strcmp(w[1],"show")){page=posePage;pageIndex=0;return;}
 if(count==6&&!strcmp(w[1],"begin")) {
  uint32_t clock,epoch,frame,skew;
  if(!decimal(w[2],&clock)||!decimal(w[3],&epoch)||!decimal(w[4],&frame)||!decimal(w[5],&skew)||skew>1000||!mulGeometryBegin(clock,epoch,frame,(uint16_t)skew))reply("ERR pose begin clock epoch frame skew_ms");
  else reply("POSE stream begun; old observations cleared");
  return;
 }
 if(count==3&&!strcmp(w[1],"put")&&strlen(w[2])==2*MUL_POSE_BYTES) {
  uint8_t bytes[MUL_POSE_BYTES];
  for(unsigned i=0;i<sizeof(bytes);i++) {
   unsigned value=0;
   for(unsigned j=0;j<2;j++){char c=w[2][2*i+j];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:99;
    if(d>15){reply("ERR pose hex record");return;}value=value*16+d;}
   bytes[i]=(uint8_t)value;
  }
  mulPose_t pose;if(!mulPoseDecode(bytes,poseNow(),&pose)){reply("ERR pose version/CRC/fields/covariance");return;}
  mulPoseStatus_t result=mulGeometryPut(&pose);
  static const char *const messages[]={"POSE accepted","ERR pose missing","ERR pose invalid/stream absent","ERR pose clock/epoch","ERR pose frame","ERR pose skew","ERR pose age unknown","ERR pose stale","ERR pose distance","ERR pose reordered","ERR pose bank full"};
  reply(messages[result]);return;
 }
 reply("ERR pose begin/put/show; see interface docs");
}
static void command(void) {
 char *words[11],**w=words;unsigned count=0;char *p=line;
 while(*p){while(*p==' ')p++;if(!*p)break;if(count==11)goto syntax;
  words[count++]=p;while(*p&&*p!=' '){if(*p>='A'&&*p<='Z')*p+='a'-'A';p++;}if(*p)*p++=0;}
 if(!count)return;
 if(!strcmp(w[0],"pose")){poseCommand(w,count);return;}
 if(count==3&&!strcmp(w[0],"geometry")&&!strcmp(w[1],"source")) {
  mulDelaySource_t source;
  if(!strcmp(w[2],"twr"))source=mulDelayTwr;else if(!strcmp(w[2],"external"))source=mulDelayExternal;else if(!strcmp(w[2],"none"))source=mulDelayNone;else {reply("ERR geometry source twr/external/none");return;}
  if(!cfgGeometrySourceWrite(source)){reply("ERR source requires RF OFF/verified storage");return;}
  mulGeometrySelect(source);reply("SAVED geometry source; poses are volatile");return;
 }
 if(!strcmp(w[0],"plan")){planCommand(w,count);return;}
 const mulStoredPlan_t *selectedPlan=NULL;
 if(cfgMulPlanCached(&selectedPlan)==mulPlanStored&&selectedPlan->selected&&
    ((!strcmp(w[0],"cfg")&&count>1&&((!strcmp(w[1],"set")&&(count<3||strncmp(w[2],"clock_",6)))||(!strcmp(w[1],"roster")&&count>2&&!strcmp(w[2],"set"))))||
     !strcmp(w[0],"role")||!strcmp(w[0],"reference")||!strcmp(w[0],"follower")||!strcmp(w[0],"addr")||!strcmp(w[0],"mode")||!strcmp(w[0],"mul"))) {
  reply("ERR selected plan; use plan commands/use off");return;
 }
 bool legacy=!strcmp(w[0],"mul");if(legacy){count--;w++;if(!count)goto syntax;}
 uint8_t node=0,role=2,ref=0x84,fol=0x85,mode=255,value;
 bool valid=saved(&node,&role,&ref,&fol);cfgReadU8(cfgMode,&mode);
 if(count==1) {
  if(!strcmp(w[0],"help")){page=help;pageIndex=0;return;}
  if(!strcmp(w[0],"console")){session=true;reply("Line console; legacy returns to old menu");return;}
  if(!strcmp(w[0],"legacy")){session=false;reply("Legacy character menu");return;}
  if(!strcmp(w[0],"show")){page=show;pageIndex=0;return;}
  if(!strcmp(w[0],"status")){page=status;pageIndex=0;return;}
  if(!strcmp(w[0],"diag")){page=diag;pageIndex=0;return;}
  if(!strcmp(w[0],"save")){reply(cfgStorageReady()?"Autosaved; no additional EEPROM write":"ERR EEPROM durability unconfirmed");return;}
  if(!strcmp(w[0],"off")){reply(rfOff()?"RF OFF requested; selected mode retained":"RF OFF requested; EEPROM unconfirmed");return;}
  if(!strcmp(w[0],"on")) {
   if(!uwbControlCanEnable()){reply("ERR on blocked: restart/config required");return;}
   if(!cfgWriteU8(cfgRfEnable,1)){uwbSetEnabled(false);reply("RF OFF requested; EEPROM unconfirmed");return;}
   reply(uwbSetEnabled(true)?"RF ON requested":"ERR on blocked; inspect status");return;
  }
 }
 if(count==2&&!strcmp(w[0],"clock")&&!strcmp(w[1],"show")){page=clockPage;pageIndex=0;return;}
 if(count==4&&!strcmp(w[0],"cfg")&&!strcmp(w[1],"set")&&
    (!strcmp(w[2],"clock_estimator")||!strcmp(w[2],"clock_compensate"))) {
  if(uwbControlRequested()||uwbControlRunning()){reply("ERR clock requires RF OFF");return;}
  mulClockFitConfig_t spec;if(!cfgMulClockRead(&spec)){reply("ERR invalid stored clock settings");return;}
  if(!strcmp(w[2],"clock_estimator")) {
   if(!strcmp(w[3],"median")||!strcmp(w[3],"median_5"))spec.estimator=0;
   else if(!strcmp(w[3],"kalman"))spec.estimator=2;
   else {reply("ERR estimator: median_5/kalman");return;}
  }else {
   if(!strcmp(w[3],"on"))spec.compensate=1;
   else if(!strcmp(w[3],"off"))spec.compensate=0;
   else {reply("ERR clock_compensate: on/off");return;}
  }
  reply(cfgMulClockWrite(&spec)?"SAVED clock; ON applies":"ERR clock/EEPROM unconfirmed");return;
 }
 if(count>=3&&!strcmp(w[0],"cfg")&&!strcmp(w[1],"roster")) {
  mulSchedule_t schedule;
  if(!cfgMulScheduleRead(node,&schedule)){reply("ERR invalid stored schedule");return;}
  if(count==3&&!strcmp(w[2],"show")){page=peers;pageIndex=0;return;}
  if(count<5||strcmp(w[2],"set")){reply("ERR cfg roster set id0 id1 [id2..id7]");return;}
  if(uwbControlRequested()||uwbControlRunning()){reply("ERR roster requires RF OFF acknowledged");return;}
  schedule.count=(uint8_t)(count-3);memset(schedule.ids,0,sizeof(schedule.ids));
  for(unsigned i=0;i<schedule.count;i++)if(!id(w[3+i],&schedule.ids[i])){reply("ERR invalid roster identity");return;}
  if(schedule.profile!=MUL_PROFILE_EXPERIMENTAL)schedule.profile=schedule.count==2?MUL_PROFILE_LEGACY:MUL_PROFILE_MULTI;
  const char *error=mulScheduleValidate(&schedule);
  if(error){reply(error);return;}
  reply(cfgMulScheduleWrite(node,&schedule)?"SAVED roster; ON applies; mode5 only for multi":"ERR roster/EEPROM unchanged or unconfirmed");return;
 }
 if(count==2&&!strcmp(w[0],"cfg")&&!strcmp(w[1],"show")){page=show;pageIndex=0;return;}
 if(count==4&&!strcmp(w[0],"cfg")&&!strcmp(w[1],"set")&&
    (!strcmp(w[2],"tx_lead_us")||!strcmp(w[2],"rx_budget_us")||!strcmp(w[2],"airtime_us")||!strcmp(w[2],"guard_us")||!strcmp(w[2],"alr_alr_twr")||!strcmp(w[2],"twr_reply_ms"))) {
  if(uwbControlRequested()||uwbControlRunning()){reply("ERR timing requires RF OFF acknowledged");return;}
  mulSchedule_t spec;if(!cfgMulScheduleRead(node,&spec)||spec.profile!=MUL_PROFILE_EXPERIMENTAL){reply("ERR select profile experimental first");return;}
  if(!strcmp(w[2],"alr_alr_twr")) {
   if(!strcmp(w[3],"on"))spec.rangingFlags|=MUL_RANGING_ALR_ALR;
   else if(!strcmp(w[3],"off"))spec.rangingFlags&=~MUL_RANGING_ALR_ALR;
   else {reply("ERR alr_alr_twr: on/off");return;}
  }else if(!strcmp(w[2],"twr_reply_ms")) {
   unsigned code=!strcmp(w[3],"20")?0:!strcmp(w[3],"10")?2:!strcmp(w[3],"5")?4:255;
   if(code==255){reply("ERR twr_reply_ms:20/10/5");return;}
   spec.rangingFlags=(spec.rangingFlags&~MUL_RANGING_REPLY_MASK)|code;
  }else {
   uint32_t n;if(!revision(w[3],&n)||n>UINT16_MAX){reply("ERR invalid timing number");return;}
   if(!strcmp(w[2],"tx_lead_us"))spec.txLeadUs=(uint16_t)n;
   else if(!strcmp(w[2],"rx_budget_us"))spec.rxBudgetUs=(uint16_t)n;
   else if(!strcmp(w[2],"airtime_us"))spec.airtimeUs=(uint16_t)n;
   else {if(n%100||n>10000){reply("ERR guard_us: 100..10000, step100");return;}spec.guard100us=(uint8_t)(n/100);}
  }
  const char *error=mulScheduleValidate(&spec);if(error){reply(error);return;}
  reply(cfgMulScheduleWrite(node,&spec)?"SAVED experimental timing; ON applies":"ERR timing/EEPROM unconfirmed");return;
 }
 if(count==4&&!strcmp(w[0],"cfg")&&!strcmp(w[1],"set")&&
    (!strcmp(w[2],"profile")||!strcmp(w[2],"schedule_revision")||!strcmp(w[2],"cycle_ms"))) {
  if(uwbControlRequested()||uwbControlRunning()){reply("ERR schedule requires RF OFF acknowledged");return;}
  mulSchedule_t spec;if(!cfgMulScheduleRead(node,&spec)){reply("ERR invalid stored schedule");return;}
  if(!strcmp(w[2],"profile")) {
   if(!strcmp(w[3],"experimental")){if(spec.profile!=MUL_PROFILE_EXPERIMENTAL)mulScheduleExperimental(&spec);}
   else {
    if(!strcmp(w[3],"legacy"))spec.profile=MUL_PROFILE_LEGACY;
    else if(!strcmp(w[3],"multi"))spec.profile=MUL_PROFILE_MULTI;
    else {reply("ERR profile: legacy/multi/experimental");return;}
    spec.txLeadUs=spec.rxBudgetUs=spec.airtimeUs=spec.guard100us=spec.rangingFlags=0;
   }
  }else if(!strcmp(w[2],"cycle_ms")) {
   uint32_t n;if(!revision(w[3],&n)||n>2000){reply("ERR cycle_ms: 100..2000");return;}spec.periodMs=(uint16_t)n;
  }else if(!revision(w[3],&spec.revision)){reply("ERR revision: 1..4294967295");return;}
  const char *error=mulScheduleValidate(&spec);if(error){reply(error);return;}
  reply(cfgMulScheduleWrite(node,&spec)?"SAVED schedule; ON applies":"ERR schedule/EEPROM unconfirmed");return;
 }
 if(count==4&&!strcmp(w[0],"cfg")&&!strcmp(w[1],"set")&&!strcmp(w[2],"slot_ms")) {
  uint32_t slot;if(!revision(w[3],&slot)||slot>UINT16_MAX){reply("ERR slot_ms: 1..65535");return;}
  if(uwbControlRequested()||uwbControlRunning()){reply("ERR slot_ms requires RF OFF acknowledged");return;}
  if(!uwbControlCanEnable()){reply("ERR slot_ms blocked: restart/config required");return;}
  mulSchedule_t spec;if(!cfgMulScheduleRead(node,&spec)){reply("ERR invalid schedule");return;}
  spec.slotMs=(uint16_t)slot;const char *error=mulScheduleValidate(&spec);if(error){reply(error);return;}
  if(!cfgMulSlotWrite(slot)){reply("ERR slot EEPROM unconfirmed; RF remains off");return;}
  reply(uwbControlRequestSlot(slot)?"SAVED slot_ms; ON activates; no restart":"ERR slot activation; RF remains off");return;
 }
 if(count==2&&!strcmp(w[0],"peer")&&!strcmp(w[1],"list")){page=peers;pageIndex=0;return;}
 if(!strcmp(w[0],"cal")&&((count==2&&!strcmp(w[1],"reset"))||(count==4&&!strcmp(w[1],"set")&&!strcmp(w[2],"offset_mm"))||(count==6&&!strcmp(w[1],"set")&&!strcmp(w[2],"tx")&&!strcmp(w[4],"rx")))) {
  uint32_t tx=MUL_ANTENNA_TX_DTU,rx=MUL_ANTENNA_RX_DTU;
  if(count==4){uint16_t a,b;if(!mulAntennaOffsetParse(w[3],MUL_ANTENNA_TX_DTU,MUL_ANTENNA_RX_DTU,&a,&b)){reply("ERR signed offset/register range");return;}tx=a;rx=b;}
  if(count==6&&(!decimal(w[3],&tx)||!decimal(w[5],&rx)||tx>65535||rx>65535)){reply("ERR antenna: 0..65535 DTU");return;}
  if(uwbControlRequested()||uwbControlRunning()){reply("ERR cal requires RF OFF acknowledged");return;}
  reply(cfgAntennaWrite(tx,rx)?"SAVED antenna; ON applies":"ERR antenna EEPROM; RF blocked");return;
 }
 if(count==2&&!strcmp(w[0],"cal")&&!strcmp(w[1],"show")){page=cal;pageIndex=0;return;}
 if(count==2&&!strcmp(w[0],"diag")&&!strcmp(w[1],"reset")) {
  reply(uwbControlResetCounters()?"Radio event counters reset; models retained":"ERR diag reset requires RF OFF acknowledged");return;
 }
 if(!strcmp(w[0],"wifi")||!strcmp(w[0],"tlm")||!strcmp(w[0],"cal")||!strcmp(w[0],"peer")) {
  reply("ERR unsupported on DW1000; see help");return;
 }
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
 reply("ERR diagnostic build overrides mode/role/ID");return;
#endif
 if(!valid&&!(legacy&&count==4&&!strcmp(w[0],"setup"))){reply("ERR invalid roster; use :mul setup");return;}
 if(count==2&&!strcmp(w[0],"mode")) {
#if MUL_ONLY
  value=255;
#else
  value=!strcmp(w[1],"anchor")?0:!strcmp(w[1],"tag")?1:!strcmp(w[1],"sniffer")?2:
    !strcmp(w[1],"tdoa2")?3:!strcmp(w[1],"tdoa3")?4:255;
#endif
  if(!strcmp(w[1],"hybrid")) {
   if(role==3&&MUL_EXPERIMENTAL_STATIONARY_RLR_RATE){reply("ERR static image; use mode hybrid_static");return;}
   value=role==3?6:role<2?5:255;
  }
  if(!strcmp(w[1],"hybrid_static")&&role==3&&MUL_EXPERIMENTAL_STATIONARY_RLR_RATE)value=6;
  if(value==255||(role==3&&value!=6)){reply("ERR mode/role; static requires opt-in build");return;}
  result(cfgWriteU8(cfgMode,value));return;
 }
 /* An explicit ordered roster owns reference/follower assignment. Old aliases
  * must not successfully mutate a shadow pair while leaving this roster active. */
 uint8_t rosterBytes;
 if(cfgFieldSize(cfgMulSchedule,&rosterBytes)&&
    (!strcmp(w[0],"reference")||!strcmp(w[0],"follower")||
     (legacy&&!strcmp(w[0],"setup"))||
     (count==2&&!strcmp(w[0],"role")&&(!strcmp(w[1],"ref")||!strcmp(w[1],"follower"))))) {
  reply("ERR explicit roster: use cfg roster set");return;
 }
 if(count==2&&(!strcmp(w[0],"addr")||!strcmp(w[0],"address"))) {
  if(!id(w[1],&value))goto syntax;
  mulSchedule_t spec;
  if(cfgFieldSize(cfgMulSchedule,&rosterBytes)&&(!cfgMulScheduleRead(node,&spec)||mulScheduleIndex(&spec,value)<0)) {
   reply("ERR address absent from explicit roster");return;
  }
  result(cfgMulChangeAddress(value));return;
 }
 uint8_t oldRole=role;
 if(count==4&&legacy&&!strcmp(w[0],"setup")) {
  role=!strcmp(w[1],"ref")?0:!strcmp(w[1],"follower")?1:!strcmp(w[1],"disabled")?2:255;
  if(!id(w[2],&ref)||!id(w[3],&fol))goto syntax;
 } else if(count==2&&!strcmp(w[0],"role")) {
  role=!strcmp(w[1],"ref")?0:!strcmp(w[1],"follower")?1:!strcmp(w[1],"disabled")||!strcmp(w[1],"standalone")?2:!strcmp(w[1],"rlr")?3:255;
  if(role==0){if(oldRole==1&&fol==node)fol=ref;ref=node;}
  if(role==1){if(oldRole==0&&ref==node)ref=fol;fol=node;}
 } else if(count==2&&!strcmp(w[0],"reference")){if(!id(w[1],&ref))goto syntax;}
 else if(count==2&&!strcmp(w[0],"follower")){if(!id(w[1],&fol))goto syntax;}
 else goto syntax;
 /* Validate BEFORE an operational stop or any persistent side effects. */
 if(role>3||ref==fol||ref==255||fol==255||(role==0&&ref!=node)||(role==1&&fol!=node)||
    (role==3&&(node==ref||node==fol))){reply("ERR role/roster conflict; unchanged");return;}
 if(count==2&&!strcmp(w[0],"role")&&!strcmp(w[1],"standalone")&&oldRole==3) {
  if(!rfOff()){reply("RF OFF requested; EEPROM unconfirmed");return;}
  if(!cfgWriteU8(cfgMode,255)){result(false);return;}
 }
 if(role==3) {
  if(!rfOff()){reply("RF OFF requested; EEPROM unconfirmed");return;}
  if(!cfgMulWrite(role,ref,fol)){result(false);return;}
  result(cfgWriteU8(cfgMode,MODE_MUL_RLR));return;
 }
 result(cfgMulWrite(role,ref,fol));return;
syntax:reply("ERR syntax; :help or :mul show");
}
void mulConsoleReset(void) {mulGeometryClear();collecting=overflow=skipLf=session=false;used=page=pageIndex=0;message=NULL;}
bool mulConsoleByte(char ch) {
 if(skipLf){skipLf=false;if(ch=='\n')return true;}
 if(!collecting){if(ch!=':'&&!session)return false;collecting=true;used=0;overflow=false;if(ch==':')return true;}
 if(ch=='\n'||ch=='\r') {
  skipLf=ch=='\r';
  if(overflow)reply("ERR line too long/invalid; discarded");else {line[used]=0;if(used)command();}
  collecting=false;used=0;return true;
 }
 if(ch=='\b'||ch==127){if(used&&!overflow)used--;return true;}
 if((unsigned char)ch<32||ch==':'){overflow=true;return true;}
 if(used+1>=sizeof(line))overflow=true;else if(!overflow)line[used++]=ch;
 return true;
}
bool mulConsolePoll(void) {
 if(!page)return true;
 static const char *const helps[]={
  "UWB: :command newline; :console for lines",
  "help show status on off save legacy",
  "geometry source twr/external/none (RF OFF)",
  "pose begin clock epoch frame skew_ms",
  "pose put HEX80; pose show (USB observations)",
#if MUL_ONLY
  "MUL-only: mode hybrid; role ref/follower/rlr",
#else
  "mode anchor/tag/sniffer/tdoa2/tdoa3/hybrid",
#endif
  "role ref/follower/rlr/standalone; addr id",
  "reference id; follower id; peer list",
  "cal show|set tx N rx N|set offset_mm N|reset",
  "wifi/tlm unsupported; cal requires OFF",
  "Autosave; restart applies; off retains mode",
  "hybrid_static: stationary opt-in build only",
  "cfg set slot_ms number; compat:10/50/500",
  "cfg roster show/set id0 id1 [..id7]",
  "cfg set profile legacy/multi; RF OFF",
  "cfg set cycle_ms number; compat:1000",
  "cfg set schedule_revision 1..4294967295",
  "cfg set clock_estimator median_5/kalman",
  "clock show; cfg set clock_compensate on/off",
  "cfg set clock_compensate on/off; RF OFF",
  "cfg set profile experimental; HW unverified",
  "cfg set tx_lead_us/rx_budget_us number",
  "cfg set airtime_us/guard_us number",
  "cfg set alr_alr_twr on/off; RF OFF",
  "cfg set twr_reply_ms 20/10/5; RF OFF",
  "plan init/show; plan use on/off; RF OFF",
  "plan alrs id0 id1...; plan rlrs id0...",
  "plan set aa/ar on/off; aa_millihz/ar_millihz",
  "plan set reply_ms/util_percent",
  "plan set aa_start_delay_us <microseconds>",
  "plan set policy fair/freshness",
  "plan set aa_priority/ar_priority 0..255",
  "plan set slot_ms/cycle_ms/revision",
  "plan set lead_us/process_us/air_us/guard_us"};
 uint8_t node=0,role=2,ref=0x84,fol=0x85,mode=255;
 bool valid=saved(&node,&role,&ref,&fol),last=true,enabled=false;
 if(page==posePage) {
  static const char *const states[]={"AVAILABLE","MISSING","INVALID","DOMAIN","FRAME","SKEW","AGE_UNKNOWN","STALE","DISTANCE","REORDERED","FULL"};
  static const char *const sources[]={"twr","external","none"};
  mulPoseGeometry_t g;unsigned n;mulPoseStatus_t state=mulGeometrySnapshot(poseNow(),&g,&n);
  if(pageIndex==0)snprintf(line,sizeof(line),"POSE src=%s n=%u state=%s",sources[mulGeometrySource()],n,states[state]);
  else if(state!=mulPoseAvailable){snprintf(line,sizeof(line),"POSE correction=NA uncertainty=UNKNOWN");last=true;}
  else if(pageIndex==1)snprintf(line,sizeof(line),"POSE %u>%u range_mm=%ld age_ms=%lu",g.reference,g.follower,(long)(g.distanceMm+0.5),(unsigned long)(poseNow()-g.measuredMs));
  else {snprintf(line,sizeof(line),"POSE covariance=%s bound=UNKNOWN",g.covarianceKnown?"provided":"UNKNOWN");last=true;}
 }
 if(page==answer)snprintf(line,sizeof(line),"%s",message);
 if(page==help){snprintf(line,sizeof(line),"%s",helps[pageIndex]);last=pageIndex+1==sizeof(helps)/sizeof(*helps);}
 if(page==planPage) {
  const mulStoredPlan_t *stored=NULL;mulPlanStoreResult_t state=cfgMulPlanCached(&stored);
  if(state!=mulPlanStored)snprintf(line,sizeof(line),"PLAN %s",state==mulPlanAbsent?"absent; RF OFF then plan init":"storage unconfirmed");
  else {
   const mulRangingPlan_t *p=&stored->plan;mulRangingBudget_t b;const char *error=mulRangingPlanValidate(p,&b);
   if(error)snprintf(line,sizeof(line),"ERR %s",error);
   else {
    unsigned base=6+p->broadcast.count+p->rlrCount,n=mulRangingPlanCount(p,1),total=base+2+2*n;last=pageIndex+1>=total;
    if(pageIndex==0)snprintf(line,sizeof(line),"PLAN selected=%u gen=%lu codec=%u p=%u",stored->selected,(unsigned long)stored->generation,mulRangingPlanVersion(p),p->policy);
    else if(pageIndex==1)snprintf(line,sizeof(line),"PLAN hash=%08lX ALR=%u RLR=%u",(unsigned long)mulRangingPlanFingerprint(p),p->broadcast.count,p->rlrCount);
    else if(pageIndex==2)snprintf(line,sizeof(line),"PLAN slot=%u cycle=%u reply=%u",p->broadcast.slotMs,p->broadcast.periodMs,p->replyMs);
    else if(pageIndex==3)snprintf(line,sizeof(line),"PLAN AA=%u AR=%u util=%u",!!(p->enabled&MUL_PLAN_AA),!!(p->enabled&MUL_PLAN_AR),p->maxUtilization);
    else if(pageIndex==4)snprintf(line,sizeof(line),"RATE AA=%u/%u AR=%u/%u",p->aaMilliHz,p->aaPriority,p->arMilliHz,p->arPriority);
    else if(pageIndex==5)snprintf(line,sizeof(line),"PLAN aa_start_delay_us=%lu",(unsigned long)p->aaStartDelayUs);
    else if(pageIndex<6+p->broadcast.count){unsigned i=pageIndex-6;snprintf(line,sizeof(line),"ALR %u id=%u offset=%u ms",i,p->broadcast.ids[i],i*p->broadcast.slotMs);}
    else if(pageIndex<base){unsigned i=pageIndex-6-p->broadcast.count;snprintf(line,sizeof(line),"RLR %u id=%u",i,p->rlrs[i]);}
    else if(pageIndex==base)snprintf(line,sizeof(line),"AIR reserve=%lu capacity=%u peak=%u",(unsigned long)b.reservationUs,b.capacity,b.peakOperations);
    else if(pageIndex==base+1)snprintf(line,sizeof(line),"PREVIEW cycle=1 units=reference_us");
    else {unsigned i=(pageIndex-base-2)/2;mulRangingOperation_t op;(void)mulRangingPlanOperation(p,1,i,&op);
     if((pageIndex-base)%2==0)snprintf(line,sizeof(line),"OP%u %c %u>%u %lu..%luus",i,op.kind==MUL_PLAN_AA?'A':'R',op.initiator,op.responder,(unsigned long)op.startUs,(unsigned long)op.endUs);
     else snprintf(line,sizeof(line),"POLL op=%u at=%lu us",i,(unsigned long)op.pollUs);
    }
   }
  }
 }
 if(page==peers) {
  mulSchedule_t schedule;
  if(!cfgMulScheduleRead(node,&schedule))snprintf(line,sizeof(line),"ERR invalid schedule");
  else if(pageIndex==0){snprintf(line,sizeof(line),"SCHEDULE profile=%u count=%u slot_ms=%u",schedule.profile,schedule.count,schedule.slotMs);last=false;}
  else if(pageIndex==1){snprintf(line,sizeof(line),"SCHEDULE cycle_ms=%u rev=%lu",schedule.periodMs,(unsigned long)schedule.revision);last=false;}
  else if(pageIndex==2){snprintf(line,sizeof(line),"SCHEDULE hash=%08lX ref=0x%02X",(unsigned long)mulScheduleFingerprint(&schedule),schedule.ids[0]);last=false;}
  else if(pageIndex<=schedule.count+2){unsigned slot=pageIndex-3;snprintf(line,sizeof(line),"ALR slot=%u id=0x%02X offset_ms=%u",slot,schedule.ids[slot],mulScheduleOffsetMs(&schedule,slot));last=pageIndex==schedule.count+2&&schedule.profile!=MUL_PROFILE_EXPERIMENTAL;}
 }
 if(page==peers&&pageIndex>0) {
  mulSchedule_t spec;if(cfgMulScheduleRead(node,&spec)&&spec.profile==MUL_PROFILE_EXPERIMENTAL) {
   if(pageIndex==spec.count+3){snprintf(line,sizeof(line),"TIMING lead=%u process=%u air=%u us",spec.txLeadUs,spec.rxBudgetUs,spec.airtimeUs);last=false;}
   else if(pageIndex==spec.count+4)snprintf(line,sizeof(line),"TIMING guard=%u AA=%u reply=%u experimental=1",spec.guard100us*100,mulScheduleAlrRanging(&spec),mulScheduleReplyMs(&spec));
  }
 }
 if(page==clockPage) {
  mulClockFitConfig_t c;
  if(!cfgMulClockRead(&c))snprintf(line,sizeof(line),"ERR invalid stored clock settings");
  else if(pageIndex==0){snprintf(line,sizeof(line),"CLOCK method=%u history=5",c.estimator);last=false;}
  else {mulSchedule_t spec;if(cfgMulScheduleRead(node,&spec))c.periodMs=spec.periodMs;snprintf(line,sizeof(line),"CLOCK period=%u comp=%u age=%u speed=%d v=%u",c.periodMs,c.compensate,MUL_PROP_MAX_AGE_MS,MUL_RELATIVE_SPEED_MM_S,MUL_PROP_MAX_AGE_MS==1000?1:2);}
 }
 if(page==show) {
  if(!valid)snprintf(line,sizeof(line),"ERR invalid roster; EEPROM retained");
  else if(pageIndex==0){snprintf(line,sizeof(line),"SAVED addr=0x%02X role=%s",node,roleName(role));last=false;}
  else if(pageIndex==1){snprintf(line,sizeof(line),"reference=0x%02X follower=0x%02X",ref,fol);last=page==peers;}
  else if(pageIndex==2){uint16_t slot=0;bool ok=cfgMulSlotRead(&slot);snprintf(line,sizeof(line),"SAVED slot_ms=%u verified=%u",slot,ok);last=false;}
  else {cfgReadU8(cfgMode,&mode);bool ok=cfgRfEnabled(&enabled);
   snprintf(line,sizeof(line),"mode=%u rf=%u verified=%u restart=%u",mode,enabled,ok,cfgPendingRestart());}
 }
 if(page==status) {
  const uwbConfig_t *c=uwbGetConfig();
  if(pageIndex==0){snprintf(line,sizeof(line),"BOOT mode=%u addr=0x%02X alr_role=%d",c->mode,c->address[0],c->mulRole);last=false;}
  else if(pageIndex==1){snprintf(line,sizeof(line),"OVERRIDE phy=%u diag=%u ads=%u",PHY_INTEROP_ROLE,MUL_DIAG_ROLE,MUL_TWR_ROLE);last=false;}
  else if(pageIndex==2){snprintf(line,sizeof(line),"ACTIVE slot_ms=%u",mulNetworkSlotMs(&c->mulNetwork));last=false;}
  else snprintf(line,sizeof(line),"RF requested=%u running=%u restart=%u",uwbControlRequested(),uwbControlRunning(),cfgPendingRestart());
 }
 if(page==diag) {
  if(pageIndex<5){snprintf(line,sizeof(line),"RADIO event=%u count=%lu",pageIndex,(unsigned long)uwbControlCount(pageIndex));last=pageIndex==4;}
 }
 if(page==cal) {
  uint16_t tx,rx;mulAntennaGet(&tx,&rx);last=false;
  if(pageIndex==0)snprintf(line,sizeof(line),"CAL EEPROM/default accuracy=UNKNOWN");
  else if(pageIndex==1)snprintf(line,sizeof(line),"BASE TX=%u RX=%u DTU",MUL_ANTENNA_TX_DTU,MUL_ANTENNA_RX_DTU);
  else if(pageIndex==2)snprintf(line,sizeof(line),"ALR saved TX=%u RX=%u DTU",tx,rx);
  else if(pageIndex==3)snprintf(line,sizeof(line),"offset_mm=MANUAL/CUSTOM (not stored)");
  else if(pageIndex==4){int32_t um;
   if(mulAntennaOffsetEquivalentUm(MUL_ANTENNA_TX_DTU,MUL_ANTENNA_RX_DTU,tx,rx,&um)){
    uint32_t magnitude=um<0?-um:um;snprintf(line,sizeof(line),"equivalent_mm=%s%lu.%03lu",um<0?"-":"",(unsigned long)(magnitude/1000),(unsigned long)(magnitude%1000));
   }else snprintf(line,sizeof(line),"equivalent_mm=MANUAL/CUSTOM");
  }else {const uwbConfig_t *c=uwbGetConfig();snprintf(line,sizeof(line),"BOOT power smart=%u forced=%u",c->smartPower,c->forceTxPower);last=true;}
 }
#ifdef USE_FTDI_UART
 puts(line);
#else
 unsigned length=strlen(line);
 /* Leave room for CRLF without a second buffer or unbounded TX wait. */
 if(length+2>48U){line[45]=0;length=45;}
 line[length++]='\r';line[length++]='\n';
 if(!usbcommTryWrite(line,length))return false;
#endif
 if(last)page=none;else pageIndex++;
 return !page;
}
