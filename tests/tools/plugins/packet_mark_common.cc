/** @file

  Shared helpers for the packet-mark test plugins.

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

#include "packet_mark_common.h"

extern "C" {
#include <sys/socket.h>
}

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace packet_mark
{
namespace
{
  // The tsapi setter and getters are bound as non-type template parameters below,
  // which pins the correct client/server trio at compile time. These must be raw
  // function-pointer types, not std::function: a non-type template parameter has
  // to be a structural type, and std::function is a runtime type-erasure wrapper,
  // so template <std::function<...> Setter> is ill-formed.
  using MarkSetter = TSReturnCode (*)(TSHttpTxn, int);
  using FdGetter   = TSReturnCode (*)(TSHttpTxn, int *);
  using RespGetter = TSReturnCode (*)(TSHttpTxn, TSMBuffer *, TSMLoc *);

  std::optional<uint32_t>
  get_uint_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view header)
  {
    // Values are parsed with strtoul (base 0), so "0x0000000A" and "10" are both
    // accepted. Returns std::nullopt if the header is absent, empty, or not a valid
    // number -- a malformed value is a test-harness error, not a silent 0.
    TSMLoc field_loc{TSMimeHdrFieldFind(bufp, hdr_loc, header.data(), static_cast<int>(header.length()))};
    if (field_loc == TS_NULL_MLOC) {
      return std::nullopt;
    }

    int         value_len{0};
    char const *value_str{TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &value_len)};

    std::optional<uint32_t> result{std::nullopt};
    if (value_str != nullptr && value_len > 0) {
      std::string value{value_str, static_cast<std::size_t>(value_len)};
      char       *end{nullptr};
      errno = 0;
      unsigned long const parsed{std::strtoul(value.c_str(), &end, 0)};
      // Reject empty, partially-numeric, or out-of-range values: a malformed
      // header is a test-harness error, not a silent 0.
      if (errno == 0 && end == value.c_str() + value.size() && parsed <= UINT32_MAX) {
        result = static_cast<uint32_t>(parsed);
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    return result;
  }

  void
  set_echo_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view header, uint32_t value)
  {
    // 0x + 8 hex digits for a uint32_t + NUL = 11 bytes; 16 is comfortably enough.
    char formatted[16];
    std::snprintf(formatted, sizeof(formatted), "0x%08x", value);

    TSMLoc field_loc{TS_NULL_MLOC};
    if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, header.data(), static_cast<int>(header.length()), &field_loc) == TS_SUCCESS) {
      // -1 length lets the API strlen the null-terminated buffer, so we do not
      // rely on snprintf's return value (which is the would-be length, not the
      // truncated length) as a byte count.
      TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, formatted, -1);
      TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }
  }

  std::optional<uint32_t>
  get_so_mark([[maybe_unused]] int fd)
  {
#if defined(SO_MARK)
    if (fd < 0) {
      return std::nullopt;
    }

    uint32_t  observed{0};
    socklen_t optlen{sizeof(observed)};
    if (getsockopt(fd, SOL_SOCKET, SO_MARK, &observed, &optlen) != 0) {
      return std::nullopt;
    }
    return observed;
#else
    // SO_MARK is Linux-only. On other platforms the accompanying AuTest is
    // skipped via Test.SkipUnless, so this readback path is never exercised;
    // keep it compilable so the plugins still build everywhere.
    return std::nullopt;
#endif
  }

  // Parameterized on the exact tsapi function and kept private to this file,
  // driven only by the named entry points below. The public API is split by
  // client/server rather than taking the function as an argument so each plugin
  // links against exactly the tsapi trio it exercises.
  template <MarkSetter Setter>
  void
  apply_mark_from_header(const LogContext &log, TSHttpTxn txnp, std::string_view header)
  {
    TSMBuffer req_bufp{nullptr};
    TSMLoc    req_loc{TS_NULL_MLOC};
    if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) != TS_SUCCESS) {
      TSError("[%.*s] Failed to get client request headers", static_cast<int>(log.plugin_name.length()), log.plugin_name.data());
      return;
    }

    std::optional<uint32_t> const mark{get_uint_header(req_bufp, req_loc, header)};
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);

    if (mark.has_value()) {
      Dbg(log.dbg_ctl, "Setting packet mark to 0x%08x (via %.*s)", *mark, static_cast<int>(header.length()), header.data());
      if (Setter(txnp, static_cast<int>(*mark)) != TS_SUCCESS) {
        TSError("[%.*s] Failed to set packet mark 0x%08x", static_cast<int>(log.plugin_name.length()), log.plugin_name.data(),
                *mark);
      }
    }
  }

  template <FdGetter FdGet, RespGetter RespGet>
  void
  echo_observed_mark(const LogContext &log, TSHttpTxn txnp, std::string_view echo_header)
  {
    int fd{-1};
    if (FdGet(txnp, &fd) != TS_SUCCESS || fd < 0) {
      TSError("[%.*s] Failed to obtain socket fd", static_cast<int>(log.plugin_name.length()), log.plugin_name.data());
      return;
    }

    std::optional<uint32_t> const observed{get_so_mark(fd)};
    if (!observed.has_value()) {
      TSError("[%.*s] Failed to read SO_MARK on fd %d", static_cast<int>(log.plugin_name.length()), log.plugin_name.data(), fd);
      return;
    }

    TSMBuffer resp_bufp{nullptr};
    TSMLoc    resp_loc{TS_NULL_MLOC};
    if (RespGet(txnp, &resp_bufp, &resp_loc) != TS_SUCCESS) {
      TSError("[%.*s] Failed to get response headers", static_cast<int>(log.plugin_name.length()), log.plugin_name.data());
      return;
    }

    set_echo_header(resp_bufp, resp_loc, echo_header, *observed);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
  }
} // anonymous namespace

void
apply_client_mark(const LogContext &log, TSHttpTxn txnp, std::string_view header)
{
  apply_mark_from_header<TSHttpTxnClientPacketMarkSet>(log, txnp, header);
}

void
echo_client_mark(const LogContext &log, TSHttpTxn txnp, std::string_view echo_header)
{
  echo_observed_mark<TSHttpTxnClientFdGet, TSHttpTxnClientRespGet>(log, txnp, echo_header);
}

} // namespace packet_mark
