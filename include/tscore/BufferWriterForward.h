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
#include <limits>
#include <utility>
#include <cstring>
#include <vector>
#include <map>

#include "tscpp/util/TextView.h"
#include "tscore/ink_assert.h"

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
    CENTER,                          ///< Center alignment '^'.
    SIGN                             ///< Align plus/minus sign before numeric fill. '='
  } _align           = Align::NONE;  ///< Output field alignment.
  char _type         = DEFAULT_TYPE; ///< Type / radix indicator.
  bool _radix_lead_p = false;        ///< Print leading radix indication.
  // @a _min is unsigned because there's no point in an invalid default, 0 works fine.
  unsigned int _min = 0;                                        ///< Minimum width.
  int _prec         = -1;                                       ///< Precision
  unsigned int _max = std::numeric_limits<unsigned int>::max(); ///< Maxium width
  int _idx          = -1;                                       ///< Positional "name" of the specification.
  std::string_view _name;                                       ///< Name of the specification.
  std::string_view _ext;                                        ///< Extension if provided.

  static const self_type DEFAULT;

  /// Validate @a c is a specifier type indicator.
  static bool is_type(char c);
  /// Check if the type flag is numeric.
  static bool is_numeric_type(char c);
  /// Check if the type is an upper case variant.
  static bool is_upper_case_type(char c);
  /// Check if the type @a in @a this is numeric.
  bool has_numeric_type() const;
  /// Check if the type in @a this is an upper case variant.
  bool has_upper_case_type() const;
  /// Check if the type is a raw pointer.
  bool has_pointer_type() const;

protected:
  /// Validate character is alignment character and return the appropriate enum value.
  Align align_of(char c);

  /// Validate is sign indicator.
  bool is_sign(char c);

  /// Handrolled initialization the character syntactic property data.
  static const struct Property {
    Property(); ///< Default constructor, creates initialized flag set.
    /// Flag storage, indexed by character value.
    uint8_t _data[0x100];
    /// Flag mask values.
    static constexpr uint8_t ALIGN_MASK        = 0x0F; ///< Alignment type.
    static constexpr uint8_t TYPE_CHAR         = 0x10; ///< A valid type character.
    static constexpr uint8_t UPPER_TYPE_CHAR   = 0x20; ///< Upper case flag.
    static constexpr uint8_t NUMERIC_TYPE_CHAR = 0x40; ///< Numeric output.
    static constexpr uint8_t SIGN_CHAR         = 0x80; ///< Is sign character.
  } _prop;
};

inline BWFSpec::Align
BWFSpec::align_of(char c)
{
  return static_cast<Align>(_prop._data[static_cast<unsigned>(c)] & Property::ALIGN_MASK);
}

inline bool
BWFSpec::is_sign(char c)
{
  return _prop._data[static_cast<unsigned>(c)] & Property::SIGN_CHAR;
}

inline bool
BWFSpec::is_type(char c)
{
  return _prop._data[static_cast<unsigned>(c)] & Property::TYPE_CHAR;
}

inline bool
BWFSpec::is_upper_case_type(char c)
{
  return _prop._data[static_cast<unsigned>(c)] & Property::UPPER_TYPE_CHAR;
}

inline bool
BWFSpec::has_numeric_type() const
{
  return _prop._data[static_cast<unsigned>(_type)] & Property::NUMERIC_TYPE_CHAR;
}

inline bool
BWFSpec::has_upper_case_type() const
{
  return _prop._data[static_cast<unsigned>(_type)] & Property::UPPER_TYPE_CHAR;
}

inline bool
BWFSpec::has_pointer_type() const
{
  return _type == 'p' || _type == 'P';
}

class BWFormat;

class BufferWriter;

} // namespace ts
