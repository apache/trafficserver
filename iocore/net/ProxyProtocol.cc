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

#include "tscore/BufferWriter.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_string.h"
#include "tscore/ink_inet.h"
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

constexpr std::string_view PPv1_DELIMITER = " "sv;

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
constexpr uint16_t PPv2_ADDR_LEN_UNIX  = 108 + 108;

const ts::BWFSpec ADDR_ONLY_FMT{"::a"};

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
  pp_info->src_addr.network_order_port() = htons(src_port);

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
  pp_info->dst_addr.network_order_port() = htons(dst_port);

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

/**
   Build PROXY Protocol v1
 */
size_t
proxy_protocol_v1_build(uint8_t *buf, size_t max_buf_len, const ProxyProtocol &pp_info)
{
  if (max_buf_len < PPv1_CONNECTION_HEADER_LEN_MAX) {
    return 0;
  }

  ts::FixedBufferWriter bw{reinterpret_cast<char *>(buf), max_buf_len};

  // preface
  bw.write(PPv1_CONNECTION_PREFACE);
  bw.write(PPv1_DELIMITER);

  // the proxied INET protocol and family
  if (pp_info.src_addr.isIp4()) {
    bw.write(PPv1_PROTO_TCP4);
  } else if (pp_info.src_addr.isIp6()) {
    bw.write(PPv1_PROTO_TCP6);
  } else {
    bw.write(PPv1_PROTO_UNKNOWN);
  }
  bw.write(PPv1_DELIMITER);

  // the layer 3 source address
  bwformat(bw, ADDR_ONLY_FMT, pp_info.src_addr);
  bw.write(PPv1_DELIMITER);

  // the layer 3 destination address
  bwformat(bw, ADDR_ONLY_FMT, pp_info.dst_addr);
  bw.write(PPv1_DELIMITER);

  // TCP source port
  {
    size_t len = ink_small_itoa(ats_ip_port_host_order(pp_info.src_addr), bw.auxBuffer(), bw.remaining());
    bw.fill(len);
    bw.write(PPv1_DELIMITER);
  }

  // TCP destination port
  {
    size_t len = ink_small_itoa(ats_ip_port_host_order(pp_info.dst_addr), bw.auxBuffer(), bw.remaining());
    bw.fill(len);
  }

  bw.write("\r\n");

  return bw.size();
}

/**
   Build PROXY Protocol v2

   UDP, Unix Domain Socket, and TLV fields are not supported yet
 */
size_t
proxy_protocol_v2_build(uint8_t *buf, size_t max_buf_len, const ProxyProtocol &pp_info)
{
  if (max_buf_len < PPv2_CONNECTION_HEADER_LEN) {
    return 0;
  }

  ts::FixedBufferWriter bw{reinterpret_cast<char *>(buf), max_buf_len};

  // # proxy_hdr_v2
  // ## preface
  bw.write(PPv2_CONNECTION_PREFACE);

  // ## version and command
  // TODO: support PPv2_CMD_LOCAL for health check
  bw.write(static_cast<char>(PPv2_CMD_PROXY));

  // ## family & address
  // TODO: support UDP
  switch (pp_info.src_addr.family()) {
  case AF_INET:
    bw.write(static_cast<char>(PPv2_PROTO_TCP4));
    break;
  case AF_INET6:
    bw.write(static_cast<char>(PPv2_PROTO_TCP6));
    break;
  case AF_UNIX:
    bw.write(static_cast<char>(PPv2_PROTO_UNIX_STREAM));
    break;
  default:
    bw.write(static_cast<char>(PPv2_PROTO_UNSPEC));
    break;
  }

  // ## len field. this will be set at the end of this function
  const size_t len_field_offset = bw.size();
  bw.fill(2);

  ink_release_assert(bw.size() == PPv2_CONNECTION_HEADER_LEN);

  // # proxy_addr
  // TODO: support UDP
  switch (pp_info.src_addr.family()) {
  case AF_INET: {
    bw.write(&ats_ip4_addr_cast(pp_info.src_addr), TS_IP4_SIZE);
    bw.write(&ats_ip4_addr_cast(pp_info.dst_addr), TS_IP4_SIZE);
    bw.write(&ats_ip_port_cast(pp_info.src_addr), TS_PORT_SIZE);
    bw.write(&ats_ip_port_cast(pp_info.dst_addr), TS_PORT_SIZE);

    break;
  }
  case AF_INET6: {
    bw.write(&ats_ip6_addr_cast(pp_info.src_addr), TS_IP6_SIZE);
    bw.write(&ats_ip6_addr_cast(pp_info.dst_addr), TS_IP6_SIZE);
    bw.write(&ats_ip_port_cast(pp_info.src_addr), TS_PORT_SIZE);
    bw.write(&ats_ip_port_cast(pp_info.dst_addr), TS_PORT_SIZE);

    break;
  }
  case AF_UNIX: {
    // unsupported yet
    bw.fill(PPv2_ADDR_LEN_UNIX);
    break;
  }
  default:
    // do nothing
    break;
  }

  // # Additional TLVs (pp2_tlv)
  // unsupported yet

  // Set len field (number of following bytes part of the header) in the hdr
  uint16_t len = htons(bw.size() - PPv2_CONNECTION_HEADER_LEN);
  memcpy(buf + len_field_offset, &len, sizeof(uint16_t));
  return bw.size();
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

/**
   PROXY Protocol Builder
 */
size_t
proxy_protocol_build(uint8_t *buf, size_t max_buf_len, const ProxyProtocol &pp_info, ProxyProtocolVersion force_version)
{
  ProxyProtocolVersion version = pp_info.version;
  if (force_version != ProxyProtocolVersion::UNDEFINED) {
    version = force_version;
  }

  size_t len = 0;

  if (version == ProxyProtocolVersion::V1) {
    len = proxy_protocol_v1_build(buf, max_buf_len, pp_info);
  } else if (version == ProxyProtocolVersion::V2) {
    len = proxy_protocol_v2_build(buf, max_buf_len, pp_info);
  } else {
    ink_abort("PROXY Protocol Version is undefined");
  }

  return len;
}

ProxyProtocolVersion
proxy_protocol_version_cast(int i)
{
  switch (i) {
  case 1:
  case 2:
    return static_cast<ProxyProtocolVersion>(i);
  default:
    return ProxyProtocolVersion::UNDEFINED;
  }
}
