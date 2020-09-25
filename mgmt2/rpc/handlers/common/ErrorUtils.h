/**
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
// TODO: we have to rename and split this file.(Errors and Errata)

#include <system_error>
#include <string_view>

#include "tscore/Errata.h"
#include "tscore/BufferWriter.h"

namespace rpc::handlers::errors
{
// High level handler error codes, each particular handler can be fit into one of the
// following categories.
// enum YourOwnHandlerEnum {
//   FOO_ERROR = Codes::SOME_CATEGORY,
// };
// With this we try to avoid error codes collision. You can also use same error Code for all your
// errors.
enum Codes : unsigned int {
  CONFIGURATION = 1,
  METRIC        = 1000,
  RECORD        = 2000,
  SERVER        = 3000,
  STORAGE       = 4000,
  PLUGIN        = 5000,
  // Add more here. Give enough space between jumps.
  GENERIC = 30000
};

static constexpr int ERRATA_DEFAULT_ID{1};

template <typename... Args>
static inline ts::Errata
make_errata(int code, std::string_view fmt, Args &&... args)
{
  std::string text;
  return ts::Errata{}.push(ERRATA_DEFAULT_ID, code, ts::bwprint(text, fmt, std::forward<Args>(args)...));
}

static inline ts::Errata
make_errata(int code, std::string_view text)
{
  return ts::Errata{}.push(ERRATA_DEFAULT_ID, code, text);
}
} // namespace rpc::handlers::errors