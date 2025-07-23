#ifndef _SESSIONTRACK_H_
#define _SESSIONTRACK_H_

#include <cstdint>
#include <optional>

struct SessionAppConfig {
  std::optional<uint8_t> channel;
  std::optional<int> max_rr_retry;
  std::optional<uint64_t> uwb_init_time;
  std::optional<int> max_measurements;

  void Append(const SessionAppConfig& config) {
    if (config.channel.has_value())
      channel = config.channel.value();
    if (config.max_rr_retry.has_value())
      max_rr_retry = config.max_rr_retry.value();
    if (config.uwb_init_time.has_value())
      uwb_init_time = config.uwb_init_time.value();
    if (config.max_measurements.has_value())
      max_measurements = config.max_measurements.value();
  }

  // getters return fira uci's default values when it's unset.
  uint8_t get_channel() const {
    return channel.value_or(9);
  }

  int get_max_rr_retry() const {
    return max_rr_retry.value_or(0);
  }

  uint64_t get_uwb_init_time() const {
    return uwb_init_time.value_or(0);
  }

  int get_max_measurements() const {
    return max_measurements.value_or(0);
  }
};

void SessionTrack_init();
void SessionTrack_deinit();
void SessionTrack_onCountryCodeChanged();
void SessionTrack_onAppConfig(uint32_t session_handle, const SessionAppConfig& config);
void SessionTrack_keepAlive();
void SessionTrack_onSessionInit(size_t packet_len, const uint8_t *packet);
void SessionTrack_onSessionStart(size_t packet_len, const uint8_t *packet);
void SessionTrack_onSessionDeinit(size_t packet_len, const uint8_t* packet);
void SessionTrack_onSessionStop(size_t packet_len, const uint8_t* packet);

#endif
