/** @file

    Forward definitions for BufferWriter formatting.

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

#include <cstdlib>
#include <utility>
#include <cstring>
#include <vector>
#include <map>
#include <ts/ink_std_compat.h>

#include <ts/TextView.h>
#include <ts/ink_assert.h>

namespace ts
{
/** A parsed version of a format specifier.
 */
struct BWFSpec {
  using self_type                    = BWFSpec; ///< Self reference type.
  static constexpr char DEFAULT_TYPE = 'g';     ///< Default format type.

  /// Constructor a default instance.
  constexpr BWFSpec() {}

  /// Construct by parsing @a fmt.
  BWFSpec(TextView fmt);

  char _fill = ' '; ///< Fill character.
  char _sign = '-'; ///< Numeric sign style, space + -
  enum class Align : char {
    NONE,                            ///< No alignment.
    LEFT,                            ///< Left alignment '<'.
    RIGHT,                           ///< Right alignment '>'.
    CENTER,                          ///< Center alignment '='.
    SIGN                             ///< Align plus/minus sign before numeric fill. '^'
  } _align           = Align::NONE;  ///< Output field alignment.
  char _type         = DEFAULT_TYPE; ///< Type / radix indicator.
  bool _radix_lead_p = false;        ///< Print leading radix indication.
  // @a _min is unsigned because there's no point in an invalid default, 0 works fine.
  unsigned int _min = 0;                                        ///< Minimum width.
  int _prec         = -1;                                       ///< Precision
  unsigned int _max = std::numeric_limits<unsigned int>::max(); ///< Maxium width
  int _idx          = -1;                                       ///< Positional "name" of the specification.
  string_view _name;                                            ///< Name of the specification.
  string_view _ext;                                             ///< Extension if provided.

  static const self_type DEFAULT;

protected:
  /// Validate character is alignment character and return the appropriate enum value.
  Align align_of(char c);

  /// Validate is sign indicator.
  bool is_sign(char c);

  /// Validate @a c is a specifier type indicator.
  bool is_type(char c);
};

inline BWFSpec::Align
BWFSpec::align_of(char c)
{
  return '<' == c ? Align::LEFT : '>' == c ? Align::RIGHT : '=' == c ? Align::CENTER : '^' == c ? Align::SIGN : Align::NONE;
}

inline bool
BWFSpec::is_sign(char c)
{
  return '+' == c || '-' == c || ' ' == c;
}

inline bool
BWFSpec::is_type(char c)
{
  return 'x' == c || 'X' == c || 'o' == c || 'b' == c || 'B' == c || 'd' == c || 's' == c || 'S' == c;
}

class BWFormat;

class BufferWriter;

} // namespace ts
