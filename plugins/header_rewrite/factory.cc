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
//////////////////////////////////////////////////////////////////////////////////////////////
// factory.cc: Factory functions for operators, conditions and condition variables.
//
//
#include <string>

#include "operators.h"
#include "conditions.h"
#include "conditions_geo.h"
#include "hrw4u/ObjTypes.h"

///////////////////////////////////////////////////////////////////////////////
// "Factory" functions, processing the parsed lines
//
Operator *
operator_factory(hrw::OperatorType op_type)
{
  switch (op_type) {
  case hrw::OperatorType::RM_HEADER:
    return new OperatorRMHeader();
  case hrw::OperatorType::SET_HEADER:
    return new OperatorSetHeader();
  case hrw::OperatorType::ADD_HEADER:
    return new OperatorAddHeader();
  case hrw::OperatorType::SET_CONFIG:
    return new OperatorSetConfig();
  case hrw::OperatorType::SET_STATUS:
    return new OperatorSetStatus();
  case hrw::OperatorType::SET_STATUS_REASON:
    return new OperatorSetStatusReason();
  case hrw::OperatorType::SET_DESTINATION:
    return new OperatorSetDestination();
  case hrw::OperatorType::RM_DESTINATION:
    return new OperatorRMDestination();
  case hrw::OperatorType::SET_REDIRECT:
    return new OperatorSetRedirect();
  case hrw::OperatorType::TIMEOUT_OUT:
    return new OperatorSetTimeoutOut();
  case hrw::OperatorType::SKIP_REMAP:
    return new OperatorSkipRemap();
  case hrw::OperatorType::NO_OP:
    return new OperatorNoOp();
  case hrw::OperatorType::COUNTER:
    return new OperatorCounter();
  case hrw::OperatorType::RM_COOKIE:
    return new OperatorRMCookie();
  case hrw::OperatorType::SET_COOKIE:
    return new OperatorSetCookie();
  case hrw::OperatorType::ADD_COOKIE:
    return new OperatorAddCookie();
  case hrw::OperatorType::SET_CONN_DSCP:
    return new OperatorSetConnDSCP();
  case hrw::OperatorType::SET_CONN_MARK:
    return new OperatorSetConnMark();
  case hrw::OperatorType::SET_DEBUG:
    return new OperatorSetDebug();
  case hrw::OperatorType::SET_BODY:
    return new OperatorSetBody();
  case hrw::OperatorType::SET_HTTP_CNTL:
    return new OperatorSetHttpCntl();
  case hrw::OperatorType::SET_PLUGIN_CNTL:
    return new OperatorSetPluginCntl();
  case hrw::OperatorType::RUN_PLUGIN:
    return new OperatorRunPlugin();
  case hrw::OperatorType::SET_BODY_FROM:
    return new OperatorSetBodyFrom();
  case hrw::OperatorType::SET_STATE_FLAG:
    return new OperatorSetStateFlag();
  case hrw::OperatorType::SET_STATE_INT8:
    return new OperatorSetStateInt8();
  case hrw::OperatorType::SET_STATE_INT16:
    return new OperatorSetStateInt16();
  case hrw::OperatorType::SET_EFFECTIVE_ADDRESS:
    return new OperatorSetEffectiveAddress();
  case hrw::OperatorType::SET_NEXT_HOP_STRATEGY:
    return new OperatorSetNextHopStrategy();
  case hrw::OperatorType::SET_CC_ALG:
    return new OperatorSetCCAlgorithm();
  case hrw::OperatorType::IF:
  case hrw::OperatorType::NONE:
  default:
    TSError("[%s] Invalid operator type: %d", PLUGIN_NAME, static_cast<int>(op_type));
    return nullptr;
  }
}

Operator *
operator_factory(const std::string &op)
{
  Operator *o = nullptr;

  if (op == "rm-header") {
    o = new OperatorRMHeader();
  } else if (op == "set-header") {
    o = new OperatorSetHeader();
  } else if (op == "add-header") {
    o = new OperatorAddHeader();
  } else if (op == "set-config") {
    o = new OperatorSetConfig();
  } else if (op == "set-status") {
    o = new OperatorSetStatus();
  } else if (op == "set-status-reason") {
    o = new OperatorSetStatusReason();
  } else if (op == "set-destination") {
    o = new OperatorSetDestination();
  } else if (op == "rm-destination") {
    o = new OperatorRMDestination();
  } else if (op == "set-redirect") {
    o = new OperatorSetRedirect();
  } else if (op == "timeout-out") {
    o = new OperatorSetTimeoutOut();
  } else if (op == "skip-remap") {
    o = new OperatorSkipRemap();
  } else if (op == "no-op") {
    o = new OperatorNoOp();
  } else if (op == "counter") {
    o = new OperatorCounter();
  } else if (op == "rm-cookie") {
    o = new OperatorRMCookie();
  } else if (op == "set-cookie") {
    o = new OperatorSetCookie();
  } else if (op == "add-cookie") {
    o = new OperatorAddCookie();
  } else if (op == "set-conn-dscp") {
    o = new OperatorSetConnDSCP();
  } else if (op == "set-conn-mark") {
    o = new OperatorSetConnMark();
  } else if (op == "set-debug") {
    o = new OperatorSetDebug();
  } else if (op == "set-body") {
    o = new OperatorSetBody();
  } else if (op == "set-http-cntl") {
    o = new OperatorSetHttpCntl();
  } else if (op == "set-plugin-cntl") {
    o = new OperatorSetPluginCntl();
  } else if (op == "run-plugin") {
    o = new OperatorRunPlugin();
  } else if (op == "set-body-from") {
    o = new OperatorSetBodyFrom();
  } else if (op == "set-state-flag") {
    o = new OperatorSetStateFlag();
  } else if (op == "set-state-int8") {
    o = new OperatorSetStateInt8();
  } else if (op == "set-state-int16") {
    o = new OperatorSetStateInt16();
  } else if (op == "set-effective-address") {
    o = new OperatorSetEffectiveAddress();
  } else if (op == "set-next-hop-strategy") {
    o = new OperatorSetNextHopStrategy();
  } else if (op == "set-cc-alg") {
    o = new OperatorSetCCAlgorithm();
  } else {
    // Note that we don't support the OperatorIf() pseudo-operator here!
    TSError("[%s] Unknown operator: %s", PLUGIN_NAME, op.c_str());
    return nullptr;
  }

  return o;
}

Condition *
condition_factory(const std::string &cond)
{
  Condition             *c = nullptr;
  std::string            c_name, c_qual;
  std::string::size_type pos = cond.find_first_of(':');

  if (pos != std::string::npos) {
    c_name = cond.substr(0, pos);
    c_qual = cond.substr(pos + 1);
  } else {
    c_name = cond;
    c_qual = "";
  }

  if (c_name == "TRUE") {
    c = new ConditionTrue();
  } else if (c_name == "FALSE") {
    c = new ConditionFalse();
  } else if (c_name == "STATUS") {
    c = new ConditionStatus();
  } else if (c_name == "RANDOM") {
    c = new ConditionRandom();
  } else if (c_name == "ACCESS") {
    c = new ConditionAccess();
  } else if (c_name == "COOKIE") {
    c = new ConditionCookie();
  } else if (c_name == "HEADER") { // This condition adapts to the hook
    c = new ConditionHeader();
  } else if (c_name == "CLIENT-HEADER") {
    c = new ConditionHeader(true);
  } else if (c_name == "CLIENT-URL") { // This condition adapts to the hook
    c = new ConditionUrl(ConditionUrl::CLIENT);
  } else if (c_name == "URL") {
    c = new ConditionUrl(ConditionUrl::URL);
  } else if (c_name == "FROM-URL") {
    c = new ConditionUrl(ConditionUrl::FROM);
  } else if (c_name == "TO-URL") {
    c = new ConditionUrl(ConditionUrl::TO);
  } else if (c_name == "DBM") {
    c = new ConditionDBM();
  } else if (c_name == "INTERNAL-TRANSACTION") {
    c = new ConditionInternalTxn();
  } else if (c_name == "INTERNAL-TXN") {
    c = new ConditionInternalTxn();
  } else if (c_name == "IP") {
    c = new ConditionIp();
  } else if (c_name == "METHOD") {
    c = new ConditionMethod();
  } else if (c_name == "TXN-COUNT") {
    c = new ConditionTransactCount();
  } else if (c_name == "NOW") {
    c = new ConditionNow();
  } else if (c_name == "GEO") {
#if TS_USE_HRW_GEOIP
    c = new GeoIPConditionGeo();
#elif TS_USE_HRW_MAXMINDDB
    c = new MMConditionGeo();
#else
    c = new ConditionGeo();
#endif
  } else if (c_name == "ID") {
    c = new ConditionId();
  } else if (c_name == "CIDR") {
    c = new ConditionCidr();
  } else if (c_name == "INBOUND") {
    c = new ConditionInbound();
  } else if (c_name == "SSN-TXN-COUNT") {
    c = new ConditionSessionTransactCount();
  } else if (c_name == "TCP-INFO") {
    c = new ConditionTcpInfo();
  } else if (c_name == "CACHE") {
    c = new ConditionCache();
  } else if (c_name == "NEXT-HOP") { // This condition adapts to the hook
    c = new ConditionNextHop();
  } else if (c_name == "HTTP-CNTL") {
    c = new ConditionHttpCntl();
  } else if (c_name == "GROUP") {
    c = new ConditionGroup();
  } else if (c_name == "STATE-FLAG") {
    c = new ConditionStateFlag();
  } else if (c_name == "STATE-INT8") {
    c = new ConditionStateInt8();
  } else if (c_name == "STATE-INT16") {
    c = new ConditionStateInt16();
  } else if (c_name == "LAST-CAPTURE") {
    c = new ConditionLastCapture();
  } else {
    TSError("[%s] Unknown condition %s", PLUGIN_NAME, c_name.c_str());
    return nullptr;
  }

  if (c_qual != "") {
    c->set_qualifier(c_qual);
  }

  return c;
}
