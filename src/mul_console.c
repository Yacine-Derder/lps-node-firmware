/* SPDX-License-Identifier: LGPL-3.0-or-later */
#include "mul_console.h"
#include "cfg.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static bool collecting,overflow,skipLf;
static uint8_t used;
static char line[48];
static bool id(const char *s,uint8_t *value) {
 unsigned n=0,base=10;if(s[0]=='0'&&s[1]=='x'){base=16;s+=2;}
 if(!*s)return false;
 for(;*s;s++){unsigned d=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='f'?*s-'a'+10:99;
  if(d>=base||n>(255-d)/base)return false;
  n=n*base+d;}
 *value=n;return true;
}
static void command(void) {
 char *words[6];unsigned count=0;char *p=line;
 while(*p){while(*p==' '||*p=='\t')p++;if(!*p)break;if(count==6)goto syntax;
  words[count++]=p;while(*p&&*p!=' '&&*p!='\t'){if(*p>='A'&&*p<='Z')*p+='a'-'A';p++;}if(*p)*p++=0;}
 if(count<2||strcmp(words[0],"mul"))goto syntax;
 uint8_t node=0,role=2,reference=0x84,follower=0x85;cfgReadU8(cfgAddress,&node);
 bool valid=cfgMulRead(node,&role,&reference,&follower),ok=false;
 if(count==2&&!strcmp(words[1],"show")) {
  if(!valid){puts("ERR invalid MUL roster; EEPROM retained");return;}
  printf("MUL SAVED addr=0x%02X role=%s reference=0x%02X follower=0x%02X; restart applies\r\n",node,role==0?"ref":role==1?"follower":"disabled",reference,follower);return;
 }
 if(count==5&&!strcmp(words[1],"setup")) {
  role=!strcmp(words[2],"ref")?0:!strcmp(words[2],"follower")?1:!strcmp(words[2],"disabled")?2:255;
  if(!id(words[3],&reference)||!id(words[4],&follower))goto syntax;
  ok=cfgMulWrite(role,reference,follower);
 } else {
  if(!valid){puts("ERR invalid MUL roster; use :mul setup role ref_id follower_id");return;}
  if(count!=3)goto syntax;
  if(!strcmp(words[1],"address")){uint8_t address;if(!id(words[2],&address))goto syntax;ok=cfgMulChangeAddress(address);}
  else if(!strcmp(words[1],"role")){uint8_t oldRole=role;role=!strcmp(words[2],"ref")?0:!strcmp(words[2],"follower")?1:!strcmp(words[2],"disabled")?2:255;
   if(role==0){if(oldRole==1&&follower==node)follower=reference;reference=node;}
   if(role==1){if(oldRole==0&&reference==node)reference=follower;follower=node;}
   ok=cfgMulWrite(role,reference,follower);}
  else if(!strcmp(words[1],"reference")){if(!id(words[2],&reference))goto syntax;ok=cfgMulWrite(role,reference,follower);}
  else if(!strcmp(words[1],"follower")){if(!id(words[2],&follower))goto syntax;ok=cfgMulWrite(role,reference,follower);}
  else goto syntax;
 }
 puts(ok?"MUL SAVED; restart to activate":"ERR roster/capacity or EEPROM unconfirmed; restart and inspect before retry");return;
syntax:puts("MUL: :mul show | address id | role ref/follower/disabled | reference id | follower id | setup role ref_id follower_id");
}
bool mulConsoleByte(char ch) {
 if(skipLf){skipLf=false;if(ch=='\n')return true;}
 if(!collecting){if(ch!=':')return false;collecting=true;used=0;overflow=false;return true;}
 if(ch=='\n'||ch=='\r') {
  skipLf=ch=='\r';
  if(overflow)puts("ERR MUL line too long; discarded");else {line[used]=0;if(used)command();}
  collecting=false;used=0;return true;
 }
 if(ch=='\b'||ch==127){if(used&&!overflow)used--;return true;}
 if((unsigned char)ch<32||ch==':'){overflow=true;return true;}
 if(used+1>=sizeof(line))overflow=true;else if(!overflow)line[used++]=ch;
 return true;
}
