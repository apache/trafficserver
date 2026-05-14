/** @file

  Helper for std::string_view comparison

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

#include <strings.h>
#include <string_view>

namespace ts
{
/**
  Returns true iff @a lhs and @a rhs compare equal, ignoring case.

  Prefer this over libswoc's @c strcasecmp(std::string_view, std::string_view) when you only need
  an equality check: this short-circuits on length mismatch, whereas the libswoc version must keep
  comparing bytes to produce a correct ordering result even when the lengths differ.

  For case-sensitive comparison, use @c std::string_view::operator==.
 */
inline bool
iequals(std::string_view lhs, std::string_view rhs) noexcept
{
  if (lhs.size() != rhs.size()) {
    return false;
  }

  return ::strncasecmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}
} // namespace ts
