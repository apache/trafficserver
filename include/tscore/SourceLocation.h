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

#include "tscore/BufferWriterForward.h"

// The SourceLocation class wraps up a source code location, including
// file name, function name, and line number, and contains a method to
// format the result into a string buffer.

#define MakeSourceLocation() SourceLocation(__FILE__, __FUNCTION__, __LINE__)

class SourceLocation
{
public:
  const char *file = nullptr;
  const char *func = nullptr;
  int line         = 0;

  SourceLocation()                          = default;
  SourceLocation(const SourceLocation &rhs) = default;

  SourceLocation(const char *_file, const char *_func, int _line) : file(_file), func(_func), line(_line) {}

  bool
  valid() const
  {
    return file && line;
  }

  SourceLocation &
  operator=(const SourceLocation &rhs)
  {
    this->file = rhs.file;
    this->func = rhs.func;
    this->line = rhs.line;
    return *this;
  }

  char *str(char *buf, int buflen) const;
  ts::BufferWriter &print(ts::BufferWriter &w, ts::BWFSpec const &spec) const;
};

namespace ts
{
inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, SourceLocation const &loc)
{
  return loc.print(w, spec);
}
} // namespace ts
