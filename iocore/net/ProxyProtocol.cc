/** @file
 *
 *  PROXY protocol definitions and parsers.
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

#include "ProxyProtocol.h"

#include "I_EventSystem.h"
#include "I_NetVConnection.h"

#include "tscore/ink_assert.h"
#include "tscpp/util/TextView.h"

namespace
{
using namespace std::literals;

constexpr ts::TextView PPv1_CONNECTION_PREFACE = "PROXY"sv;
constexpr ts::TextView PPv2_CONNECTION_PREFACE = "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A\x02"sv;

constexpr size_t PPv1_CONNECTION_HEADER_LEN_MIN = 15;
constexpr size_t PPv2_CONNECTION_HEADER_LEN_MIN = 16;

/**
   PROXY Protocol v1 Parser

   @return read length
 */
size_t
proxy_protocol_v1_parse(ProxyProtocol *pp_info, ts::TextView hdr)
{
  //  Find the terminating newline
  ts::TextView::size_type pos = hdr.find('\n');
  if (pos == hdr.npos) {
    Debug("proxyprotocol_v1", "ssl_has_proxy_v1: newline not found");
    return 0;
  }

  ts::TextView token;
  in_port_t port;

  // All the cases are special and sequence, might as well unroll them.

  // The header should begin with the PROXY preface
  token = hdr.split_prefix_at(' ');
  if (0 == token.size() || token != PPv1_CONNECTION_PREFACE) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: header [%.*s] does not start with preface [%.*s]", static_cast<int>(hdr.size()),
          hdr.data(), static_cast<int>(PPv1_CONNECTION_PREFACE.size()), PPv1_CONNECTION_PREFACE.data());
    return 0;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = PREFACE", static_cast<int>(token.size()), token.data());

  // The INET protocol family - TCP4, TCP6 or UNKNOWN
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return 0;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = INET Family", static_cast<int>(token.size()), token.data());

  // Next up is the layer 3 source address
  // - 255.255.255.255 or ffff:f...f:ffff ffff:f...f:fff
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return 0;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Source Address", static_cast<int>(token.size()), token.data());
  if (0 != ats_ip_pton(token, &pp_info->src_addr)) {
    return 0;
  }

  // Next is the layer3 destination address
  // - 255.255.255.255 or ffff:f...f:ffff ffff:f...f:fff
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return 0;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Destination Address", static_cast<int>(token.size()), token.data());
  if (0 != ats_ip_pton(token, &pp_info->dst_addr)) {
    return 0;
  }

  // Next is the TCP source port represented as a decimal number in the range of [0..65535] inclusive.
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return 0;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Source Port", static_cast<int>(token.size()), token.data());

  if (0 == (port = ts::svtoi(token))) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: src port [%d] token [%.*s] failed to parse", port,
          static_cast<int>(token.size()), token.data());
    return 0;
  }
  pp_info->src_addr.port() = htons(port);

  // Next is the TCP destination port represented as a decimal number in the range of [0..65535] inclusive.
  // Final trailer is CR LF so split at CR.
  token = hdr.split_prefix_at('\r');
  if (0 == token.size()) {
    return 0;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Destination Port", static_cast<int>(token.size()), token.data());
  if (0 == (port = ts::svtoi(token))) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: dst port [%d] token [%.*s] failed to parse", port,
          static_cast<int>(token.size()), token.data());
    return 0;
  }
  pp_info->dst_addr.port() = htons(port);

  pp_info->version = ProxyProtocolVersion::V1;

  return pos + 1;
}

/**
   PROXY Protocol v2 Parser

   @return read length
 */
size_t
proxy_protocol_v2_parse(ProxyProtocol * /*pp_info*/, ts::TextView /*hdr*/)
{
  return 0;
}
} // namespace

/**
   PROXY Protocol Parser
 */
size_t
proxy_protocol_parse(ProxyProtocol *pp_info, ts::TextView tv)
{
  size_t len = 0;

  // Parse the TextView before moving the bytes in the buffer
  if (tv.size() >= PPv1_CONNECTION_HEADER_LEN_MIN && PPv1_CONNECTION_PREFACE.isPrefixOf(tv)) {
    // Client must send at least 15 bytes to get a reasonable match.
    len = proxy_protocol_v1_parse(pp_info, tv);
  } else if (tv.size() >= PPv2_CONNECTION_HEADER_LEN_MIN && PPv2_CONNECTION_PREFACE.isPrefixOf(tv)) {
    len = proxy_protocol_v2_parse(pp_info, tv);
  } else {
    // if we don't have the PROXY preface, we don't have a ProxyProtocol header
    // TODO: print hexdump of buffer safely
    Debug("proxyprotocol", "failed to find ProxyProtocol preface");
  }

  return len;
}
