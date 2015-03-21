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

#ifndef SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB
#define SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB

#include <inttypes.h>
#include <stddef.h>
#include <stdexcept>
#include <string>
#include <map>

#include "zstream.h"

namespace spdy
{
enum protocol_version : unsigned {
  PROTOCOL_VERSION_2 = 2,
  PROTOCOL_VERSION_3 = 3,
};

enum : unsigned {
  PROTOCOL_VERSION = 3,
  MAX_FRAME_LENGTH = (1u << 24),
};

enum : unsigned {
  FLAG_FIN = 1,
  FLAG_COMPRESSED = 2,
};

struct protocol_error : public std::runtime_error {
  explicit protocol_error(const std::string &msg) : std::runtime_error(msg) {}
};

enum control_frame_type : unsigned {
  CONTROL_SYN_STREAM = 1,
  CONTROL_SYN_REPLY = 2,
  CONTROL_RST_STREAM = 3,
  CONTROL_SETTINGS = 4,
  CONTROL_PING = 6,
  CONTROL_GOAWAY = 7,
  CONTROL_HEADERS = 8,
  CONTROL_WINDOW_UPDATE = 9
};


enum error : unsigned {
  PROTOCOL_ERROR = 1,
  INVALID_STREAM = 2,
  REFUSED_STREAM = 3,
  UNSUPPORTED_VERSION = 4,
  CANCEL = 5,
  FLOW_CONTROL_ERROR = 6,
  STREAM_IN_USE = 7,
  STREAM_ALREADY_CLOSED = 8
};

// Control frame header:
// +----------------------------------+
// |C| Version(15bits) | Type(16bits) |
// +----------------------------------+
// | Flags (8)  |  Length (24 bits)   |
// +----------------------------------+
// |               Data               |
// +----------------------------------+
//
// Data frame header:
// +----------------------------------+
// |C|       Stream-ID (31bits)       |
// +----------------------------------+
// | Flags (8)  |  Length (24 bits)   |
// +----------------------------------+
// |               Data               |
// +----------------------------------+

struct message_header {
  union {
    struct {
      unsigned version;
      control_frame_type type;
    } control;
    struct {
      unsigned stream_id;
    } data;
  };

  bool is_control;
  uint8_t flags;
  uint32_t datalen;

  static message_header parse(const uint8_t *, size_t);
  static size_t marshall(const message_header &, uint8_t *, size_t);
  enum : unsigned {
    size = 8,
  }; /* bytes */
};

// SYN_STREAM frame:
//
// +------------------------------------+
// |1|    version    |         1        |
// +------------------------------------+
// |  Flags (8)  |  Length (24 bits)    |
// +------------------------------------+
// |X|           Stream-ID (31bits)     |
// +------------------------------------+
// |X| Associated-To-Stream-ID (31bits) |
// +------------------------------------+
// |  Pri | Unused | Header Count(int16)|
// +------------------------------------+   <+
// |     Length of name (int32)         |    | This section is the "Name/Value
// +------------------------------------+    | Header Block", and is compressed.
// |           Name (string)            |    |
// +------------------------------------+    |
// |     Length of value  (int32)       |    |
// +------------------------------------+    |
// |          Value   (string)          |    |
// +------------------------------------+    |
// |           (repeats)                |   <+

struct syn_stream_message {
  unsigned stream_id;
  unsigned associated_id;
  unsigned priority;
  unsigned header_count;

  static syn_stream_message parse(const uint8_t *, size_t);
  enum : unsigned {
    size = 10,
  }; /* bytes */
};

// SYN_REPLY frame:
//
// +------------------------------------+
// |1|    version    |         2        |
// +------------------------------------+
// |  Flags (8)  |  Length (24 bits)    |
// +------------------------------------+
// |X|           Stream-ID (31bits)     |
// +------------------------------------+
// | Number of Name/Value pairs (int32) |   <+
// +------------------------------------+    |
// |     Length of name (int32)         |    | This section is the "Name/Value
// +------------------------------------+    | Header Block", and is compressed.
// |           Name (string)            |    |
// +------------------------------------+    |
// |     Length of value  (int32)       |    |
// +------------------------------------+    |
// |          Value   (string)          |    |
// +------------------------------------+    |
// |           (repeats)                |   <+

struct syn_reply_message {
  unsigned stream_id;

  static syn_stream_message parse(const uint8_t *, size_t);
  static size_t marshall(protocol_version, const syn_reply_message &, uint8_t *, size_t);

  static unsigned
  size(protocol_version v)
  {
    return (v == PROTOCOL_VERSION_2) ? 6 : 4; /* bytes */
  }
};

// GOAWAY frame:
//
// +----------------------------------+
// |1|   version    |         7       |
// +----------------------------------+
// | 0 (flags) |     8 (length)       |
// +----------------------------------|
// |X|  Last-good-stream-ID (31 bits) |
// +----------------------------------+
// |          Status code             |
// +----------------------------------+

struct goaway_message {
  unsigned last_stream_id;
  unsigned status_code;

  static goaway_message parse(const uint8_t *, size_t);
  enum : unsigned {
    size = 8,
  }; /* bytes */
};

struct rst_stream_message {
  unsigned stream_id;
  unsigned status_code;

  static rst_stream_message parse(const uint8_t *, size_t);
  static size_t marshall(const rst_stream_message &, uint8_t *, size_t);
  enum : unsigned {
    size = 8,
  }; /* bytes */
};

struct ping_message {
  unsigned ping_id;

  static ping_message parse(const uint8_t *, size_t);
  static size_t marshall(const ping_message &, uint8_t *, size_t);
  enum : unsigned {
    size = 4,
  }; /* bytes */
};

struct url_components {
  std::string method;
  std::string scheme;
  std::string hostport;
  std::string path;
  std::string version;

  bool
  is_complete() const
  {
    return !(method.empty() && scheme.empty() && hostport.empty() && path.empty() && version.empty());
  }
};

struct key_value_block {
  typedef std::map<std::string, std::string> map_type;
  typedef map_type::const_iterator const_iterator;
  typedef map_type::iterator iterator;

  map_type::size_type
  size() const
  {
    return headers.size();
  }

  bool
  exists(const std::string &key) const
  {
    return headers.find(key) != headers.end();
  }

  // Insert the lower-cased key.
  void insert(std::string key, std::string value);

  std::string &operator[](const std::string &key) { return headers[key]; }

  const std::string &operator[](const std::string &key) const { return headers[key]; }

  // Return the number of marshalling bytes this kvblock needs.
  size_t nbytes(protocol_version) const;

  const_iterator
  begin() const
  {
    return headers.begin();
  }
  const_iterator
  end() const
  {
    return headers.end();
  }

  url_components &
  url()
  {
    return components;
  }
  const url_components &
  url() const
  {
    return components;
  }

  url_components components;
  mutable /* XXX */ map_type headers;

  static key_value_block parse(protocol_version, zstream<decompress> &, const uint8_t *, size_t);
  static size_t marshall(protocol_version, zstream<compress> &, const key_value_block &, uint8_t *, size_t);
};


} // namespace spdy

template <typename T> std::string stringof(const T &);

template <> std::string stringof<spdy::control_frame_type>(const spdy::control_frame_type &);

template <> std::string stringof<spdy::error>(const spdy::error &);

#endif /* SPDY_H_57211D6A_F320_42E3_8205_89E651B4A5DB */
/* vim: set sw=4 ts=4 tw=79 et : */
