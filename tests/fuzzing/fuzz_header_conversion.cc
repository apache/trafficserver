/** @file

  Fuzzing HTTP header validation and version conversion.

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

#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HeaderValidator.h"
#include "proxy/hdrs/VersionConverter.h"
#include "tscore/Diags.h"
#include "tsutil/PostScript.h"

#include <fuzzer/FuzzedDataProvider.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

extern int cmd_disable_pfreelist;

namespace
{
constexpr size_t                           MAX_INPUT_LENGTH = 64 * 1024;
constexpr std::array<std::string_view, 17> FIELD_NAMES      = {
  ":method",
  ":scheme",
  ":authority",
  ":path",
  ":status",
  "host",
  "cookie",
  "connection",
  "keep-alive",
  "proxy-connection",
  "upgrade",
  "transfer-encoding",
  "te",
  "content-length",
  "content-type",
  "x-test",
  "",
};

// Bit flags in the first input byte controlling the harness behavior.
constexpr uint8_t FLAG_IS_RESPONSE    = 1 << 0; // Decode as a response instead of a request.
constexpr uint8_t FLAG_IS_TEXT        = 1 << 1; // Consume the payload as HTTP/1.1 text.
constexpr uint8_t FLAG_HTTP_3         = 1 << 2; // Target HTTP/3 instead of HTTP/2.
constexpr uint8_t FLAG_PSEUDO_HEADERS = 1 << 3; // Add the required pseudo-headers.
constexpr uint8_t FLAG_CONNECT        = 1 << 4; // Use the CONNECT method (requests only).
constexpr uint8_t FLAG_TRAILER        = 1 << 5; // Treat the structured fields as trailers.

void
add_field(HTTPHdr &header, std::string_view name, std::string_view value)
{
  // An empty name allocates an unnamed field for validator rejection coverage.
  MIMEField *field = header.field_create(name);

  header.field_value_set(field, value);
  header.field_attach(field);
}

void
print_header(const HTTPHdr &header)
{
  std::array<char, 32> small_buffer{};
  int                  buffer_index = 0;
  int                  dump_offset  = 0;
  int                  length       = header.length_get();

  header.print(small_buffer.data(), small_buffer.size(), &buffer_index, &dump_offset);
  if (length >= 0 && length <= 2 * static_cast<int>(MAX_INPUT_LENGTH)) {
    std::vector<char> buffer(static_cast<size_t>(length) + 1);
    buffer_index = 0;
    dump_offset  = 0;
    header.print(buffer.data(), static_cast<int>(buffer.size()), &buffer_index, &dump_offset);
  }
}

void
convert_decoded_header(const HTTPHdr &header, int version, bool is_response, bool is_trailer)
{
  if (!HeaderValidator::is_h2_h3_header_valid(header, is_response, is_trailer)) {
    return;
  }

  // Trailers lack the request/status pseudo headers required by full-header conversion.
  if (is_trailer) {
    return;
  }

  HTTPHdr          converted;
  ts::PostScript   cleanup([&]() { converted.destroy(); });
  VersionConverter converter;

  converted.create(is_response ? HTTPType::RESPONSE : HTTPType::REQUEST);
  converted.copy(&header);
  if (converter.convert(converted, version, 1) == 0) {
    print_header(converted);
  }
}
} // namespace

extern "C" int
LLVMFuzzerInitialize(int *, char ***)
{
  cmd_disable_pfreelist = true;
  DiagsPtr::set(new Diags("fuzzing", "", "", nullptr));
  http_init();
  return 0;
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size == 0 || size > MAX_INPUT_LENGTH) {
    return 0;
  }

  uint8_t          flags       = data[0];
  bool             is_response = (flags & FLAG_IS_RESPONSE) != 0;
  bool             is_text     = (flags & FLAG_IS_TEXT) != 0;
  int              version     = (flags & FLAG_HTTP_3) != 0 ? 3 : 2;
  HTTPType         type        = is_response ? HTTPType::RESPONSE : HTTPType::REQUEST;
  HTTPHdr          header;
  ts::PostScript   cleanup([&]() { header.destroy(); });
  VersionConverter converter;

  if (is_text) {
    std::string    input(reinterpret_cast<const char *>(data + 1), size - 1);
    HTTPParser     parser;
    ts::PostScript parser_cleanup([&]() { http_parser_clear(&parser); });
    const char    *start = input.data();
    const char    *end   = start + input.size();

    // Reserve pseudo-header slots required by HTTP/1-to-HTTP/2 or HTTP/3 conversion.
    header.create(type, version == 2 ? HTTP_2_0 : HTTP_3_0);
    http_parser_init(&parser);
    ParseResult result = is_response ? header.parse_resp(&parser, &start, end, true) : header.parse_req(&parser, &start, end, true);
    if (result == ParseResult::DONE && converter.convert(header, 1, version) == 0) {
      convert_decoded_header(header, version, is_response, false);
    }
  } else {
    FuzzedDataProvider provider(data + 1, size - 1);

    header.create(type);
    if ((flags & FLAG_PSEUDO_HEADERS) != 0) {
      if (is_response) {
        add_field(header, ":status", "200");
      } else {
        bool is_connect = (flags & FLAG_CONNECT) != 0;

        add_field(header, ":method", is_connect ? "CONNECT" : "GET");
        if (!is_connect) {
          add_field(header, ":scheme", "https");
        }
        add_field(header, ":authority", "example.test:443");
        if (!is_connect) {
          add_field(header, ":path", "/index?q=1");
        }
      }
    }

    for (size_t count = 0; count < 32 && provider.remaining_bytes() > 0; ++count) {
      bool             use_known_name = provider.ConsumeBool();
      std::string      custom_name;
      std::string_view name;

      if (use_known_name) {
        name = FIELD_NAMES[provider.ConsumeIntegralInRange<size_t>(0, FIELD_NAMES.size() - 1)];
      } else {
        custom_name = provider.ConsumeRandomLengthString(64);
        name        = custom_name;
      }
      std::string value = provider.ConsumeRandomLengthString(1024);

      add_field(header, name, value);
    }

    convert_decoded_header(header, version, is_response, (flags & FLAG_TRAILER) != 0);
    // Same-version conversion still validates names and values, even after semantic rejection.
    converter.convert(header, version, version);
  }

  return 0;
}
