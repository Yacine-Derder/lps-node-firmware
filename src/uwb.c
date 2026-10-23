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
/* uwb.c: Uwb radio implementation, low level handling */

#include <stm32f0xx_hal.h>

#include "uwb.h"
#include "uwb_control.h"
#include "mul_alr.h"
#include <string.h>
#include "mul_rlr.h"
#include "mul_rx.h"

#include "libdw1000.h"
#include "dwOps.h"
#include "phy_interop_dw.h"
#include "mul_diag_dw.h"
#include "mul_ads_dw.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

// Implemented UWB algoritm. The dummy one is at the end of this file.
static uwbAlgorithm_t dummyAlgorithm;
extern uwbAlgorithm_t uwbTwrAnchorAlgorithm;
extern uwbAlgorithm_t uwbTwrTagAlgorithm;
extern uwbAlgorithm_t uwbSnifferAlgorithm;
extern uwbAlgorithm_t uwbTdoaAlgorithm;
extern uwbAlgorithm_t uwbTdoa2Algorithm;
extern uwbAlgorithm_t uwbTdoa3Algorithm;
static uwbAlgorithm_t *algorithm = &dummyAlgorithm;

/* Build-only experiments always replace the persisted algorithm below. Keep
 * legacy names/IDs for the menu, but do not retain unreachable radio engines
 * through their table pointers (particularly the larger ADS diagnostic image). */
#if PHY_INTEROP_ROLE || MUL_DIAG_ROLE || MUL_TWR_ROLE
#define TABLE_ALGORITHM(name) (&dummyAlgorithm)
#else
#define TABLE_ALGORITHM(name) (&name)
#endif
struct {
  uwbAlgorithm_t *algorithm;
  char *name;
} availableAlgorithms[] = {
  {.algorithm = TABLE_ALGORITHM(uwbTwrAnchorAlgorithm), .name = "TWR Anchor"},
  {.algorithm = TABLE_ALGORITHM(uwbTwrTagAlgorithm),    .name = "TWR Tag"},
  {.algorithm = TABLE_ALGORITHM(uwbSnifferAlgorithm),   .name = "Sniffer"},
  {.algorithm = TABLE_ALGORITHM(uwbTdoa2Algorithm),     .name = "TDoA Anchor V2"},
  {.algorithm = TABLE_ALGORITHM(uwbTdoa3Algorithm),     .name = "TDoA Anchor V3"},
  {.algorithm = TABLE_ALGORITHM(mulAlrAlgorithm),       .name = "MUL ALR"},
  {.algorithm = TABLE_ALGORITHM(mulRlrAlgorithm),       .name = "MUL RLR"},
  {NULL, NULL},
};

// Low level radio handling context (to be separated)
static bool isInit = false;
static int uwbErrorCode = 0;
static SemaphoreHandle_t irqSemaphore;
static dwDevice_t dwm_device;
static dwDevice_t *dwm = &dwm_device;

// System configuration
static struct uwbConfig_s config = {
  address: {0,0,0,0,0,0,0xcf,0xbc},
};

static uint32_t timeout;

static void txcallback(dwDevice_t *dev)
{
  if(uwbControlEvent(eventPacketSent))timeout=algorithm->onEvent(dev,eventPacketSent);
}

static void rxcallback(dwDevice_t *dev)
{
  if(uwbControlEvent(eventPacketReceived))timeout=algorithm->onEvent(dev,eventPacketReceived);
}

static void rxTimeoutCallback(dwDevice_t * dev) {
  if(uwbControlEvent(eventReceiveTimeout))timeout=algorithm->onEvent(dev,eventReceiveTimeout);
}

static void rxfailedcallback(dwDevice_t *dev) {
  if(uwbControlEvent(eventReceiveFailed))timeout=algorithm->onEvent(dev,eventReceiveFailed);
}


void uwbInit()
{
  // Initializing the low level radio handling
  static StaticSemaphore_t irqSemaphoreBuffer;
  irqSemaphore = xSemaphoreCreateBinaryStatic(&irqSemaphoreBuffer);

  dwInit(dwm, &dwOps);       // Init libdw
  dwOpsInit(dwm);
  uwbErrorCode = dwConfigure(dwm); // Configure the dw1000 chip
  if (uwbErrorCode == 0) {
    dwEnableAllLeds(dwm);
  } else {
    return;
  }
  dwTime_t delay = {.full = 0};
  dwSetAntenaDelay(dwm, delay);

  // Reading and setting node configuration
  cfgReadU8(cfgAddress, &config.address[0]);
  cfgReadU8(cfgMode, &config.mode);
  uint8_t mulRole=2,reference=0x84,follower=0x85;
  config.mulConfigured=true;config.mulRole=mulAlrUnsupported;
  memset(&config.mulNetwork,0,sizeof(config.mulNetwork));
  if(cfgMulRead(config.address[0],&mulRole,&reference,&follower)&&
     mulNetworkInit(&config.mulNetwork,reference,follower)&&cfgMulSlotRead(&config.mulNetwork.slotMs)) {
    if(mulRole<2)config.mulRole=(mulAlrRole_t)mulRole;
  } else printf("MUL CONFIG: invalid roster; MUL disabled, EEPROM retained\r\n");
  if(!cfgMulScheduleRead(config.address[0],&config.mulSchedule)) {
    memset(&config.mulSchedule,0,sizeof(config.mulSchedule));config.mulRole=mulAlrUnsupported;
  } else if(config.mulSchedule.profile==MUL_PROFILE_MULTI) {
    int slot=mulScheduleIndex(&config.mulSchedule,config.address[0]);
    config.mulRole=slot==0?mulAlrReference:slot>0?mulAlrFollower:mulAlrUnsupported;
  }
  cfgFieldSize(cfgAnchorlist, &config.anchorListSize);
  if (config.anchorListSize <= MAX_ANCHORS) {
    cfgReadU8list(cfgAnchorlist, config.anchors, config.anchorListSize);
  }

  if (config.mode < uwbAlgorithmCount()) {
    algorithm = availableAlgorithms[config.mode].algorithm;
  } else {
    algorithm = &dummyAlgorithm;
  }

  config.positionEnabled = cfgReadFP32list(cfgAnchorPos, config.position, 3);

  dwAttachSentHandler(dwm, txcallback);
  dwAttachReceivedHandler(dwm, rxcallback);
  dwAttachReceiveTimeoutHandler(dwm, rxTimeoutCallback);
  dwAttachReceiveFailedHandler(dwm, rxfailedcallback);

  dwNewConfiguration(dwm);
  dwSetDefaults(dwm);

  uint8_t useLowBitrate = 0;
  cfgReadU8(cfgLowBitrate, &useLowBitrate);
  #ifdef LPS_LONGER_RANGE
  useLowBitrate = 1;
  #endif
  config.lowBitrate = (useLowBitrate == 1);

  uint8_t useLongPreamble = 0;
  cfgReadU8(cfgLongPreamble, &useLongPreamble);
  config.longPreamble = (useLongPreamble == 1);

  const uint8_t* mode = MODE_SHORTDATA_FAST_ACCURACY;
  if (useLowBitrate && !useLongPreamble) {
    mode = MODE_SHORTDATA_MID_ACCURACY;
  } else if (!useLowBitrate && useLongPreamble) {
    mode = MODE_LONGDATA_FAST_ACCURACY;
  } else if (useLowBitrate && useLongPreamble) {
    mode = MODE_LONGDATA_MID_ACCURACY;
  }
  dwEnableMode(dwm, mode);

  dwSetChannel(dwm, CHANNEL_2);

  // Enable smart power by default
  uint8_t enableSmartPower = 1;
  cfgReadU8(cfgSmartPower, &enableSmartPower);
  config.smartPower = enableSmartPower != 0;
  if (enableSmartPower) {
    dwUseSmartPower(dwm, true);
  }

  // Do not force power by default
  uint8_t forceTxPower = 0;
  cfgReadU8(cfgForceTxPower, &forceTxPower);
  config.forceTxPower = forceTxPower != 0;
  if (forceTxPower) {
    uint32_t txPower = 0x1F1F1F1Ful;
    cfgReadU32(cfgTxPower, &txPower);
    config.txPower = txPower;
    dwSetTxPower(dwm, txPower);
  }

  dwSetPreambleCode(dwm, PREAMBLE_CODE_64MHZ_9);

#if MUL_TWR_ROLE
  algorithm = &mulAdsAlgorithm;
  phyInteropConfigure(dwm);
  // Explicit experimental antenna reference; legacy/diagnostic profiles untouched.
  dwTime_t adsDelay={.full=MUL_ADS_DW_ANT_DTU};
  dwSetAntenaDelay(dwm,adsDelay);
#elif MUL_DIAG_ROLE
  algorithm = &mulDiagAlgorithm;
  phyInteropConfigure(dwm);
#elif PHY_INTEROP_ROLE
  // Explicit build-only diagnostic: no persisted role/PHY changes.
  algorithm = &phyInteropAlgorithm;
  phyInteropConfigure(dwm);
#endif
#if MUL_HYBRID_CH5
  // DWM3000 cannot receive channel2. Opt-in changes PHY only for custom modes;
  // never select a diagnostic algorithm or alter their TDMA/packet/EEPROM state.
  if(config.mode==5||config.mode==6) {
    phyInteropConfigure(dwm);
    printf("MUL PHY: HYBRID_CH5 channel=5 rate=6800 preamble=128 code=9 sfd=IEEE saved_PHY_overridden=1\r\n");
  }
#endif
  dwCommitConfiguration(dwm);
  // Original modes retain their software antenna compensation and zero registers.
  // Configure only ALRs, after the last full commit, without changing PHY/EEPROM.
  if (config.mode == 5 && !PHY_INTEROP_ROLE && !MUL_DIAG_ROLE && !MUL_TWR_ROLE) {
    mulAntennaConfigure(dwm);
  }

  uwbControlInit(&config);
  isInit = true;
}

bool uwbTest()
{
  return isInit;
}

int uwbAlgorithmCount()
{
  int count = 0;

  while (availableAlgorithms[count].algorithm != NULL) {
    count ++;
  }
  return count;
}

char * uwbAlgorithmName(unsigned int id)
{
  if (id < uwbAlgorithmCount()) {
    return availableAlgorithms[id].name;
  } else {
    return "UKNOWN";
  }
}

static int checkIrq()
{
  return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);
}

static void uwbTask(void* parameters)
{
  configASSERT(isInit);

  dwIdle(dwm);dwClearAllStatus(dwm);timeout=1;
  uwbControlApply(dwm,algorithm);
  while(1) {
    bool irq=xSemaphoreTake(irqSemaphore, timeout/portTICK_PERIOD_MS);
    if(uwbControlApply(dwm,algorithm))timeout=1;
    if(!uwbControlRunning()){timeout=MAX_TIMEOUT;continue;}
    if(irq) {
      do {dwHandleInterrupt(dwm);} while(checkIrq()!=0&&uwbControlRequested());
    } else if(uwbControlEvent(eventTimeout))timeout=algorithm->onEvent(dwm,eventTimeout);
  }
}

bool uwbSetEnabled(bool enabled) {
 if(!uwbControlRequest(enabled))return false;
 if(irqSemaphore)xSemaphoreGive(irqSemaphore);
 return true;
}

void uwbStart()
{
  static StaticTask_t uwbStaticTask;
  static StackType_t uwbStaticStack[2*configMINIMAL_STACK_SIZE];

  if (isInit) {
    xTaskCreateStatic(uwbTask, "uwb", 2*configMINIMAL_STACK_SIZE, NULL,
                      configMAX_PRIORITIES - 1, uwbStaticStack, &uwbStaticTask);
  }
}

char * uwbStrError()
{
  return dwStrError(uwbErrorCode);
}

struct uwbConfig_s * uwbGetConfig()
{
  return &config;
}

/**** DWM1000 interrupt handling *****/
#define DWM_IRQn EXTI0_1_IRQn
#define DWM_IRQ_PIN GPIO_PIN_0

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  BaseType_t higherPriorityTaskWoken = pdFALSE;

  switch (GPIO_Pin) {
    case DWM_IRQ_PIN:
      xSemaphoreGiveFromISR(irqSemaphore, &higherPriorityTaskWoken);

      HAL_NVIC_ClearPendingIRQ(DWM_IRQn);
      break;
    default:
      break;
  }
  portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

/* Dummy algorithm (used if UKNOWN algorithm is selected ...)*/
static void dummyInit(uwbConfig_t * config, dwDevice_t *dev)
{
  ;
}

static uint32_t dummyOnEvent(dwDevice_t *dev, uwbEvent_t event)
{
  return MAX_TIMEOUT;
}

static uwbAlgorithm_t dummyAlgorithm = {
  .init = dummyInit,
  .onEvent = dummyOnEvent,
};
