/** @file

  ATS Tracing API implementation (OpenTracing)

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

#include <opentracing/dynamic_load.h>
#include <opentracing/tracer.h>
#include <opentracing/span.h>
#include <string_view>
#include <vector>
#include "ink_thread.h"
#include "Diags.h"

using TRACER = opentracing::Span;

extern opentracing::expected<opentracing::DynamicTracingLibraryHandle, std::error_code> ot_lib;
extern std::string ot_config;
extern std::mutex tracers_mutex;
extern std::vector<std::shared_ptr<opentracing::Tracer>> tracers;
extern ink_thread_key thread_specific_tracer_key;
extern std::string ot_config;
extern opentracing::expected<opentracing::DynamicTracingLibraryHandle, std::error_code> ot_lib;

inline void
tracing_tag(TRACER &out, std::string_view name, std::string_view value)
{
  out.SetTag({name.data(), name.length()}, opentracing::string_view(value.data(), value.length()));
}

inline void
tracing_tag(TRACER &out, std::string_view name, int value)
{
  out.SetTag({name.data(), name.length()}, value);
}

inline void
tracing_log(TRACER &out, std::string_view category, std::string_view message)
{
  out.Log({{category.data(), category.length()}, {message.data(), message.length()}});
}

inline void
tracing_log(TRACER &out, std::string_view category, int value)
{
  out.Log({{opentracing::string_view(category.data(), category.length()), opentracing::Value(value)}});
}

inline TRACER *
tracing_new(const char *name)
{
  opentracing::Tracer *tracer = static_cast<opentracing::Tracer *>(ink_thread_getspecific(thread_specific_tracer_key));
  if (tracer == nullptr) {
    std::lock_guard<std::mutex> lock(tracers_mutex);
    std::string ot_emsg;
    auto t = std::static_pointer_cast<opentracing::Tracer>(*ot_lib->tracer_factory().MakeTracer(ot_config.c_str(), ot_emsg));
    if (t) {
      tracers.push_back(t);
      ink_thread_setspecific(thread_specific_tracer_key, t.get());
      tracer = t.get();
    } else {
      Error("Failed to inintialize tracer: %s", ot_emsg.c_str());
      return nullptr;
    }
  }
  auto span = tracer->StartSpan(name);
  return span.release();
}

inline void
tracing_delete(TRACER *tracer)
{
  tracer->Finish();
  delete tracer;
}
