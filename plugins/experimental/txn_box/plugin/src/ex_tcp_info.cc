/**
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

  Copyright 2021, Oath Inc.
*/

#include <type_traits>

#include "txn_box/common.h"

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>

#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"

#if __has_include("linux/tcp.h")
#include <linux/tcp.h>
#endif
#if __has_include("netinet/in.h")
#include <netinet/in.h>
#endif

using swoc::Errata;
using swoc::MemSpan;
using swoc::Rv;
using swoc::TextView;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
// Unfortunate, but there are limits to what can be done when using C based interfaces.
// The value is picked out of the header files, but ultimately it doesn't matter as it is not used
// if the other tcp_info support is not available.
#if !defined TCP_INFO
#define TCP_INFO 11
#endif

namespace
{

// All of this is for OS compatibility - it enables the code to compile and run regardless of
// whether tcp_info is available. If it is not, the NULL value is returned by the extractors.

// Determine the size of the tcp_info struct - calculate as 0 if there is no such type declared.
template <typename A, typename = void> struct type_size : public std::integral_constant<size_t, 0> {
};
template <typename A> struct type_size<A, std::void_t<decltype(sizeof(A))>> : public std::integral_constant<size_t, sizeof(A)> {
};

// Used for storage sizing and existence flag.
constexpr size_t tcp_info_size = type_size<struct tcp_info>::value;

// Need specialized support for retrans because the name is not consistent. This prefers
// @a tcpi_retrans if available, and falls back to @a __tcpi_retrans
template <typename T>
auto
field_retrans(T const &info, swoc::meta::CaseTag<1>) -> decltype(info.__tcpi_retrans)
{
  return info.__tcpi_retrans;
}

template <typename T>
auto
field_retrans(T const &info, swoc::meta::CaseTag<2>) -> decltype(info.tcpi_retrans)
{
  return info.tcpi_retrans;
}

} // namespace
/* ------------------------------------------------------------------------------------ */
/** Extract tcp_info.
 *
 */
class Ex_tcp_info : public Extractor
{
  using self_type  = Ex_tcp_info; ///< Self reference type.
  using super_type = Extractor;   ///< Parent type.
public:
  static constexpr TextView NAME{"inbound-tcp-info"};
  using Extractor::extract; // declare hidden member function
  /// Usage validation.
  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  /// Extract the feature.
  Feature extract(Context &ctx, Spec const &spec) override;

protected:
  /// Support fields for extraction.
  enum Field { NONE, RTT, RTO, SND_CWND, RETRANS };

  // OS Compatibility - necessary because constexpr if still requires the excluded code to compile
  // and using undeclared types will break.
  template <typename>
  static auto
  value(Context &, Field, swoc::meta::CaseTag<0>) -> intmax_t
  {
    return 0;
  };
  template <typename tcp_info>
  static auto value(Context &ctx, Field field,
                    swoc::meta::CaseTag<1>) -> std::enable_if_t<(type_size<tcp_info>::value > 0), intmax_t>;

  /// Data stored per context if needed.
  struct CtxInfo {
    Hook hook    = Hook::INVALID; ///< Hook for which the data is valid.
    bool valid_p = false;         ///< Successfully loaded data - avoid repeated fails if not working.
    /// cached tcp_info data.
    alignas(int32_t) std::array<std::byte, tcp_info_size> info;
  };

  /// Reserved storage per context.
  static inline ReservedSpan _ctx_storage;

  // Conversion between names and enumerations.
  inline static const swoc::Lexicon<Field> _field_lexicon{
    {{NONE, "none"}, {RTT, "rtt"}, {RTO, "rto"}, {SND_CWND, "snd-cwnd"}, {RETRANS, "retrans"}},
    NONE
  };
};

Rv<ActiveType>
Ex_tcp_info::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to specify the field.)", NAME);
  }
  auto field = _field_lexicon[arg];
  if (NONE == field) {
    return Errata(S_ERROR, R"(Field "{}" for "{}" extractor is not supported.)", arg, NAME);
  }
  // Ugly - need to store the enum, and it's not worth allocating some chunk of config to do that
  // instead of just stashing it in the span size.
  spec._data.u = size_t(field);

  // Extractor has been used
  // => reserve needed context storage if tcp_info available and no storage reserved.
  if (tcp_info_size > 0 && _ctx_storage.n == 0) {
    _ctx_storage = cfg.reserve_ctx_storage(sizeof(CtxInfo));
  }
  return {
    {NIL, INTEGER}
  }; // Result can be an integer or NULL (NIL).
}

Feature
Ex_tcp_info::extract(Context &ctx, const Spec &spec)
{
  if constexpr (tcp_info_size == 0) { // No tcp_info is available.
    return NIL_FEATURE;
  }
  if (ctx._txn.is_internal()) { // Internal requests do not have TCP Info
    return NIL_FEATURE;
  }

  return self_type::template value<struct tcp_info>(ctx, Field(spec._data.u), swoc::meta::CaseArg);
}

template <typename tcp_info>
auto
Ex_tcp_info::value(Context &ctx, Ex_tcp_info::Field field,
                   swoc::meta::CaseTag<1>) -> std::enable_if_t<(type_size<tcp_info>::value > 0), intmax_t>
{
  auto fd = ctx._txn.inbound_fd();
  if (fd >= 0) {
    auto &ctx_info = ctx.template initialized_storage_for<CtxInfo>(_ctx_storage)[0];
    // cached tcp_info is only valid for the same hook - reload if stale.
    if (ctx_info.hook != ctx._cur_hook) {
      socklen_t info_len = ctx_info.info.size();
      ctx_info.valid_p   = (0 == ::getsockopt(fd, IPPROTO_TCP, TCP_INFO, ctx_info.info.data(), &info_len) && info_len > 0);
      ctx_info.hook      = ctx._cur_hook;
    }

    if (ctx_info.valid_p) { // data is loaded and fresh
      auto info = reinterpret_cast<tcp_info *>(ctx_info.info.data());
      switch (field) {
      case NONE:
        return 0;
      case RTT:
        return info->tcpi_rtt;
      case RTO:
        return info->tcpi_rto;
      case SND_CWND:
        return info->tcpi_snd_cwnd;
      case RETRANS:
        return field_retrans(*info, swoc::meta::CaseArg);
      }
    }
  }
  return 0;
}

/* ------------------------------------------------------------------------------------ */

namespace
{
Ex_tcp_info tcp_info;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Extractor::define(tcp_info.NAME, &tcp_info);
  return true;
}();
} // namespace
