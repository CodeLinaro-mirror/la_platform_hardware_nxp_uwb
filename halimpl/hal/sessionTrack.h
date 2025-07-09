#ifndef _SESSIONTRACK_H_
#define _SESSIONTRACK_H_

#include <cstdint>
#include <optional>

void SessionTrack_init();
void SessionTrack_deinit();
void SessionTrack_onCountryCodeChanged();
void SessionTrack_onAppConfig(uint32_t session_handle,
                              std::optional<uint8_t> channel,
                              std::optional<int> max_rr_retry,
                              std::optional<int> max_measurements);
void SessionTrack_keepAlive();
void SessionTrack_onSessionInit(size_t packet_len, const uint8_t *packet);
void SessionTrack_onSessionStart(size_t packet_len, const uint8_t *packet);
void SessionTrack_onSessionDeinit(size_t packet_len, const uint8_t* packet);
void SessionTrack_onSessionStop(size_t packet_len, const uint8_t* packet);

#endif