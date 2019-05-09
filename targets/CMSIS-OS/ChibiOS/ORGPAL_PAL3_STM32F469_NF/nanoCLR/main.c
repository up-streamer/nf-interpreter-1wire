//
// Copyright (c) 2017 The nanoFramework project contributors
// See LICENSE file in the project root for full license information.
//

#include <ch.h>
#include <hal.h>
#include <cmsis_os.h>

#include "usbcfg.h"
#include <swo.h>
#include <CLR_Startup_Thread.h>
#include <WireProtocol_ReceiverThread.h>
#include <nanoCLR_Application.h>
#include <nanoPAL_BlockStorage.h>
#include <nanoHAL_v2.h>
#include <targetPAL.h>

extern uint8_t hal_spiffs_config();

// need to declare the Receiver thread here
osThreadDef(ReceiverThread, osPriorityHigh, 2048, "ReceiverThread");
// declare CLRStartup thread here 
osThreadDef(CLRStartupThread, osPriorityNormal, 4096, "CLRStartupThread"); 

#if HAL_USE_SDC
// declare SD Card working thread here 
osThreadDef(SdCardWorkingThread, osPriorityNormal, 1024, "SDCWT"); 
#endif
#if HAL_USBH_USE_MSD
// declare USB MSD thread here 
osThreadDef(UsbMsdWorkingThread, osPriorityNormal, 1024, "USBMSDWT"); 
#endif

//  Application entry point.
int main(void) {

  bool forceReset = false;

  // find out wakeup reason
  if((RTC->ISR & RTC_ISR_ALRAF) == RTC_ISR_ALRAF)
  {
    // standby, match WakeupReason_FromStandby enum
    WakeupReasonStore = 1;
  }
  else if((PWR->CSR & PWR_CSR_WUF) == PWR_CSR_WUF)
  {
    // wake from pin, match WakeupReason_FromPin enum
    WakeupReasonStore = 2;
  }
  else
  {
    // undetermined reason, match WakeupReason_Undetermined enum
    WakeupReasonStore = 0;
  }

  // check if waking from STDBY mode
  if(PWR->CSR & PWR_CSR_SBF)
  {
    // silicone bug in STM32F469x
    // errata ES0321 Rev6
    // 2.2.7 Wakeup from Standby mode  with RTC
    // need to force a reset after clearing the SBF flag
    forceReset = true;
  }

  // first things first: need to clear any possible wakeup flags
  // if this is not done here the next standby -> wakeup sequence won't work
  CLEAR_BIT(RTC->CR, RTC_CR_ALRAIE);
  CLEAR_BIT(RTC->ISR, RTC_ISR_ALRAF);
  SET_BIT(PWR->CR, PWR_CR_CWUF);
  SET_BIT(PWR->CR, PWR_CR_CSBF);

  if(forceReset)
  {
      // silicone bug in STM32F469x
      // errata ES0321 Rev6
      // 2.2.7 Wakeup from Standby mode  with RTC 
      CPU_Reset();
  }

  // HAL initialization, this also initializes the configured device drivers
  // and performs the board-specific initializations.
  halInit();
  
  // init SWO as soon as possible to make it available to output ASAP
#if (SWO_OUTPUT == TRUE)  
  SwoInit();
#endif

  // The kernel is initialized but not started yet, this means that
  // main() is executing with absolute priority but interrupts are already enabled.
  osKernelInitialize();

  // config and init external memory
  // this has to be called after osKernelInitialize, otherwise an hard fault will occur
  Target_ExternalMemoryInit();

  #if NF_FEATURE_USE_SPIFFS
  // config and init SPIFFS
  hal_spiffs_config();
  #endif

  //  Initializes a serial-over-USB CDC driver.
  sduObjectInit(&SDU1);
  sduStart(&SDU1, &serusbcfg);

  // Activates the USB driver and then the USB bus pull-up on D+.
  // Note, a delay is inserted in order to not have to disconnect the cable after a reset
  usbDisconnectBus(serusbcfg.usbp);
  chThdSleepMilliseconds(100);
  usbStart(serusbcfg.usbp, &usbcfg);
  usbConnectBus(serusbcfg.usbp);

  // create the receiver thread
  osThreadCreate(osThread(ReceiverThread), NULL);

  // CLR settings to launch CLR thread
  CLR_SETTINGS clrSettings;
  (void)memset(&clrSettings, 0, sizeof(CLR_SETTINGS));

  clrSettings.MaxContextSwitches         = 50;
  clrSettings.WaitForDebugger            = false;
  clrSettings.EnterDebuggerLoopAfterExit = true;

  // create the CLR Startup thread 
  osThreadCreate(osThread(CLRStartupThread), &clrSettings);

  #if HAL_USE_SDC
  // creates the SD card working thread 
  osThreadCreate(osThread(SdCardWorkingThread), NULL);
  #endif

  #if HAL_USBH_USE_MSD
  // create the USB MSD working thread
  osThreadCreate(osThread(UsbMsdWorkingThread), NULL);
  #endif

  // start kernel, after this main() will behave like a thread with priority osPriorityNormal
  osKernelStart();

  while (true) { 
    osDelay(100);
  }
}
