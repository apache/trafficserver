// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file

    Solid Wall of C++ Library

    @mainpage

    A collection of basic utilities derived from Apache Traffic Server code.
    Much of the focus is on low level text manipulation, in particular

    - @c TextView, an extension of @c std::string_view with a collection od
      methods to make working with the text in the view fast and convenient.

    - @c BufferWriter, a safe mechanism for writing to fixed sized buffers. As
      an optional extension this supports python like output formatting along
      with the ability to extend the formatt
      ing to arbitrary types, bind names
      in to the formatting context, and substitute alternate parsers for
      custom format styles.
 */

#pragma once

#if !defined(SWOC_VERSION_NS)
#define SWOC_VERSION_NS _1_5_0
#endif

namespace swoc { inline namespace SWOC_VERSION_NS {
static constexpr unsigned MAJOR_VERSION = 1;
static constexpr unsigned MINOR_VERSION = 5;
static constexpr unsigned POINT_VERSION = 0;
}} // namespace swoc::SWOC_VERSION_NS
