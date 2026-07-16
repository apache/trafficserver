/** @file

  Shared helpers for the packet-mark test plugins.

  The plugin reads a target mark out of a request header, applies it to a
  connection via the tsapi under test, reads the applied mark back off the
  relevant socket with getsockopt(SO_MARK), and echoes the observed value into a
  response header for the accompanying AuTest to assert on. Everything except
  the tsapi call and the fd getter is identical, so it lives here.

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

#include <ts/ts.h>

#include <string_view>

namespace packet_mark
{
struct LogContext {
  std::string_view plugin_name;
  const DbgCtl    &dbg_ctl;
};

void apply_client_mark(const LogContext &log, TSHttpTxn txnp, std::string_view header);

void echo_client_mark(const LogContext &log, TSHttpTxn txnp, std::string_view echo_header);

} // namespace packet_mark
