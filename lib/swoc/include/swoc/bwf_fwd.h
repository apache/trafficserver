// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Forward declarations for BufferWriter formatting.
 */

#pragma once

#include "swoc/swoc_version.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
class BufferWriter;
class FixedBufferWriter;
template <size_t N> class LocalBufferWriter;

namespace bwf {
struct Spec;
class Format;
} // namespace bwf
}} // namespace swoc
