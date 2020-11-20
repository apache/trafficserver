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
constexpr ts::TextView PPv2_CONNECTION_PREFACE = "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A"sv;

constexpr size_t PPv1_CONNECTION_HEADER_LEN_MIN = 15;

constexpr ts::TextView PPv1_PROTO_UNKNOWN = "UNKNOWN"sv;
constexpr ts::TextView PPv1_PROTO_TCP4    = "TCP4"sv;
constexpr ts::TextView PPv1_PROTO_TCP6    = "TCP6"sv;

constexpr uint8_t PPv2_CMD_LOCAL = 0x20;
constexpr uint8_t PPv2_CMD_PROXY = 0x21;

constexpr uint8_t PPv2_PROTO_UNSPEC        = 0x00;
constexpr uint8_t PPv2_PROTO_TCP4          = 0x11;
constexpr uint8_t PPv2_PROTO_UDP4          = 0x12;
constexpr uint8_t PPv2_PROTO_TCP6          = 0x21;
constexpr uint8_t PPv2_PROTO_UDP6          = 0x22;
constexpr uint8_t PPv2_PROTO_UNIX_STREAM   = 0x31;
constexpr uint8_t PPv2_PROTO_UNIX_DATAGRAM = 0x32;

constexpr uint16_t PPv2_ADDR_LEN_INET  = 4 + 4 + 2 + 2;
constexpr uint16_t PPv2_ADDR_LEN_INET6 = 16 + 16 + 2 + 2;
// constexpr uint16_t PPv2_ADDR_LEN_UNIX  = 108 + 108;

struct PPv2Hdr {
  uint8_t sig[12]; ///< preface
  uint8_t ver_cmd; ///< protocol version and command
  uint8_t fam;     ///< protocol family and transport
  uint16_t len;    ///< number of following bytes part of the header
  union {
    // for TCP/UDP over IPv4, len = 12 (PPv2_ADDR_LEN_INET)
    struct {
      uint32_t src_addr;
      uint32_t dst_addr;
      uint16_t src_port;
      uint16_t dst_port;
    } ip4;
    // for TCP/UDP over IPv6, len = 36 (PPv2_ADDR_LEN_INET6)
    struct {
      uint8_t src_addr[16];
      uint8_t dst_addr[16];
      uint16_t src_port;
      uint16_t dst_port;
    } ip6;
    // for AF_UNIX sockets, len = 216 (PPv2_ADDR_LEN_UNIX)
    struct {
      uint8_t src_addr[108];
      uint8_t dst_addr[108];
    } unix;
  } addr;
};

/**
   PROXY Protocol v1 Parser

   @return read length
 */
size_t
proxy_protocol_v1_parse(ProxyProtocol *pp_info, ts::TextView hdr)
{
  ink_release_assert(hdr.size() >= PPv1_CONNECTION_HEADER_LEN_MIN);

  // Find the terminating newline
  ts::TextView::size_type pos = hdr.find('\n');
  if (pos == hdr.npos) {
    Debug("proxyprotocol_v1", "ssl_has_proxy_v1: LF not found");
    return 0;
  }

  if (hdr[pos - 1] != '\r') {
    Debug("proxyprotocol_v1", "ssl_has_proxy_v1: CR not found");
    return 0;
  }

  ts::TextView token;

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
  if (PPv1_PROTO_UNKNOWN.isPrefixOf(hdr)) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: [UNKNOWN] = INET Family");

    // Ignore anything presented before the CRLF
    pp_info->version = ProxyProtocolVersion::V1;

    return pos + 1;
  } else if (PPv1_PROTO_TCP4.isPrefixOf(hdr)) {
    token = hdr.split_prefix_at(' ');
    if (0 == token.size()) {
      return 0;
    }

    pp_info->ip_family = AF_INET;
  } else if (PPv1_PROTO_TCP6.isPrefixOf(hdr)) {
    token = hdr.split_prefix_at(' ');
    if (0 == token.size()) {
      return 0;
    }

    pp_info->ip_family = AF_INET6;
  } else {
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

  in_port_t src_port = ts::svtoi(token);
  if (src_port == 0) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: src port [%d] token [%.*s] failed to parse", src_port,
          static_cast<int>(token.size()), token.data());
    return 0;
  }
  pp_info->src_addr.port() = htons(src_port);

  // Next is the TCP destination port represented as a decimal number in the range of [0..65535] inclusive.
  // Final trailer is CR LF so split at CR.
  token = hdr.split_prefix_at('\r');
  if (0 == token.size() || token.find(0x20) != token.npos) {
    return 0;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Destination Port", static_cast<int>(token.size()), token.data());

  in_port_t dst_port = ts::svtoi(token);
  if (dst_port == 0) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: dst port [%d] token [%.*s] failed to parse", dst_port,
          static_cast<int>(token.size()), token.data());
    return 0;
  }
  pp_info->dst_addr.port() = htons(dst_port);

  pp_info->version = ProxyProtocolVersion::V1;

  return pos + 1;
}

/**
   PROXY Protocol v2 Parser

   TODO: TLVs Support

   @return read length
 */
size_t
proxy_protocol_v2_parse(ProxyProtocol *pp_info, const ts::TextView &msg)
{
  ink_release_assert(msg.size() >= PPv2_CONNECTION_HEADER_LEN);

  const PPv2Hdr *hdr_v2 = reinterpret_cast<const PPv2Hdr *>(msg.data());

  // Assuming PREFACE check is done

  // length check
  const uint16_t len     = ntohs(hdr_v2->len);
  const size_t total_len = PPv2_CONNECTION_HEADER_LEN + len;

  if (msg.size() < total_len) {
    return 0;
  }

  // protocol version and command
  switch (hdr_v2->ver_cmd) {
  case PPv2_CMD_LOCAL: {
    // protocol byte should be UNSPEC (\x00) with LOCAL command
    if (hdr_v2->fam != PPv2_PROTO_UNSPEC) {
      return 0;
    }

    pp_info->version   = ProxyProtocolVersion::V2;
    pp_info->ip_family = AF_UNSPEC;

    return total_len;
  }
  case PPv2_CMD_PROXY: {
    switch (hdr_v2->fam) {
    case PPv2_PROTO_TCP4: {
      if (len < PPv2_ADDR_LEN_INET) {
        return 0;
      }

      IpAddr src_addr(reinterpret_cast<in_addr_t>(hdr_v2->addr.ip4.src_addr));
      pp_info->src_addr.assign(src_addr, hdr_v2->addr.ip4.src_port);

      IpAddr dst_addr(reinterpret_cast<in_addr_t>(hdr_v2->addr.ip4.dst_addr));
      pp_info->dst_addr.assign(dst_addr, hdr_v2->addr.ip4.dst_port);

      pp_info->version   = ProxyProtocolVersion::V2;
      pp_info->ip_family = AF_INET;

      break;
    }
    case PPv2_PROTO_TCP6: {
      if (len < PPv2_ADDR_LEN_INET6) {
        return 0;
      }

      IpAddr src_addr(reinterpret_cast<in6_addr const &>(hdr_v2->addr.ip6.src_addr));
      pp_info->src_addr.assign(src_addr, hdr_v2->addr.ip6.src_port);

      IpAddr dst_addr(reinterpret_cast<in6_addr const &>(hdr_v2->addr.ip6.dst_addr));
      pp_info->dst_addr.assign(dst_addr, hdr_v2->addr.ip6.dst_port);

      pp_info->version   = ProxyProtocolVersion::V2;
      pp_info->ip_family = AF_INET6;

      break;
    }
    case PPv2_PROTO_UDP4:
      [[fallthrough]];
    case PPv2_PROTO_UDP6:
      [[fallthrough]];
    case PPv2_PROTO_UNIX_STREAM:
      [[fallthrough]];
    case PPv2_PROTO_UNIX_DATAGRAM:
      [[fallthrough]];
    case PPv2_PROTO_UNSPEC:
      [[fallthrough]];
    default:
      // unsupported
      return 0;
    }

    // TODO: Parse TLVs

    return total_len;
  }
  default:
    break;
  }

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
  } else if (tv.size() >= PPv2_CONNECTION_HEADER_LEN && PPv2_CONNECTION_PREFACE.isPrefixOf(tv)) {
    len = proxy_protocol_v2_parse(pp_info, tv);
  } else {
    // if we don't have the PROXY preface, we don't have a ProxyProtocol header
    // TODO: print hexdump of buffer safely
    Debug("proxyprotocol", "failed to find ProxyProtocol preface");
  }

  return len;
}
