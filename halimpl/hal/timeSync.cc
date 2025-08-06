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

struct UwbTimestamp {
  uint8_t status;
  uint64_t timestamp_us;
  uint32_t uncertainty_us;

  explicit UwbTimestamp(uint8_t status) : status(status) {
    // better to check status != UCI_STATUS_OK here but.
    // status codes are not defined with constexpr.
  }

  UwbTimestamp(uint64_t uwbs_time, uint32_t uncertainty) :
    status(UCI_STATUS_OK),
    timestamp_us(uwbs_time), uncertainty_us(uncertainty) {}
};

// Convert given system time to the timestamp in UWBS time domain
// by calculating the offset between system time and UWBS time.
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

  uint64_t uwbs_time_us = uwbs_time.value();
  if (system_time_base != 0) {
    // Applies the elapsed time in querying UWBS timestamp.
    uwbs_time_us += (system_time_0 + elapsed_time / 2) - system_time_base;
  }
  uint32_t uncertainty_us = elapsed_time + kExtraUncertaintyInConvertUs;

  UwbTimestamp timestamp { uwbs_time_us, uncertainty_us };

  NXPLOG_UCIHAL_D("TimeSync: convert system-time %" PRIu64 ", ble-time %" PRIu64
    " --> uwbs-time %" PRIu64 ", uncertainty %" PRIu32,
    system_time_base, ble_time_base,
    timestamp.timestamp_us, timestamp.uncertainty_us);

  return timestamp;
}

}   // namespace

uint64_t GetSystemTime() {
  struct timespec ts;
  ::clock_gettime(CLOCK_BOOTTIME, &ts);
  return (ts.tv_sec * 1000000ull) + (ts.tv_nsec / 1000ull);
}

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
  const uint64_t ble_time_base = le_bytes_to_cpu<uint64_t>(
    &cmd[UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_CMD_OFFSET_BLE]);

  UwbTimestamp timestamp = ConvertTimestamp(system_time_base, ble_time_base);

  // Constructs 17 bytes RSP packet.
  rsp.push_back(timestamp.status);

  if (timestamp.status == UCI_STATUS_OK) {
    uint8_t uwb_time_bytes[8];
    cpu_to_le_bytes(uwb_time_bytes, timestamp.timestamp_us);
    rsp.insert(rsp.end(), std::begin(uwb_time_bytes), std::end(uwb_time_bytes));

    uint8_t uncertainty_bytes[4];
    static_assert(sizeof(uncertainty_bytes) == sizeof(timestamp.uncertainty_us));
    cpu_to_le_bytes(uncertainty_bytes, timestamp.uncertainty_us);
    rsp.insert(rsp.end(), std::begin(uncertainty_bytes), std::end(uncertainty_bytes));
  }

  uint8_t payload_len = rsp.size() - UCI_MSG_HDR_SIZE;

  if ((payload_len + UCI_MSG_HDR_SIZE) != UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_RSP_LEN) {
    // This should not happen.
    NXPLOG_UCIHAL_E("ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_RSP payload length unexpected!");
    rsp[UCI_MSG_ANDROID_TIMESTAMP_ANCHOR_TO_UWBS_RSP_OFFSET_STATUS] = UCI_STATUS_FAILED;
    payload_len = 1;
  }
  rsp[UCI_PAYLOAD_LENGTH_OFFSET] = payload_len;
  report_uci_message(rsp.data(), rsp.size());
}

