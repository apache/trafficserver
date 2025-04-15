/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "proxy/hdrs/HeaderValidator.h"
#include "proxy/hdrs/HTTP.h"

using namespace std::literals;

bool
HeaderValidator::is_h2_h3_header_valid(const HTTPHdr &hdr, bool is_response, bool is_trailing_header)
{
  const MIMEField *field = nullptr;
  MIMEFieldIter    iter;
  auto             method_field       = hdr.field_find(PSEUDO_HEADER_METHOD);
  bool             has_connect_method = false;
  if (method_field) {
    auto method{method_field->value_get()};
    has_connect_method =
      method == std::string_view{HTTP_METHOD_CONNECT, static_cast<std::string_view::size_type>(HTTP_LEN_CONNECT)};
  }
  unsigned int expected_pseudo_header_count = is_response ? 1 : has_connect_method ? 2 : 4;
  unsigned int pseudo_header_count          = 0;

  if (is_trailing_header) {
    expected_pseudo_header_count = 0;
  }
  for (auto &field : hdr) {
    auto name{field.name_get()};
    // Pseudo headers must appear before regular headers
    if (name.length() && name[0] == ':') {
      ++pseudo_header_count;
      if (pseudo_header_count > expected_pseudo_header_count) {
        return false;
      }
    } else if (name.length() == 0) {
      return false;
    } else {
      if (pseudo_header_count != expected_pseudo_header_count) {
        return false;
      }
    }
  }

  // rfc7540,sec8.1.2.2 and rfc9114,sec4.2: Any message containing
  // connection-specific header fields MUST be treated as malformed.
  if (hdr.field_find(MIME_FIELD_CONNECTION_sv) != nullptr || hdr.field_find(MIME_FIELD_KEEP_ALIVE_sv) != nullptr ||
      hdr.field_find(MIME_FIELD_PROXY_CONNECTION_sv) != nullptr || hdr.field_find(MIME_FIELD_UPGRADE_sv) != nullptr) {
    return false;
  }

  // :path pseudo header MUST NOT empty for http or https URIs
  field = hdr.field_find(PSEUDO_HEADER_PATH);
  if (field) {
    auto value{field->value_get()};
    if (value.length() == 0) {
      return false;
    }
  }

  // when The TE header field is received, it MUST NOT contain any
  // value other than "trailers".
  field = hdr.field_find(MIME_FIELD_TE_sv);
  if (field) {
    auto value{field->value_get()};
    if (value != "trailers"sv) {
      return false;
    }
  }

  if (is_trailing_header) {
    // Done with validation for trailing headers, which doesn't have any pseudo headers.
    return true;
  }
  // Check pseudo headers
  if (is_response) {
    if (hdr.fields_count() >= 1) {
      if (hdr.field_find(PSEUDO_HEADER_STATUS) == nullptr) {
        return false;
      }
    } else {
      // There should at least be :status pseudo header.
      return false;
    }
  } else {
    // This is a request.
    if (!has_connect_method && hdr.fields_count() >= 4) {
      if (hdr.field_find(PSEUDO_HEADER_SCHEME) == nullptr || hdr.field_find(PSEUDO_HEADER_METHOD) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_PATH) == nullptr || hdr.field_find(PSEUDO_HEADER_AUTHORITY) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_STATUS) != nullptr) {
        // Decoded header field is invalid
        return false;
      }
    } else if (has_connect_method && hdr.fields_count() >= 2) {
      if (hdr.field_find(PSEUDO_HEADER_SCHEME) != nullptr || hdr.field_find(PSEUDO_HEADER_METHOD) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_PATH) != nullptr || hdr.field_find(PSEUDO_HEADER_AUTHORITY) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_STATUS) != nullptr) {
        // Decoded header field is invalid
        return false;
      }

    } else {
      // Pseudo headers is insufficient
      return false;
    }
  }
  return true;
}
