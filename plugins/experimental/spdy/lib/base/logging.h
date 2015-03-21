/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LOGGING_H_E307AFC6_4429_42F6_8E66_4004C6A78795
#define LOGGING_H_E307AFC6_4429_42F6_8E66_4004C6A78795

#include <string>

extern "C" {

// TS logging APIs don't get format attributes, so make sure we have a
// compatible forward declaration.
void TSDebug(const char *, const char *, ...) __attribute__((format(printf, 2, 3)));

void TSError(const char *, ...) __attribute__((format(printf, 1, 2)));

int TSIsDebugTagSet(const char *);
}

template <typename T> std::string stringof(const T &);
#define cstringof(x) stringof(x).c_str()

#define debug_tag(tag, fmt, ...)          \
  do {                                    \
    if (unlikely(TSIsDebugTagSet(tag))) { \
      TSDebug(tag, fmt, ##__VA_ARGS__);   \
    }                                     \
  } while (0)

#define debug_protocol(fmt, ...) debug_tag("spdy.protocol", "%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define debug_plugin(fmt, ...) debug_tag("spdy.plugin", "%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define debug_http(fmt, ...) debug_tag("spdy.http", "%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)

// Internal logging helpers
namespace detail
{
template <typename T> struct named_value {
  const char *name;
  T value;
};

template <typename T, unsigned N>
const char *
match(const named_value<T>(&names)[N], const T &value)
{
  for (unsigned i = 0; i < N; ++i) {
    if (names[i].value == value) {
      return names[i].name;
    }
  }

  return "";
}
}

#endif /* LOGGING_H_E307AFC6_4429_42F6_8E66_4004C6A78795 */
/* vim: set sw=4 ts=4 tw=79 et : */
