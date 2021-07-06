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

#include "TextBuffer.h"

#include <utility>

// Result is simple error object that carries a success/fail status and
// a corresponding error message for the failure case. It is a simplified
// form of Rust's Result object in that we don't carry a return value for
// the success case. Arguably it ought to just be Error(), but Diags.h
// already owns that name.

struct Result {
  Result() {}

  Result &
  operator=(Result &&other)
  {
    if (this != &other) {
      buf = std::move(other.buf);
    }
    return *this;
  }

  Result(Result &&other) { *this = std::move(other); }

  bool
  failed() const
  {
    return !buf.empty();
  }

  const char *
  message() const
  {
    return buf.bufPtr();
  }

  static Result
  ok()
  {
    return Result();
  }

  static Result
  failure(const char *fmt, ...) TS_PRINTFLIKE(1, 2)
  {
    Result result;
    va_list ap;

    va_start(ap, fmt);
    result.buf.vformat(fmt, ap);
    va_end(ap);

    return result;
  }

private:
  TextBuffer buf;
};
