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

#include <stdarg.h>

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

/// mgmt_message_length -------------------------------------------------------
template <typename T>
MgmtMarshallInt
mgmt_message_length(T field)
{
  // We should always have a template specialization
  // If we get here, it means we are marshalling an object of an invalid type.
  errno = EINVAL;
  return -1;
}
MgmtMarshallInt mgmt_message_length(MgmtMarshallInt *field);
MgmtMarshallInt mgmt_message_length(MgmtMarshallLong *field);
MgmtMarshallInt mgmt_message_length(MgmtMarshallString *field);
MgmtMarshallInt mgmt_message_length(MgmtMarshallData *field);

template <typename T, typename... Rest>
MgmtMarshallInt
mgmt_message_length(T first, Rest... rest)
{
  MgmtMarshallInt len = mgmt_message_length(first);
  return (len == -1) ? len + mgmt_message_length(rest...) : len;
}
/// end mgmt_message_length ---------------------------------------------------

/// mgmt_message_read ---------------------------------------------------------
template <typename T>
ssize_t
mgmt_message_read(int fd, T field)
{
  // We should always have a template specialization
  // If we get here, it means we are marshalling an object of an invalid type.
  errno = EINVAL;
  return -1;
}
ssize_t mgmt_message_read(int fd, MgmtMarshallInt *field);
ssize_t mgmt_message_read(int fd, MgmtMarshallLong *field);
ssize_t mgmt_message_read(int fd, MgmtMarshallString *field);
ssize_t mgmt_message_read(int fd, MgmtMarshallData *field);

template <typename T, typename... Rest>
ssize_t
mgmt_message_read(int fd, T first, Rest... rest)
{
  ssize_t nbytes = mgmt_message_read(fd, first);
  return (nbytes == -1) ? nbytes + mgmt_message_read(fd, rest...) : nbytes;
}
/// mgmt_message_read ---------------------------------------------------------

/// mgmt_message_write --------------------------------------------------------
template <typename T>
ssize_t
mgmt_message_write(int fd, T field)
{
  // We should always have a template specialization
  // If we get here, it means we are marshalling an object of an invalid type.
  errno = EINVAL;
  return -1;
}
ssize_t mgmt_message_write(int fd, MgmtMarshallInt *field);
ssize_t mgmt_message_write(int fd, MgmtMarshallLong *field);
ssize_t mgmt_message_write(int fd, MgmtMarshallString *field);
ssize_t mgmt_message_write(int fd, MgmtMarshallData *field);

template <typename T, typename... Rest>
ssize_t
mgmt_message_write(int fd, T first, Rest... rest)
{
  ssize_t nbytes = mgmt_message_write(fd, first);
  return (nbytes != -1) ? nbytes + mgmt_message_write(fd, rest...) : nbytes;
}
/// end mgmt_message_write ----------------------------------------------------

/// mgmt_message_marshall -----------------------------------------------------
template <typename T>
ssize_t
mgmt_message_marshall(void *buf, size_t remain, T field)
{
  // We should always have a template specialization
  // If we get here, it means we are marshalling an object of an invalid type.
  errno = EINVAL;
  return -1;
}

ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallInt *field);
ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallLong *field);
ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallString *field);
ssize_t mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallData *field);

template <typename T, typename... Rest>
ssize_t
mgmt_message_marshall(void *buf, size_t remain, T first, Rest... rest)
{
  ssize_t nbytes = mgmt_message_marshall(buf, remain, first);
  return (nbytes != -1) ? nbytes + mgmt_message_marshall(static_cast<uint8_t *>(buf) + nbytes, remain - nbytes, rest...) : nbytes;
}
/// end mgmt_message_marshall ---------------------------------------------------

/// mgmt_message_parse ----------------------------------------------------------
template <typename T>
ssize_t
mgmt_message_parse(const void *buf, size_t len, T field)
{
  // We should always have a template specialization
  // If we get here, it means we are marshalling an object of an invalid type.
  errno = EINVAL;
  return -1;
}
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallInt *field);
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallLong *field);
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallString *field);
ssize_t mgmt_message_parse(const void *buf, size_t len, MgmtMarshallData *field);

template <typename T, typename... Rest>
ssize_t
mgmt_message_parse(const void *buf, size_t len, T first, Rest... rest)
{
  ssize_t nbytes = mgmt_message_parse(buf, len, first);
  return (nbytes != -1) ? nbytes + mgmt_message_parse(static_cast<const uint8_t *>(buf) + nbytes, len - nbytes, rest...) : nbytes;
}
/// end mgmt_message_parse ------------------------------------------------------
