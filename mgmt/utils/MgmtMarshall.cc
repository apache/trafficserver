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
#include "ts/Diags.h"

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

/// Each header is 32 bits. The leading 8 indicate the marshalled type and the last 24 indicate the length.
static constexpr size_t MGMT_LEN_BITS = 24;
static constexpr size_t MGMT_LEN_MASK = 0x00FFFFFF;

// For integral values, we ignore the length so we can create constants for the headers.
static const uint32_t MGMT_INT_HDR  = (MGMT_INT_TYPE << MGMT_LEN_BITS);
static const uint32_t MGMT_LONG_HDR = (MGMT_LONG_TYPE << MGMT_LEN_BITS);

static uint32_t
get_type_from_hdr(const MgmtMarshallHdr hdr)
{
  return (hdr >> MGMT_LEN_BITS); // shift away the length bits.
}

static uint32_t
get_len_from_hdr(const MgmtMarshallHdr hdr)
{
  // clears out the top 8 bits
  return hdr & MGMT_LEN_MASK;
}

MgmtMarshallHdr
mgmt_message_build_hdr(const uint8_t type, const uint32_t len)
{
  return (type << MGMT_LEN_BITS) | (len & MGMT_LEN_MASK);
}

static bool
data_is_nul_terminated(const MgmtMarshallData *data)
{
  const char *str = (const char *)(data->ptr);

  uint32_t len = get_len_from_hdr(data->len);
  if (len == 0) {
    return false;
  }

  if (str[len - 1] != '\0') {
    return false;
  }

  if (strlen(str) != (len - 1)) {
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

  nwrite = socket_write_bytes(fd, &(data->len), MGMT_HDR_LENGTH);
  if (nwrite != MGMT_HDR_LENGTH) {
    goto fail;
  }

  if (data->len) {
    uint32_t len = get_len_from_hdr(data->len);

    nwrite = socket_write_bytes(fd, data->ptr, len);
    if (nwrite != static_cast<ssize_t>(len)) {
      goto fail;
    }
  }

  return nwrite + MGMT_HDR_LENGTH;

fail:
  return -1;
}

static ssize_t
socket_read_buffer(int fd, MgmtMarshallData *data, uint8_t expected)
{
  ssize_t nread;

  ink_zero(*data);

  nread = socket_read_bytes(fd, &(data->len), MGMT_HDR_LENGTH);
  if (nread != MGMT_HDR_LENGTH) {
    goto fail;
  }

  if (data->len) {
    uint8_t type = get_type_from_hdr(data->len);
    if (type != expected) {
      Fatal("mgmt_message_read mismatch. Expected %d but got %d", expected, type);
      goto fail;
    }
  }

  if (data->len) {
    data->len = get_len_from_hdr(data->len);

    data->ptr = ats_malloc(data->len);
    nread     = socket_read_bytes(fd, data->ptr, data->len);
    if (nread != static_cast<ssize_t>(data->len)) {
      goto fail;
    }
  }

  return nread + MGMT_HDR_LENGTH;

fail:
  ats_free(data->ptr);
  ink_zero(*data);
  return -1;
}

static ssize_t
buffer_read_buffer(const uint8_t *buf, size_t len, MgmtMarshallData *data)
{
  ink_zero(*data);

  if (len < MGMT_HDR_LENGTH) {
    goto fail;
  }

  memcpy(&(data->len), buf, MGMT_HDR_LENGTH);
  buf += MGMT_HDR_LENGTH;
  len -= MGMT_HDR_LENGTH;

  data->len = get_len_from_hdr(data->len);

  if (len < data->len) {
    goto fail;
  }

  if (data->len) {
    data->ptr = ats_malloc(data->len);
    memcpy(data->ptr, buf, data->len);
  }

  return data->len + MGMT_HDR_LENGTH;

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

  return MGMT_HDR_LENGTH + strlen(*field) + 1;
}

MgmtMarshallInt
mgmt_message_length(MgmtMarshallData *field)
{
  return MGMT_HDR_LENGTH + get_len_from_hdr(field->len);
}

//-----------------------------------------------------------------------
// mgmt_message_write
//-----------------------------------------------------------------------
ssize_t
mgmt_message_write(int fd)
{
  return 0;
}

ssize_t
mgmt_message_write(int fd, MgmtMarshallInt *field)
{
  // First, send the header.
  ssize_t nhdr = socket_write_bytes(fd, &MGMT_INT_HDR, MGMT_HDR_LENGTH);
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
  // First, send the header
  ssize_t nhdr = socket_write_bytes(fd, &MGMT_LONG_HDR, MGMT_HDR_LENGTH);
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
  if (*field == nullptr) {
    field = &empty;
  }

  data.ptr = *field;
  data.len = mgmt_message_build_hdr(MGMT_STRING_TYPE, static_cast<uint32_t>(strlen(*field) + 1));

  ssize_t nbytes = socket_write_buffer(fd, &data);
  if (nbytes == -1) {
    return -1;
  }
  return nbytes;
}

// under the hood, we augment the MgmtMarshallData object to match the protocol.
ssize_t
mgmt_message_write(int fd, MgmtMarshallData *field)
{
  MgmtMarshallData data = *field;

  data.ptr = field->ptr;
  data.len = mgmt_message_build_hdr(MGMT_DATA_TYPE, field->len);

  ssize_t nbytes = socket_write_buffer(fd, &data);
  if (nbytes == -1) {
    return -1;
  }
  return nbytes;
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
  if (remain < MGMT_INT_LENGTH) {
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
  uint32_t msglen = static_cast<uint32_t>(strlen(*field) + 1);

  data.ptr = *field;
  data.len = mgmt_message_build_hdr(MGMT_STRING_TYPE, msglen);

  if (remain < (MGMT_HDR_LENGTH + msglen)) {
    return nospace();
  }

  memcpy(buf, &(data.len), MGMT_HDR_LENGTH);
  memcpy(static_cast<char *>(buf) + MGMT_HDR_LENGTH, data.ptr, msglen);
  return MGMT_HDR_LENGTH + msglen;
}

ssize_t
mgmt_message_marshall(void *buf, size_t remain, MgmtMarshallData *field)
{
  if (empty_buf(buf, remain)) {
    return 0;
  }
  if (remain < (MGMT_HDR_LENGTH + field->len)) {
    return nospace();
  }

  MgmtMarshallHdr hdr = mgmt_message_build_hdr(MGMT_DATA_TYPE, field->len);

  memcpy(buf, &(hdr), MGMT_HDR_LENGTH);
  memcpy(static_cast<char *>(buf) + MGMT_HDR_LENGTH, field->ptr, field->len);
  return MGMT_HDR_LENGTH + field->len;
}

//-----------------------------------------------------------------------
// mgmt_message_read
//-----------------------------------------------------------------------
ssize_t
mgmt_message_read(int fd)
{
  return 0;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallInt *field)
{
  MgmtMarshallHdr hdr;
  ssize_t nhdr = socket_read_bytes(fd, &hdr, MGMT_HDR_LENGTH);
  if (hdr != MGMT_INT_HDR) {
    Fatal("mgmt_message_read mismatch. Expected MgmtMarshallInt but got %d", hdr >> MGMT_LEN_BITS);
    return -1;
  }

  ssize_t nbytes = socket_read_bytes(fd, field, MGMT_INT_LENGTH);
  if (nbytes == -1) {
    return -1;
  }
  return nbytes + nhdr;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallLong *field)
{
  MgmtMarshallHdr hdr;
  ssize_t nhdr = socket_read_bytes(fd, &hdr, MGMT_HDR_LENGTH);
  if (hdr != MGMT_LONG_HDR) {
    Fatal("mgmt_message_read mismatch. Expected MgmtMarshallLong but got %d", hdr >> MGMT_LEN_BITS);
    return -1;
  }

  ssize_t nbytes = socket_read_bytes(fd, field, MGMT_LONG_LENGTH);
  if (nbytes == -1) {
    return -1;
  }
  return nbytes + nhdr;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallString *field)
{
  MgmtMarshallData data;

  ssize_t nread = socket_read_buffer(fd, &data, MGMT_STRING_TYPE);
  if (nread == -1) {
    return -1;
  }

  ink_assert(data_is_nul_terminated(&data));
  *field = static_cast<char *>(data.ptr);
  return nread;
}

ssize_t
mgmt_message_read(int fd, MgmtMarshallData *field)
{
  ssize_t nread = socket_read_buffer(fd, field, MGMT_DATA_TYPE);
  if (nread == -1) {
    return -1;
  }
  return nread;
}

//-----------------------------------------------------------------------
// mgmt_message_parse
//-----------------------------------------------------------------------
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
  MgmtMarshallHdr hdr;
  memcpy(&hdr, buf, MGMT_HDR_LENGTH);
  if (hdr != MGMT_INT_HDR) {
    Fatal("mgmt_message_parse mismatch. Expected MgmtMarshallInt but got %d", hdr >> MGMT_LEN_BITS);
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
  MgmtMarshallHdr hdr;
  memcpy(&hdr, buf, MGMT_HDR_LENGTH);
  if (hdr != MGMT_LONG_HDR) {
    Fatal("mgmt_message_parse mismatch. Expected MgmtMarshallLong but got %d", hdr >> MGMT_LEN_BITS);
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
  MgmtMarshallHdr hdr;
  memcpy(&hdr, buf, MGMT_HDR_LENGTH);
  if (get_type_from_hdr(hdr) != MGMT_STRING_TYPE) {
    Fatal("mgmt_message_parse mismatch. Expected MgmtMarshallString but got %d", hdr >> MGMT_LEN_BITS);
    return -1;
  }

  MgmtMarshallData data;
  ssize_t nread = buffer_read_buffer(static_cast<const uint8_t *>(buf), len, &data);
  if (nread == -1) {
    return nospace();
  }

  ink_assert(data_is_nul_terminated(&data));

  *field = static_cast<char *>(data.ptr);
  return nread;
}
ssize_t
mgmt_message_parse(const void *buf, size_t len, MgmtMarshallData *field)
{
  if (empty_buf(buf, len)) {
    return 0;
  }

  MgmtMarshallHdr hdr;
  memcpy(&hdr, buf, MGMT_HDR_LENGTH);
  if (get_type_from_hdr(hdr) != MGMT_DATA_TYPE) {
    Fatal("mgmt_message_parse mismatch. Expected MgmtMarshallData but got %d", hdr >> MGMT_LEN_BITS);
    return -1;
  }

  ssize_t nread = buffer_read_buffer(static_cast<const uint8_t *>(buf), len, field);
  return (nread == -1) ? nospace() : nread;
}
