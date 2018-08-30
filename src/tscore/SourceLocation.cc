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
#include <cstring>
#include "tscore/SourceLocation.h"
#include "tscore/ink_defs.h"
#include "tscore/BufferWriter.h"
#include "tscore/bwf_std_format.h"

// This method takes a SourceLocation source location data structure and
// converts it to a human-readable representation, in the buffer <buf>
// with length <buflen>.  The buffer will always be NUL-terminated, and
// must always have a length of at least 1.  The buffer address is
// returned on success.  The routine will only fail if the SourceLocation is
// not valid, or the buflen is less than 1.

char *
SourceLocation::str(char *buf, int buflen) const
{
  const char *shortname;

  if (!this->valid() || buflen < 1) {
    return (nullptr);
  }

  shortname = strrchr(file, '/');
  shortname = shortname ? (shortname + 1) : file;

  if (func != nullptr) {
    snprintf(buf, buflen, "%s:%d (%s)", shortname, line, func);
  } else {
    snprintf(buf, buflen, "%s:%d", shortname, line);
  }
  buf[buflen - 1] = NUL;
  return (buf);
}

ts::BufferWriter &
SourceLocation::print(ts::BufferWriter &w, ts::BWFSpec const &) const
{
  if (this->valid()) {
    ts::TextView base{ts::TextView{file, strlen(file)}.take_suffix_at('/')};
    w.print("{}:{}{}", base, line, ts::bwf::OptionalAffix(func, ")"_sv, " ("_sv));
  };
  return w;
}
