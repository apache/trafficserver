/** @file
 *
 * XDebug plugin utility functions implementation.
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

#include "xdebug_utils.h"
#include "xdebug_types.h"
#include <swoc/TextView.h>
#include <cctype>
#include <cstring>

namespace xdebug
{

bool
parse_probe_full_json_field_value(std::string_view value, BodyEncoding_t &encoding)
{
  swoc::TextView tv = value;
  tv.trim_if(isspace);
  if (!tv.starts_with_nocase("probe-full-json"))
    return false;
  encoding = BodyEncoding_t::AUTO;
  if (tv.size() == strlen("probe-full-json"))
    return true; // No suffix.
  tv.remove_prefix(strlen("probe-full-json"));
  tv.trim_if(isspace);
  if (!tv)
    return true; // No suffix.
  if (tv.front() != '=')
    return false; // Unrecognized suffix.
  tv.remove_prefix(1);
  tv.trim_if(isspace);
  swoc::TextView suffix = tv; // whole remainder
  if (suffix.starts_with_nocase("hex") && suffix.size() == 3) {
    encoding = BodyEncoding_t::HEX;
  } else if (suffix.starts_with_nocase("escape") && suffix.size() == 6) {
    encoding = BodyEncoding_t::ESCAPE;
  } else if (suffix.starts_with_nocase("nobody") && suffix.size() == 6) {
    encoding = BodyEncoding_t::OMIT_BODY;
  } else {
    return false; // Unrecognized suffix.
  }
  return true;
}

bool
is_textual_content_type(std::string_view ct)
{
  swoc::TextView content_type = ct;
  content_type.trim_if(isspace);

  // Helper to check case-insensitive substring containment
  auto contains_nocase = [&content_type](std::string_view needle) -> bool {
    swoc::TextView remaining = content_type;
    while (remaining.size() >= needle.size()) {
      if (remaining.starts_with_nocase(needle)) {
        return true;
      }
      remaining.remove_prefix(1);
    }
    return false;
  };

  // Check for text/ prefix (case insensitive)
  if (content_type.starts_with_nocase("text/")) {
    return true;
  }

  // Check for common textual content indicators (case insensitive)
  if (contains_nocase("json")) {
    return true;
  }
  if (contains_nocase("xml")) {
    return true;
  }
  if (contains_nocase("javascript")) {
    return true;
  }
  if (contains_nocase("csv")) {
    return true;
  }
  if (contains_nocase("html")) {
    return true;
  }
  if (contains_nocase("plain")) {
    return true;
  }

  return false;
}

} // namespace xdebug
