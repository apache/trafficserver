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

#include <cstdio>
#include <ctime>

#include "tsutil/ts_iso8601.h"

namespace ts
{

size_t
format_iso8601_utc(std::chrono::system_clock::time_point tp, char *buf, size_t size)
{
  static constexpr size_t DATE_TIME_LEN = sizeof("YYYY-MM-DDTHH:MM:SS") - 1;

  if (size <= ISO8601_TIMESTAMP_LEN) {
    return 0;
  }

  // Floor rather than truncate so times before the epoch keep a non-negative millisecond part.
  auto   secs  = std::chrono::floor<std::chrono::seconds>(tp);
  auto   ms    = std::chrono::duration_cast<std::chrono::milliseconds>(tp - secs).count();
  time_t epoch = std::chrono::system_clock::to_time_t(secs);
  tm     utc;

  if (gmtime_r(&epoch, &utc) == nullptr) {
    return 0;
  }

  // A year outside four digits would make the output longer than ISO8601_TIMESTAMP_LEN.
  if (strftime(buf, size, "%Y-%m-%dT%H:%M:%S", &utc) != DATE_TIME_LEN) {
    return 0;
  }

  snprintf(buf + DATE_TIME_LEN, size - DATE_TIME_LEN, ".%03dZ", static_cast<int>(ms));

  return ISO8601_TIMESTAMP_LEN;
}

} // namespace ts
