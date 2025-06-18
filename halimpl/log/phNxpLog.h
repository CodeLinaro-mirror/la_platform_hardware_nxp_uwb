/*
 * Copyright 2012-2018, 2023 NXP
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

#if !defined(NXPLOG__H_INCLUDED)
#define NXPLOG__H_INCLUDED
#include <thread>

#include <android/log.h>

#ifndef LOG_PRI
#define LOG_PRI(priority, tag, ...) __android_log_print(priority, tag, __VA_ARGS__)
#endif

typedef struct uci_log_level {
  uint8_t global_log_level;
  uint8_t extns_log_level;
  uint8_t hal_log_level;
  uint8_t dnld_log_level;
  uint8_t tml_log_level;
  uint8_t ucix_log_level;
  uint8_t ucir_log_level;
} uci_log_level_t;

typedef struct {
  std::thread log_thread_handler;
  FILE *FwCrashLogFile;
} phNxpUciHalLog_Control_t;

typedef struct uci_debug_log_file {
  FILE *debuglogFile;
  bool is_log_file_required;
  uint32_t fileSize;
  bool init_sequence_started;
} uci_debug_log_file_t;

/* global log level Ref */
extern uci_log_level_t gLog_level;
extern uci_debug_log_file_t gLogFile;

void phNxpLog_printErrorLogsTime(const char *format, ...);

// define log module included when compile */
#define ENABLE_EXTNS_TRACES TRUE
#define ENABLE_HAL_TRACES TRUE
#define ENABLE_TML_TRACES TRUE
#define ENABLE_FWDNLD_TRACES TRUE
#define ENABLE_UCIX_TRACES TRUE
#define ENABLE_UCIR_TRACES TRUE

// Set the log module name in .conf file
#define NAME_NXPLOG_EXTNS_LOGLEVEL "NXP_LOG_EXTNS_LOGLEVEL"
#define NAME_NXPLOG_HAL_LOGLEVEL "NXP_LOG_UCIHAL_LOGLEVEL"
#define NAME_NXPLOG_UCIX_LOGLEVEL "NXP_LOG_UCIX_LOGLEVEL"
#define NAME_NXPLOG_UCIR_LOGLEVEL "NXP_LOG_UCIR_LOGLEVEL"
#define NAME_NXPLOG_FWDNLD_LOGLEVEL "NXP_LOG_FWDNLD_LOGLEVEL"
#define NAME_NXPLOG_TML_LOGLEVEL "NXP_LOG_TML_LOGLEVEL"

// Set the log module name by Android property
#define PROP_NAME_NXPLOG_GLOBAL_LOGLEVEL  "persist.vendor.uwb.nxp_log_level_global"
#define PROP_NAME_NXPLOG_EXTNS_LOGLEVEL   "persist.vendor.uwb.nxp_log_level_extns"
#define PROP_NAME_NXPLOG_HAL_LOGLEVEL     "persist.vendor.uwb.nxp_log_level_hal"
#define PROP_NAME_NXPLOG_UCI_LOGLEVEL     "persist.vendor.uwb.nxp_log_level_uci"
#define PROP_NAME_NXPLOG_FWDNLD_LOGLEVEL  "persist.vendor.uwb.nxp_log_level_dnld"
#define PROP_NAME_NXPLOG_TML_LOGLEVEL     "persist.vendor.uwb.nxp_log_level_tml"

// Set the logging level for EVERY COMPONENT here
#define NXPLOG_LOG_SILENT_LOGLEVEL 0x00
#define NXPLOG_LOG_ERROR_LOGLEVEL 0x01
#define NXPLOG_LOG_WARN_LOGLEVEL 0x02
#define NXPLOG_LOG_DEBUG_LOGLEVEL 0x03
#define NXPLOG_LOG_VERBOSE_LOGLEVEL 0x04

// The Default log level for all the modules.
#define NXPLOG_DEFAULT_LOGLEVEL NXPLOG_LOG_ERROR_LOGLEVEL

// Component Names
inline constexpr char NXPLOG_ITEM_EXTNS[] = "NxpExtns";
inline constexpr char NXPLOG_ITEM_UCIHAL[] = "NxpUwbHal";
inline constexpr char NXPLOG_ITEM_UCIX[] = "NxpUciX";
inline constexpr char NXPLOG_ITEM_UCIR[] = "NxpUciR";
inline constexpr char NXPLOG_ITEM_FWDNLD[] = "NxpFwDnld";
inline constexpr char NXPLOG_ITEM_TML[] = "NxpUwbTml";
inline constexpr char NXP_HAL_ERROR[] = "NxpHalE";

//
// Logging APIs of actual modules
//

// Logging APIs used by NxpExtns module
#if (ENABLE_EXTNS_TRACES == TRUE)
#define NXPLOG_EXTNS_D(...)                                        \
  {                                                                \
    if ((gLog_level.extns_log_level >= NXPLOG_LOG_DEBUG_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_DEBUG, NXPLOG_ITEM_EXTNS, __VA_ARGS__);  \
  }
#define NXPLOG_EXTNS_W(...)                                       \
  {                                                               \
    if ((gLog_level.extns_log_level >= NXPLOG_LOG_WARN_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_WARN, NXPLOG_ITEM_EXTNS, __VA_ARGS__);  \
  }
#define NXPLOG_EXTNS_E(...)                                       \
  {                                                               \
    if (gLog_level.extns_log_level >= NXPLOG_LOG_ERROR_LOGLEVEL)  \
      LOG_PRI(ANDROID_LOG_ERROR, NXPLOG_ITEM_EXTNS, __VA_ARGS__); \
  }
#else
#define NXPLOG_EXTNS_D(...)
#define NXPLOG_EXTNS_W(...)
#define NXPLOG_EXTNS_E(...)
#endif /* Logging APIs used by NxpExtns module */

/* Logging APIs used by NxpUciHal module */
#if (ENABLE_HAL_TRACES == TRUE)
#define NXPLOG_UCIHAL_V(...)                                       \
  {                                                                \
    if ((gLog_level.hal_log_level >= NXPLOG_LOG_VERBOSE_LOGLEVEL))   \
      LOG_PRI(ANDROID_LOG_VERBOSE, NXPLOG_ITEM_UCIHAL, __VA_ARGS__); \
  }
#define NXPLOG_UCIHAL_D(...)                                       \
  {                                                                \
    if ((gLog_level.hal_log_level >= NXPLOG_LOG_DEBUG_LOGLEVEL))   \
      LOG_PRI(ANDROID_LOG_DEBUG, NXPLOG_ITEM_UCIHAL, __VA_ARGS__); \
  }
#define NXPLOG_UCIHAL_W(...)                                      \
  {                                                               \
    if ((gLog_level.hal_log_level >= NXPLOG_LOG_WARN_LOGLEVEL))   \
      LOG_PRI(ANDROID_LOG_WARN, NXPLOG_ITEM_UCIHAL, __VA_ARGS__); \
  }
#define NXPLOG_UCIHAL_E(...)                                                   \
  {                                                                            \
    if (gLog_level.hal_log_level >= NXPLOG_LOG_ERROR_LOGLEVEL)                 \
      LOG_PRI(ANDROID_LOG_ERROR, NXPLOG_ITEM_UCIHAL, __VA_ARGS__);             \
    phNxpLog_printErrorLogsTime(__VA_ARGS__);                                  \
  }
#else
#define NXPLOG_UCIHAL_V(...)
#define NXPLOG_UCIHAL_D(...)
#define NXPLOG_UCIHAL_W(...)
#define NXPLOG_UCIHAL_E(...)
#endif /* Logging APIs used by HAL module */

/* Logging APIs used by NxpUciX module */
#if (ENABLE_UCIX_TRACES == TRUE)
#define NXPLOG_UCIX_D(...)                                        \
  {                                                               \
    if ((gLog_level.ucix_log_level >= NXPLOG_LOG_DEBUG_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_DEBUG, NXPLOG_ITEM_UCIX, __VA_ARGS__);  \
  }
#define NXPLOG_UCIX_W(...)                                       \
  {                                                              \
    if ((gLog_level.ucix_log_level >= NXPLOG_LOG_WARN_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_WARN, NXPLOG_ITEM_UCIX, __VA_ARGS__);  \
  }
#define NXPLOG_UCIX_E(...)                                       \
  {                                                              \
    if (gLog_level.ucix_log_level >= NXPLOG_LOG_ERROR_LOGLEVEL)  \
      LOG_PRI(ANDROID_LOG_ERROR, NXPLOG_ITEM_UCIX, __VA_ARGS__); \
  }
#else
#define NXPLOG_UCIX_D(...)
#define NXPLOG_UCIX_W(...)
#define NXPLOG_UCIX_E(...)
#endif /* Logging APIs used by UCIx module */

/* Logging APIs used by NxpUciR module */
#if (ENABLE_UCIR_TRACES == TRUE)
#define NXPLOG_UCIR_D(...)                                        \
  {                                                               \
    if ((gLog_level.ucir_log_level >= NXPLOG_LOG_DEBUG_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_DEBUG, NXPLOG_ITEM_UCIR, __VA_ARGS__);  \
  }
#define NXPLOG_UCIR_W(...)                                       \
  {                                                              \
    if ((gLog_level.ucir_log_level >= NXPLOG_LOG_WARN_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_WARN, NXPLOG_ITEM_UCIR, __VA_ARGS__);  \
  }
#define NXPLOG_UCIR_E(...)                                       \
  {                                                              \
    if (gLog_level.ucir_log_level >= NXPLOG_LOG_ERROR_LOGLEVEL)  \
      LOG_PRI(ANDROID_LOG_ERROR, NXPLOG_ITEM_UCIR, __VA_ARGS__); \
  }
#else
#define NXPLOG_UCIR_D(...)
#define NXPLOG_UCIR_W(...)
#define NXPLOG_UCIR_E(...)
#endif /* Logging APIs used by UCIR module */

/* Logging APIs used by NxpFwDnld module */
#if (ENABLE_FWDNLD_TRACES == TRUE)
#define NXPLOG_FWDNLD_D(...)                                       \
  {                                                                \
    if ((gLog_level.dnld_log_level >= NXPLOG_LOG_DEBUG_LOGLEVEL))  \
      LOG_PRI(ANDROID_LOG_DEBUG, NXPLOG_ITEM_FWDNLD, __VA_ARGS__); \
  }
#define NXPLOG_FWDNLD_W(...)                                      \
  {                                                               \
    if ((gLog_level.dnld_log_level >= NXPLOG_LOG_WARN_LOGLEVEL))  \
      LOG_PRI(ANDROID_LOG_WARN, NXPLOG_ITEM_FWDNLD, __VA_ARGS__); \
  }
#define NXPLOG_FWDNLD_E(...)                                       \
  {                                                                \
    if (gLog_level.dnld_log_level >= NXPLOG_LOG_ERROR_LOGLEVEL)    \
      LOG_PRI(ANDROID_LOG_ERROR, NXPLOG_ITEM_FWDNLD, __VA_ARGS__); \
  }
#else
#define NXPLOG_FWDNLD_D(...)
#define NXPLOG_FWDNLD_W(...)
#define NXPLOG_FWDNLD_E(...)
#endif /* Logging APIs used by NxpFwDnld module */

/* Logging APIs used by NxpTml module */
#if (ENABLE_TML_TRACES == TRUE)
#define NXPLOG_TML_V(...)                                        \
  {                                                              \
    if ((gLog_level.tml_log_level >= NXPLOG_LOG_VERBOSE_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_VERBOSE, NXPLOG_ITEM_TML, __VA_ARGS__);  \
  }
#define NXPLOG_TML_D(...)                                        \
  {                                                              \
    if ((gLog_level.tml_log_level >= NXPLOG_LOG_DEBUG_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_DEBUG, NXPLOG_ITEM_TML, __VA_ARGS__);  \
  }
#define NXPLOG_TML_W(...)                                       \
  {                                                             \
    if ((gLog_level.tml_log_level >= NXPLOG_LOG_WARN_LOGLEVEL)) \
      LOG_PRI(ANDROID_LOG_WARN, NXPLOG_ITEM_TML, __VA_ARGS__);  \
  }
#define NXPLOG_TML_E(...)                                       \
  {                                                             \
    if (gLog_level.tml_log_level >= NXPLOG_LOG_ERROR_LOGLEVEL)  \
      LOG_PRI(ANDROID_LOG_ERROR, NXPLOG_ITEM_TML, __VA_ARGS__); \
  }
#else
#define NXPLOG_TML_V(...)
#define NXPLOG_TML_D(...)
#define NXPLOG_TML_W(...)
#define NXPLOG_TML_E(...)
#endif /* Logging APIs used by NxpTml module */

void phNxpLog_InitializeLogLevel(void);
/* Log functions */
void phNxpUciHalProp_trigger_fw_crash_log_dump();
bool phNxpUciHal_dump_log(size_t data_len, const uint8_t *p_rx_data);
void phNxpUciLog_initialize();
void phNxpUciLog_deinitialize();
void phNxpUciHalProp_print_log(uint8_t what, const uint8_t *p_data,
                               uint16_t len);

#endif /* NXPLOG__H_INCLUDED */
