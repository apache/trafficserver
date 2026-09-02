/** @file

  Per-plugin identity carried on the continuations a plugin creates.

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

#include <string>
#include <string_view>

#include "tscore/Ptr.h"
#include "tsutil/Metrics.h"

/** Carries a plugin's identity on the continuations it creates so that
 *  proxy.process.plugin.<name>.* workload counters can be attributed back to the originating
 *  plugin DSO.
 *
 *  This lives in ts::proxy rather than ts::http_remap because it is shared by both remap plugins
 *  (PluginDso) and global plugins (GlobalPluginContext, in Plugin.cc). The library dependency only
 *  runs ts::http_remap -> ts::proxy, so putting it here lets both paths resolve these symbols. */
class PluginThreadContext : public RefCountObjInHeap
{
public:
  virtual void acquire() = 0;
  virtual void release() = 0;

  /** Register this plugin's proxy.process.plugin.<name>.* metrics. @a plugin_name is the DSO path;
   *  only its basename stem (extension removed) is used as <name>. */
  void registerPluginMetrics(std::string_view plugin_name);

  void countInvocation();

  ts::Metrics::Counter::AtomicType *_invocations = nullptr;
  ts::Metrics::Counter::AtomicType *_bytes       = nullptr;
  ts::Metrics::Counter::AtomicType *_transfers   = nullptr;

  static constexpr const char *const _tag = "plugin_context"; /** @brief log tag used by this class */

private:
  /** Derive a metric-safe token from a plugin path: the basename with the extension removed, then any
   *  character outside [A-Za-z0-9_-] replaced by '_' (e.g. "/.../header_rewrite.so" -> "header_rewrite"). */
  static std::string _metric_token(std::string_view name);
};
