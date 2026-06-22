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

#include "proxy/PluginThreadContext.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

void
PluginThreadContext::registerPluginMetrics(std::string_view plugin_name)
{
  std::string prefix = "proxy.process.plugin." + _metric_token(plugin_name) + ".";

  _invocations = ts::Metrics::Counter::createPtr(prefix + "invocations");
  _bytes       = ts::Metrics::Counter::createPtr(prefix + "bytes");
  _transfers   = ts::Metrics::Counter::createPtr(prefix + "transfers");
}

void
PluginThreadContext::countInvocation()
{
  if (_invocations != nullptr) {
    _invocations->increment(1);
  }
}

std::string
PluginThreadContext::_metric_token(std::string_view name)
{
  if (auto slash = name.find_last_of('/'); slash != std::string_view::npos) {
    name.remove_prefix(slash + 1);
  }
  if (auto dot = name.find_last_of('.'); dot != std::string_view::npos) {
    name = name.substr(0, dot);
  }

  std::string token{name};
  std::replace_if(token.begin(), token.end(), [](unsigned char c) { return !(std::isalnum(c) || c == '_' || c == '-'); }, '_');
  if (token.empty()) {
    token = "unknown";
  }
  return token;
}
