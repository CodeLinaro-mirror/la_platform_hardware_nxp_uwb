#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <ctime>
#include <optional>
#include <thread>
#include <vector>

#include "phNxpConfig.h"
#include "phNxpUciHal.h"
#include "phNxpUciHal_ext.h"
#include "timeSync.h"

extern phNxpUciHal_Control_t nxpucihal_ctrl;

namespace {

// Extra values to be added to measured uncertainty.
constexpr uint64_t kExtraUncertaintyInConvertUs = 1000;

// Converting system time only possible within 5s.
constexpr uint64_t kSystemTimeMaxLatencyUs = 5000 * 1000;

// Max uncertainty from querying uwb timestamp.
constexpr uint64_t kUwbsTimeMaxLatencyUs = 1000 * 1000;

// Uncertainty when TIMESYNC_TIMESTAMP_NTF's flag has "low" accuracy,
// 2ms latency from a chip internal, we're taking double of it.
constexpr uint32_t kUwbChipTimestampAccuracyLowUs = 4 * 1000;

// Uncertainty when TIMESYNC_TIMESTAMP_NTF'S flag has "high" accuracy,
// HW spec is 3 micro seconds from GPIO, we're using 200us here
// considering IRQ latency.
constexpr uint32_t kUwbChipTimestampAccuracyHighUs = 200;

// Saved timestamp from TIMESYNC_TIMESTAMP_NTF
// is received within 20ms, we'll take that as 'hit'.
// If the timestamp is older than 20ms, we'll wait for
// another notification.
constexpr unsigned long kTimestampNtfFreshMs = 20;

// Time window size where this timesync service routine waits for
// another asynchronous Timestamp NTF coming from a chip
// when it's not "fresh".
constexpr unsigned long kTimestampNtfRefreshWindowMs = 200;

// Polling cycle for the above "refreshing" window.
constexpr unsigned long kTimestampNtfRefreshPollingMs = 10;

class UwbTimestamp {
 public:
  // Default constructor with "FAILED" status code.
  UwbTimestamp() : status_(UCI_STATUS_FAILED) { }

  // Constructs a timestamp with error status code.
  explicit UwbTimestamp(uint8_t status) : status_(status) {
    if (status_ == UCI_STATUS_OK)
      status_ = UCI_STATUS_FAILED;
  }

  // Constructs a timestamp with valid timestamp + uncertainty.
  UwbTimestamp(uint64_t system_time, uint64_t uwbs_time, uint32_t uncertainty) :
    status_(UCI_STATUS_OK),
    system_time_us_(system_time),
    timestamp_us_(uwbs_time),
    uncertainty_us_(uncertainty) {}

  uint8_t GetStatus() const { return status_; }

  uint64_t GetSystemTime() const { return system_time_us_; }

  uint64_t GetTimestamp() const { return timestamp_us_; }

  uint32_t GetUncertainty() const { return uncertainty_us_; }

  // Creates a response packet ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_RSP.
  std::vector<uint8_t> ConstructResponse() {
    std::vector<uint8_t> rsp {
      (UCI_MT_RSP << UCI_MT_SHIFT) | UCI_GID_ANDROID,
      UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS, 0, 0,
    };

    rsp.push_back(status_);

    if (status_ == UCI_STATUS_OK) {
      uint8_t uwb_time_bytes[8];
      static_assert(sizeof(uwb_time_bytes) == sizeof(timestamp_us_));
      cpu_to_le_bytes(uwb_time_bytes, timestamp_us_);
      rsp.insert(rsp.end(), std::begin(uwb_time_bytes), std::end(uwb_time_bytes));

      uint8_t uncertainty_bytes[4];
      static_assert(sizeof(uncertainty_bytes) == sizeof(uncertainty_us_));
      cpu_to_le_bytes(uncertainty_bytes, uncertainty_us_);
      rsp.insert(rsp.end(), std::begin(uncertainty_bytes), std::end(uncertainty_bytes));
    }
    rsp[UCI_PAYLOAD_LENGTH_OFFSET] = rsp.size() - UCI_MSG_HDR_SIZE;
    return rsp;
  }

 private:
  uint8_t status_;          // UCI status code.

  // These variables are only valid when status_ == UCI_STATUS_OK.
  uint64_t system_time_us_; // System time when the timestamp is captured.
  uint64_t timestamp_us_;   // UWBS timesetamp.
  uint32_t uncertainty_us_; // Uncertainty.
};

class AsyncTimestamp {
 public:
  AsyncTimestamp() {
    use_async_timestamp_ =
      NxpConfig_GetBool(NAME_CCC_USE_ASYNC_TIMESTAMP).value_or(false);

    if (use_async_timestamp_) {
      // Register RX handler for TIMESYNC_TIMESTAMP_NTF.
      rx_handler_timestamp_ntf_ = phNxpUciHal_rx_handler_add(
        UCI_MT_NTF, UCI_GID_PROPRIETARY, UCI_MSG_TIMESYNC_TIMESTAMP_NTF,
        /*run_once=*/false,
        std::bind(&AsyncTimestamp::OnTimestampNtf, this,
          std::placeholders::_1, std::placeholders::_2));
    }
  }

  virtual ~AsyncTimestamp() {
    if (rx_handler_timestamp_ntf_ != nullptr) {
      phNxpUciHal_rx_handler_del(rx_handler_timestamp_ntf_);
    }
  }

  // Checks the latest timestamp value's validity.
  // And Waits for another async notification if the saved one
  // is no longer fresh to the current system time.
  UwbTimestamp PickAsyncTimestamp(const uint64_t system_time_now) {
    if (!use_async_timestamp_) {
      return UwbTimestamp {UCI_STATUS_FAILED};
    }

    NXPLOG_UCIHAL_D("Async timestamp requested at %" PRIu64, system_time_now);

    // A fast path - the saved timestamp value is fresh enough, let's use this.
    UwbTimestamp timestamp = GetLatestTimestamp();
    if (IsFreshTimestamp(system_time_now, timestamp)) {
      NXPLOG_UCIHAL_D("Picked a fresh timestamp %" PRIu64 " captured at %" PRIu64,
        timestamp_.GetTimestamp(), timestamp_.GetSystemTime());
      return timestamp;
    }

    if (timestamp.GetStatus() == UCI_STATUS_OK) {
      NXPLOG_UCIHAL_D("Current timestamp %" PRIu64 " captured at %" PRIu64 ", refresh!",
        timestamp.GetTimestamp(), timestamp.GetSystemTime());
    } else {
      NXPLOG_UCIHAL_D("No valid async timestamp received, refresh!");
    }

    // Refresh loop
    const uint64_t deadline = GetSystemTime() + kTimestampNtfRefreshWindowMs * 1000;
    while (GetSystemTime() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kTimestampNtfRefreshPollingMs));
      timestamp = GetLatestTimestamp();
      if (IsFreshTimestamp(system_time_now, timestamp)) {
        return timestamp;
      }
    }

    return UwbTimestamp {UCI_STATUS_FAILED};
  }

 private:
  // TIMESYNC_TIMESTAMP_NTF rx handler.
  bool OnTimestampNtf(size_t packet_len, const uint8_t *packet) {
    const uint64_t system_time = GetSystemTime();

    if (packet_len != UCI_MSG_TIMESYNC_TIMESTAMP_NTF_LENGTH) {
      NXPLOG_UCIHAL_E("Unrecognized TIMESYNC_TIMESTAMP_NTF packet.");
      return true;
    }

    const uint8_t status = packet[UCI_MSG_TIMESYNC_TIMESTAMP_NTF_OFFSET_STATUS];
    if (status != UCI_STATUS_OK) {
      NXPLOG_UCIHAL_E("UCI_MSG_TIMESYNC_TIMESTAMP with status code 0x%x", status);
      return true;
    }

    const uint8_t flag = packet[UCI_MSG_TIMESYNC_TIMESTAMP_NTF_OFFSET_FLAG];

    const uint64_t uwbs_time =
      le_bytes_to_cpu<uint64_t>(&packet[UCI_MSG_TIMESYNC_TIMESTAMP_NTF_OFFSET_TIMESTAMP]);

    const uint32_t uncertainty =
      (flag == UCI_MSG_TIMESYNC_TIMESTAMP_FLAG_HIGH_ACCURACY) ?
        kUwbChipTimestampAccuracyHighUs : kUwbChipTimestampAccuracyLowUs;

    UwbTimestamp timestamp { system_time, uwbs_time, uncertainty };
    SaveTimestamp(timestamp);
    NXPLOG_UCIHAL_D("Async timestamp received: %" PRIu64 " system_time=%" PRIu64 ", uncertainty=0x%x",
      timestamp.GetTimestamp(), timestamp.GetSystemTime(), timestamp.GetUncertainty());

    return true;
  }

  void SaveTimestamp(const UwbTimestamp& timestamp) {
    std::lock_guard<std::mutex> lock(lock_);
    timestamp_ = timestamp;
  }

  UwbTimestamp GetLatestTimestamp() {
    std::lock_guard<std::mutex> lock(lock_);
    return timestamp_;
  }

  bool IsFreshTimestamp(const uint64_t system_time_now, const UwbTimestamp& timestamp) {
    return timestamp.GetStatus() == UCI_STATUS_OK &&
           timestamp.GetSystemTime() >= (system_time_now - kTimestampNtfFreshMs * 1000);
  }

 private:
  // The flag controls whether Async timestamps is being used.
  bool use_async_timestamp_;

  // Saved asynchronous timestamp value from TIMESYNC_TIMESTAMP_NTF
  UwbTimestamp timestamp_;
  std::mutex lock_;

  // RX handler for TIMESYNC_TIMESTAMP_NTF.
  std::shared_ptr<phNxpUciHal_RxHandler> rx_handler_timestamp_ntf_;
};

std::unique_ptr<AsyncTimestamp> async_timestamp;

// Converts given system time to the timestamp in UWBS time domain
// by calculating the offset between system time and UWBS time.
// ble_time_base is just for the debug logs.
UwbTimestamp ConvertTimestamp(const uint64_t system_time_base,
                              const uint64_t ble_time_base)
{
  // All time values are monotonic / micro-seconds.
  uint64_t system_time_0 = GetSystemTime();

  std::optional<uint64_t> uwbs_time = GetUwbsTimeStamp();

  uint64_t system_time_1 = GetSystemTime();

  uint64_t elapsed_time = system_time_1 - system_time_0;

  if (!uwbs_time.has_value()) {
    return UwbTimestamp { UCI_STATUS_FAILED };
  }

  uint64_t uwbs_time_us = uwbs_time.value();

  NXPLOG_UCIHAL_E("TimeSync: UWBS timestamp=%" PRIu64 ", elapsed=%" PRIu64,
    uwbs_time_us, elapsed_time);

  if (system_time_base != 0) {
    // given system time should be monotonic, or zero.
    if (system_time_0 < system_time_base ||
        system_time_0 > (system_time_base + kSystemTimeMaxLatencyUs)) {
      NXPLOG_UCIHAL_E("TimeSync: Given system time %" PRIu64 " is incompatible. "
        "current system time=%" PRIu64 " - %" PRIu64,
        system_time_base, system_time_0, system_time_1);
      return UwbTimestamp { UCI_STATUS_INVALID_PARAM };
    }
  }

  if (elapsed_time > kUwbsTimeMaxLatencyUs) {
    NXPLOG_UCIHAL_E("TimeSync: took too long time to get uwbs timestamp %" PRIu64,
      elapsed_time);
    return UwbTimestamp { UCI_STATUS_FAILED };
  }

  // Applies the elapsed time in this function.
  uwbs_time_us -= (elapsed_time / 2);

  // Applies the elapsed time before this function.
  if (system_time_base != 0) {
    uwbs_time_us -= (system_time_0 - system_time_base);
  }

  uint32_t uncertainty_us = elapsed_time + kExtraUncertaintyInConvertUs;

  UwbTimestamp timestamp { system_time_base, uwbs_time_us, uncertainty_us };

  NXPLOG_UCIHAL_D("TimeSync: convert system-time %" PRIu64 ", ble-time %" PRIu64
    " --> uwbs-time %" PRIu64 ", uncertainty %" PRIu32,
    system_time_base, ble_time_base,
    timestamp.GetTimestamp(), timestamp.GetUncertainty());

  return timestamp;
}

}   // namespace

// Returns the current AP (monotonic) time in micro-seconds.
uint64_t GetSystemTime() {
  struct timespec ts;
  ::clock_gettime(CLOCK_BOOTTIME, &ts);
  return (ts.tv_sec * 1000000ull) + (ts.tv_nsec / 1000ull);
}

// Returns UWBS timestamp by querying.
std::optional<uint64_t> GetUwbsTimeStamp() {
  std::optional<uint64_t> timestamp;

  phNxpUciHal_rx_handler_add(UCI_MT_RSP, UCI_GID_CORE,
    UCI_MSG_CORE_QUERY_UWBS_TIMESTAMP, /*run_once=*/true,
    [&timestamp](size_t packet_len, const uint8_t *packet) -> bool {
      if (packet_len != UCI_MSG_CORE_QUERY_UWBS_TIMESTAMP_RSP_LEN) {
        NXPLOG_UCIHAL_E("TimeSync: Failed to decode CORE_QUERY_UWBS_TIMESTAMP_RSP");
        return true;
      }

      if (packet[4] != UWBSTATUS_SUCCESS) {
        NXPLOG_UCIHAL_E("TimeSync: Failed to get UWBS timestamp.");
        return true;
      }
      timestamp = le_bytes_to_cpu<uint64_t>(&packet[5]);
      return true;
    }
  );

  std::vector<uint8_t> packet {
    (UCI_MT_CMD << UCI_MT_SHIFT) | UCI_GID_CORE,
    UCI_MSG_CORE_QUERY_UWBS_TIMESTAMP,
    0, 0
  };

  phNxpUciHal_send_ext_cmd(packet.size(), packet.data());
  return timestamp;
}

void TimeSync_init()
{
  async_timestamp = std::make_unique<AsyncTimestamp>();
}

// ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_CMD tx handler
void TimeSync_onConvertTimestamp(size_t cmdlen, const uint8_t* cmd) {
  if (cmdlen != UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_CMD_LEN) {
      NXPLOG_UCIHAL_E("TimeSync: Failed to decode ANDROID_TIMESTAMP_ANCHOR_TO_UWBS.");
      const uint8_t rsp[] = {
        (UCI_MT_RSP << UCI_MT_SHIFT) | UCI_GID_ANDROID,
        UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS, 0, 1,
        UCI_STATUS_SYNTAX_ERROR
      };
      report_uci_message(rsp, sizeof(rsp));
      return;
  }

  const uint64_t system_time_base = le_bytes_to_cpu<uint64_t>(
    &cmd[UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_CMD_OFFSET_SYSTEM]);
  const uint64_t ble_time_base = le_bytes_to_cpu<uint64_t>(
    &cmd[UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_CMD_OFFSET_BLE]);

  UwbTimestamp timestamp;

  // Checks if "Asynchronos" timestamp from a chip is valid.
  // This is "high-precision" method.
  if (async_timestamp != nullptr) {
    timestamp = async_timestamp->PickAsyncTimestamp(system_time_base);
  }

  // Converts given system time to UWBS time by querying + latency compensation
  // as a fallback, "low-precision" method.
  if (timestamp.GetStatus() != UCI_STATUS_OK) {
    timestamp = ConvertTimestamp(system_time_base, ble_time_base);
  }

  // Reports it back to upper-layer.
  std::vector<uint8_t> rsp = timestamp.ConstructResponse();
  report_uci_message(rsp.data(), rsp.size());
}

