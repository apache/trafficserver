// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Forward declarations for BufferWriter formatting.
 */

#pragma once

#include <cstdint>
#include "swoc/swoc_version.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
class BufferWriter;
class FixedBufferWriter;
template <std::size_t N> class LocalBufferWriter;

template <typename... Args>
std::string & bwprint_v(std::string &s, TextView fmt, std::tuple<Args...> const &args);

template <typename... Args>
std::string & bwprint(std::string &s, TextView fmt, Args &&... args);

namespace bwf {
struct Spec;
struct Format;
class NameBinding;
class ArgPack;
} // namespace bwf
}} // namespace swoc
