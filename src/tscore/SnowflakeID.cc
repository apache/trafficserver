/**
  @file Implement Snowflake ID.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "tscore/SnowflakeID.h"
#include "tscore/Diags.h"
#include "tscore/ink_base64.h"
#include "tscore/ink_hrtime.h"
#include "tsutil/DbgCtl.h"

#include <cstdint>
#include <cinttypes>
#include <memory>
#include <mutex>
#include <string_view>

std::atomic<uint64_t> SnowflakeIDUtils::global_machine_id{0};

DbgCtl dbg_ctl_snowflake{"snowflake"};

SnowflakeIDUtils::SnowflakeIDUtils(uint64_t id) : m_snowflake_value{id} {}

void
SnowflakeIDUtils::set_machine_id(uint64_t machine_id)
{
  Dbg(dbg_ctl_snowflake, "Setting machine ID to: %" PRIx64, machine_id);
  global_machine_id = machine_id;
}

std::string_view
SnowflakeIDUtils::get_string() const
{
  if (m_id_string.empty()) {
    // Base64 encode the snowflake ID as m_id_string.
    constexpr size_t                   max_encoded_size = ats_base64_encode_dstlen(sizeof(m_snowflake_value));
    std::array<char, max_encoded_size> encoded_buffer;
    size_t                             encoded_length   = 0;
    auto                              *snowflake_char_p = reinterpret_cast<char const *>(&m_snowflake_value);
    if (ats_base64_encode(snowflake_char_p, sizeof(m_snowflake_value), encoded_buffer.data(), max_encoded_size, &encoded_length)) {
      m_id_string = std::string(encoded_buffer.data(), encoded_length);
    } else {
      // Very unlikely.
      Error("Failed to encode snowflake ID: %" PRIx64, m_snowflake_value);
    }
  }
  return m_id_string;
}

SnowflakeID::SnowflakeIDGenerator &
SnowflakeID::SnowflakeIDGenerator::instance()
{
  static SnowflakeIDGenerator g;
  return g;
}

uint64_t
SnowflakeID::SnowflakeIDGenerator::get_next_id()
{
  ink_release_assert(SnowflakeIDUtils::get_machine_id() != 0);
  snowflake_t new_snowflake;
  new_snowflake.pieces.always_zero = 0;
  new_snowflake.pieces.machine_id  = SnowflakeIDUtils::get_machine_id();
  uint64_t now                     = ink_hrtime_to_msec(ink_get_hrtime()) - SnowflakeIDUtils::EPOCH;

  // Comparing and setting uint64_t values is one CPU cycle each. Setting a
  // bit field takes more, maybe 6 CPU cycles or so. Therefore, we modify the
  // bit field values outside of the lock.
  uint64_t local_last_sequence = 0;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (now == m_last_timestamp) {
      // If the timestamp is the same as the last one, increment the sequence.
      ++m_last_sequence;
    } else if (now > m_last_timestamp) {
      // If the timestamp is greater than the last one, update the last
      // timestamp seen and reset the sequence.
      m_last_sequence  = 0;
      m_last_timestamp = now;
    } else { // now < m_last_timestamp
      // Presumably, another thread set an even newer timestamp than the one we
      // got before the lock. This would imply the lock was held over a
      // millisecond, which probably indicates the box is not healthy. This
      // should be exceedingly rare, but if it happens, we use the newer
      // timestamp that the other thread set.
      now              = m_last_timestamp;
      m_last_sequence += 1;
    }
    local_last_sequence = m_last_sequence;
  } // Release the lock.
  new_snowflake.pieces.timestamp = now;
  new_snowflake.pieces.sequence  = local_last_sequence;
  return new_snowflake.value;
}

uint64_t
SnowflakeID::generate_next_snowflake_value()
{
  return SnowflakeIDGenerator::instance().get_next_id();
}

SnowflakeID::SnowflakeID() : m_snowflake{.value = generate_next_snowflake_value()}, m_utils{m_snowflake.value} {}

std::string_view
SnowflakeID::get_string() const
{
  return m_utils.get_string();
}

uint64_t
SnowflakeIdNoSequence::generate_next_snowflake_value()
{
  snowflake_t new_snowflake;
  new_snowflake.pieces.always_zero = 0;
  new_snowflake.pieces.machine_id  = SnowflakeIDUtils::get_machine_id();
  uint64_t const now               = ink_hrtime_to_msec(ink_get_hrtime());
  new_snowflake.pieces.timestamp   = now;
  return new_snowflake.value;
}

SnowflakeIdNoSequence::SnowflakeIdNoSequence() : m_snowflake{.value = generate_next_snowflake_value()}, m_utils{m_snowflake.value}
{
}

std::string_view
SnowflakeIdNoSequence::get_string() const
{
  // No sequence number, so we can use the same method as SnowflakeIDUtils.
  return m_utils.get_string();
}
