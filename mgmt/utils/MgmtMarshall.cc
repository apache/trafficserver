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
mgmt_message_length(MgmtMarshallInt *field)
{
  return 4;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallLong *field)
{
  return 8;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallString *field)
{
  if (*field == nullptr) {
    field = &empty;
  }
  return 4 + strlen(*field) + 1;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallData *field)
{
  return 4 + field->len;
}

//-----------------------------------------------------------------------
// mgmt_message_write
//-----------------------------------------------------------------------
ssize_t
mgmt_message_write(int fd, MgmtMarshallInt *field)
{
  return socket_write_bytes(fd, field, 4);
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallLong *field)
{
  return socket_write_bytes(fd, field, 8);
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallString *field)
{
  MgmtMarshallData data;
  if (*field == nullptr) {
    field = &empty;
  }
  data.ptr = *field;
  data.len = strlen(*field) + 1;
  return socket_write_buffer(fd, &data);
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallData *field)
{
  return socket_write_buffer(fd, field);
}

//-----------------------------------------------------------------------
// mgmgt_message_marshall
//-----------------------------------------------------------------------
ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallInt *field)
{
  if (remain < 4) {
    return nospace();
  }
  memcpy(buf, field, 4);
  return 4;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallLong *field)
{
  if (remain < 8) {
    return nospace();
  }
  memcpy(buf, field, 8);
  return 8;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallString *field)
{
  MgmtMarshallData data;
  if (*field == nullptr) {
    field = &empty;
  }

  data.ptr = field;
  data.len = strlen(*field) + 1;

  if (remain < (4 + data.len)) {
    return nospace();
  }

  memcpy(buf, &data.len, 4);
  memcpy((uint8_t *)buf + 4, data.ptr, data.len);
  return 4 + data.len;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallData *field)
{
  if (remain < (4 + field->len)) {
    return nospace();
  }
  memcpy(buf, &(field->len), 4);
  memcpy((uint8_t *)buf + 4, field->ptr, field->len);
  return 4 + field->len;
}

//-----------------------------------------------------------------------
// mgmt_message_read
//-----------------------------------------------------------------------
ssize_t
mgmt_message_read(int fd, MgmtMarshallInt *field)
{
  return socket_read_bytes(fd, field, 4);
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallLong *field)
{
  return socket_read_bytes(fd, field, 8);
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallString *field)
{
  MgmtMarshallData data;

  ssize_t nread = socket_read_buffer(fd, &data);
  if (nread == -1) {
    return -1;
  }

  ink_assert(data_is_nul_terminated(&data));
  *field = (char *)data.ptr;
  return nread;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallData *field)
{
  return socket_read_buffer(fd, field);
}

//-----------------------------------------------------------------------
// mgmt_message_parse
//-----------------------------------------------------------------------
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallInt *field)
{
  if (len < 4) {
    return nospace();
  }
  memcpy(field, buf, 4);
  return 4;
}
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallLong *field)
{
  if (len < 8) {
    return nospace();
  }
  memcpy(field, buf, 8);
  return 8;
}
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallString *field)
{
  MgmtMarshallData data;
  ssize_t nread = buffer_read_buffer((const uint8_t *)buf, len, &data);
  if (nread == -1) {
    return nospace();
  }

  ink_assert(data_is_nul_terminated(&data));

  *field = (char *)data.ptr;
  return nread;
}
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallData *field)
{
  ssize_t nread = buffer_read_buffer((const uint8_t *)buf, len, field);
  return (nread == -1) ? nospace() : nread;
}
