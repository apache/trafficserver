/** @file

  Management packet marshalling.

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

#include <cstdarg>

#define MAX_TIME_WAIT 60 // num secs for a timeout on a select call (remote only)

// Simple message marshalling.
//
// MGMT_MARSHALL_INT
// Wire size is 4 bytes signed. This type is used for enum and boolean values, as well as embedded lengths and general
// integer values.
//
// MGMT_MARSHALL_LONG
// Wire size is 8 bytes signed.
//
// MGMT_MARSHALL_STRING
// Wire size is a 4 byte length followed by N bytes. The trailing NUL is always sent and NULL strings are sent as empty
// strings. This means that the minimum wire size for a string is 5 bytes (4 byte length + NUL byte). The unmarshalled
// string point is guaranteed to be non-NULL.
//
// MGMT_MARSHALL_DATA
// Wire size is 4 byte length followed by N data bytes. If the length is 0, no subsequent bytes are sent. In this case
// the unmarshalled data pointer is guaranteed to be NULL.
//
enum MgmtMarshallType {
  MGMT_MARSHALL_INT,    // int32_t
  MGMT_MARSHALL_LONG,   // int64_t
  MGMT_MARSHALL_STRING, // NUL-terminated string
  MGMT_MARSHALL_DATA    // byte buffer
};

typedef int32_t MgmtMarshallInt;
typedef int64_t MgmtMarshallLong;
typedef char *MgmtMarshallString;

struct MgmtMarshallData {
  void *ptr;
  size_t len;
};

MgmtMarshallInt mgmt_message_length(const MgmtMarshallType *fields, unsigned count, ...);
MgmtMarshallInt mgmt_message_length_v(const MgmtMarshallType *fields, unsigned count, va_list ap);

ssize_t mgmt_message_read(int fd, const MgmtMarshallType *fields, unsigned count, ...);
ssize_t mgmt_message_read_v(int fd, const MgmtMarshallType *fields, unsigned count, va_list ap);

ssize_t mgmt_message_write(int fd, const MgmtMarshallType *fields, unsigned count, ...);
ssize_t mgmt_message_write_v(int fd, const MgmtMarshallType *fields, unsigned count, va_list ap);

ssize_t mgmt_message_parse(const void *ptr, size_t len, const MgmtMarshallType *fields, unsigned count, ...);
ssize_t mgmt_message_parse_v(const void *ptr, size_t len, const MgmtMarshallType *fields, unsigned count, va_list ap);

ssize_t mgmt_message_marshall(void *ptr, size_t len, const MgmtMarshallType *fields, unsigned count, ...);
ssize_t mgmt_message_marshall_v(void *ptr, size_t len, const MgmtMarshallType *fields, unsigned count, va_list ap);
