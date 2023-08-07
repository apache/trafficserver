// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    BufferWriter formatters for types in the std namespace.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <bitset>

#include "swoc/swoc_version.h"
#include "swoc/bwf_base.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
using namespace literals;

/// Format atomics by stripping the atomic and formatting the underlying type.
template <typename T>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::atomic<T> const &v) {
  return ::swoc::bwformat(w, spec, v.load());
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, std::error_code const &ec);

template <size_t N>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const & /* spec */, std::bitset<N> const &bits) {
  for (unsigned idx = 0; idx < N; ++idx) {
    w.write(bits[idx] ? '1' : '0');
  }
  return w;
}

template <typename Rep, typename Period>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::chrono::duration<Rep, Period> const &d)
{
  return bwformat(w, spec, d.count());
}

template <typename Clock, typename Duration>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, std::chrono::time_point<Clock, Duration> const &t)
{
  return bwformat(w, spec, t.time_since_epoch());
}

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const& spec, std::exception const& e) {
  w.write("Exception - "_tv);
  return bwformat(w, spec, e.what());
}

}} // end namespace swoc
