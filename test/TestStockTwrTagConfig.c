#include "unity.h"
#include "mock_libdw1000.h"
#include "mock_uwb.h"
#include "mock_lpp.h"
#include "mock_led.h"
#include "uwb_twr_tag.c"
uint32_t HAL_GetTick(void){return 0;}
void vAssertCalled(unsigned long line,const char *const file){(void)file;(void)line;TEST_FAIL_MESSAGE("unexpected firmware assertion");}
void setUp(void){}
void tearDown(void){}
void test_stockTagRetainsOwnedAddressAndAnchorListSnapshots(void) {
 uwbConfig_t source={0};dwDevice_t dev={0};const uint8_t address[8]={43,0,0,0,0,0,0xcf,0xbc};
 const uint8_t anchors[MAX_ANCHORS]={132,133,134,135,136,137};
 memcpy(source.address,address,8);memcpy(source.anchors,anchors,MAX_ANCHORS);source.anchorListSize=4;
 ledOn_Expect(ledMode);uwbTwrTagAlgorithm.init(&source,&dev);memset(&source,0xa5,sizeof(source));
 TEST_ASSERT_EQUAL_UINT8_ARRAY(address,config.address,8);TEST_ASSERT_EQUAL_UINT8_ARRAY(anchors,config.anchors,MAX_ANCHORS);
 TEST_ASSERT_EQUAL_UINT(4,config.anchorListSize);TEST_ASSERT_EQUAL_UINT(8+MAX_ANCHORS+1,sizeof(config));
}
