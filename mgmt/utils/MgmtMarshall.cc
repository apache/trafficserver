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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_assert.h"
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

  ink_assert(str);
  if (str[data->len - 1] != '\0') {
    return false;
  }

  if (strlen(str) != (data->len - 1)) {
    return false;
  }

  return true;
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

MgmtMarshallInt
mgmt_message_length(const MgmtMarshallType *fields, unsigned count, ...)
{
  MgmtMarshallInt length;
  va_list ap;

  va_start(ap, count);
  length = mgmt_message_length_v(fields, count, ap);
  va_end(ap);

  return length;
}

MgmtMarshallInt
mgmt_message_length_v(const MgmtMarshallType *fields, unsigned count, va_list ap)
{
  MgmtMarshallAnyPtr ptr;
  MgmtMarshallInt nbytes = 0;

  for (unsigned n = 0; n < count; ++n) {
    switch (fields[n]) {
    case MGMT_MARSHALL_INT:
      ptr.m_int = va_arg(ap, MgmtMarshallInt *);
      nbytes += 4;
      break;
    case MGMT_MARSHALL_LONG:
      ptr.m_long = va_arg(ap, MgmtMarshallLong *);
      nbytes += 8;
      break;
    case MGMT_MARSHALL_STRING:
      nbytes += 4;
      ptr.m_string = va_arg(ap, MgmtMarshallString *);
      if (*ptr.m_string == nullptr) {
        ptr.m_string = &empty;
      }
      nbytes += strlen(*ptr.m_string) + 1;
      break;
    case MGMT_MARSHALL_DATA:
      nbytes += 4;
      ptr.m_data = va_arg(ap, MgmtMarshallData *);
      nbytes += ptr.m_data->len;
      break;
    default:
      errno = EINVAL;
      return -1;
    }
  }

  return nbytes;
}

ssize_t
mgmt_message_write(int fd, const MgmtMarshallType *fields, unsigned count, ...)
{
  ssize_t nbytes;
  va_list ap;

  va_start(ap, count);
  nbytes = mgmt_message_write_v(fd, fields, count, ap);
  va_end(ap);

  return nbytes;
}

ssize_t
mgmt_message_write_v(int fd, const MgmtMarshallType *fields, unsigned count, va_list ap)
{
  MgmtMarshallAnyPtr ptr;
  ssize_t nbytes = 0;

  for (unsigned n = 0; n < count; ++n) {
    ssize_t nwritten = 0;

    switch (fields[n]) {
    case MGMT_MARSHALL_INT:
      ptr.m_int = va_arg(ap, MgmtMarshallInt *);
      nwritten  = socket_write_bytes(fd, ptr.m_void, 4);
      break;
    case MGMT_MARSHALL_LONG:
      ptr.m_long = va_arg(ap, MgmtMarshallLong *);
      nwritten   = socket_write_bytes(fd, ptr.m_void, 8);
      break;
    case MGMT_MARSHALL_STRING: {
      MgmtMarshallData data;
      ptr.m_string = va_arg(ap, MgmtMarshallString *);
      if (*ptr.m_string == nullptr) {
        ptr.m_string = &empty;
      }
      data.ptr = *ptr.m_string;
      data.len = strlen(*ptr.m_string) + 1;
      nwritten = socket_write_buffer(fd, &data);
      break;
    }
    case MGMT_MARSHALL_DATA:
      ptr.m_data = va_arg(ap, MgmtMarshallData *);
      nwritten   = socket_write_buffer(fd, ptr.m_data);
      break;
    default:
      errno = EINVAL;
      return -1;
    }

    if (nwritten == -1) {
      return -1;
    }

    nbytes += nwritten;
  }

  return nbytes;
}

ssize_t
mgmt_message_read(int fd, const MgmtMarshallType *fields, unsigned count, ...)
{
  ssize_t nbytes;
  va_list ap;

  va_start(ap, count);
  nbytes = mgmt_message_read_v(fd, fields, count, ap);
  va_end(ap);

  return nbytes;
}

ssize_t
mgmt_message_read_v(int fd, const MgmtMarshallType *fields, unsigned count, va_list ap)
{
  MgmtMarshallAnyPtr ptr;
  ssize_t nbytes = 0;

  for (unsigned n = 0; n < count; ++n) {
    ssize_t nread;

    switch (fields[n]) {
    case MGMT_MARSHALL_INT:
      ptr.m_int = va_arg(ap, MgmtMarshallInt *);
      nread     = socket_read_bytes(fd, ptr.m_void, 4);
      break;
    case MGMT_MARSHALL_LONG:
      ptr.m_long = va_arg(ap, MgmtMarshallLong *);
      nread      = socket_read_bytes(fd, ptr.m_void, 8);
      break;
    case MGMT_MARSHALL_STRING: {
      MgmtMarshallData data;

      nread = socket_read_buffer(fd, &data);
      if (nread == -1) {
        break;
      }

      ink_assert(data_is_nul_terminated(&data));
      ptr.m_string  = va_arg(ap, MgmtMarshallString *);
      *ptr.m_string = (char *)data.ptr;
      break;
    }
    case MGMT_MARSHALL_DATA:
      ptr.m_data = va_arg(ap, MgmtMarshallData *);
      nread      = socket_read_buffer(fd, ptr.m_data);
      break;
    default:
      errno = EINVAL;
      return -1;
    }

    if (nread == -1) {
      return -1;
    }

    nbytes += nread;
  }

  return nbytes;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, const MgmtMarshallType *fields, unsigned count, ...)
{
  ssize_t nbytes = 0;
  va_list ap;

  va_start(ap, count);
  nbytes = mgmt_message_marshall_v(buf, remain, fields, count, ap);
  va_end(ap);

  return nbytes;
}

ssize_t
mgmt_message_marshall_v(void *buf, size_t remain, const MgmtMarshallType *fields, unsigned count, va_list ap)
{
  MgmtMarshallAnyPtr ptr;
  ssize_t nbytes = 0;

  for (unsigned n = 0; n < count; ++n) {
    ssize_t nwritten = 0;

    switch (fields[n]) {
    case MGMT_MARSHALL_INT:
      if (remain < 4) {
        goto nospace;
      }
      ptr.m_int = va_arg(ap, MgmtMarshallInt *);
      memcpy(buf, ptr.m_int, 4);
      nwritten = 4;
      break;
    case MGMT_MARSHALL_LONG:
      if (remain < 8) {
        goto nospace;
      }
      ptr.m_long = va_arg(ap, MgmtMarshallLong *);
      memcpy(buf, ptr.m_long, 8);
      nwritten = 8;
      break;
    case MGMT_MARSHALL_STRING: {
      MgmtMarshallData data;
      ptr.m_string = va_arg(ap, MgmtMarshallString *);
      if (*ptr.m_string == nullptr) {
        ptr.m_string = &empty;
      }

      data.ptr = *ptr.m_string;
      data.len = strlen(*ptr.m_string) + 1;

      if (remain < (4 + data.len)) {
        goto nospace;
      }

      memcpy(buf, &data.len, 4);
      memcpy((uint8_t *)buf + 4, data.ptr, data.len);
      nwritten = 4 + data.len;
      break;
    }
    case MGMT_MARSHALL_DATA:
      ptr.m_data = va_arg(ap, MgmtMarshallData *);
      if (remain < (4 + ptr.m_data->len)) {
        goto nospace;
      }
      memcpy(buf, &(ptr.m_data->len), 4);
      memcpy((uint8_t *)buf + 4, ptr.m_data->ptr, ptr.m_data->len);
      nwritten = 4 + ptr.m_data->len;
      break;
    default:
      errno = EINVAL;
      return -1;
    }

    nbytes += nwritten;
    buf = (uint8_t *)buf + nwritten;
    remain -= nwritten;
  }

  return nbytes;

nospace:
  errno = EMSGSIZE;
  return -1;
}

ssize_t
mgmt_message_parse(const void *buf, size_t len, const MgmtMarshallType *fields, unsigned count, ...)
{
  MgmtMarshallInt nbytes = 0;
  va_list ap;

  va_start(ap, count);
  nbytes = mgmt_message_parse_v(buf, len, fields, count, ap);
  va_end(ap);

  return nbytes;
}

ssize_t
mgmt_message_parse_v(const void *buf, size_t len, const MgmtMarshallType *fields, unsigned count, va_list ap)
{
  MgmtMarshallAnyPtr ptr;
  ssize_t nbytes = 0;

  for (unsigned n = 0; n < count; ++n) {
    ssize_t nread;

    switch (fields[n]) {
    case MGMT_MARSHALL_INT:
      if (len < 4) {
        goto nospace;
      }
      ptr.m_int = va_arg(ap, MgmtMarshallInt *);
      memcpy(ptr.m_int, buf, 4);
      nread = 4;
      break;
    case MGMT_MARSHALL_LONG:
      if (len < 8) {
        goto nospace;
      }
      ptr.m_long = va_arg(ap, MgmtMarshallLong *);
      memcpy(ptr.m_int, buf, 8);
      nread = 8;
      break;
    case MGMT_MARSHALL_STRING: {
      MgmtMarshallData data;
      nread = buffer_read_buffer((const uint8_t *)buf, len, &data);
      if (nread == -1) {
        goto nospace;
      }

      ink_assert(data_is_nul_terminated(&data));

      ptr.m_string  = va_arg(ap, MgmtMarshallString *);
      *ptr.m_string = (char *)data.ptr;
      break;
    }
    case MGMT_MARSHALL_DATA:
      ptr.m_data = va_arg(ap, MgmtMarshallData *);
      nread      = buffer_read_buffer((const uint8_t *)buf, len, ptr.m_data);
      if (nread == -1) {
        goto nospace;
      }
      break;
    default:
      errno = EINVAL;
      return -1;
    }

    nbytes += nread;
    buf = (uint8_t *)buf + nread;
    len -= nread;
  }

  return nbytes;

nospace:
  errno = EMSGSIZE;
  return -1;
}
