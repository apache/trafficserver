/** @file

  Unit tests for LogAccess.

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

#include <catch2/catch_test_macros.hpp>

#include "proxy/PreTransactionLogData.h"
#include "proxy/logging/LogAccess.h"
#include "tscore/ink_inet.h"

#include <string_view>
#include <vector>

using namespace std::literals;

extern int cmd_disable_pfreelist;

namespace
{
void
initialize_headers_once()
{
  static bool initialized = false;

  if (!initialized) {
    cmd_disable_pfreelist = true;
    url_init();
    mime_init();
    http_init();
    initialized = true;
  }
}

void
add_header_field(HTTPHdr &hdr, std::string_view name, std::string_view value)
{
  MIMEField *field = hdr.field_create(name);
  REQUIRE(field != nullptr);
  field->value_set(hdr.m_heap, hdr.m_mime, value);
  hdr.field_attach(field);
}

std::string
synthesize_target(std::string_view method, std::string_view scheme, std::string_view authority, std::string_view path)
{
  if (method == static_cast<std::string_view>(HTTP_METHOD_CONNECT)) {
    if (!authority.empty()) {
      return std::string(authority);
    }
    if (!path.empty()) {
      return std::string(path);
    }
    return {};
  }

  if (!scheme.empty() && !authority.empty()) {
    std::string url;
    url.reserve(scheme.size() + authority.size() + path.size() + 4);
    url.append(scheme);
    url.append("://");
    url.append(authority);
    if (!path.empty()) {
      url.append(path);
    } else {
      url.push_back('/');
    }
    return url;
  }

  if (!path.empty()) {
    return std::string(path);
  }

  return authority.empty() ? std::string{} : std::string(authority);
}

void
set_socket_address(IpEndpoint &ep, std::string_view text)
{
  REQUIRE(0 == ats_ip_pton(text, &ep.sa));
}

void
populate_pre_transaction_data(PreTransactionLogData &data, std::string_view method, std::string_view scheme,
                              std::string_view authority, std::string_view path)
{
  initialize_headers_once();

  auto *heap = new_HdrHeap(HdrHeap::DEFAULT_SIZE + 64);
  data.owned_client_request.create(HTTPType::REQUEST, HTTP_2_0, heap);
  data.owned_client_request.method_set(method);
  data.m_client_connection_is_ssl = true;
  data.m_log_code                 = SquidLogCode::ERR_INVALID_REQ;
  data.m_hit_miss_code            = SQUID_MISS_NONE;
  data.m_hier_code                = SquidHierarchyCode::NONE;
  data.m_server_transact_count    = 0;
  data.owned_client_protocol_str  = "http/2";
  data.owned_method.assign(method.data(), method.size());
  data.owned_scheme.assign(scheme.data(), scheme.size());
  data.owned_authority.assign(authority.data(), authority.size());
  data.owned_path.assign(path.data(), path.size());
  data.owned_url = synthesize_target(method, scheme, authority, path);
  set_socket_address(data.owned_client_addr, "192.0.2.10:4321"sv);
  ats_ip_copy(&data.owned_client_src_addr.sa, &data.owned_client_addr.sa);
  data.m_client_port = ats_ip_port_host_order(&data.owned_client_addr.sa);

  add_header_field(data.owned_client_request, PSEUDO_HEADER_METHOD, method);
  if (!scheme.empty()) {
    add_header_field(data.owned_client_request, PSEUDO_HEADER_SCHEME, scheme);
  }
  if (!authority.empty()) {
    add_header_field(data.owned_client_request, PSEUDO_HEADER_AUTHORITY, authority);
  }
  if (!path.empty()) {
    add_header_field(data.owned_client_request, PSEUDO_HEADER_PATH, path);
  }
  add_header_field(data.owned_client_request, static_cast<std::string_view>(MIME_FIELD_USER_AGENT), "TikTok/1.0");

  data.owned_milestones[TS_MILESTONE_SM_START]            = ink_hrtime_from_msec(10);
  data.owned_milestones[TS_MILESTONE_UA_BEGIN]            = ink_hrtime_from_msec(10);
  data.owned_milestones[TS_MILESTONE_UA_FIRST_READ]       = ink_hrtime_from_msec(12);
  data.owned_milestones[TS_MILESTONE_UA_READ_HEADER_DONE] = ink_hrtime_from_msec(14);
  data.owned_milestones[TS_MILESTONE_SM_FINISH]           = ink_hrtime_from_msec(15);
}

template <typename Marshal>
std::string
marshal_string(Marshal marshal)
{
  const int         len = marshal(nullptr);
  std::vector<char> buffer(len);
  marshal(buffer.data());
  return std::string(buffer.data());
}

template <typename Marshal>
int64_t
marshal_int_value(Marshal marshal)
{
  std::vector<char> buffer(INK_MIN_ALIGN * 2);
  marshal(buffer.data());
  char *ptr = buffer.data();
  return LogAccess::unmarshal_int(&ptr);
}
} // namespace

TEST_CASE("LogAccess pre-transaction CONNECT fields", "[LogAccess]")
{
  PreTransactionLogData data;
  populate_pre_transaction_data(data, "CONNECT", ""sv, "example.com:443", ""sv);
  LogAccess access(data);

  access.init();

  CHECK(marshal_string([&](char *buf) { return access.marshal_client_req_http_method(buf); }) == "CONNECT");
  CHECK(marshal_string([&](char *buf) { return access.marshal_client_req_protocol_version(buf); }) == "http/2");
  CHECK(marshal_string([&](char *buf) { return access.marshal_client_req_url(buf); }) == "example.com:443");
  CHECK(marshal_int_value([&](char *buf) { return access.marshal_cache_result_code(buf); }) ==
        static_cast<int64_t>(SquidLogCode::ERR_INVALID_REQ));
  CHECK(marshal_int_value([&](char *buf) { return access.marshal_server_transact_count(buf); }) == 0);

  char user_agent[] = "User-Agent";
  CHECK(marshal_string([&](char *buf) { return access.marshal_http_header_field(LogField::CQH, user_agent, buf); }) ==
        "TikTok/1.0");
}

TEST_CASE("LogAccess malformed CONNECT without authority falls back to path", "[LogAccess]")
{
  PreTransactionLogData data;
  populate_pre_transaction_data(data, "CONNECT", "https"sv, ""sv, "/"sv);
  LogAccess access(data);

  access.init();

  CHECK(marshal_string([&](char *buf) { return access.marshal_client_req_url(buf); }) == "/");
  CHECK(marshal_string([&](char *buf) { return access.marshal_client_req_url_canon(buf); }) == "/");
  CHECK(marshal_string([&](char *buf) { return access.marshal_client_req_url_path(buf); }) == "/");
  CHECK(marshal_string([&](char *buf) { return access.marshal_client_req_url_scheme(buf); }) == "https");
  CHECK(marshal_int_value([&](char *buf) { return access.marshal_transfer_time_ms(buf); }) == 5);
}

TEST_CASE("LogAccess pre-transaction client host port is null-safe", "[LogAccess]")
{
  PreTransactionLogData data;
  populate_pre_transaction_data(data, "GET", "https"sv, "example.com", "/client-port"sv);
  LogAccess access(data);

  access.init();

  CHECK(access.marshal_client_host_port(nullptr) == INK_MIN_ALIGN);
  CHECK(marshal_int_value([&](char *buf) { return access.marshal_client_host_port(buf); }) == 4321);
}
