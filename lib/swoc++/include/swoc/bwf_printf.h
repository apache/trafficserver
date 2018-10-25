/** @file

    BufferWriter formatting in snprintf style.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
 */

#pragma once

#include <string_view>

#include "swoc/TextView.h"
#include "swoc/bwf_base.h"

namespace swoc
{
namespace bwf
{
  /** C / printf style formatting for BufferWriter.
   *
   * This is a wrapper style class, it is not for use in a persistent context. The general use pattern
   * will be to pass a temporary instance in to the @c BufferWriter formatting. E.g
   *
   * @code
   * void bwprintf(BufferWriter& w, TextView fmt, arg1, arg2, arg3, ...) {
   *   w.print_v(C_Format(fmt), std::forward_as_tuple(args));
   * @endcode
   */
  class C_Format
  {
  public:
    /// Construct for @a fmt.
    C_Format(TextView const &fmt);

    /// Check if there is any more format to process.
    explicit operator bool() const;

    /// Get the next pieces of the format.
    bool operator()(std::string_view &literal, Spec &spec);

    void capture(BufferWriter &w, Spec const &spec, std::any const &value);

  protected:
    TextView _fmt;
    Spec _saved;          // spec for which the width and/or prec is needed.
    bool _saved_p{false}; // flag for having a saved _spec.
    bool _prec_p{false};  // need the precision captured?
  };

  // ---- Implementation ----
  inline C_Format::C_Format(TextView const &fmt) : _fmt(fmt) {}

  inline C_Format::operator bool() const { return _saved_p || !_fmt.empty(); }

} // namespace bwf

template <typename... Args>
int
bwprintf(BufferWriter &w, TextView const &fmt, Args &&... args)
{
  size_t n = w.size();
  w.print_nv(bwf::NilBoundNames(), bwf::C_Format(fmt), std::forward_as_tuple(args...));
  return static_cast<int>(w.size() - n);
}

} // namespace swoc
