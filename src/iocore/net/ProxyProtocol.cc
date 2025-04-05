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

#include "iocore/net/ProxyProtocol.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_string.h"
#include "tscore/ink_inet.h"
#include "swoc/TextView.h"
#include "swoc/bwf_base.h"
#include "tsutil/DbgCtl.h"
#include <optional>
#include <string_view>

namespace
{
using namespace std::literals;

constexpr swoc::TextView PPv1_CONNECTION_PREFACE = "PROXY"sv;
constexpr swoc::TextView PPv2_CONNECTION_PREFACE = "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A"sv;

constexpr size_t PPv1_CONNECTION_HEADER_LEN_MIN = 15;

constexpr swoc::TextView PPv1_PROTO_UNKNOWN = "UNKNOWN"sv;
constexpr swoc::TextView PPv1_PROTO_TCP4    = "TCP4"sv;
constexpr swoc::TextView PPv1_PROTO_TCP6    = "TCP6"sv;

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

const swoc::bwf::Spec ADDR_ONLY_FMT{"::a"};

DbgCtl dbg_ctl_proxyprotocol_v1{"proxyprotocol_v1"};
DbgCtl dbg_ctl_proxyprotocol_v2{"proxyprotocol_v2"};
DbgCtl dbg_ctl_proxyprotocol{"proxyprotocol"};

struct PPv2Hdr {
  uint8_t  sig[12]; ///< preface
  uint8_t  ver_cmd; ///< protocol version and command
  uint8_t  fam;     ///< protocol family and transport
  uint16_t len;     ///< number of following bytes part of the header
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
      uint8_t  src_addr[16];
      uint8_t  dst_addr[16];
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
proxy_protocol_v1_parse(ProxyProtocol *pp_info, swoc::TextView hdr)
{
  ink_release_assert(hdr.size() >= PPv1_CONNECTION_HEADER_LEN_MIN);

  // Find the terminating newline
  swoc::TextView::size_type pos = hdr.find('\n');
  if (pos == hdr.npos) {
    Dbg(dbg_ctl_proxyprotocol_v1, "ssl_has_proxy_v1: LF not found");
    return 0;
  }

  if (hdr[pos - 1] != '\r') {
    Dbg(dbg_ctl_proxyprotocol_v1, "ssl_has_proxy_v1: CR not found");
    return 0;
  }

  swoc::TextView token;

  // All the cases are special and sequence, might as well unroll them.

  // The header should begin with the PROXY preface
  token = hdr.split_prefix_at(' ');
  if (0 == token.size() || token != PPv1_CONNECTION_PREFACE) {
    Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: header [%.*s] does not start with preface [%.*s]",
        static_cast<int>(hdr.size()), hdr.data(), static_cast<int>(PPv1_CONNECTION_PREFACE.size()), PPv1_CONNECTION_PREFACE.data());
    return 0;
  }
  Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: [%.*s] = PREFACE", static_cast<int>(token.size()), token.data());

  // The INET protocol family - TCP4, TCP6 or UNKNOWN
  if (hdr.starts_with(PPv1_PROTO_UNKNOWN)) {
    Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: [UNKNOWN] = INET Family");

    // Ignore anything presented before the CRLF
    pp_info->version = ProxyProtocolVersion::V1;

    return pos + 1;
  } else if (hdr.starts_with(PPv1_PROTO_TCP4)) {
    token = hdr.split_prefix_at(' ');
    if (0 == token.size()) {
      return 0;
    }

    pp_info->ip_family = AF_INET;
    pp_info->type      = SOCK_STREAM;
  } else if (hdr.starts_with(PPv1_PROTO_TCP6)) {
    token = hdr.split_prefix_at(' ');
    if (0 == token.size()) {
      return 0;
    }

    pp_info->ip_family = AF_INET6;
    pp_info->type      = SOCK_STREAM;
  } else {
    return 0;
  }
  Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: [%.*s] = INET Family", static_cast<int>(token.size()), token.data());

  // Next up is the layer 3 source address
  // - 255.255.255.255 or ffff:f...f:ffff ffff:f...f:fff
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return 0;
  }
  Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: [%.*s] = Source Address", static_cast<int>(token.size()), token.data());
  if (0 != ats_ip_pton(token, &pp_info->src_addr)) {
    return 0;
  }

  // Next is the layer3 destination address
  // - 255.255.255.255 or ffff:f...f:ffff ffff:f...f:fff
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return 0;
  }
  Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: [%.*s] = Destination Address", static_cast<int>(token.size()), token.data());
  if (0 != ats_ip_pton(token, &pp_info->dst_addr)) {
    return 0;
  }

  // Next is the TCP source port represented as a decimal number in the range of [0..65535] inclusive.
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return 0;
  }
  Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: [%.*s] = Source Port", static_cast<int>(token.size()), token.data());

  in_port_t src_port = swoc::svtoi(token);
  if (src_port == 0) {
    Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: src port [%d] token [%.*s] failed to parse", src_port,
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
  Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: [%.*s] = Destination Port", static_cast<int>(token.size()), token.data());

  in_port_t dst_port = swoc::svtoi(token);
  if (dst_port == 0) {
    Dbg(dbg_ctl_proxyprotocol_v1, "proxy_protov1_parse: dst port [%d] token [%.*s] failed to parse", dst_port,
        static_cast<int>(token.size()), token.data());
    return 0;
  }
  pp_info->dst_addr.network_order_port() = htons(dst_port);

  pp_info->version = ProxyProtocolVersion::V1;

  return pos + 1;
}

/**
   PROXY Protocol v2 Parser

   @return read length
 */
size_t
proxy_protocol_v2_parse(ProxyProtocol *pp_info, const swoc::TextView &msg)
{
  ink_release_assert(msg.size() >= PPv2_CONNECTION_HEADER_LEN);

  const PPv2Hdr *hdr_v2 = reinterpret_cast<const PPv2Hdr *>(msg.data());

  // Assuming PREFACE check is done

  // length check
  const uint16_t len       = ntohs(hdr_v2->len);
  const size_t   total_len = PPv2_CONNECTION_HEADER_LEN + len;
  uint16_t       tlv_len   = 0;

  if (msg.size() < total_len) {
    Dbg(dbg_ctl_proxyprotocol_v2, "The amount of available data is smaller than the expected size");
    return 0;
  }

  // protocol version and command
  switch (hdr_v2->ver_cmd) {
  case PPv2_CMD_LOCAL: {
    // protocol byte should be UNSPEC (\x00) with LOCAL command
    if (hdr_v2->fam != PPv2_PROTO_UNSPEC) {
      Dbg(dbg_ctl_proxyprotocol_v2, "UNSPEC is unexpected");
      return 0;
    }

    pp_info->version   = ProxyProtocolVersion::V2;
    pp_info->ip_family = AF_UNSPEC;

    return total_len;
  }
  case PPv2_CMD_PROXY: {
    switch (hdr_v2->fam) {
    case PPv2_PROTO_TCP4:
    case PPv2_PROTO_UDP4:
      if (len < PPv2_ADDR_LEN_INET) {
        Dbg(dbg_ctl_proxyprotocol_v2, "There is not enough data left for IPv4 info");
        return 0;
      }
      tlv_len = len - PPv2_ADDR_LEN_INET;

      pp_info->set_ipv4_addrs(reinterpret_cast<in_addr_t>(hdr_v2->addr.ip4.src_addr), hdr_v2->addr.ip4.src_port,
                              reinterpret_cast<in_addr_t>(hdr_v2->addr.ip4.dst_addr), hdr_v2->addr.ip4.dst_port);
      pp_info->type    = hdr_v2->fam == PPv2_PROTO_TCP4 ? SOCK_STREAM : SOCK_DGRAM;
      pp_info->version = ProxyProtocolVersion::V2;

      break;
    case PPv2_PROTO_TCP6:
    case PPv2_PROTO_UDP6:
      if (len < PPv2_ADDR_LEN_INET6) {
        Dbg(dbg_ctl_proxyprotocol_v2, "There is not enough data left for IPv6 info");
        return 0;
      }
      tlv_len = len - PPv2_ADDR_LEN_INET6;

      pp_info->set_ipv6_addrs(reinterpret_cast<in6_addr const &>(hdr_v2->addr.ip6.src_addr), hdr_v2->addr.ip6.src_port,
                              reinterpret_cast<in6_addr const &>(hdr_v2->addr.ip6.dst_addr), hdr_v2->addr.ip6.dst_port);
      pp_info->type    = hdr_v2->fam == PPv2_PROTO_TCP6 ? SOCK_STREAM : SOCK_DGRAM;
      pp_info->version = ProxyProtocolVersion::V2;

      break;
    case PPv2_PROTO_UNIX_STREAM:
      [[fallthrough]];
    case PPv2_PROTO_UNIX_DATAGRAM:
      [[fallthrough]];
    case PPv2_PROTO_UNSPEC:
      [[fallthrough]];
    default:
      // unsupported
      Dbg(dbg_ctl_proxyprotocol_v2, "Unsupported protocol family (%d)", hdr_v2->fam);
      return 0;
    }

    if (tlv_len > 0) {
      if (pp_info->set_additional_data(msg.substr(total_len - tlv_len, tlv_len)) < 0) {
        Dbg(dbg_ctl_proxyprotocol_v2, "Failed to parse additional fields");
        return 0;
      }
    }

    return total_len;
  }
  default:
    Dbg(dbg_ctl_proxyprotocol_v2, "Unsupported command (%d)", hdr_v2->ver_cmd);
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

  swoc::FixedBufferWriter bw{reinterpret_cast<char *>(buf), max_buf_len};

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
    size_t len = ink_small_itoa(ats_ip_port_host_order(pp_info.src_addr), bw.aux_data(), bw.remaining());
    bw.commit(len);
    bw.write(PPv1_DELIMITER);
  }

  // TCP destination port
  {
    size_t len = ink_small_itoa(ats_ip_port_host_order(pp_info.dst_addr), bw.aux_data(), bw.remaining());
    bw.commit(len);
  }

  Dbg(dbg_ctl_proxyprotocol_v1, "Proxy Protocol v1: %.*s", static_cast<int>(bw.size()), bw.data());
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

  swoc::FixedBufferWriter bw{reinterpret_cast<char *>(buf), max_buf_len};

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
  bw.commit(2);

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
    bw.commit(PPv2_ADDR_LEN_UNIX);
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
  Dbg(dbg_ctl_proxyprotocol_v2, "Proxy Protocol v2 of %zu bytes", bw.size());
  return bw.size();
}

} // namespace

/**
   PROXY Protocol Parser
 */
size_t
proxy_protocol_parse(ProxyProtocol *pp_info, swoc::TextView tv)
{
  size_t len = 0;

  // Parse the TextView before moving the bytes in the buffer
  if (tv.size() >= PPv1_CONNECTION_HEADER_LEN_MIN && tv.starts_with(PPv1_CONNECTION_PREFACE)) {
    // Client must send at least 15 bytes to get a reasonable match.
    len = proxy_protocol_v1_parse(pp_info, tv);
  } else if (tv.size() >= PPv2_CONNECTION_HEADER_LEN && tv.starts_with(PPv2_CONNECTION_PREFACE)) {
    len = proxy_protocol_v2_parse(pp_info, tv);
  } else {
    // if we don't have the PROXY preface, we don't have a ProxyProtocol header
    // TODO: print hexdump of buffer safely
    Dbg(dbg_ctl_proxyprotocol, "failed to find ProxyProtocol preface in the first %zu bytes", tv.size());
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

void
ProxyProtocol::set_ipv4_addrs(in_addr_t src_addr, uint16_t src_port, in_addr_t dst_addr, uint16_t dst_port)
{
  IpAddr src(src_addr);
  IpAddr dst(dst_addr);

  this->src_addr.assign(src, src_port);
  this->dst_addr.assign(dst, dst_port);

  this->ip_family = AF_INET;
}

void
ProxyProtocol::set_ipv6_addrs(const in6_addr &src_addr, uint16_t src_port, const in6_addr &dst_addr, uint16_t dst_port)
{
  IpAddr src(src_addr);
  IpAddr dst(dst_addr);

  this->src_addr.assign(src, src_port);
  this->dst_addr.assign(dst, dst_port);

  this->ip_family = AF_INET6;
}

std::optional<std::string_view>
ProxyProtocol::get_tlv(const uint8_t tlvCode) const
{
  if (version == ProxyProtocolVersion::V2) {
    if (auto v = tlv.find(tlvCode); v != tlv.end()) {
      return v->second;
    }
  }
  return std::nullopt;
}

int
ProxyProtocol::set_additional_data(std::string_view data)
{
  uint16_t len = data.length();
  Dbg(dbg_ctl_proxyprotocol_v2, "Parsing %d byte additional data", len);
  additional_data = static_cast<char *>(ats_malloc(len));
  if (additional_data == nullptr) {
    Dbg(dbg_ctl_proxyprotocol_v2, "Memory allocation failed");
    return -1;
  }
  data.copy(additional_data, len);

  const char *p   = additional_data;
  const char *end = p + len;
  while (p != end) {
    if (end - p < 3) {
      // The size of a TLV entry must be 3 bytes or more
      Dbg(dbg_ctl_proxyprotocol_v2, "Remaining data (%ld bytes) is not enough for a TLV field", end - p);
      return -2;
    }

    // Type
    uint8_t type  = *p;
    p            += 1;

    // Length
    uint16_t length  = ntohs(*reinterpret_cast<const uint16_t *>(p));
    p               += 2;

    // Value
    if (end - p < length) {
      // Does not have enough data
      Dbg(dbg_ctl_proxyprotocol_v2, "Remaining data (%ld bytes) is not enough for a TLV field (ID:%u LEN:%hu)", end - p, type,
          length);
      return -3;
    }
    Dbg(dbg_ctl_proxyprotocol, "TLV: ID=%u LEN=%hu", type, length);
    tlv.emplace(type, std::string_view(p, length));
    p += length;
  }

  return 0;
}
