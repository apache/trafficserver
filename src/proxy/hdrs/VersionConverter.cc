/** @file

  HTTP header version converter

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

#include <string_view>

using namespace std::literals;

#include "proxy/hdrs/VersionConverter.h"
#include "proxy/hdrs/HTTP.h"
#include "tsutil/LocalBuffer.h"

int
VersionConverter::convert(HTTPHdr &header, int from, int to) const
{
  int type = 0;

  switch (http_hdr_type_get(header.m_http)) {
  case HTTPType::REQUEST:
    type = 0;
    break;
  case HTTPType::RESPONSE:
    type = 1;
    break;
  case HTTPType::UNKNOWN:
    ink_abort("HTTPType::UNKNOWN");
    break;
  }

  ink_assert(MIN_VERSION <= from && from <= MAX_VERSION);
  ink_assert(MIN_VERSION <= to && to <= MAX_VERSION);

  ParseResult ret = (this->*_convert_functions[type][from - 1][to - 1])(header);
  if (static_cast<int>(ret) < 0) {
    return -1;
  }

  // Check validity of all names and values
  for (auto &&mf : header) {
    if (!mf.name_is_valid(is_control_BIT | is_ws_BIT) || !mf.value_is_valid()) {
      return -1;
    }
  }

  return 0;
}

ParseResult
VersionConverter::_convert_nop(HTTPHdr & /* header ATS_UNUSED */) const
{
  return ParseResult::DONE;
}

ParseResult
VersionConverter::_convert_req_from_1_to_2(HTTPHdr &header) const
{
  // :method
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_METHOD); field != nullptr) {
    auto value{header.method_get()};

    field->value_set(header.m_heap, header.m_mime, value);
  } else {
    ink_abort("initialize HTTP/2 pseudo-headers, no :method");
    return ParseResult::ERROR;
  }

  // :scheme
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_SCHEME); field != nullptr) {
    auto value{header.scheme_get()};

    if (!value.empty()) {
      field->value_set(header.m_heap, header.m_mime, value);
    } else {
      field->value_set(header.m_heap, header.m_mime, std::string_view{URL_SCHEME_HTTPS});
    }
  } else {
    ink_abort("initialize HTTP/2 pseudo-headers, no :scheme");
    return ParseResult::ERROR;
  }

  // :authority
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_AUTHORITY); field != nullptr) {
    auto value{header.host_get()};
    auto value_len{static_cast<int>(value.length())};

    if (header.is_port_in_header()) {
      int                   port = header.port_get();
      ts::LocalBuffer<char> buf(value_len + 8);
      char                 *host_and_port = buf.data();
      value_len                           = snprintf(host_and_port, value_len + 8, "%.*s:%d", value_len, value.data(), port);

      field->value_set(header.m_heap, header.m_mime,
                       std::string_view{host_and_port, static_cast<std::string_view::size_type>(value_len)});
    } else {
      field->value_set(header.m_heap, header.m_mime, value);
    }
    // Remove the host header field, redundant to the authority field
    // For istio/envoy, having both was causing 404 responses
    header.field_delete(static_cast<std::string_view>(MIME_FIELD_HOST));
  } else {
    ink_abort("initialize HTTP/2 pseudo-headers, no :authority");
    return ParseResult::ERROR;
  }

  // :path
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_PATH); field != nullptr) {
    auto value{header.path_get()};
    auto query{header.query_get()};
    int  path_len = static_cast<int>(value.length()) + 1;

    ts::LocalBuffer<char> buf(static_cast<int>(value.length()) + 1 + 1 + 1 + static_cast<int>(query.length()));
    char                 *path = buf.data();
    path[0]                    = '/';
    memcpy(path + 1, value.data(), static_cast<int>(value.length()));
    if (static_cast<int>(query.length()) > 0) {
      path[path_len] = '?';
      memcpy(path + path_len + 1, query.data(), static_cast<int>(query.length()));
      path_len += 1 + static_cast<int>(query.length());
    }
    field->value_set(header.m_heap, header.m_mime, std::string_view{path, static_cast<std::string_view::size_type>(path_len)});
  } else {
    ink_abort("initialize HTTP/2 pseudo-headers, no :path");
    return ParseResult::ERROR;
  }

  // TODO: remove host/Host header
  // [RFC 7540] 8.1.2.3. Clients that generate HTTP/2 requests directly SHOULD use the ":authority" pseudo-header field instead
  // of the Host header field.

  this->_remove_connection_specific_header_fields(header);

  return ParseResult::DONE;
}

ParseResult
VersionConverter::_convert_req_from_2_to_1(HTTPHdr &header) const
{
  bool is_connect_method = false;

  // HTTP Version
  header.version_set(HTTPVersion(1, 1));

  // :method
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_METHOD);
      field != nullptr && field->value_is_valid(is_control_BIT | is_ws_BIT)) {
    auto method{field->value_get()};
    if (method == static_cast<std::string_view>(HTTP_METHOD_CONNECT)) {
      is_connect_method = true;
    }

    header.method_set(method);
    header.field_delete(field);
  } else {
    return ParseResult::ERROR;
  }

  if (!is_connect_method) {
    // :scheme
    if (MIMEField *field = header.field_find(PSEUDO_HEADER_SCHEME);
        field != nullptr && field->value_is_valid(is_control_BIT | is_ws_BIT)) {
      auto        scheme{field->value_get()};
      const char *scheme_wks;

      int scheme_wks_idx = hdrtoken_tokenize(scheme.data(), scheme.length(), &scheme_wks);

      if (!(scheme_wks_idx > 0 && hdrtoken_wks_to_token_type(scheme_wks) == HdrTokenType::SCHEME)) {
        // unknown scheme, validate the scheme
        if (!validate_scheme(scheme)) {
          return ParseResult::ERROR;
        }
      }

      header.m_http->u.req.m_url_impl->set_scheme(header.m_heap, scheme, scheme_wks_idx, true);

      header.field_delete(field);
    } else {
      return ParseResult::ERROR;
    }
  }

  // :authority
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_AUTHORITY);
      field != nullptr && field->value_is_valid(is_control_BIT | is_ws_BIT)) {
    auto authority{field->value_get()};
    header.m_http->u.req.m_url_impl->set_host(header.m_heap, authority, true);

    if (!is_connect_method) {
      MIMEField *host = header.field_find(static_cast<std::string_view>(MIME_FIELD_HOST));
      if (host == nullptr) {
        // Add a Host header field. [RFC 7230] 5.4 says that if a client sends a
        // Host header field, it SHOULD be the first header in the header section
        // of a request. We accomplish that by simply renaming the :authority
        // header as Host.
        header.field_detach(field);
        field->name_set(header.m_heap, header.m_mime, static_cast<std::string_view>(MIME_FIELD_HOST));
        header.field_attach(field);
      } else {
        // There already is a Host header field. Simply set the value of the Host
        // field to the current value of :authority and delete the :authority
        // field.
        host->value_set(header.m_heap, header.m_mime, authority);
        header.field_delete(field);
      }
    }
  } else {
    return ParseResult::ERROR;
  }

  if (!is_connect_method) {
    // :path
    if (MIMEField *field = header.field_find(PSEUDO_HEADER_PATH);
        field != nullptr && field->value_is_valid(is_control_BIT | is_ws_BIT)) {
      auto path{field->value_get()};

      // cut first '/' if there, because `url_print()` add '/' before printing path
      if (path.starts_with("/"sv)) {
        path.remove_prefix(1);
      }

      header.m_http->u.req.m_url_impl->set_path(header.m_heap, path, true);

      header.field_delete(field);
    } else {
      return ParseResult::ERROR;
    }

    // Combine Cookie header.([RFC 7540] 8.1.2.5.)
    if (MIMEField *field = header.field_find(static_cast<std::string_view>(MIME_FIELD_COOKIE)); field != nullptr) {
      header.field_combine_dups(field, true, ';');
    }
  }

  return ParseResult::DONE;
}

ParseResult
VersionConverter::_convert_res_from_1_to_2(HTTPHdr &header) const
{
  constexpr int STATUS_VALUE_LEN = 3;

  // :status
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_STATUS); field != nullptr) {
    // ink_small_itoa() requires 5+ buffer length
    char status_str[STATUS_VALUE_LEN + 3];
    mime_format_int(status_str, static_cast<int32_t>(header.status_get()), sizeof(status_str));

    field->value_set(header.m_heap, header.m_mime,
                     std::string_view{status_str, static_cast<std::string_view::size_type>(STATUS_VALUE_LEN)});
  } else {
    ink_abort("initialize HTTP/2 pseudo-headers, no :status");
    return ParseResult::ERROR;
  }

  this->_remove_connection_specific_header_fields(header);

  return ParseResult::DONE;
}

ParseResult
VersionConverter::_convert_res_from_2_to_1(HTTPHdr &header) const
{
  // HTTP Version
  header.version_set(HTTPVersion(1, 1));

  // Set status from :status
  if (MIMEField *field = header.field_find(PSEUDO_HEADER_STATUS); field != nullptr) {
    auto status{field->value_get()};

    header.status_set(http_parse_status(status.data(), status.data() + status.length()));
    header.field_delete(field);
  } else {
    return ParseResult::ERROR;
  }

  return ParseResult::DONE;
}

void
VersionConverter::_remove_connection_specific_header_fields(HTTPHdr &header) const
{
  // Intermediaries SHOULD remove connection-specific header fields.
  for (auto &&h : connection_specific_header_fields) {
    if (MIMEField *field = header.field_find(h); field != nullptr) {
      header.field_delete(field);
    }
  }
}
