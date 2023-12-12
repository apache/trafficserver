/** @file

  Common assert definitions

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

#include "tsutil/SourceLocation.h"
#include "tsutil/ts_bw_format.h"
namespace ts
{

void do_abort(const SourceLocation &loc, const char *expr, const char *message = nullptr);

}

#if defined(DEBUG) || defined(ENABLE_ALL_ASSERTS) || defined(__clang_analyzer__) || defined(__COVERITY__)
#define debug_assert(EX)                     \
  if (!(EX)) {                               \
    ts::do_abort(MakeSourceLocation(), #EX); \
  }
#define debug_assert_message(EX, MSG)               \
  if (!(EX)) {                                      \
    ts::do_abort(MakeSourceLocation(), #EX, (MSG)); \
  }
#else
#define debug_assert(EX)              (void)(EX)
#define debug_assert_message(EX, MSG) (void)(EX)
#endif

#define release_assert(EX)                   \
  if (!(EX)) {                               \
    ts::do_abort(MakeSourceLocation(), #EX); \
  }
#define release_assert_message(EX, MSG)             \
  if (!(EX)) {                                      \
    ts::do_abort(MakeSourceLocation(), #EX, (MSG)); \
  }

#define fatal_error(format, ...) \
  ts::do_abort(MakeSourceLocation(), "FATAL", swoc::bwprint(ts::bw_dbg, format, __VA_ARGS__).c_str());
