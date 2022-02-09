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

namespace std {
/// Format atomics by stripping the atomic and formatting the underlying type.
template <typename T>
swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, atomic<T> const &v) {
  return ::swoc::bwformat(w, spec, v.load());
}

swoc::BufferWriter &bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, error_code const &ec);

template <size_t N>
swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const & /* spec */, bitset<N> const &bits) {
  for (unsigned idx = 0; idx < N; ++idx) {
    w.write(bits[idx] ? '1' : '0');
  }
  return w;
}

} // end namespace std
