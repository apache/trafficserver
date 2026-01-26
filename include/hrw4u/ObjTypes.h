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
#pragma once

#include <cstdint>
#include <string_view>

namespace hrw
{

enum class ConditionType : uint8_t {
  NONE = 0,
  COND_TRUE,
  COND_FALSE,
  COND_STATUS,
  COND_METHOD,
  COND_RANDOM,
  COND_ACCESS,
  COND_COOKIE,
  COND_HEADER,
  COND_CLIENT_HEADER,
  COND_CLIENT_URL,
  COND_URL,
  COND_FROM_URL,
  COND_TO_URL,
  COND_DBM,
  COND_INTERNAL_TXN,
  COND_IP,
  COND_TRANSACT_COUNT,
  COND_NOW,
  COND_GEO,
  COND_ID,
  COND_CIDR,
  COND_INBOUND,
  COND_SESSION_TRANSACT_COUNT,
  COND_TCP_INFO,
  COND_CACHE,
  COND_NEXT_HOP,
  COND_HTTP_CNTL,
  COND_GROUP,
  COND_STATE_FLAG,
  COND_STATE_INT8,
  COND_STATE_INT16,
  COND_LAST_CAPTURE,
};

enum class OperatorType : uint8_t {
  NONE = 0,
  RM_HEADER,
  SET_HEADER,
  ADD_HEADER,
  SET_CONFIG,
  SET_STATUS,
  SET_STATUS_REASON,
  SET_DESTINATION,
  RM_DESTINATION,
  SET_REDIRECT,
  TIMEOUT_OUT,
  SKIP_REMAP,
  NO_OP,
  COUNTER,
  RM_COOKIE,
  SET_COOKIE,
  ADD_COOKIE,
  SET_CONN_DSCP,
  SET_CONN_MARK,
  SET_DEBUG,
  SET_BODY,
  SET_HTTP_CNTL,
  SET_PLUGIN_CNTL,
  RUN_PLUGIN,
  SET_BODY_FROM,
  SET_STATE_FLAG,
  SET_STATE_INT8,
  SET_STATE_INT16,
  SET_EFFECTIVE_ADDRESS,
  SET_NEXT_HOP_STRATEGY,
  SET_CC_ALG,
  IF,
};

// Returns the canonical condition name (e.g., "STATUS", "METHOD", "IP")
constexpr std::string_view
condition_type_name(ConditionType type)
{
  switch (type) {
  case ConditionType::NONE:
    return "";
  case ConditionType::COND_TRUE:
    return "TRUE";
  case ConditionType::COND_FALSE:
    return "FALSE";
  case ConditionType::COND_STATUS:
    return "STATUS";
  case ConditionType::COND_METHOD:
    return "METHOD";
  case ConditionType::COND_RANDOM:
    return "RANDOM";
  case ConditionType::COND_ACCESS:
    return "ACCESS";
  case ConditionType::COND_COOKIE:
    return "COOKIE";
  case ConditionType::COND_HEADER:
    return "HEADER";
  case ConditionType::COND_CLIENT_HEADER:
    return "CLIENT-HEADER";
  case ConditionType::COND_CLIENT_URL:
    return "CLIENT-URL";
  case ConditionType::COND_URL:
    return "URL";
  case ConditionType::COND_FROM_URL:
    return "FROM-URL";
  case ConditionType::COND_TO_URL:
    return "TO-URL";
  case ConditionType::COND_DBM:
    return "DBM";
  case ConditionType::COND_INTERNAL_TXN:
    return "INTERNAL-TRANSACTION";
  case ConditionType::COND_IP:
    return "IP";
  case ConditionType::COND_TRANSACT_COUNT:
    return "TXN-COUNT";
  case ConditionType::COND_NOW:
    return "NOW";
  case ConditionType::COND_GEO:
    return "GEO";
  case ConditionType::COND_ID:
    return "ID";
  case ConditionType::COND_CIDR:
    return "CIDR";
  case ConditionType::COND_INBOUND:
    return "INBOUND";
  case ConditionType::COND_SESSION_TRANSACT_COUNT:
    return "SSN-TXN-COUNT";
  case ConditionType::COND_TCP_INFO:
    return "TCP-INFO";
  case ConditionType::COND_CACHE:
    return "CACHE";
  case ConditionType::COND_NEXT_HOP:
    return "NEXT-HOP";
  case ConditionType::COND_HTTP_CNTL:
    return "HTTP-CNTL";
  case ConditionType::COND_GROUP:
    return "GROUP";
  case ConditionType::COND_STATE_FLAG:
    return "STATE-FLAG";
  case ConditionType::COND_STATE_INT8:
    return "STATE-INT8";
  case ConditionType::COND_STATE_INT16:
    return "STATE-INT16";
  case ConditionType::COND_LAST_CAPTURE:
    return "LAST-CAPTURE";
  }
  return "";
}

// Returns the canonical operator name (e.g., "set-header", "rm-cookie")
constexpr std::string_view
operator_type_name(OperatorType type)
{
  switch (type) {
  case OperatorType::NONE:
    return "";
  case OperatorType::RM_HEADER:
    return "rm-header";
  case OperatorType::SET_HEADER:
    return "set-header";
  case OperatorType::ADD_HEADER:
    return "add-header";
  case OperatorType::SET_CONFIG:
    return "set-config";
  case OperatorType::SET_STATUS:
    return "set-status";
  case OperatorType::SET_STATUS_REASON:
    return "set-status-reason";
  case OperatorType::SET_DESTINATION:
    return "set-destination";
  case OperatorType::RM_DESTINATION:
    return "rm-destination";
  case OperatorType::SET_REDIRECT:
    return "set-redirect";
  case OperatorType::TIMEOUT_OUT:
    return "timeout-out";
  case OperatorType::SKIP_REMAP:
    return "skip-remap";
  case OperatorType::NO_OP:
    return "no-op";
  case OperatorType::COUNTER:
    return "counter";
  case OperatorType::RM_COOKIE:
    return "rm-cookie";
  case OperatorType::SET_COOKIE:
    return "set-cookie";
  case OperatorType::ADD_COOKIE:
    return "add-cookie";
  case OperatorType::SET_CONN_DSCP:
    return "set-conn-dscp";
  case OperatorType::SET_CONN_MARK:
    return "set-conn-mark";
  case OperatorType::SET_DEBUG:
    return "set-debug";
  case OperatorType::SET_BODY:
    return "set-body";
  case OperatorType::SET_HTTP_CNTL:
    return "set-http-cntl";
  case OperatorType::SET_PLUGIN_CNTL:
    return "set-plugin-cntl";
  case OperatorType::RUN_PLUGIN:
    return "run-plugin";
  case OperatorType::SET_BODY_FROM:
    return "set-body-from";
  case OperatorType::SET_STATE_FLAG:
    return "set-state-flag";
  case OperatorType::SET_STATE_INT8:
    return "set-state-int8";
  case OperatorType::SET_STATE_INT16:
    return "set-state-int16";
  case OperatorType::SET_EFFECTIVE_ADDRESS:
    return "set-effective-address";
  case OperatorType::SET_NEXT_HOP_STRATEGY:
    return "set-next-hop-strategy";
  case OperatorType::SET_CC_ALG:
    return "set-cc-alg";
  case OperatorType::IF:
    return "if";
  }
  return "";
}

} // namespace hrw
