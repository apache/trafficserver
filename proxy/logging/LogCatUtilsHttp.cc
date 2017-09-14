/** @file

 This file contains a set of utility routines that are used for HTTP
 related logging in both traffic_server and traffic_logcat.

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

#include <cstdint>

#include <ts/ink_align.h>
#include <ts/ink_assert.h>
#include <ts/BufferWriter.h>
#include <ts/string_view.h>

#include "LogUtilsHttp.h"

namespace
{
void
unmarshalStr(ts::FixedBufferWriter &bw, const char *&data)
{
  bw << '{';

  while (*data) {
    bw << *(data++);
  }

  // Skip over terminal nul.
  ++data;

  bw << '}';
}

} // end anonymous namespace

namespace LogUtils
{
// Unmarshaled/printable format is {{{tag1}:{value1}}{{tag2}:{value2}} ... }
//
int
unmarshalMimeHdr(char **buf, char *dest, int destLength)
{
  ink_assert(*buf != nullptr);

  const char *data{*buf};

  ink_assert(data != nullptr);

  ts::FixedBufferWriter bw(dest, destLength);

  bw << '{';

  int pairEndFallback{0}, pairEndFallback2{0}, pairSeparatorFallback{0};

  while (*data) {
    if (!bw.error()) {
      pairEndFallback2 = pairEndFallback;
      pairEndFallback  = bw.size();
    }

    // Add open bracket of pair.
    //
    bw << '{';

    // Unmarshal field name.
    unmarshalStr(bw, data);

    bw << ':';

    if (!bw.error()) {
      pairSeparatorFallback = bw.size();
    }

    // Unmarshal field value.
    unmarshalStr(bw, data);

    // Add close bracket of pair.
    bw << '}';

  } // end for loop

  bw << '}';

  if (bw.error()) {
    // The output buffer wasn't big enough.

    static ts::string_view FULL_ELLIPSES("...}}}");

    if ((pairSeparatorFallback > pairEndFallback) and ((pairSeparatorFallback + 7) <= destLength)) {
      // In the report, we can show the existence of the last partial tag/value pair, and maybe part of the value.  If we only
      // show part of the value, we want to end it with an elipsis, to make it clear it's not complete.

      bw.reduce(destLength - FULL_ELLIPSES.size());
      bw << FULL_ELLIPSES;

    } else if (pairEndFallback and (pairEndFallback < destLength)) {
      bw.reduce(pairEndFallback);
      bw << '}';

    } else if ((pairSeparatorFallback > pairEndFallback2) and ((pairSeparatorFallback + 7) <= destLength)) {
      bw.reduce(destLength - FULL_ELLIPSES.size());
      bw << FULL_ELLIPSES;

    } else if (pairEndFallback2 and (pairEndFallback2 < destLength)) {
      bw.reduce(pairEndFallback2);
      bw << '}';

    } else if (destLength > 1) {
      bw.reduce(1);
      bw << '}';

    } else {
      bw.reduce(0);
    }
  }

  *buf += INK_ALIGN_DEFAULT(data - *buf + 1);

  return bw.size();
}

} // end namespace LogUtils
