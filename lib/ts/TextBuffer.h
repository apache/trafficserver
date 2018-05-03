/** @file

  A brief file description

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

/****************************************************************************
 *
 *  TextBuffer.h - A self-expanding buffer, primarly meant for strings
 *
 *
 *
 ****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_apidefs.h"

#include <cstdarg>
class TextBuffer
{
public:
  TextBuffer() {}
  TextBuffer(const TextBuffer &rhs)
  {
    if (!rhs.empty()) {
      copyFrom(rhs.bufPtr(), rhs.spaceUsed());
    }
  }
  TextBuffer &
  operator=(TextBuffer &&other)
  {
    if (this != &other) {
      ats_free(bufferStart);
      bufferStart       = other.bufferStart;
      other.bufferStart = nullptr;
    }
    return *this;
  }

  TextBuffer(int size);
  ~TextBuffer();

  int rawReadFromFile(int fd);
  int readFromFD(int fd);
  int copyFrom(const void *, unsigned num_bytes);
  void reUse();
  void chomp();
  void slurp(int);

  char *
  bufPtr()
  {
    return bufferStart;
  }

  const char *
  bufPtr() const
  {
    return bufferStart;
  }

  void
  clear()
  {
    this->reUse();
  }

  void
  resize(unsigned nbytes)
  {
    this->enlargeBuffer(nbytes);
  }

  size_t
  spaceUsed() const
  {
    return (size_t)(nextAdd - bufferStart);
  };

  bool
  empty() const
  {
    return this->spaceUsed() == 0;
  }

  void format(const char *fmt, ...) TS_PRINTFLIKE(2, 3);
  void vformat(const char *fmt, va_list ap);

  char *release();

private:
  int enlargeBuffer(unsigned N);

  size_t currentSize = 0;
  size_t spaceLeft   = 0;
  char *bufferStart  = nullptr;
  char *nextAdd      = nullptr;
};
