/** @file

  A brief file description

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
#ifndef LOG_UTILS_H
#define LOG_UTILS_H

#include "time.h"

#include "ts/ink_platform.h"
#include "ts/Arena.h"

namespace LogUtils
{
enum AlarmType {
  LOG_ALARM_ERROR = 0,
  LOG_ALARM_WARNING,
  LOG_ALARM_N_TYPES,
};

static inline long
timestamp()
{
  return (long)time(0);
}

int timestamp_to_str(long timestamp, char *buf, int size);
char *timestamp_to_netscape_str(long timestamp);
char *timestamp_to_date_str(long timestamp);
char *timestamp_to_time_str(long timestamp);
unsigned ip_from_host(char *host);
void manager_alarm(AlarmType alarm_type, const char *msg, ...) TS_PRINTFLIKE(2, 3);
void strip_trailing_newline(char *buf);
char *escapify_url(Arena *arena, char *url, size_t len_in, int *len_out, char *dst = nullptr, size_t dst_size = 0,
                   const unsigned char *map = nullptr);
char *pure_escapify_url(Arena *arena, char *url, size_t len_in, int *len_out, char *dst = nullptr, size_t dst_size = 0,
                        const unsigned char *map = nullptr);
char *int64_to_str(char *buf, unsigned int buf_size, int64_t val, unsigned int *total_chars, unsigned int req_width = 0,
                   char pad_char = '0');
void remove_content_type_attributes(char *type_str, int *type_len);
int timestamp_to_hex_str(unsigned timestamp, char *str, size_t len, size_t *n_chars = 0);
int seconds_to_next_roll(time_t time_now, int rolling_offset, int rolling_interval);
int file_is_writeable(const char *full_filename, off_t *size_bytes = 0, bool *has_size_limit = 0,
                      uint64_t *current_size_limit_bytes = 0);
};
#endif
