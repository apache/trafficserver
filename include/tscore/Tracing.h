/** @file

  ATS Tracing API

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

#pragma once

#include <atomic>
#include <memory>
#include <string_view>
#include "tscore/ink_assert.h"
#include "tscore/ink_config.h"

using TRACER = int;
inline void
tracing_tag(TRACER &out, std::string_view name, std::string_view value)
{
  ink_assert(!"No tracng library is available");
}

inline void
tracing_tag(TRACER &out, std::string_view name, int value)
{
  ink_assert(!"No tracng library is available");
}

inline void
tracing_log(TRACER &out, std::string_view category, std::string_view tag)
{
  ink_assert(!"No tracng library is available");
}

inline void
tracing_log(TRACER &out, std::string_view category, int value)
{
  ink_assert(!"No tracng library is available");
}

inline TRACER *
tracing_new(const char *name)
{
  return nullptr;
}

inline void
tracing_delete(TRACER *tracer)
{
}

class Tracing
{
public:
  void enable(int value = 1);
  void disable();

  bool is_enabled();

  TRACER *make_tracer(const char *name);
  void delete_tracer(TRACER *tracer);

private:
  std::atomic<int> _enabled;
};

inline bool
Tracing::is_enabled()
{
  return _enabled > 0;
}

inline TRACER *
Tracing::make_tracer(const char *name)
{
  return tracing_new(name);
}

inline void
Tracing::delete_tracer(TRACER *tracer)
{
  tracing_delete(tracer);
}

extern std::unique_ptr<Tracing> tracing;

#define TRACE_TAG(out, category, message)   \
  do {                                      \
    if (out) {                              \
      tracing_tag(*out, category, message); \
    }                                       \
  } while (0)

#define TRACE_LOG(out, category, message)   \
  do {                                      \
    if (out) {                              \
      tracing_log(*out, category, message); \
    }                                       \
  } while (0)
