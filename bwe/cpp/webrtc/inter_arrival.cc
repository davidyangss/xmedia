/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// #include "webrtc/modules/remote_bitrate_estimator/inter_arrival.h"

#include <algorithm>
#include <cassert>

// #include "webrtc/base/logging.h"
// #include "webrtc/modules/include/module_common_types.h"

#include "inter_arrival.h"


inline bool IsNewerSequenceNumber(uint16_t sequence_number,
                                  uint16_t prev_sequence_number) {
  // Distinguish between elements that are exactly 0x8000 apart.
  // If s1>s2 and |s1-s2| = 0x8000: IsNewer(s1,s2)=true, IsNewer(s2,s1)=false
  // rather than having IsNewer(s1,s2) = IsNewer(s2,s1) = false.
  if (static_cast<uint16_t>(sequence_number - prev_sequence_number) == 0x8000) {
    return sequence_number > prev_sequence_number;
  }
  return sequence_number != prev_sequence_number &&
         static_cast<uint16_t>(sequence_number - prev_sequence_number) < 0x8000;
}

inline bool IsNewerTimestamp(uint32_t timestamp, uint32_t prev_timestamp) {
  // Distinguish between elements that are exactly 0x80000000 apart.
  // If t1>t2 and |t1-t2| = 0x80000000: IsNewer(t1,t2)=true,
  // IsNewer(t2,t1)=false
  // rather than having IsNewer(t1,t2) = IsNewer(t2,t1) = false.
  if (static_cast<uint32_t>(timestamp - prev_timestamp) == 0x80000000) {
    return timestamp > prev_timestamp;
  }
  return timestamp != prev_timestamp &&
         static_cast<uint32_t>(timestamp - prev_timestamp) < 0x80000000;
}

inline uint16_t LatestSequenceNumber(uint16_t sequence_number1,
                                     uint16_t sequence_number2) {
  return IsNewerSequenceNumber(sequence_number1, sequence_number2)
             ? sequence_number1
             : sequence_number2;
}

inline uint32_t LatestTimestamp(uint32_t timestamp1, uint32_t timestamp2) {
  return IsNewerTimestamp(timestamp1, timestamp2) ? timestamp1 : timestamp2;
}


namespace webrtc {

static const int kBurstDeltaThresholdMs  = 5;

InterArrival::InterArrival(uint32_t timestamp_group_length_ticks,
                           double timestamp_to_ms_coeff,
                           bool enable_burst_grouping)
    : kTimestampGroupLengthTicks(timestamp_group_length_ticks),
      current_timestamp_group_(),
      prev_timestamp_group_(),
      timestamp_to_ms_coeff_(timestamp_to_ms_coeff),
      burst_grouping_(enable_burst_grouping) {}

bool InterArrival::ComputeDeltas(uint32_t timestamp,
                                 int64_t arrival_time_ms,
                                 size_t packet_size,
                                 uint32_t* timestamp_delta,
                                 int64_t* arrival_time_delta_ms,
                                 int* packet_size_delta) {
  assert(timestamp_delta != NULL);
  assert(arrival_time_delta_ms != NULL);
  assert(packet_size_delta != NULL);
  bool calculated_deltas = false;
  if (current_timestamp_group_.IsFirstPacket()) {
    // We don't have enough data to update the filter, so we store it until we
    // have two frames of data to process.
    current_timestamp_group_.timestamp = timestamp;
    current_timestamp_group_.first_timestamp = timestamp;
  } else if (!PacketInOrder(timestamp)) {
    return false;
  }else if( (timestamp - current_timestamp_group_.timestamp) >= 0x80000000){ // simon-fix
    return false;
  }else if (NewTimestampGroup(arrival_time_ms, timestamp)) {
    // First packet of a later frame, the previous frame sample is ready.
    if (prev_timestamp_group_.complete_time_ms >= 0) {
      *timestamp_delta = current_timestamp_group_.timestamp -
                         prev_timestamp_group_.timestamp;
      *arrival_time_delta_ms = current_timestamp_group_.complete_time_ms -
                               prev_timestamp_group_.complete_time_ms;
      if (*arrival_time_delta_ms < 0) {
        // The group of packets has been reordered since receiving its local
        // arrival timestamp.
        // LOG(LS_WARNING) << "Packets are being reordered on the path from the "
        //                    "socket to the bandwidth estimator. Ignoring this "
        //                    "packet for bandwidth estimation.";
        return false;
      }
      assert(*arrival_time_delta_ms >= 0);
      *packet_size_delta = static_cast<int>(current_timestamp_group_.size) -
          static_cast<int>(prev_timestamp_group_.size);
      calculated_deltas = true;
    }
    prev_timestamp_group_ = current_timestamp_group_;
    // The new timestamp is now the current frame.
    current_timestamp_group_.first_timestamp = timestamp;
    current_timestamp_group_.timestamp = timestamp;
    current_timestamp_group_.size = 0;
  } else {
    current_timestamp_group_.timestamp = LatestTimestamp(
        current_timestamp_group_.timestamp, timestamp);
  }
  // Accumulate the frame size.
  current_timestamp_group_.size += packet_size;
  current_timestamp_group_.complete_time_ms = arrival_time_ms;

  return calculated_deltas;
}

bool InterArrival::PacketInOrder(uint32_t timestamp) {
  if (current_timestamp_group_.IsFirstPacket()) {
    return true;
  } else {
    // Assume that a diff which is bigger than half the timestamp interval
    // (32 bits) must be due to reordering. This code is almost identical to
    // that in IsNewerTimestamp() in module_common_types.h.
    uint32_t timestamp_diff = timestamp -
        current_timestamp_group_.first_timestamp;
    return timestamp_diff < 0x80000000;
  }
}

// Assumes that |timestamp| is not reordered compared to
// |current_timestamp_group_|.
bool InterArrival::NewTimestampGroup(int64_t arrival_time_ms,
                                     uint32_t timestamp) const {
  if (current_timestamp_group_.IsFirstPacket()) {
    return false;
  } else if (BelongsToBurst(arrival_time_ms, timestamp)) {
    return false;
  } else {
    uint32_t timestamp_diff = timestamp -
        current_timestamp_group_.first_timestamp;
    return timestamp_diff > kTimestampGroupLengthTicks;
  }
}

bool InterArrival::BelongsToBurst(int64_t arrival_time_ms,
                                  uint32_t timestamp) const {
  if (!burst_grouping_) {
    return false;
  }
  assert(current_timestamp_group_.complete_time_ms >= 0);
  int64_t arrival_time_delta_ms = arrival_time_ms -
      current_timestamp_group_.complete_time_ms;
  uint32_t timestamp_diff = timestamp - current_timestamp_group_.timestamp;
  int64_t ts_delta_ms = timestamp_to_ms_coeff_ * timestamp_diff + 0.5;
  if (ts_delta_ms == 0)
    return true;
  int propagation_delta_ms = arrival_time_delta_ms - ts_delta_ms;
  return propagation_delta_ms < 0 &&
      arrival_time_delta_ms <= kBurstDeltaThresholdMs;
}
}  // namespace webrtc
