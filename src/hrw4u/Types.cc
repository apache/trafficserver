/*
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

#include "hrw4u/Types.h"

#include <algorithm>
#include <unordered_map>
#include <cctype>

namespace hrw4u
{

std::string_view
section_type_to_string(SectionType type)
{
  switch (type) {
  case SectionType::UNKNOWN:
    return "UNKNOWN";
  case SectionType::READ_REQUEST:
    return "read_request";
  case SectionType::SEND_REQUEST:
    return "send_request";
  case SectionType::READ_RESPONSE:
    return "read_response";
  case SectionType::SEND_RESPONSE:
    return "send_response";
  case SectionType::PRE_REMAP:
    return "pre_remap";
  case SectionType::POST_REMAP:
    return "post_remap";
  case SectionType::REMAP:
    return "remap";
  case SectionType::TXN_START:
    return "txn_start";
  case SectionType::TXN_CLOSE:
    return "txn_close";
  }
  return "UNKNOWN";
}

SectionType
section_type_from_string(std::string_view name)
{
  static const std::unordered_map<std::string, SectionType> map = {
    {"read_request",  SectionType::READ_REQUEST },
    {"send_request",  SectionType::SEND_REQUEST },
    {"read_response", SectionType::READ_RESPONSE},
    {"send_response", SectionType::SEND_RESPONSE},
    {"pre_remap",     SectionType::PRE_REMAP    },
    {"post_remap",    SectionType::POST_REMAP   },
    {"remap",         SectionType::REMAP        },
    {"txn_start",     SectionType::TXN_START    },
    {"txn_close",     SectionType::TXN_CLOSE    },
  };

  auto it = map.find(to_lower(name));

  if (it != map.end()) {
    return it->second;
  }

  return SectionType::UNKNOWN;
}

const VarTypeInfo &
var_type_info(VarType type)
{
  static const VarTypeInfo info_table[] = {
    {"bool",  "FLAG",  "set-state-flag",  hrw::OperatorType::SET_STATE_FLAG,  16},
    {"int8",  "INT8",  "set-state-int8",  hrw::OperatorType::SET_STATE_INT8,  4 },
    {"int16", "INT16", "set-state-int16", hrw::OperatorType::SET_STATE_INT16, 1 },
  };

  return info_table[static_cast<int>(type)];
}

std::string_view
var_type_to_string(VarType type)
{
  return var_type_info(type).name;
}

std::optional<VarType>
var_type_from_string(std::string_view name)
{
  static const std::unordered_map<std::string, VarType> map = {
    {"bool",    VarType::BOOL },
    {"boolean", VarType::BOOL },
    {"int8",    VarType::INT8 },
    {"int16",   VarType::INT16},
  };

  auto it = map.find(to_lower(name));
  if (it != map.end()) {
    return it->second;
  }
  return std::nullopt;
}

namespace
{

  const std::vector<std::string_view> URL_FIELDS_VEC = {"host", "port", "path", "query", "scheme", "url"};

  const std::vector<std::string_view> HTTP_CNTL_FIELDS_VEC = {
    "logging", "intercept_retry", "resp_cacheable", "req_cacheable", "server_no_store", "txn_debug", "skip_remap"};

  const std::vector<std::string_view> CONN_FIELDS_VEC = {"dscp", "mark", "local-addr", "remote-addr", "local-port", "remote-port",
                                                         "tls",  "h2",   "ipv4",       "ipv6",        "ip-family",  "stack"};

  const std::vector<std::string_view> GEO_FIELDS_VEC = {"country", "country-iso", "asn", "asn-name"};

  const std::vector<std::string_view> ID_FIELDS_VEC = {"unique", "process", "request", "uuid"};

  const std::vector<std::string_view> DATE_FIELDS_VEC = {"year", "month", "day", "hour", "minute", "second", "weekday", "yearday"};

  const std::vector<std::string_view> CERT_FIELDS_VEC = {"subject",   "issuer",   "serial", "signature",
                                                         "notbefore", "notafter", "pem"};

  const std::vector<std::string_view> SAN_FIELDS_VEC = {"dns", "uri", "email", "ip"};

  const std::vector<std::string_view> BOOL_FIELDS_VEC = {"true", "false"};

  const std::vector<std::string_view> PLUGIN_CNTL_FIELDS_VEC = {
    "request-enable", "response-enable", "tls-tunnel", "websocket", "h2", "negative-reval", "non-reval", "req-read", "cntl"};

  const std::vector<std::string_view> EMPTY_VEC;

} // namespace

bool
validate_suffix(SuffixGroup group, std::string_view suffix)
{
  const auto &valid = get_valid_suffixes(group);

  if (valid.empty()) {
    return true;
  }

  return std::find(valid.begin(), valid.end(), to_lower(suffix)) != valid.end();
}

const std::vector<std::string_view> &
get_valid_suffixes(SuffixGroup group)
{
  switch (group) {
  case SuffixGroup::URL_FIELDS:
    return URL_FIELDS_VEC;
  case SuffixGroup::HTTP_CNTL_FIELDS:
    return HTTP_CNTL_FIELDS_VEC;
  case SuffixGroup::CONN_FIELDS:
    return CONN_FIELDS_VEC;
  case SuffixGroup::GEO_FIELDS:
    return GEO_FIELDS_VEC;
  case SuffixGroup::ID_FIELDS:
    return ID_FIELDS_VEC;
  case SuffixGroup::DATE_FIELDS:
    return DATE_FIELDS_VEC;
  case SuffixGroup::CERT_FIELDS:
    return CERT_FIELDS_VEC;
  case SuffixGroup::SAN_FIELDS:
    return SAN_FIELDS_VEC;
  case SuffixGroup::BOOL_FIELDS:
    return BOOL_FIELDS_VEC;
  case SuffixGroup::PLUGIN_CNTL_FIELDS:
    return PLUGIN_CNTL_FIELDS_VEC;
  }
  return EMPTY_VEC;
}

} // namespace hrw4u
