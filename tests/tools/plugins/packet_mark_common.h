/** @file

  Shared helpers for the client_packet_mark and server_packet_mark test plugins.

  Both plugins read a target mark out of a request header, apply it to a
  connection via the tsapi under test, read the applied mark back off the
  relevant socket with getsockopt(SO_MARK), and echo the observed value into a
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

void apply_server_mark(const LogContext &log, TSHttpTxn txnp, std::string_view header);

// Masked client variant: sets only the bits selected by @a mask_header using the
// three-argument TSHttpTxnClientPacketMarkSet overload. Kept separate from
// apply_client_mark so the existing whole-mark path is unchanged. Returns true when
// a masked set was performed (both headers present), so the caller can skip the
// whole-mark path and avoid clobbering the value the masked read-modify-write is
// supposed to preserve.
bool apply_client_mark_masked(const LogContext &log, TSHttpTxn txnp, std::string_view mark_header, std::string_view mask_header);

// Masked server variant: the server-side mirror of apply_client_mark_masked, using
// the three-argument TSHttpTxnServerPacketMarkSet overload. Same true/false
// contract so the caller can skip the whole-mark path when a masked set ran.
bool apply_server_mark_masked(const LogContext &log, TSHttpTxn txnp, std::string_view mark_header, std::string_view mask_header);

void echo_client_mark(const LogContext &log, TSHttpTxn txnp, std::string_view echo_header);

void echo_server_mark(const LogContext &log, TSHttpTxn txnp, std::string_view echo_header);

} // namespace packet_mark
