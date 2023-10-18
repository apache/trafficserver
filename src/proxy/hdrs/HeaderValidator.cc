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

#include "HeaderValidator.h"
#include "HTTP.h"

bool
HeaderValidator::is_h2_h3_header_valid(const HTTPHdr &hdr, bool is_response, bool is_trailing_header)
{
  const MIMEField *field = nullptr;
  const char *name       = nullptr;
  int name_len           = 0;
  const char *value      = nullptr;
  int value_len          = 0;
  MIMEFieldIter iter;
  auto method_field       = hdr.field_find(PSEUDO_HEADER_METHOD.data(), PSEUDO_HEADER_METHOD.size());
  bool has_connect_method = false;
  if (method_field) {
    int method_len;
    const char *method_value = method_field->value_get(&method_len);
    has_connect_method       = method_len == HTTP_LEN_CONNECT && strncmp(HTTP_METHOD_CONNECT, method_value, HTTP_LEN_CONNECT) == 0;
  }
  unsigned int expected_pseudo_header_count = is_response ? 1 : has_connect_method ? 2 : 4;
  unsigned int pseudo_header_count          = 0;

  if (is_trailing_header) {
    expected_pseudo_header_count = 0;
  }
  for (auto &field : hdr) {
    name = field.name_get(&name_len);
    // Pseudo headers must appear before regular headers
    if (name_len && name[0] == ':') {
      ++pseudo_header_count;
      if (pseudo_header_count > expected_pseudo_header_count) {
        return false;
      }
    } else if (name_len <= 0) {
      return false;
    } else {
      if (pseudo_header_count != expected_pseudo_header_count) {
        return false;
      }
    }
  }

  // rfc7540,sec8.1.2.2 and rfc9114,sec4.2: Any message containing
  // connection-specific header fields MUST be treated as malformed.
  if (hdr.field_find(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION) != nullptr ||
      hdr.field_find(MIME_FIELD_KEEP_ALIVE, MIME_LEN_KEEP_ALIVE) != nullptr ||
      hdr.field_find(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION) != nullptr ||
      hdr.field_find(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE) != nullptr) {
    return false;
  }

  // :path pseudo header MUST NOT empty for http or https URIs
  field = hdr.field_find(PSEUDO_HEADER_PATH.data(), PSEUDO_HEADER_PATH.size());
  if (field) {
    field->value_get(&value_len);
    if (value_len == 0) {
      return false;
    }
  }

  // when The TE header field is received, it MUST NOT contain any
  // value other than "trailers".
  field = hdr.field_find(MIME_FIELD_TE, MIME_LEN_TE);
  if (field) {
    value = field->value_get(&value_len);
    if (!(value_len == 8 && memcmp(value, "trailers", 8) == 0)) {
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
      if (hdr.field_find(PSEUDO_HEADER_STATUS.data(), PSEUDO_HEADER_STATUS.size()) == nullptr) {
        return false;
      }
    } else {
      // There should at least be :status pseudo header.
      return false;
    }
  } else {
    // This is a request.
    if (!has_connect_method && hdr.fields_count() >= 4) {
      if (hdr.field_find(PSEUDO_HEADER_SCHEME.data(), PSEUDO_HEADER_SCHEME.size()) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_METHOD.data(), PSEUDO_HEADER_METHOD.size()) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_PATH.data(), PSEUDO_HEADER_PATH.size()) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_AUTHORITY.data(), PSEUDO_HEADER_AUTHORITY.size()) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_STATUS.data(), PSEUDO_HEADER_STATUS.size()) != nullptr) {
        // Decoded header field is invalid
        return false;
      }
    } else if (has_connect_method && hdr.fields_count() >= 2) {
      if (hdr.field_find(PSEUDO_HEADER_SCHEME.data(), PSEUDO_HEADER_SCHEME.size()) != nullptr ||
          hdr.field_find(PSEUDO_HEADER_METHOD.data(), PSEUDO_HEADER_METHOD.size()) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_PATH.data(), PSEUDO_HEADER_PATH.size()) != nullptr ||
          hdr.field_find(PSEUDO_HEADER_AUTHORITY.data(), PSEUDO_HEADER_AUTHORITY.size()) == nullptr ||
          hdr.field_find(PSEUDO_HEADER_STATUS.data(), PSEUDO_HEADER_STATUS.size()) != nullptr) {
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
