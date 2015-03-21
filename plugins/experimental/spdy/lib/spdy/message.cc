/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "spdy.h"
#include "zstream.h"
#include <base/logging.h>

#include <stdexcept>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include <string.h>
#include <arpa/inet.h>

template <>
std::string
stringof<spdy::control_frame_type>(const spdy::control_frame_type &ev)
{
  static const detail::named_value<unsigned> control_names[] = {{"CONTROL_SYN_STREAM", 1},
                                                                {"CONTROL_SYN_REPLY", 2},
                                                                {"CONTROL_RST_STREAM", 3},
                                                                {"CONTROL_SETTINGS", 4},
                                                                {"CONTROL_PING", 6},
                                                                {"CONTROL_GOAWAY", 7},
                                                                {"CONTROL_HEADERS", 8},
                                                                {"CONTROL_WINDOW_UPDATE", 9}};

  return detail::match(control_names, (unsigned)ev);
}

template <>
std::string
stringof<spdy::error>(const spdy::error &e)
{
  static const detail::named_value<unsigned> error_names[] = {{"PROTOCOL_ERROR", 1},
                                                              {"INVALID_STREAM", 2},
                                                              {"REFUSED_STREAM", 3},
                                                              {"UNSUPPORTED_VERSION", 4},
                                                              {"CANCEL", 5},
                                                              {"FLOW_CONTROL_ERROR", 6},
                                                              {"STREAM_IN_USE", 7},
                                                              {"STREAM_ALREADY_CLOSED", 8}};

  return detail::match(error_names, (unsigned)e);
}

// XXX for the insert and extract we assume that the compiler uses an intrinsic
// to inline the memcpy() calls. Need to verify this by examining the
// assembler.

template <typename T>
T
extract(const uint8_t *&ptr)
{
  T val;
  memcpy(&val, ptr, sizeof(val));
  std::advance(ptr, sizeof(val));
  return val;
}

template <>
uint8_t
extract<uint8_t>(const uint8_t *&ptr)
{
  return *ptr++;
}

template <typename T>
void
insert(const T &val, uint8_t *&ptr)
{
  memcpy(ptr, &val, sizeof(val));
  std::advance(ptr, sizeof(val));
}

static inline uint32_t
extract_stream_id(const uint8_t *&ptr)
{
  return ntohl(extract<uint32_t>(ptr)) & 0x7fffffffu;
}

static inline void
insert_stream_id(uint32_t stream_id, uint8_t *&ptr)
{
  insert<uint32_t>(htonl(stream_id & 0x7fffffffu), ptr);
}

spdy::message_header
spdy::message_header::parse(const uint8_t *ptr, size_t len)
{
  message_header header;

  if (len < message_header::size) {
    throw protocol_error(std::string("short frame header"));
  }

  header.is_control = ((*ptr) & 0x80u) ? true : false;
  if (header.is_control) {
    uint32_t val;
    header.control.version = ntohs(extract<uint16_t>(ptr)) & 0x7fffu;
    header.control.type = (control_frame_type)ntohs(extract<uint16_t>(ptr));
    val = ntohl(extract<uint32_t>(ptr));
    header.flags = (val >> 24);
    header.datalen = (val & 0x00ffffffu);
  } else {
    uint32_t val;
    header.data.stream_id = extract_stream_id(ptr);
    val = ntohl(extract<uint32_t>(ptr));
    header.flags = (val >> 24);
    header.datalen = (val & 0x00ffffffu);
  }

  return header;
}

size_t
spdy::message_header::marshall(const message_header &msg, uint8_t *ptr, size_t len)
{
  if (len < message_header::size) {
    throw protocol_error(std::string("short message_header buffer"));
  }

  if (msg.is_control) {
    insert<uint16_t>(htons(0x8000u | msg.control.version), ptr);
    insert<uint16_t>(htons(msg.control.type), ptr);
    insert<uint32_t>(htonl((msg.flags << 24) | (msg.datalen & 0x00ffffffu)), ptr);
  } else {
    insert_stream_id(msg.data.stream_id, ptr);
    insert<uint32_t>(htonl((msg.flags << 24) | (msg.datalen & 0x00ffffffu)), ptr);
  }

  return message_header::size;
}

spdy::syn_stream_message
spdy::syn_stream_message::parse(const uint8_t *ptr, size_t len)
{
  syn_stream_message msg;

  if (len < syn_stream_message::size) {
    throw protocol_error(std::string("short syn_stream message"));
  }

  msg.stream_id = extract_stream_id(ptr);
  msg.associated_id = extract_stream_id(ptr);
  msg.priority = extract<uint8_t>(ptr) >> 5; // top 3 bits are priority
  (void)extract<uint8_t>(ptr);               // skip unused byte
  return msg;
}

spdy::goaway_message
spdy::goaway_message::parse(const uint8_t *ptr, size_t len)
{
  goaway_message msg;

  if (len < goaway_message::size) {
    throw protocol_error(std::string("short goaway_stream message"));
  }

  msg.last_stream_id = extract_stream_id(ptr);
  msg.status_code = extract_stream_id(ptr);
  return msg;
}

spdy::rst_stream_message
spdy::rst_stream_message::parse(const uint8_t *ptr, size_t len)
{
  rst_stream_message msg;

  if (len < rst_stream_message::size) {
    throw protocol_error(std::string("short rst_stream message"));
  }

  msg.stream_id = extract_stream_id(ptr);
  msg.status_code = extract_stream_id(ptr);
  return msg;
}

size_t
spdy::rst_stream_message::marshall(const rst_stream_message &msg, uint8_t *ptr, size_t len)
{
  if (len < rst_stream_message::size) {
    throw protocol_error(std::string("short rst_stream buffer"));
  }

  insert_stream_id(msg.stream_id, ptr);
  insert<uint32_t>(msg.status_code, ptr);
  return rst_stream_message::size;
}

size_t
spdy::syn_reply_message::marshall(protocol_version version, const syn_reply_message &msg, uint8_t *ptr, size_t len)
{
  if (len < size(version)) {
    throw protocol_error(std::string("short syn_reply buffer"));
  }

  if (version < PROTOCOL_VERSION_3) {
    // SPDYv2 has 2 extraneous bytes at the end. How nice that the SPDYv2
    // spec is no longer on the internets.
    insert_stream_id(msg.stream_id, ptr);
    insert<uint16_t>(0, ptr);
  } else {
    insert_stream_id(msg.stream_id, ptr);
  }

  return size(version);
}

// +------------------------------------+
// | Number of Name/Value pairs (int32) |
// +------------------------------------+
// |     Length of name (int32)         |
// +------------------------------------+
// |           Name (string)            |
// +------------------------------------+
// |     Length of value  (int32)       |
// +------------------------------------+
// |          Value   (string)          |
// +------------------------------------+
// |           (repeats)                |

static spdy::zstream_error
decompress_headers(spdy::zstream<spdy::decompress> &decompressor, std::vector<uint8_t> &bytes)
{
  ssize_t nbytes;

  do {
    size_t avail;
    size_t old = bytes.size();
    bytes.resize(bytes.size() + getpagesize());
    avail = bytes.size() - old;
    nbytes = decompressor.consume(&bytes[old], avail);
    if (nbytes > 0) {
      bytes.resize(old + nbytes);
    } else {
      bytes.resize(old);
    }
  } while (nbytes > 0);

  if (nbytes < 0) {
    return (spdy::zstream_error)(-nbytes);
  }

  return spdy::z_ok;
}

static ssize_t
marshall_string_v2(spdy::zstream<spdy::compress> &compressor, const std::string &strval, uint8_t *ptr, size_t len, unsigned flags)
{
  size_t nbytes = 0;
  ssize_t status;
  uint16_t tmp16;

  tmp16 = htons(strval.size());
  compressor.input(&tmp16, sizeof(tmp16));
  status = compressor.consume(ptr + nbytes, len - nbytes, flags);
  if (status < 0) {
    return status;
  }

  nbytes += status;

  compressor.input(strval.c_str(), strval.size());
  status = compressor.consume(ptr + nbytes, len - nbytes, flags);
  if (status < 0) {
    return status;
  }

  nbytes += status;
  return nbytes;
}

static ssize_t
marshall_name_value_pairs_v2(spdy::zstream<spdy::compress> &compressor, const spdy::key_value_block &kvblock, uint8_t *ptr,
                             size_t len)
{
  size_t nbytes = 0;
  ssize_t status;
  uint16_t tmp16;

  tmp16 = htons(kvblock.size());
  compressor.input(&tmp16, sizeof(tmp16));
  status = compressor.consume(ptr + nbytes, len - nbytes, 0);
  if (status < 0) {
    return status;
  }

  nbytes += status;

  for (auto kv(kvblock.begin()); kv != kvblock.end(); ++kv) {
    status = marshall_string_v2(compressor, kv->first, ptr + nbytes, len - nbytes, 0);
    if (status < 0) {
      return status;
    }

    nbytes += status;

    status = marshall_string_v2(compressor, kv->second, ptr + nbytes, len - nbytes, 0);
    if (status < 0) {
      return status;
    }

    nbytes += status;
  }

  do {
    status = compressor.consume(ptr + nbytes, len - nbytes, Z_SYNC_FLUSH);
    if (status < 0) {
      return status;
    }
    nbytes += status;
  } while (status != 0);

  return nbytes;
}

static spdy::key_value_block
parse_name_value_pairs_v2(const uint8_t *ptr, size_t len)
{
  int32_t npairs;
  const uint8_t *end = ptr + len;

  spdy::key_value_block kvblock;

  if (len < sizeof(int32_t)) {
    // XXX throw
  }

  npairs = ntohs(extract<int16_t>(ptr));
  if (npairs < 1) {
    //
  }

  while (npairs--) {
    std::string key;
    std::string val;
    int32_t nbytes;

    if (std::distance(ptr, end) < 8) {
      // XXX
    }

    nbytes = ntohs(extract<uint16_t>(ptr));
    if (std::distance(ptr, end) < nbytes) {
      // XXX
    }

    key.assign((const char *)ptr, nbytes);
    std::advance(ptr, nbytes);

    nbytes = ntohs(extract<uint16_t>(ptr));
    if (std::distance(ptr, end) < nbytes) {
      // XXX
    }

    val.assign((const char *)ptr, nbytes);
    std::advance(ptr, nbytes);

    // XXX Extract this assignment section into a lambda. This would let us
    // parse the kvblock into a key_value_block, or straight into the
    // corresponding ATS data structures.
    if (key == "host") {
      kvblock.url().hostport = val;
    } else if (key == "scheme") {
      kvblock.url().scheme = val;
    } else if (key == "url") {
      kvblock.url().path = val;
    } else if (key == "method") {
      kvblock.url().method = val;
    } else if (key == "version") {
      kvblock.url().version = val;
    } else {
      kvblock.headers[key] = val;
    }
  }

  return kvblock;
}

spdy::key_value_block
spdy::key_value_block::parse(protocol_version version, zstream<decompress> &decompressor, const uint8_t *ptr, size_t len)
{
  std::vector<uint8_t> bytes;
  key_value_block kvblock;

  if (version != PROTOCOL_VERSION_2) {
    // XXX support v3 and throw a proper damn error.
    throw std::runtime_error("unsupported version");
  }

  decompressor.input(ptr, len);
  if (decompress_headers(decompressor, bytes) != z_ok) {
    // XXX
  }

  return parse_name_value_pairs_v2(&bytes[0], bytes.size());
}

size_t
spdy::key_value_block::marshall(protocol_version version, spdy::zstream<compress> &compressor, const key_value_block &kvblock,
                                uint8_t *ptr, size_t len)
{
  ssize_t nbytes;

  if (version != PROTOCOL_VERSION_2) {
    // XXX support v3 and throw a proper damn error.
    throw std::runtime_error("unsupported version");
  }

  nbytes = marshall_name_value_pairs_v2(compressor, kvblock, ptr, len);
  if (nbytes < 0) {
    throw std::runtime_error("marshalling failure");
  }

  return nbytes;
}

size_t
spdy::key_value_block::nbytes(protocol_version version) const
{
  size_t nbytes = 0;
  size_t lensz;

  // Length fields are 2 bytes in SPDYv2 and 4 in later versions.
  switch (version) {
  case PROTOCOL_VERSION_3:
    lensz = 4;
    break;
  case PROTOCOL_VERSION_2:
    lensz = 2;
    break;
  default:
    throw std::runtime_error("unsupported version");
  }

  nbytes += lensz;
  for (auto ptr(begin()); ptr != end(); ++ptr) {
    nbytes += lensz + ptr->first.size();
    nbytes += lensz + ptr->second.size();
  }

  return nbytes;
}

struct lowercase : public std::unary_function<char, char> {
  char operator()(char c) const
  {
    // Return the lowercase ASCII only if it's in the uppercase
    // ASCII range.
    if (c > 0x40 && c < 0x5b) {
      return c + 0x20;
    }

    return c;
  }
};

void
spdy::key_value_block::insert(std::string key, std::string value)
{
  std::transform(key.begin(), key.end(), key.begin(), lowercase());
  headers[key] = value;
}

spdy::ping_message
spdy::ping_message::parse(const uint8_t *ptr, size_t len)
{
  ping_message msg;

  if (len < ping_message::size) {
    throw protocol_error(std::string("short ping message"));
  }

  msg.ping_id = ntohl(extract<uint32_t>(ptr));
  return msg;
}

size_t
spdy::ping_message::marshall(const ping_message &msg, uint8_t *ptr, size_t len)
{
  if (len < ping_message::size) {
    throw protocol_error(std::string("short ping_message buffer"));
  }

  insert<uint32_t>(htonl(msg.ping_id), ptr);
  return ping_message::size;
}

/* vim: set sw=4 ts=4 tw=79 et : */
