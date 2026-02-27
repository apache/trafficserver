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

#include "objtypes.h"
#include "conditions.h"
#include "conditions_geo.h"
#include "operators.h"

namespace hrw
{

Condition *
create_condition(const ConditionSpec &spec)
{
  Condition *cond = nullptr;

  switch (spec.type) {
  case ConditionType::NONE:
    return nullptr;

  case ConditionType::COND_TRUE:
    cond = new ConditionTrue();
    break;

  case ConditionType::COND_FALSE:
    cond = new ConditionFalse();
    break;

  case ConditionType::COND_STATUS:
    cond = new ConditionStatus();
    break;

  case ConditionType::COND_METHOD:
    cond = new ConditionMethod();
    break;

  case ConditionType::COND_RANDOM:
    cond = new ConditionRandom();
    break;

  case ConditionType::COND_ACCESS:
    cond = new ConditionAccess();
    break;

  case ConditionType::COND_COOKIE:
    cond = new ConditionCookie();
    break;

  case ConditionType::COND_HEADER:
    cond = new ConditionHeader();
    break;

  case ConditionType::COND_CLIENT_HEADER:
    cond = new ConditionHeader(true);
    break;

  case ConditionType::COND_CLIENT_URL:
    cond = new ConditionUrl(ConditionUrl::CLIENT);
    break;

  case ConditionType::COND_URL:
    cond = new ConditionUrl(ConditionUrl::URL);
    break;

  case ConditionType::COND_FROM_URL:
    cond = new ConditionUrl(ConditionUrl::FROM);
    break;

  case ConditionType::COND_TO_URL:
    cond = new ConditionUrl(ConditionUrl::TO);
    break;

  case ConditionType::COND_DBM:
    cond = new ConditionDBM();
    break;

  case ConditionType::COND_INTERNAL_TXN:
    cond = new ConditionInternalTxn();
    break;

  case ConditionType::COND_IP:
    cond = new ConditionIp();
    break;

  case ConditionType::COND_TRANSACT_COUNT:
    cond = new ConditionTransactCount();
    break;

  case ConditionType::COND_NOW:
    cond = new ConditionNow();
    break;

  case ConditionType::COND_GEO:
#if TS_USE_HRW_GEOIP
    cond = new GeoIPConditionGeo();
#elif TS_USE_HRW_MAXMINDDB
    cond = new MMConditionGeo();
#else
    cond = new ConditionGeo();
#endif
    break;

  case ConditionType::COND_ID:
    cond = new ConditionId();
    break;

  case ConditionType::COND_CIDR:
    cond = new ConditionCidr();
    break;

  case ConditionType::COND_INBOUND:
    cond = new ConditionInbound();
    break;

  case ConditionType::COND_SESSION_TRANSACT_COUNT:
    cond = new ConditionSessionTransactCount();
    break;

  case ConditionType::COND_TCP_INFO:
    cond = new ConditionTcpInfo();
    break;

  case ConditionType::COND_CACHE:
    cond = new ConditionCache();
    break;

  case ConditionType::COND_NEXT_HOP:
    cond = new ConditionNextHop();
    break;

  case ConditionType::COND_HTTP_CNTL:
    cond = new ConditionHttpCntl();
    break;

  case ConditionType::COND_GROUP:
    cond = new ConditionGroup();
    break;

  case ConditionType::COND_STATE_FLAG:
    cond = new ConditionStateFlag();
    break;

  case ConditionType::COND_STATE_INT8:
    cond = new ConditionStateInt8();
    break;

  case ConditionType::COND_STATE_INT16:
    cond = new ConditionStateInt16();
    break;

  case ConditionType::COND_LAST_CAPTURE:
    cond = new ConditionLastCapture();
    break;
  }

  if (cond) {
    cond->initialize(spec);
  }

  return cond;
}

Operator *
create_operator(const OperatorSpec &spec)
{
  Operator *op = nullptr;

  switch (spec.type) {
  case OperatorType::NONE:
    return nullptr;

  case OperatorType::RM_HEADER:
    op = new OperatorRMHeader();
    break;

  case OperatorType::SET_HEADER:
    op = new OperatorSetHeader();
    break;

  case OperatorType::ADD_HEADER:
    op = new OperatorAddHeader();
    break;

  case OperatorType::SET_CONFIG:
    op = new OperatorSetConfig();
    break;

  case OperatorType::SET_STATUS:
    op = new OperatorSetStatus();
    break;

  case OperatorType::SET_STATUS_REASON:
    op = new OperatorSetStatusReason();
    break;

  case OperatorType::SET_DESTINATION:
    op = new OperatorSetDestination();
    break;

  case OperatorType::RM_DESTINATION:
    op = new OperatorRMDestination();
    break;

  case OperatorType::SET_REDIRECT:
    op = new OperatorSetRedirect();
    break;

  case OperatorType::TIMEOUT_OUT:
    op = new OperatorSetTimeoutOut();
    break;

  case OperatorType::SKIP_REMAP:
    op = new OperatorSkipRemap();
    break;

  case OperatorType::NO_OP:
    op = new OperatorNoOp();
    break;

  case OperatorType::COUNTER:
    op = new OperatorCounter();
    break;

  case OperatorType::RM_COOKIE:
    op = new OperatorRMCookie();
    break;

  case OperatorType::SET_COOKIE:
    op = new OperatorSetCookie();
    break;

  case OperatorType::ADD_COOKIE:
    op = new OperatorAddCookie();
    break;

  case OperatorType::SET_CONN_DSCP:
    op = new OperatorSetConnDSCP();
    break;

  case OperatorType::SET_CONN_MARK:
    op = new OperatorSetConnMark();
    break;

  case OperatorType::SET_DEBUG:
    op = new OperatorSetDebug();
    break;

  case OperatorType::SET_BODY:
    op = new OperatorSetBody();
    break;

  case OperatorType::SET_HTTP_CNTL:
    op = new OperatorSetHttpCntl();
    break;

  case OperatorType::SET_PLUGIN_CNTL:
    op = new OperatorSetPluginCntl();
    break;

  case OperatorType::RUN_PLUGIN:
    op = new OperatorRunPlugin();
    break;

  case OperatorType::SET_BODY_FROM:
    op = new OperatorSetBodyFrom();
    break;

  case OperatorType::SET_STATE_FLAG:
    op = new OperatorSetStateFlag();
    break;

  case OperatorType::SET_STATE_INT8:
    op = new OperatorSetStateInt8();
    break;

  case OperatorType::SET_STATE_INT16:
    op = new OperatorSetStateInt16();
    break;

  case OperatorType::SET_EFFECTIVE_ADDRESS:
    op = new OperatorSetEffectiveAddress();
    break;

  case OperatorType::SET_NEXT_HOP_STRATEGY:
    op = new OperatorSetNextHopStrategy();
    break;

  case OperatorType::SET_CC_ALG:
    op = new OperatorSetCCAlgorithm();
    break;

  case OperatorType::IF:
    op = new OperatorIf();
    break;
  }

  if (op) {
    op->initialize(spec);
  }

  return op;
}

ConditionSpec
parse_condition_string(const std::string &cond_str, const std::string &arg)
{
  ConditionSpec          spec;
  std::string            c_name;
  std::string            c_qual;
  std::string::size_type pos = cond_str.find(':');

  if (pos != std::string::npos) {
    c_name = cond_str.substr(0, pos);
    c_qual = cond_str.substr(pos + 1);
  } else {
    c_name = cond_str;
  }

  if (c_name == "TRUE") {
    spec.type = ConditionType::COND_TRUE;
  } else if (c_name == "FALSE") {
    spec.type = ConditionType::COND_FALSE;
  } else if (c_name == "STATUS") {
    spec.type = ConditionType::COND_STATUS;
  } else if (c_name == "METHOD") {
    spec.type = ConditionType::COND_METHOD;
  } else if (c_name == "RANDOM") {
    spec.type = ConditionType::COND_RANDOM;
  } else if (c_name == "ACCESS") {
    spec.type = ConditionType::COND_ACCESS;
  } else if (c_name == "COOKIE") {
    spec.type = ConditionType::COND_COOKIE;
  } else if (c_name == "HEADER") {
    spec.type = ConditionType::COND_HEADER;
  } else if (c_name == "CLIENT-HEADER") {
    spec.type = ConditionType::COND_CLIENT_HEADER;
  } else if (c_name == "CLIENT-URL") {
    spec.type = ConditionType::COND_CLIENT_URL;
  } else if (c_name == "URL") {
    spec.type = ConditionType::COND_URL;
  } else if (c_name == "FROM-URL") {
    spec.type = ConditionType::COND_FROM_URL;
  } else if (c_name == "TO-URL") {
    spec.type = ConditionType::COND_TO_URL;
  } else if (c_name == "DBM") {
    spec.type = ConditionType::COND_DBM;
  } else if (c_name == "INTERNAL-TRANSACTION" || c_name == "INTERNAL-TXN") {
    spec.type = ConditionType::COND_INTERNAL_TXN;
  } else if (c_name == "IP") {
    spec.type = ConditionType::COND_IP;
  } else if (c_name == "TXN-COUNT") {
    spec.type = ConditionType::COND_TRANSACT_COUNT;
  } else if (c_name == "NOW") {
    spec.type = ConditionType::COND_NOW;
  } else if (c_name == "GEO") {
    spec.type = ConditionType::COND_GEO;
  } else if (c_name == "ID") {
    spec.type = ConditionType::COND_ID;
  } else if (c_name == "CIDR") {
    spec.type = ConditionType::COND_CIDR;
  } else if (c_name == "INBOUND") {
    spec.type = ConditionType::COND_INBOUND;
  } else if (c_name == "SSN-TXN-COUNT") {
    spec.type = ConditionType::COND_SESSION_TRANSACT_COUNT;
  } else if (c_name == "TCP-INFO") {
    spec.type = ConditionType::COND_TCP_INFO;
  } else if (c_name == "CACHE") {
    spec.type = ConditionType::COND_CACHE;
  } else if (c_name == "NEXT-HOP") {
    spec.type = ConditionType::COND_NEXT_HOP;
  } else if (c_name == "HTTP-CNTL") {
    spec.type = ConditionType::COND_HTTP_CNTL;
  } else if (c_name == "GROUP") {
    spec.type = ConditionType::COND_GROUP;
  } else if (c_name == "STATE-FLAG") {
    spec.type = ConditionType::COND_STATE_FLAG;
  } else if (c_name == "STATE-INT8") {
    spec.type = ConditionType::COND_STATE_INT8;
  } else if (c_name == "STATE-INT16") {
    spec.type = ConditionType::COND_STATE_INT16;
  } else if (c_name == "LAST-CAPTURE") {
    spec.type = ConditionType::COND_LAST_CAPTURE;
  }

  spec.qualifier = c_qual;
  spec.match_arg = arg;

  return spec;
}

OperatorSpec
parse_operator_string(const std::string &op_str, const std::string &arg, const std::string &val)
{
  OperatorSpec spec;

  if (op_str == "rm-header") {
    spec.type = OperatorType::RM_HEADER;
  } else if (op_str == "set-header") {
    spec.type = OperatorType::SET_HEADER;
  } else if (op_str == "add-header") {
    spec.type = OperatorType::ADD_HEADER;
  } else if (op_str == "set-config") {
    spec.type = OperatorType::SET_CONFIG;
  } else if (op_str == "set-status") {
    spec.type = OperatorType::SET_STATUS;
  } else if (op_str == "set-status-reason") {
    spec.type = OperatorType::SET_STATUS_REASON;
  } else if (op_str == "set-destination") {
    spec.type = OperatorType::SET_DESTINATION;
  } else if (op_str == "rm-destination") {
    spec.type = OperatorType::RM_DESTINATION;
  } else if (op_str == "set-redirect") {
    spec.type = OperatorType::SET_REDIRECT;
  } else if (op_str == "timeout-out") {
    spec.type = OperatorType::TIMEOUT_OUT;
  } else if (op_str == "skip-remap") {
    spec.type = OperatorType::SKIP_REMAP;
  } else if (op_str == "no-op") {
    spec.type = OperatorType::NO_OP;
  } else if (op_str == "counter") {
    spec.type = OperatorType::COUNTER;
  } else if (op_str == "rm-cookie") {
    spec.type = OperatorType::RM_COOKIE;
  } else if (op_str == "set-cookie") {
    spec.type = OperatorType::SET_COOKIE;
  } else if (op_str == "add-cookie") {
    spec.type = OperatorType::ADD_COOKIE;
  } else if (op_str == "set-conn-dscp") {
    spec.type = OperatorType::SET_CONN_DSCP;
  } else if (op_str == "set-conn-mark") {
    spec.type = OperatorType::SET_CONN_MARK;
  } else if (op_str == "set-debug") {
    spec.type = OperatorType::SET_DEBUG;
  } else if (op_str == "set-body") {
    spec.type = OperatorType::SET_BODY;
  } else if (op_str == "set-http-cntl") {
    spec.type = OperatorType::SET_HTTP_CNTL;
  } else if (op_str == "set-plugin-cntl") {
    spec.type = OperatorType::SET_PLUGIN_CNTL;
  } else if (op_str == "run-plugin") {
    spec.type = OperatorType::RUN_PLUGIN;
  } else if (op_str == "set-body-from") {
    spec.type = OperatorType::SET_BODY_FROM;
  } else if (op_str == "set-state-flag") {
    spec.type = OperatorType::SET_STATE_FLAG;
  } else if (op_str == "set-state-int8") {
    spec.type = OperatorType::SET_STATE_INT8;
  } else if (op_str == "set-state-int16") {
    spec.type = OperatorType::SET_STATE_INT16;
  } else if (op_str == "set-effective-address") {
    spec.type = OperatorType::SET_EFFECTIVE_ADDRESS;
  } else if (op_str == "set-next-hop-strategy") {
    spec.type = OperatorType::SET_NEXT_HOP_STRATEGY;
  } else if (op_str == "set-cc-alg") {
    spec.type = OperatorType::SET_CC_ALG;
  }

  spec.arg   = arg;
  spec.value = val;

  return spec;
}

} // namespace hrw
