/*
 * Copyright 2012-2020, 2024 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <phNxpLog.h>
#include <phNxpUciHal_utils.h>
#include <phOsalUwb_Timer.h>
#include <phTmlUwb.h>
#include <phTmlUwb_spi.h>
#include <phNxpUciHal.h>

namespace {

constexpr long kHalWriteTimeoutMs = 1000;

// Structure containing details related to read and write operations
struct phTmlUwb_ReadInfo {
  std::atomic_bool bThreadShouldStop;
  std::atomic_bool bThreadRunning;

  // Transaction completion Callback function
  ReadCallback OnPacketReceived;

  uint8_t* pBuffer;         // Buffer passed while invocation of operation
  size_t wLength;           // Length of data read/written
};

// Base Context Structure containing members required for entire session
struct phTmlUwb_Context {
  pthread_t readerThread;

  phTmlUwb_ReadInfo tReadInfo;  /*Pointer to Reader Thread Structure */
  void* pDevHandle;                    /* Pointer to Device Handle */
  std::shared_ptr<MessageQueue<phLibUwb_Message>> pClientMq; /* Pointer to Client thread message queue */
};

std::unique_ptr<phTmlUwb_Context> gpphTmlUwb_Context;

}   // namespace

extern void setDeviceHandle(void* pDevHandle);

/* Local Function prototypes */
static void phTmlUwb_CleanUp(void);
static void* phTmlUwb_TmlReaderThread(void* pParam);

/* Function definitions */

/*******************************************************************************
**
** Function         phTmlUwb_Init
**
** Description      Provides initialization of TML layer and hardware interface
**                  Configures given hardware interface and sends handle to the
**                  caller
**
** Parameters       pConfig - TML configuration details as provided by the upper
**                            layer
**
** Returns          UWB status:
**                  UWBSTATUS_SUCCESS - initialization successful
**                  UWBSTATUS_INVALID_PARAMETER - at least one parameter is
**                                                invalid
**                  UWBSTATUS_FAILED - initialization failed (for example,
**                                     unable to open hardware interface)
**                  UWBSTATUS_INVALID_DEVICE - device has not been opened or has
**                                             been disconnected
**
*******************************************************************************/
tHAL_UWB_STATUS phTmlUwb_Init(const char* pDevName,
    std::shared_ptr<MessageQueue<phLibUwb_Message>> pClientMq)
{
  if (gpphTmlUwb_Context != nullptr) {
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_ALREADY_INITIALISED);
  }

  if (!pDevName || !pClientMq) {
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_INVALID_PARAMETER);
  }

  // Allocate memory for TML context
  gpphTmlUwb_Context = std::make_unique<phTmlUwb_Context>();
  if (gpphTmlUwb_Context == nullptr) {
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_FAILED);
  }

  // Open the device file to which data is read/written
  tHAL_UWB_STATUS wInitStatus =
    phTmlUwb_spi_open_and_configure(pDevName, &(gpphTmlUwb_Context->pDevHandle));
  if (UWBSTATUS_SUCCESS != wInitStatus) {
    gpphTmlUwb_Context->pDevHandle = NULL;
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_INVALID_DEVICE);
  }
  gpphTmlUwb_Context->pClientMq = pClientMq;

  setDeviceHandle(gpphTmlUwb_Context->pDevHandle);  // To set device handle for FW download usecase

  return UWBSTATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         phTmlUwb_TmlReaderThread
**
** Description      Read the data from the lower layer driver
**
** Parameters       pParam  - parameters for Reader thread function
**
** Returns          None
**
*******************************************************************************/
static void* phTmlUwb_TmlReaderThread(void* pParam)
{
  UNUSED(pParam);

  if (gpphTmlUwb_Context == nullptr)
    return nullptr;

  phTmlUwb_ReadInfo *info = &gpphTmlUwb_Context->tReadInfo;
  if (info->bThreadRunning.exchange(true)) {
    NXPLOG_TML_E("TmlReader: Thread already started.");
    return nullptr;
  }
  NXPLOG_TML_D("TmlReader: Thread Started");

  /* Writer thread loop shall be running till shutdown is invoked */
  while (!info->bThreadShouldStop) {
    NXPLOG_TML_V("TmlReader: Running");

    /* Read the data from the file onto the buffer */
    if (!gpphTmlUwb_Context->pDevHandle) {
      NXPLOG_TML_E("TmlRead: invalid file handle");
      break;
    }

    NXPLOG_TML_V("TmlReader:  Invoking SPI Read");

    uint8_t temp[UCI_MAX_DATA_LEN];
    int dwNoBytesWrRd =
        phTmlUwb_spi_read(gpphTmlUwb_Context->pDevHandle, temp, UCI_MAX_DATA_LEN);

    if(info->bThreadShouldStop) {
      break;
    }

    if (-1 == dwNoBytesWrRd) {
      NXPLOG_TML_E("TmlReader: Error in SPI Read");
    } else if (dwNoBytesWrRd > UCI_MAX_DATA_LEN) {
      NXPLOG_TML_E("TmlReader: Numer of bytes read exceeds the limit");
    } else if(0 == dwNoBytesWrRd) {
      NXPLOG_TML_E("TmlReader: Empty packet Read, Ignore read and try new read");
    } else {
      memcpy(info->pBuffer, temp, dwNoBytesWrRd);
      info->wLength = dwNoBytesWrRd;

      // Read operation completed successfully. Calls read callback.
      NXPLOG_TML_V("TmlReader: SPI Read successful. Call read complete callback.");

      // Callback
      info->OnPacketReceived(info->pBuffer, info->wLength);
    }
  } /* End of While loop */

  gpphTmlUwb_Context->tReadInfo.bThreadRunning = false;
  NXPLOG_TML_D("Tml Reader: Thread stopped");

  return NULL;
}

/*******************************************************************************
**
** Function         phTmlUwb_CleanUp
**
** Description      Clears all handles opened during TML initialization
**
** Parameters       None
**
** Returns          None
**
*******************************************************************************/
static void phTmlUwb_CleanUp(void) {
  if (gpphTmlUwb_Context == nullptr) {
    return;
  }

  if (NULL != gpphTmlUwb_Context->pDevHandle) {
    (void)phTmlUwb_Spi_Ioctl(gpphTmlUwb_Context->pDevHandle, phTmlUwb_ControlCode_t::SetPower, 0);
  }

  phTmlUwb_spi_close(gpphTmlUwb_Context->pDevHandle);
  gpphTmlUwb_Context->pDevHandle = NULL;

  gpphTmlUwb_Context.reset();
}

/*******************************************************************************
**
** Function         phTmlUwb_Shutdown
**
** Description      Uninitializes TML layer and hardware interface
**
** Parameters       None
**
** Returns          UWB status:
**                  UWBSTATUS_SUCCESS - TML configuration released successfully
**                  UWBSTATUS_INVALID_PARAMETER - at least one parameter is
**                                                invalid
**                  UWBSTATUS_FAILED - un-initialization failed (example: unable
**                                     to close interface)
**
*******************************************************************************/
tHAL_UWB_STATUS phTmlUwb_Shutdown(void)
{
  if (!gpphTmlUwb_Context) {
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_NOT_INITIALISED);
  }

  // Abort io threads
  phTmlUwb_StopRead();

  phTmlUwb_CleanUp();

  return UWBSTATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         phTmlUwb_Write
**
** Description      Asynchronously writes given data block to hardware
**                  interface/driver. Enables writer thread if there are no
**                  write requests pending. Returns successfully once writer
**                  thread completes write operation. Notifies upper layer using
**                  callback mechanism.
**
**                  NOTE:
**                  * if CRC needs to be computed, then input buffer should be
**                    capable to store two more bytes apart from length of
**                    packet
**
** Parameters       pBuffer - data to be sent
**                  wLength - length of data buffer
**                  pTmlWriteComplete - pointer to the function to be invoked
**                                      upon completion
**                  pContext - context provided by upper layer
**
** Returns          UWB status:
**                  UWBSTATUS_PENDING - command is yet to be processed
**                  UWBSTATUS_INVALID_PARAMETER - at least one parameter is
**                                                invalid
**                  UWBSTATUS_BUSY - write request is already in progress
**
*******************************************************************************/
tHAL_UWB_STATUS phTmlUwb_Write(const uint8_t* pBuffer, size_t wLength)
{
  if (pBuffer == nullptr || wLength == 0) {
    return UWBSTATUS_INVALID_PARAMETER;
  }

  if (gpphTmlUwb_Context == nullptr || gpphTmlUwb_Context->pDevHandle == nullptr) {
    NXPLOG_TML_E("Tml not initialized.");
    return UWBSTATUS_NOT_INITIALISED;
  }

  phNxpUciHal_print_packet(NXP_TML_UCI_CMD_AP_2_UWBS, pBuffer, wLength);

  int dwNoBytesWrRd =
    phTmlUwb_spi_write(gpphTmlUwb_Context->pDevHandle, pBuffer, wLength);

  if (-1 == dwNoBytesWrRd) {
    NXPLOG_TML_E("TmlWriter: Error in SPI Write %d", errno);
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_FAILED);
  }

  return UWBSTATUS_SUCCESS;
}

/*******************************************************************************
**
** Function         phTmlUwb_Read
**
** Description      Asynchronously reads data from the driver
**                  Number of bytes to be read and buffer are passed by upper
**                  layer.
**                  Enables reader thread if there are no read requests pending
**                  Returns successfully once read operation is completed
**                  Notifies upper layer using callback mechanism
**
** Parameters       pBuffer - location to send read data to the upper layer via
**                            callback
**                  wLength - length of read data buffer passed by upper layer
**                  pTmlReadComplete - pointer to the function to be invoked
**                                     upon completion of read operation
**                  pContext - context provided by upper layer
**
** Returns          UWB status:
**                  UWBSTATUS_PENDING - command is yet to be processed
**                  UWBSTATUS_INVALID_PARAMETER - at least one parameter is
**                                                invalid
**                  UWBSTATUS_BUSY - read request is already in progress
**
*******************************************************************************/
tHAL_UWB_STATUS phTmlUwb_StartRead(ReadCallback pTmlReadComplete)
{
  // TODO: move this to gpphTmlUwb_Context
  static uint8_t shared_rx_buffer[UCI_MAX_DATA_LEN];

  /* Check whether TML is Initialized */
  if (gpphTmlUwb_Context == nullptr || gpphTmlUwb_Context->pDevHandle == nullptr) {
    NXPLOG_TML_E("Tml not initialized.");
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_NOT_INITIALISED);
  }

  if (!pTmlReadComplete) {
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_INVALID_PARAMETER);
  }

  if (gpphTmlUwb_Context->tReadInfo.bThreadRunning) {
    return PHUWBSTVAL(CID_UWB_TML, UWBSTATUS_BUSY);
  }

  /* Setting the flag marks beginning of a Read Operation */
  gpphTmlUwb_Context->tReadInfo.pBuffer = shared_rx_buffer;
  gpphTmlUwb_Context->tReadInfo.wLength = sizeof(shared_rx_buffer);
  gpphTmlUwb_Context->tReadInfo.OnPacketReceived = pTmlReadComplete;

  NXPLOG_TML_D("StartRead");

  /* Create Reader threads */
  gpphTmlUwb_Context->tReadInfo.bThreadShouldStop = false;
  int ret = pthread_create(&gpphTmlUwb_Context->readerThread, NULL,
                       &phTmlUwb_TmlReaderThread, NULL);
  if (ret) {
    return UWBSTATUS_FAILED;
  } else {
    return UWBSTATUS_SUCCESS;
  }
}

void phTmlUwb_StopRead()
{
  NXPLOG_TML_D("StopRead");

  if (gpphTmlUwb_Context == nullptr || gpphTmlUwb_Context->pDevHandle == nullptr) {
    NXPLOG_TML_E("Tml not initialzed.");
    return;
  }

  if (gpphTmlUwb_Context->tReadInfo.bThreadShouldStop.exchange(true)) {
    NXPLOG_TML_D("Reader thread already requested to be stopped.");
    return;
  }

  if (!gpphTmlUwb_Context->tReadInfo.bThreadRunning) {
    NXPLOG_TML_E("Reader thread requested stopped while already in stopped.");
    return;
  }

  // to wakeup from blocking read()
  phTmlUwb_Spi_Ioctl(gpphTmlUwb_Context->pDevHandle, phTmlUwb_ControlCode_t::SetPower, ABORT_READ_PENDING);

  if (pthread_join(gpphTmlUwb_Context->readerThread, NULL)) {
    NXPLOG_TML_E("[%s] pthread_join failed for reader thread at line: %d ", __func__, __LINE__);
  }
  if (gpphTmlUwb_Context->tReadInfo.bThreadRunning) {
    NXPLOG_TML_E("Failed to terminate read thread.");
  }
}

/*******************************************************************************
**
** Function         phTmlUwb_Chip_Reset
**
** Description      Invoke this API to Chip enable/Disable
**
** Parameters       None
**
** Returns          void
**
*******************************************************************************/
void phTmlUwb_Chip_Reset(void){

  if (gpphTmlUwb_Context == nullptr || gpphTmlUwb_Context->pDevHandle == nullptr) {
    NXPLOG_TML_E("Tml not initialzed.");
    return;
  }

  phTmlUwb_Spi_Ioctl(gpphTmlUwb_Context->pDevHandle, phTmlUwb_ControlCode_t::SetPower, 0);
  usleep(1000);
  phTmlUwb_Spi_Ioctl(gpphTmlUwb_Context->pDevHandle, phTmlUwb_ControlCode_t::SetPower, 1);
}

void phTmlUwb_Suspend(void)
{
  NXPLOG_TML_D("Suspend");

  if (gpphTmlUwb_Context == nullptr || gpphTmlUwb_Context->pDevHandle == nullptr) {
    NXPLOG_TML_E("Tml not initialized.");
  }

  phTmlUwb_Spi_Ioctl(gpphTmlUwb_Context->pDevHandle, phTmlUwb_ControlCode_t::SetPower, PWR_SUSPEND);

}

void phTmlUwb_Resume(void)
{
  NXPLOG_TML_D("Resume");

  if (gpphTmlUwb_Context == nullptr || gpphTmlUwb_Context->pDevHandle == nullptr) {
    NXPLOG_TML_E("Tml not initialized.");
  }

  phTmlUwb_Spi_Ioctl(gpphTmlUwb_Context->pDevHandle, phTmlUwb_ControlCode_t::SetPower, PWR_RESUME);
}
