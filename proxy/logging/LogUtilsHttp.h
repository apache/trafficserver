/** @file

  This file contains definitions/declarations for utility routines that are used for HTTP related logging.

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

#ifndef LOG_UTILS_HTTP_H
#define LOG_UTILS_HTTP_H

#ifndef TEST_LOG_UTILS_HTTP

#include <MIME.h>

#else

#include "unit-tests/test_LogUtilsHttp.h"

#endif

namespace LogUtils
{
// Marshals header tags and values together, with a single terminating nul character.  Returns buffer space required.  'buf' points
// to where to put the marshaled data.  If 'buf' is null, no data is marshaled, but the function returns the amount of space that
// would have been used.
int marshalMimeHdr(MIMEHdr *hdr, char *buf);

// Unmarshelled/printable format is {{{tag1}:{value1}}{{tag2}:{value2}} ... }
//
// Returns -1 if data corruption is detected, otherwise the actual amount of data put into the 'dest' buffer.  '*buf' is advanced
// to byte after the last byte consumed.
int unmarshalMimeHdr(char **buf, char *dest, int destLength);

} // end namespace LogUtils

#endif // include once
