/* Execute the actual USB callbacks, packet reader and prefixed configuration
 * parser together; no duplicate RX implementation or emulated byte queue. */
#include "unity.h"
#include "cfg.h"
#include "mul_common.h"
#include "mul_console.h"
#include "uwb_control.h"
#include "../src/usbd_cdc_if.c"
#include <string.h>
static uwbConfig_t boot;
uwbConfig_t *uwbGetConfig(void){return &boot;}
bool uwbSetEnabled(bool enabled){return uwbControlRequest(enabled);}
void dwIdle(dwDevice_t *dev){(void)dev;}
void dwClearAllStatus(dwDevice_t *dev){(void)dev;}
bool usbcommTryWrite(const char *data,unsigned len){return CDC_TryWrite(data,len);}
static uint8_t nv[100];static unsigned writes,armedCount,queueCreates,txBytes;
static bool armed,connected;static unsigned critical,txSpaces=512;
USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_HandleTypeDef cdc;
void vPortEnterCritical(void){critical++;}
void vPortExitCritical(void){TEST_ASSERT_EQUAL(1,critical);critical--;}
QueueHandle_t xQueueGenericCreateStatic(UBaseType_t n,UBaseType_t width,uint8_t *storage,StaticQueue_t *q,uint8_t type) {
 (void)storage;(void)q;(void)type;queueCreates++;TEST_ASSERT_TRUE((n==512&&width==1)||(n==1&&width==0));return (QueueHandle_t)(uintptr_t)queueCreates;
}
BaseType_t xQueueGenericSend(QueueHandle_t q,const void *item,TickType_t ticks,BaseType_t position) {
 (void)q;(void)item;(void)position;TEST_ASSERT_EQUAL(0,ticks);txBytes++;return pdTRUE;
}
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t q){(void)q;return txSpaces;}
BaseType_t xQueueReceiveFromISR(QueueHandle_t q,void *item,BaseType_t *woken){(void)q;(void)item;(void)woken;return pdFALSE;}
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *d,uint8_t *p,uint16_t n){(void)d;cdc.TxBuffer=p;cdc.TxLength=n;return USBD_OK;}
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *d,uint8_t *p){(void)d;cdc.RxBuffer=p;return USBD_OK;}
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *d){(void)d;return USBD_OK;}
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *d){TEST_ASSERT_TRUE(d==&hUsbDeviceFS&&usbInit);TEST_ASSERT_FALSE(armed);armed=true;armedCount++;return USBD_OK;}
bool USBD_IsSerialConnected(void){return connected;}
bool eepromRead(int at,void *p,size_t n){TEST_ASSERT_TRUE(at>=0&&(size_t)at+n<=sizeof(nv));memcpy(p,nv+at,n);return true;}
bool eepromWrite(int at,void *p,size_t n){TEST_ASSERT_TRUE(at>=0&&(size_t)at+n<=sizeof(nv));writes++;memcpy(nv+at,p,n);return true;}
void HAL_Delay(uint32_t ms){(void)ms;}
void setUp(void) {
 memset(nv,0,sizeof(nv));uint8_t image[]={0xbc,1,0,6,0,cfgAddress,1,0x84,cfgMode,1,5};memcpy(nv,image,sizeof(image));
 for(unsigned i=0;i<sizeof(image);i++)nv[11]+=nv[i];cfgInit();mulConsoleReset();boot.mode=5;boot.address[0]=0x84;uwbControlInit(&boot);
 armed=false;connected=false;critical=queueCreates=txBytes=armedCount=writes=0;txSpaces=512;
 memset(&hUsbDeviceFS,0,sizeof(hUsbDeviceFS));memset(&cdc,0,sizeof(cdc));hUsbDeviceFS.pClassData=&cdc;
 TEST_ASSERT_EQUAL(USBD_OK,CDC_Init_FS());TEST_ASSERT_EQUAL(2,queueCreates);
 armed=true; // Class init arms the first OUT transfer after this callback.
}
void tearDown(void){TEST_ASSERT_EQUAL(0,critical);}
static void packet(const void *data,unsigned n) {
 TEST_ASSERT_TRUE(armed&&n<=64);memcpy(UserRxBufferFS,data,n);armed=false;uint32_t size=n;
 TEST_ASSERT_EQUAL(USBD_OK,CDC_Receive_FS(UserRxBufferFS,&size));
}
static unsigned drain(bool parse) {char ch;unsigned n=0;while(CDC_Read(&ch,1)){n++;if(parse){TEST_ASSERT_TRUE(mulConsoleByte(ch));while(!mulConsolePoll()){};}}return n;}
void test_usbFullPacketBackpressurePreservesEveryByteAndLegacyTx(void) {
 uint8_t data[64];for(unsigned i=0;i<64;i++)data[i]=i;
 packet(data,64);TEST_ASSERT_FALSE(armed);char out[80];
 TEST_ASSERT_EQUAL(9,CDC_Read(out,9));TEST_ASSERT_EQUAL_MEMORY(data,out,9);TEST_ASSERT_FALSE(armed);TEST_ASSERT_EQUAL(0,armedCount);
 TEST_ASSERT_EQUAL(55,CDC_Read(out+9,71));TEST_ASSERT_EQUAL_MEMORY(data,out,64);TEST_ASSERT_TRUE(armed);TEST_ASSERT_EQUAL(1,armedCount);
 TEST_ASSERT_EQUAL(0,CDC_Read(out,1));TEST_ASSERT_EQUAL(0,CDC_Read(NULL,5));TEST_ASSERT_EQUAL(0,CDC_Read(out,-1));
 TEST_ASSERT_EQUAL(6,CDC_Write("legacy",6));TEST_ASSERT_EQUAL(6,txBytes);
 packet("",0);TEST_ASSERT_TRUE(armed);TEST_ASSERT_EQUAL(2,armedCount);
}
void test_usbTerminalBulkAndFragmentedCommandsReachActualPersistentParser(void) {
 const char *first=":mul setup follower 0x23 0x84\n";packet(first,strlen(first));TEST_ASSERT_EQUAL(strlen(first),drain(true));
 uint8_t role,ref,fol;TEST_ASSERT_TRUE(cfgMulRead(0x84,&role,&ref,&fol));TEST_ASSERT_EQUAL(1,role);TEST_ASSERT_EQUAL(0x23,ref);
 packet(":mul add",8);drain(true);TEST_ASSERT_TRUE(armed);
 const char *rest="ress 0x42\r\n:mul show\n";packet(rest,strlen(rest));drain(true);
 uint8_t address;TEST_ASSERT_TRUE(cfgReadU8(cfgAddress,&address));TEST_ASSERT_EQUAL(0x42,address);
 TEST_ASSERT_TRUE(cfgMulRead(address,&role,&ref,&fol));TEST_ASSERT_EQUAL(1,role);TEST_ASSERT_EQUAL(0x23,ref);TEST_ASSERT_EQUAL(0x42,fol);
 unsigned before=writes;uint8_t large[64];memset(large,'a',64);large[0]=':';packet(large,64);drain(true);packet("a\n",2);drain(true);TEST_ASSERT_EQUAL(before,writes);
 packet("h",1);char ch;TEST_ASSERT_EQUAL(1,CDC_Read(&ch,1));TEST_ASSERT_FALSE(mulConsoleByte(ch));TEST_ASSERT_EQUAL('h',ch);
}
void test_usbDisconnectAndInvalidPacketInvalidatePartialCommandGeneration(void) {
 packet(":mul address 0x",15);drain(true);uint32_t epoch=CDC_ReceiveGeneration();
 TEST_ASSERT_EQUAL(USBD_OK,CDC_DeInit_FS());TEST_ASSERT_NOT_EQUAL(epoch,CDC_ReceiveGeneration());char ch;TEST_ASSERT_EQUAL(0,CDC_Read(&ch,1));
 mulConsoleReset();armed=false;TEST_ASSERT_EQUAL(USBD_OK,CDC_Init_FS());armed=true;
 const char *show=":mul show\n";packet(show,strlen(show));drain(true);TEST_ASSERT_EQUAL(0,writes);
 packet(":mul a",6);epoch=CDC_ReceiveGeneration();connected=true;
 TEST_ASSERT_EQUAL(USBD_OK,CDC_Control_FS(CDC_SET_CONTROL_LINE_STATE,NULL,0));TEST_ASSERT_NOT_EQUAL(epoch,CDC_ReceiveGeneration());TEST_ASSERT_TRUE(armed);TEST_ASSERT_EQUAL(0,CDC_Read(&ch,1));mulConsoleReset();
 armed=false;uint32_t invalid=65;epoch=CDC_ReceiveGeneration();TEST_ASSERT_EQUAL(USBD_FAIL,CDC_Receive_FS(UserRxBufferFS,&invalid));TEST_ASSERT_NOT_EQUAL(epoch,CDC_ReceiveGeneration());TEST_ASSERT_TRUE(armed);TEST_ASSERT_EQUAL(0,CDC_Read(&ch,1));
}

void test_boundedAtomicTxReservesErrorSpaceAndNeverPartiallyEnqueues(void) {
 unsigned before=txBytes;txSpaces=70;
 TEST_ASSERT_FALSE(CDC_TryWrite("reply text",10));TEST_ASSERT_EQUAL(before,txBytes);
 txSpaces=74;TEST_ASSERT_TRUE(CDC_TryWrite("reply text",10));TEST_ASSERT_EQUAL(before+10,txBytes);
 TEST_ASSERT_FALSE(CDC_TryWrite("",49));TEST_ASSERT_FALSE(CDC_TryWrite(NULL,1));
 CDC_DeInit_FS();TEST_ASSERT_FALSE(CDC_TryWrite("x",1));TEST_ASSERT_EQUAL(0,critical);
}
void test_unifiedLineSessionSurvivesPacketBoundariesAndDisconnectClearsIt(void) {
 packet(":con",4);drain(true);packet("sole\r\nSH",8);drain(true);
 packet("OW\nOFF extra\n",13);drain(true);TEST_ASSERT_EQUAL(0,writes);TEST_ASSERT_TRUE(uwbControlRequested());
 packet("off\n",4);drain(true);TEST_ASSERT_FALSE(uwbControlRequested());TEST_ASSERT_EQUAL(1,writes);
 packet("on\n",3);drain(true);TEST_ASSERT_TRUE(uwbControlRequested());TEST_ASSERT_EQUAL(2,writes);
 uint32_t generation=CDC_ReceiveGeneration();CDC_DeInit_FS();TEST_ASSERT_NOT_EQUAL(generation,CDC_ReceiveGeneration());mulConsoleReset();
 TEST_ASSERT_FALSE(mulConsoleByte('o'));TEST_ASSERT_EQUAL(2,queueCreates);
}
