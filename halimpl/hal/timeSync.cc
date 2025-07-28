#include <cinttypes>
#include <cstdint>
#include <ctime>
#include <optional>
#include <vector>

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

// Returns the UWBS timestamp in micro seconds.
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
      timestamp = le_bytes_to_cpu<uint32_t>(&packet[5]);
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

// Returns system time in micro seconds.
uint64_t GetSystemTime() {
  struct timespec ts;
  ::clock_gettime(CLOCK_BOOTTIME, &ts);
  return (ts.tv_sec * 1000000ull) + (ts.tv_nsec / 1000ull);
}

struct UwbTimestamp {
  uint8_t status;
  uint64_t timestamp_us;
  uint64_t uncertainty_us;

  UwbTimestamp(uint8_t status) : status(status) {}
};

// Convert given system time to the timestamp in UWBS time domain
// by calculating the offset between system time and UWBS time.
UwbTimestamp ConvertTimestamp(const uint64_t system_time_base)
{
  // All time values are monotonic / micro-seconds.
  uint64_t system_time_0 = GetSystemTime();

  std::optional<uint64_t> uwbs_time = GetUwbsTimeStamp();

  uint64_t system_time_1 = GetSystemTime();

  uint64_t elapsed_time = system_time_1 - system_time_0;

  if (!uwbs_time.has_value()) {
    return UwbTimestamp { UCI_STATUS_FAILED };
  }

  NXPLOG_UCIHAL_E("TimeSync: UWBS timestamp=%" PRIu64 ", elapsed=%" PRIu64,
    uwbs_time.value(), elapsed_time);

  if (system_time_base != 0) {
    // given system time should be monotonic, or zero.
    if (system_time_0 < system_time_base ||
        system_time_0 > (system_time_base + kSystemTimeMaxLatencyUs)) {
      NXPLOG_UCIHAL_E("TimeSync: Given system time %" PRIu64 " is incompatible. "
        "current system time=%" PRIu64 " - %" PRIu64,
        system_time_base, system_time_0, system_time_1);
      return UwbTimestamp { UCI_STATUS_FAILED };
    }
  }

  if (elapsed_time > kUwbsTimeMaxLatencyUs) {
    NXPLOG_UCIHAL_E("TimeSync: took too long time to get uwbs timestamp %" PRIu64,
      elapsed_time);
    return UwbTimestamp { UCI_STATUS_FAILED };
  }

  // Converts the given system time to UWBS time domain
  UwbTimestamp timestamp { UCI_STATUS_OK };
  if (system_time_base != 0) {
    timestamp.timestamp_us =
      (system_time_0 + elapsed_time / 2) - system_time_base + uwbs_time.value();
  } else {
    // If given base system time is zero, don't consider the offset
    // between system time and UWBS time.
    timestamp.timestamp_us = uwbs_time.value();
  }
  timestamp.uncertainty_us = elapsed_time + kExtraUncertaintyInConvertUs;

  NXPLOG_UCIHAL_D("TimeSync: timestamp converted %" PRIu64 " --> %"
    PRIu64 " / %" PRIu64,
    system_time_base, timestamp.timestamp_us, timestamp.uncertainty_us);

  return timestamp;
}

}   // namespace

void TimeSync_init()
{

}

void TimeSync_onConvertTimestamp(size_t cmdlen, const uint8_t* cmd)
{
  std::vector<uint8_t> rsp {
    (UCI_MT_RSP << UCI_MT_SHIFT) | UCI_GID_ANDROID,
    UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS, 0, 0,
  };

  if (cmdlen != UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_CMD_LEN) {
      NXPLOG_UCIHAL_E("TimeSync: Failed to decode ANDROID_TIMESTAMP_ANCHOR_TO_UWBS.");
      rsp.push_back(UCI_STATUS_SYNTAX_ERROR);
      rsp[UCI_PAYLOAD_LENGTH_OFFSET] = rsp.size() - UCI_MSG_HDR_SIZE;
      report_uci_message(rsp.data(), rsp.size());
      return;
  }

  const uint64_t system_time_base = le_bytes_to_cpu<uint64_t>(
    &cmd[UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_CMD_OFFSET_SYSTEM]);

  UwbTimestamp timestamp = ConvertTimestamp(system_time_base);
  rsp.push_back(timestamp.status);

  if (timestamp.status == UCI_STATUS_OK) {
    uint8_t bytes[8];

    cpu_to_le_bytes(bytes, timestamp.timestamp_us);
    rsp.insert(rsp.end(), std::begin(bytes), std::end(bytes));

    cpu_to_le_bytes(bytes, timestamp.uncertainty_us);
    rsp.insert(rsp.end(), std::begin(bytes), std::end(bytes));
  }

  rsp[UCI_PAYLOAD_LENGTH_OFFSET] = rsp.size() - UCI_MSG_HDR_SIZE;
  report_uci_message(rsp.data(), rsp.size());
}

