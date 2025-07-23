#ifndef _TIMESYNC_H_
#define _TIMESYNC_H_

#include <cstddef>
#include <cstdint>
#include <optional>

// Initializes timesync handler.
void TimeSync_init();

// Command packet handler of ANDROID_TIMESTAMP_ANCHOR_TO_UWBS (gid:0xc, oid:0x03).
void TimeSync_onConvertTimestamp(size_t cmdlen, const uint8_t* cmd);

// Returns system time (CPU boot time) in micro seconds.
uint64_t GetSystemTime();

// Returns the UWBS timestamp in micro seconds, or nullopt on fail.
std::optional<uint64_t> GetUwbsTimeStamp();

#endif  //_TIMESYNC_H_
