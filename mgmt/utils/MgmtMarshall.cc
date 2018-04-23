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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_assert.h"
#include "MgmtMarshall.h"
#include "MgmtSocket.h"

union MgmtMarshallAnyPtr {
  MgmtMarshallInt *m_int;
  MgmtMarshallLong *m_long;
  MgmtMarshallString *m_string;
  MgmtMarshallData *m_data;
  void *m_void;
};

static char *empty = const_cast<char *>("");

static bool
data_is_nul_terminated(const MgmtMarshallData *data)
{
  const char *str = (const char *)(data->ptr);

  if (data->len == 0) {
    return false;
  }

  if (str[data->len - 1] != '\0') {
    return false;
  }

  if (strlen(str) != (data->len - 1)) {
    return false;
  }

  return true;
}

static ssize_t
nospace()
{
  errno = EMSGSIZE;
  return -1;
}

static bool
empty_buf(const void *buf, size_t len)
{
  return (buf == nullptr || len == 0) ? true : false;
}

static ssize_t
socket_read_bytes(int fd, void *buf, size_t needed)
{
  size_t nread = 0;

  // makes sure the descriptor is readable
  if (mgmt_read_timeout(fd, MAX_TIME_WAIT, 0) <= 0) {
    return -1;
  }

  while (needed > nread) {
    ssize_t ret = read(fd, buf, needed - nread);

    if (ret < 0) {
      if (mgmt_transient_error()) {
        continue;
      } else {
        return -1;
      }
    }

    if (ret == 0) {
      // End of file before reading the remaining bytes.
      errno = ECONNRESET;
      return -1;
    }

    buf = (uint8_t *)buf + ret;
    nread += ret;
  }

  return nread;
}

static ssize_t
socket_write_bytes(int fd, const void *buf, ssize_t bytes)
{
  ssize_t nwritten = 0;

  // makes sure the descriptor is writable
  if (mgmt_write_timeout(fd, MAX_TIME_WAIT, 0) <= 0) {
    return -1;
  }

  // write until we fulfill the number
  while (nwritten < bytes) {
    ssize_t ret = write(fd, buf, bytes - nwritten);
    if (ret < 0) {
      if (mgmt_transient_error()) {
        continue;
      }
      return -1;
    }

    buf = (uint8_t *)buf + ret;
    nwritten += ret;
  }

  return nwritten;
}

static ssize_t
socket_write_buffer(int fd, const MgmtMarshallData *data)
{
  ssize_t nwrite;

  nwrite = socket_write_bytes(fd, &(data->len), 4);
  if (nwrite != 4) {
    goto fail;
  }

  if (data->len) {
    nwrite = socket_write_bytes(fd, data->ptr, data->len);
    if (nwrite != (ssize_t)data->len) {
      goto fail;
    }
  }

  return data->len + 4;

fail:
  return -1;
}

static ssize_t
socket_read_buffer(int fd, MgmtMarshallData *data)
{
  ssize_t nread;

  ink_zero(*data);

  nread = socket_read_bytes(fd, &(data->len), 4);
  if (nread != 4) {
    goto fail;
  }

  if (data->len) {
    data->ptr = ats_malloc(data->len);
    nread     = socket_read_bytes(fd, data->ptr, data->len);
    if (nread != (ssize_t)data->len) {
      goto fail;
    }
  }

  return data->len + 4;

fail:
  ats_free(data->ptr);
  ink_zero(*data);
  return -1;
}

static ssize_t
buffer_read_buffer(const uint8_t *buf, size_t len, MgmtMarshallData *data)
{
  ink_zero(*data);

  if (len < 4) {
    goto fail;
  }

  memcpy(&(data->len), buf, 4);
  buf += 4;
  len -= 4;

  if (len < data->len) {
    goto fail;
  }

  if (data->len) {
    data->ptr = ats_malloc(data->len);
    memcpy(data->ptr, buf, data->len);
  }

  return data->len + 4;

fail:
  ats_free(data->ptr);
  ink_zero(*data);
  return -1;
}

//-----------------------------------------------------------------------
// mgmt_message_length
//-----------------------------------------------------------------------
MgmtMarshallInt
mgmt_message_length()
{
  return 0;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallInt *field)
{
  return MGMT_INT_LENGTH + MGMT_HDR_LENGTH;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallLong *field)
{
  return MGMT_LONG_LENGTH + MGMT_HDR_LENGTH;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallString *field)
{
  if (*field == nullptr) {
    field = &empty;
  }

  return MGMT_INT_LENGTH + strlen(*field) + 1 + MGMT_HDR_LENGTH;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallData *field)
{
  return MGMT_INT_LENGTH + field->len + MGMT_HDR_LENGTH;
}

//-----------------------------------------------------------------------
// mgmt_message_write
//-----------------------------------------------------------------------
ssize_t
mgmt_header_write(int fd, MgmtMarshallHdr hdr)
{
  return socket_write_bytes(fd, &hdr, MGMT_HDR_LENGTH);
}

ssize_t
mgmt_message_write(int fd)
{
  return 0;
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallInt *field)
{
  // first, send the type info.
  ssize_t nhdr = mgmt_header_write(fd, MGMT_INT_HDR);
  if (nhdr == -1) {
    return -1;
  }

  // send the actual integer value.
  ssize_t nbytes = socket_write_bytes(fd, field, MGMT_INT_LENGTH);
  if (nbytes == -1) {
    return -1;
  }
  return nhdr + nbytes;
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallLong *field)
{
  // first, send the type info.
  ssize_t nhdr = mgmt_header_write(fd, MGMT_LONG_HDR);
  if (nhdr == -1) {
    return -1;
  }

  // send the actual value.
  ssize_t nbytes = socket_write_bytes(fd, field, MGMT_LONG_LENGTH);
  if (nbytes == -1) {
    return -1;
  }
  return nhdr + nbytes;
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallString *field)
{
  MgmtMarshallData data;

  ssize_t nhdr = mgmt_header_write(fd, MGMT_STRING_HDR);
  // first, send the type info.
  if (nhdr == -1) {
    return -1;
  }
  if (*field == nullptr) {
    field = &empty;
  }

  data.ptr = *field;
  data.len = strlen(*field) + 1;
  // send the actual string.
  ssize_t nbytes = socket_write_buffer(fd, &data);
  if (nbytes == -1) {
    return -1;
  }
  return nhdr + nbytes;
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallData *field)
{
  ssize_t nhdr = mgmt_header_write(fd, MGMT_DATA_HDR);
  if (nhdr == -1) {
    return -1;
  }

  ssize_t nbytes = socket_write_buffer(fd, field);
  if (nbytes == -1) {
    return -1;
  }
  return nhdr + nbytes;
}

//-----------------------------------------------------------------------
// mgmgt_message_marshall
//-----------------------------------------------------------------------
ssize_t
mgmt_message_marshall(void *buf, size_t remain)
{
  return 0;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallInt *field)
{
  if (empty_buf(buf, remain)) {
    return 0;
  }
  if (remain < 4) {
    return nospace();
  }
  memcpy(buf, &MGMT_INT_HDR, MGMT_HDR_LENGTH);
  memcpy(static_cast<char *>(buf) + MGMT_HDR_LENGTH, field, MGMT_INT_LENGTH);
  return MGMT_HDR_LENGTH + MGMT_INT_LENGTH;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallLong *field)
{
  if (empty_buf(buf, remain)) {
    return 0;
  }
  if (remain < 8) {
    return nospace();
  }
  memcpy(buf, &MGMT_LONG_HDR, MGMT_HDR_LENGTH);
  memcpy(static_cast<char *>(buf) + MGMT_HDR_LENGTH, field, MGMT_LONG_LENGTH);
  return MGMT_HDR_LENGTH + MGMT_LONG_LENGTH;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallString *field)
{
  if (empty_buf(buf, remain)) {
    return 0;
  }
  MgmtMarshallData data;
  if (*field == nullptr) {
    field = &empty;
  }

  data.ptr = *field;
  data.len = strlen(*field) + 1;

  if (remain < (MGMT_HDR_LENGTH + MGMT_INT_LENGTH + data.len)) {
    return nospace();
  }

  uint32_t offset = 0;
  memcpy(buf, &MGMT_STRING_HDR, MGMT_HDR_LENGTH);
  offset += MGMT_HDR_LENGTH;
  memcpy(static_cast<char *>(buf) + offset, &data.len, MGMT_INT_LENGTH);
  offset += MGMT_INT_LENGTH;
  memcpy(static_cast<char *>(buf) + offset, data.ptr, data.len);
  return offset + data.len;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallData *field)
{
  if (empty_buf(buf, remain)) {
    return 0;
  }
  if (remain < (MGMT_HDR_LENGTH + MGMT_INT_LENGTH + field->len)) {
    return nospace();
  }

  uint32_t offset = 0;
  memcpy(buf, &MGMT_DATA_HDR, MGMT_HDR_LENGTH);
  offset += MGMT_HDR_LENGTH;
  memcpy(static_cast<char *>(buf) + offset, &(field->len), MGMT_INT_LENGTH);
  offset += MGMT_INT_LENGTH;
  memcpy(static_cast<char *>(buf) + offset, field->ptr, field->len);
  return offset + field->len;
}

//-----------------------------------------------------------------------
// mgmt_message_read
//-----------------------------------------------------------------------
bool
mgmt_header_match(int fd, MgmtMarshallHdr expected)
{
  MgmtMarshallHdr hdr;
  ink_zero(hdr);
  ssize_t nhdr = socket_read_bytes(fd, &hdr, MGMT_HDR_LENGTH);
  if (nhdr == -1) {
    return false;
  }
  if (hdr != expected) {
    Fatal("mgmt_header_read mismatch. expected %d but got %d", expected, hdr);
    return false;
  }
  return true;
}

ssize_t
mgmt_message_read(int fd)
{
  return 0;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallInt *field)
{
  if (!mgmt_header_match(fd, MGMT_INT_HDR)) {
    return -1;
  }

  ssize_t nbytes = socket_read_bytes(fd, field, MGMT_INT_LENGTH);
  if (nbytes == -1) {
    return -1;
  }
  return nbytes + MGMT_HDR_LENGTH;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallLong *field)
{
  if (!mgmt_header_match(fd, MGMT_LONG_HDR)) {
    return -1;
  }

  ssize_t nbytes = socket_read_bytes(fd, field, MGMT_LONG_LENGTH);
  if (nbytes == -1) {
    return -1;
  }
  return nbytes + MGMT_HDR_LENGTH;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallString *field)
{
  if (!mgmt_header_match(fd, MGMT_STRING_HDR)) {
    return -1;
  }

  MgmtMarshallData data;

  ssize_t nread = socket_read_buffer(fd, &data);
  if (nread == -1) {
    return -1;
  }

  ink_assert(data_is_nul_terminated(&data));
  *field = static_cast<char *>(data.ptr);
  return nread + MGMT_HDR_LENGTH;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallData *field)
{
  if (!mgmt_header_match(fd, MGMT_DATA_HDR)) {
    return -1;
  }

  ssize_t nread = socket_read_buffer(fd, field);
  if (nread == -1) {
    return -1;
  }
  return nread + MGMT_HDR_LENGTH;
}

//-----------------------------------------------------------------------
// mgmt_message_parse
//-----------------------------------------------------------------------
bool
mgmt_header_match(const void *buf, MgmtMarshallHdr expected)
{
  if (buf == nullptr) {
    return false;
  }
  MgmtMarshallHdr hdr = *(static_cast<const MgmtMarshallHdr *>(buf));
  if (hdr != expected) {
    Fatal("mgmt_message_parse field mismatch. should be %d but got %d", expected, hdr);
    return false;
  }
  return true;
}

ssize_t
mgmt_message_parse(const void *buf, size_t len)
{
  return 0;
}

ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallInt *field)
{
  if (len < MGMT_INT_LENGTH + MGMT_HDR_LENGTH) {
    return nospace();
  }
  if (!mgmt_header_match(buf, MGMT_INT_HDR)) {
    return -1;
  }
  memcpy(field, static_cast<const uint8_t *>(buf) + MGMT_HDR_LENGTH, MGMT_INT_LENGTH);
  return MGMT_HDR_LENGTH + MGMT_INT_LENGTH;
}
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallLong *field)
{
  if (len < MGMT_LONG_LENGTH + MGMT_HDR_LENGTH) {
    return nospace();
  }
  if (!mgmt_header_match(buf, MGMT_LONG_HDR)) {
    return -1;
  }
  memcpy(field, static_cast<const uint8_t *>(buf) + MGMT_HDR_LENGTH, MGMT_LONG_LENGTH);
  return MGMT_HDR_LENGTH + MGMT_LONG_LENGTH;
}
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallString *field)
{
  if (empty_buf(buf, len)) {
    return 0;
  }
  if (!mgmt_header_match(buf, MGMT_STRING_HDR)) {
    return -1;
  }

  MgmtMarshallData data;
  ssize_t nread = buffer_read_buffer(static_cast<const uint8_t *>(buf) + MGMT_HDR_LENGTH, len - MGMT_HDR_LENGTH, &data);
  if (nread == -1) {
    return nospace();
  }

  ink_assert(data_is_nul_terminated(&data));

  *field = static_cast<char *>(data.ptr);
  return nread + MGMT_HDR_LENGTH;
}
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallData *field)
{
  if (empty_buf(buf, len)) {
    return 0;
  }

  if (!mgmt_header_match(buf, MGMT_DATA_HDR)) {
    return -1;
  }

  ssize_t nread = buffer_read_buffer(static_cast<const uint8_t *>(buf) + MGMT_HDR_LENGTH, len, field);
  return (nread == -1) ? nospace() : nread + MGMT_HDR_LENGTH;
}
