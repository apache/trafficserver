/** @file

  ISO 8601 timestamp formatting shared by the diagnostic and error logs.

  @par License

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

#pragma once

#include <chrono>
#include <cstddef>

namespace ts
{

/// Length of a timestamp written by @c format_iso8601_utc, not counting the terminating NUL.
inline constexpr size_t ISO8601_TIMESTAMP_LEN = 24;

/** Write a time as ISO 8601 in UTC with milliseconds, for example @c 2026-10-08T18:58:01.123Z.
 *
 * The output is NUL terminated. Using UTC lets log lines from hosts in different time zones be
 * compared directly.
 *
 * @param tp The time to write.
 * @param buf Destination buffer.
 * @param size Size of @a buf, which must be greater than @c ISO8601_TIMESTAMP_LEN.
 * @return The number of characters written, not counting the NUL, or 0 if @a buf is too small or
 * the year is outside 0000-9999.
 */
size_t format_iso8601_utc(std::chrono::system_clock::time_point tp, char *buf, size_t size);

} // namespace ts
