/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_console.h"
#include "cfg.h"
#include "uwb_control.h"
#include "mul_rx.h"
#include "mul_localization.h"
#include "phy_interop_dw.h"
#include "mul_diag_dw.h"
#include "mul_ads_dw.h"
#include <usbcomm.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static bool collecting,overflow,skipLf,session;
static uint8_t used,page,pageIndex;
static const char *message;
/* Input and paged output reuse the existing48-byte allocation. */
static char line[48];
enum {none,answer,help,show,status,peers,diag,cal};
static void reply(const char *text) {message=text;page=answer;pageIndex=0;}
static bool id(const char *s,uint8_t *value) {
 unsigned n=0,base=10;if(s[0]=='0'&&s[1]=='x'){base=16;s+=2;}
 if(!*s)return false;
 for(;*s;s++){unsigned d=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='f'?*s-'a'+10:99;
  if(d>=base||n>(255-d)/base)return false;
  n=n*base+d;}
 *value=n;return true;
}
static const char *roleName(uint8_t role) {return role==0?"ref":role==1?"follower":role==3?"rlr":"disabled";}
static bool saved(uint8_t *node,uint8_t *role,uint8_t *ref,uint8_t *fol) {
 return cfgReadU8(cfgAddress,node)&&cfgMulRead(*node,role,ref,fol);
}
static void result(bool ok) {reply(ok?"SAVED; restart to activate":"ERR EEPROM/roster unconfirmed; inspect reboot");}
static bool rfOff(void) {
 uwbSetEnabled(false);
 return cfgWriteU8(cfgRfEnable,0);
}
static void command(void) {
 char *words[6],**w=words;unsigned count=0;char *p=line;
 while(*p){while(*p==' ')p++;if(!*p)break;if(count==6)goto syntax;
  words[count++]=p;while(*p&&*p!=' '){if(*p>='A'&&*p<='Z')*p+='a'-'A';p++;}if(*p)*p++=0;}
 if(!count)return;
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
 if(count==2&&!strcmp(w[0],"peer")&&!strcmp(w[1],"list")){page=peers;pageIndex=0;return;}
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
  value=!strcmp(w[1],"anchor")?0:!strcmp(w[1],"tag")?1:!strcmp(w[1],"sniffer")?2:
    !strcmp(w[1],"tdoa2")?3:!strcmp(w[1],"tdoa3")?4:255;
  if(!strcmp(w[1],"hybrid")) {
   if(role==3&&MUL_EXPERIMENTAL_STATIONARY_RLR_RATE){reply("ERR static image; use mode hybrid_static");return;}
   value=role==3?6:role<2?5:255;
  }
  if(!strcmp(w[1],"hybrid_static")&&role==3&&MUL_EXPERIMENTAL_STATIONARY_RLR_RATE)value=6;
  if(value==255||(role==3&&value!=6)){reply("ERR mode/role; static requires opt-in build");return;}
  result(cfgWriteU8(cfgMode,value));return;
 }
 if(count==2&&(!strcmp(w[0],"addr")||!strcmp(w[0],"address"))) {
  if(!id(w[1],&value))goto syntax;
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
void mulConsoleReset(void) {collecting=overflow=skipLf=session=false;used=page=pageIndex=0;message=NULL;}
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
  "mode anchor/tag/sniffer/tdoa2/tdoa3/hybrid",
  "role ref/follower/rlr/standalone; addr id",
  "reference id; follower id; peer list",
  "cal show; diag [reset]; :mul aliases retained",
  "wifi/tlm and runtime cal unsupported",
  "Autosave; restart applies; off retains mode",
  "hybrid_static: stationary opt-in build only"};
 uint8_t node=0,role=2,ref=0x84,fol=0x85,mode=255;
 bool valid=saved(&node,&role,&ref,&fol),last=true,enabled=false;
 if(page==answer)snprintf(line,sizeof(line),"%s",message);
 if(page==help){snprintf(line,sizeof(line),"%s",helps[pageIndex]);last=pageIndex+1==sizeof(helps)/sizeof(*helps);}
 if(page==show||page==peers) {
  if(!valid)snprintf(line,sizeof(line),"ERR invalid roster; EEPROM retained");
  else if(pageIndex==0){snprintf(line,sizeof(line),"SAVED addr=0x%02X role=%s",node,roleName(role));last=false;}
  else if(pageIndex==1){snprintf(line,sizeof(line),"reference=0x%02X follower=0x%02X",ref,fol);last=page==peers;}
  else {cfgReadU8(cfgMode,&mode);bool ok=cfgRfEnabled(&enabled);
   snprintf(line,sizeof(line),"mode=%u rf=%u verified=%u restart=%u",mode,enabled,ok,cfgPendingRestart());}
 }
 if(page==status) {
  const uwbConfig_t *c=uwbGetConfig();
  if(pageIndex==0){snprintf(line,sizeof(line),"BOOT mode=%u addr=0x%02X alr_role=%d",c->mode,c->address[0],c->mulRole);last=false;}
  else if(pageIndex==1){snprintf(line,sizeof(line),"OVERRIDE phy=%u diag=%u ads=%u",PHY_INTEROP_ROLE,MUL_DIAG_ROLE,MUL_TWR_ROLE);last=false;}
  else snprintf(line,sizeof(line),"RF requested=%u running=%u restart=%u",uwbControlRequested(),uwbControlRunning(),cfgPendingRestart());
 }
 if(page==diag) {
  if(pageIndex<5){snprintf(line,sizeof(line),"RADIO event=%u count=%lu",pageIndex,(unsigned long)uwbControlCount(pageIndex));last=pageIndex==4;}
 }
 if(page==cal) {
  if(pageIndex==0){snprintf(line,sizeof(line),"CAL runtime=UNSUPPORTED accuracy=UNKNOWN");last=false;}
  else if(pageIndex==1){snprintf(line,sizeof(line),"ALR build TX=%u RX=%u DTU",MUL_ANTENNA_TX_DTU,MUL_ANTENNA_RX_DTU);last=false;}
  else {const uwbConfig_t *c=uwbGetConfig();snprintf(line,sizeof(line),"BOOT power smart=%u forced=%u",c->smartPower,c->forceTxPower);}
 }
#ifdef USE_FTDI_UART
 puts(line);
#else
 unsigned length=strlen(line);
 /* Leave room for CRLF without a second buffer or unbounded TX wait. */
 if(length+2>sizeof(line)){line[sizeof(line)-3]=0;length=sizeof(line)-3;}
 line[length++]='\r';line[length++]='\n';
 if(!usbcommTryWrite(line,length))return false;
#endif
 if(last)page=none;else pageIndex++;
 return !page;
}
