/** @file

 This file contains a set of utility routines that are used for HTTP
 related logging in traffic_server but not in traffic_logcat.

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
// Get a string out of a MIMEField using one of its member funcitions, and put it into a buffer writer, terminated with a nul.
//
void
marshalStr(ts::FixedBufferWriter &bw, const MIMEField &mf, const char *(MIMEField::*get_func)(int *length) const)
{
  int length;
  const char *data = (mf.*get_func)(&length);

  if (!data or (*data == '\0')) {
    // Empty string.  This is a problem, since it would result in two successive nul characters, which indicates the end of the
    // marshaled hearer.  Change the string to a single blank character.

    static const char Blank[] = " ";
    data                      = Blank;
    length                    = 1;
  }

  bw << ts::string_view(data, length) << '\0';
}

} // end anonymous namespace

namespace LogUtils
{
// Marshals header tags and values together, with a single terminating nul character.  Returns buffer space required.  'buf' points
// to where to put the marshaled data.  If 'buf' is null, no data is marshaled, but the function returns the amount of space that
// would have been used.
//
int
marshalMimeHdr(MIMEHdr *hdr, char *buf)
{
  std::size_t bwSize = buf ? SIZE_MAX : 0;

  ts::FixedBufferWriter bw(buf, bwSize);

  if (hdr) {
    MIMEFieldIter mfIter;
    const MIMEField *mfp = hdr->iter_get_first(&mfIter);

    while (mfp) {
      marshalStr(bw, *mfp, &MIMEField::name_get);
      marshalStr(bw, *mfp, &MIMEField::value_get);

      mfp = hdr->iter_get_next(&mfIter);
    }
  }

  bw << '\0';

  return int(INK_ALIGN_DEFAULT(bw.extent()));
}

} // end namespace LogUtils
