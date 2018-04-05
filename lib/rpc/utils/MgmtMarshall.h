/** @file

  Managment packet marshalling.

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

#include <tuple>
#include <stdarg.h>

#define MAX_TIME_WAIT 60 // num secs for a timeout on a select call (remote only)

// Simple message marshalling. Every message begins with a 32-bit header. The leading 8 bits indicate the type of the
// message and the following 24 bits indicate the message length. For integer and long types, the lower 24 length bits
// are ignored because it is fixed at compile time.
//
// MGMT_MARSHALL_INT
// Wire size is 4 bytes signed. This type is used for enum and boolean values, as well as embedded lengths and general
// integer values.
//
// MGMT_MARSHALL_LONG
// Wire size is 8 bytes signed.
//
// MGMT_MARSHALL_STRING
// Wire size is a 4 byte header followed by N bytes. The trailing NUL is always sent and NULL strings are sent as empty
// strings. This means that the minimum wire size for a string is 5 bytes (4 byte length + NUL byte). The unmarshalled
// string point is guaranteed to be non-NULL.
//
// MGMT_MARSHALL_DATA
// Wire size is 4 byte header followed by N data bytes. If the length is 0, no subsequent bytes are sent. In this case
// the unmarshalled data pointer is guaranteed to be NULL. Within the protocol, one byte is added to the
// MgmtMarshallData.len during marshalling to specify it's type. This is stripped in the unmarshalling step so that the
// message length can be used.
//
// !!! Should use mgmt_message_length to figure out the length needed in mgmt_message_read and mgmt_message_parse !!!
// This is because there are additional headers being sent and marshalled in the protocol.
//

enum MgmtMarshallType {
  MGMT_MARSHALL_INT,    // int32_t
  MGMT_MARSHALL_LONG,   // int64_t
  MGMT_MARSHALL_STRING, // NUL-terminated string
  MGMT_MARSHALL_DATA    // byte buffer
};

static constexpr size_t MGMT_HDR_LENGTH  = 4; // in bytes.
static constexpr size_t MGMT_INT_LENGTH  = 4;
static constexpr size_t MGMT_LONG_LENGTH = 8;

typedef uint32_t MgmtMarshallHdr;
typedef int32_t MgmtMarshallInt;
typedef int64_t MgmtMarshallLong;
typedef char *MgmtMarshallString;

struct MgmtMarshallData {
  void *ptr;
  size_t len;
};

/// A byte to idenify the marshalled type.
static constexpr uint8_t MGMT_INT_TYPE    = 0x00;
static constexpr uint8_t MGMT_LONG_TYPE   = 0x01;
static constexpr uint8_t MGMT_STRING_TYPE = 0x02;
static constexpr uint8_t MGMT_DATA_TYPE   = 0x03;

/**
  Various marshalling functions are implemented below. All functions are written
  recursively with variadic templates and overloads for specific MgmtMarshall types.

  The benefit of this is that the marshall functions are all type-safe at compile time
  because an invalid type will not resolve to a overload function. However, because
  there is no generic template definition, we need to overload each function with an
  empty version to allow for correct expansion.
  */

/// mgmt_message_length -------------------------------------------------------
MgmtMarshallInt mgmt_message_length(MgmtMarshallInt *field);
MgmtMarshallInt mgmt_message_length(MgmtMarshallLong *field);
MgmtMarshallInt mgmt_message_length(MgmtMarshallString *field);
MgmtMarshallInt mgmt_message_length(MgmtMarshallData *field);
MgmtMarshallInt mgmt_message_length(); // to allow for expansion

template <typename T, typename... Rest>
MgmtMarshallInt
mgmt_message_length(T first, Rest &&... rest)
{
  MgmtMarshallInt nfirst = mgmt_message_length(first);
  MgmtMarshallInt nrest  = mgmt_message_length(std::forward<Rest>(rest)...);
  return (nfirst != -1 && nrest != -1) ? nfirst + nrest : -1;
}
/// end mgmt_message_length ---------------------------------------------------

/// mgmt_message_read ---------------------------------------------------------
ssize_t mgmt_message_read(int fd, MgmtMarshallInt *field);
ssize_t mgmt_message_read(int fd, MgmtMarshallLong *field);
ssize_t mgmt_message_read(int fd, MgmtMarshallString *field);
ssize_t mgmt_message_read(int fd, MgmtMarshallData *field);
ssize_t mgmt_message_read(int fd); // to allow for expansion

template <typename T, typename... Rest>
ssize_t
mgmt_message_read(int fd, T first, Rest &&... rest)
{
  ssize_t nfirst = mgmt_message_read(fd, first);
  ssize_t nrest  = mgmt_message_read(fd, std::forward<Rest>(rest)...);
  return (nfirst != -1 && nrest != -1) ? nfirst + nrest : -1;
}
/// mgmt_message_read ---------------------------------------------------------

/// mgmt_message_write --------------------------------------------------------
ssize_t mgmt_message_write(int fd, MgmtMarshallInt *field);
ssize_t mgmt_message_write(int fd, MgmtMarshallLong *field);
ssize_t mgmt_message_write(int fd, MgmtMarshallString *field);
ssize_t mgmt_message_write(int fd, MgmtMarshallData *field);
ssize_t mgmt_message_write(int fd); // to allow for expansion

template <typename T, typename... Rest>
ssize_t
mgmt_message_write(int fd, T first, Rest &&... rest)
{
  ssize_t nfirst = mgmt_message_write(fd, first);
  ssize_t nrest  = mgmt_message_write(fd, std::forward<Rest>(rest)...);
  return (nfirst != -1 && nrest != -1) ? nfirst + nrest : -1;
}
/// end mgmt_message_write ----------------------------------------------------

/// mgmt_message_marshall -----------------------------------------------------
ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallInt *field);
ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallLong *field);
ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallString *field);
ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallData *field);
ssize_t mgmt_message_marshall(void *buf, size_t remain); // to allow for expansion

template <typename T, typename... Rest>
ssize_t
mgmt_message_marshall(void *buf, size_t remain, T first, Rest &&... rest)
{
  if (buf == nullptr) {
    return -1;
  }
  ssize_t nfirst = mgmt_message_marshall(buf, remain, first);
  ssize_t nrest  = mgmt_message_marshall(static_cast<uint8_t *>(buf) + nfirst, remain - nfirst, std::forward<Rest>(rest)...);
  return (nfirst != -1 && nrest != -1) ? nfirst + nrest : -1;
}
/// end mgmt_message_marshall ---------------------------------------------------

/// mgmt_message_parse ----------------------------------------------------------
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallInt *field);
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallLong *field);
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallString *field);
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallData *field);
ssize_t mgmt_message_parse(const void *buf, size_t len); // to allow for expansion

template <typename T, typename... Rest>
ssize_t
mgmt_message_parse(const void *buf, size_t len, T first, Rest &&... rest)
{
  if (buf == nullptr) {
    return -1;
  }
  ssize_t nfirst = mgmt_message_parse(buf, len, first);
  ssize_t nrest  = mgmt_message_parse(static_cast<const uint8_t *>(buf) + nfirst, len - nfirst, std::forward<Rest>(rest)...);
  return (nfirst != -1 && nrest != -1) ? nfirst + nrest : -1;
}
/// end mgmt_message_parse ------------------------------------------------------

/// This is so external functions can build headers if necessary.
MgmtMarshallHdr mgmt_message_build_hdr(const uint8_t type, const uint32_t len);
