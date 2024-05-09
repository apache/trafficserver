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

#include <cstdio>
#include "tsutil/SourceLocation.h"
#include "swoc/BufferWriter.h"
#include "swoc/bwf_ex.h"

using namespace swoc::literals;

std::string_view
SourceLocation::basefile() const
{
  std::string_view shortname;
  if (!file.empty()) {
    auto idx = file.find_last_of('/');
    if (idx != std::string_view::npos) {
      shortname = file.substr(idx + 1);
    } else {
      shortname = file;
    }
  }
  return shortname;
}

// This method takes a SourceLocation source location data structure and
// converts it to a human-readable representation, in the buffer <buf>
// with length <buflen>.  The buffer will always be NUL-terminated, and
// must always have a length of at least 1.  The buffer address is
// returned on success.  The routine will only fail if the SourceLocation is
// not valid, or the buflen is less than 1.

char *
SourceLocation::str(char *buf, int buflen) const
{
  if (!this->valid() || buflen < 1) {
    return (nullptr);
  }

  std::string_view shortname = basefile();
  if (!func.empty()) {
    snprintf(buf, buflen, "%.*s:%d (%.*s)", static_cast<int>(shortname.size()), shortname.data(), line,
             static_cast<int>(func.size()), func.data());
  } else {
    snprintf(buf, buflen, "%.*s:%d", static_cast<int>(shortname.size()), shortname.data(), line);
  }
  buf[buflen - 1] = 0;
  return (buf);
}

swoc::BufferWriter &
SourceLocation::print(swoc::BufferWriter &w, swoc::bwf::Spec const &) const
{
  if (this->valid()) {
    auto base = swoc::TextView{file}.take_suffix_at('/');
    w.print("{}:{}{}", base, line, swoc::bwf::If(!func.empty(), " ({})"_tv, func));
  };
  return w;
}
