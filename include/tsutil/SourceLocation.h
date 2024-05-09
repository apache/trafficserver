/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "swoc/bwf_fwd.h"
#include <string_view>

// The SourceLocation class wraps up a source code location, including
// file name, function name, and line number, and contains a method to
// format the result into a string buffer.

#define MakeSourceLocation()                                                                                                 \
  SourceLocation(std::string_view{__FILE__, sizeof(__FILE__) - 1}, std::string_view{__FUNCTION__, sizeof(__FUNCTION__) - 1}, \
                 __LINE__)

class SourceLocation
{
public:
  std::string_view file;
  std::string_view func;
  int              line = 0;

  SourceLocation()                          = default;
  SourceLocation(const SourceLocation &rhs) = default;

  SourceLocation(std::string_view _file, std::string_view _func, int _line) : file(_file), func(_func), line(_line) {}

  bool
  valid() const
  {
    return !file.empty() && line;
  }

  SourceLocation &
  operator=(const SourceLocation &rhs)
  {
    this->file = rhs.file;
    this->func = rhs.func;
    this->line = rhs.line;
    return *this;
  }

  std::string_view basefile() const;

  char               *str(char *buf, int buflen) const;
  swoc::BufferWriter &print(swoc::BufferWriter &w, swoc::bwf::Spec const &spec) const;
};

namespace swoc
{
inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, SourceLocation const &loc)
{
  return loc.print(w, spec);
}
} // namespace swoc
