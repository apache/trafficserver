/** @file

  PROXY Protocol

  See:  https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt

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

#pragma once

#include <tscore/ink_inet.h>
#include <swoc/TextView.h>
#include <unordered_map>
#include <cstdlib>

enum class ProxyProtocolVersion {
  UNDEFINED,
  V1,
  V2,
};

enum class ProxyProtocolData {
  UNDEFINED,
  SRC,
  DST,
};

constexpr uint8_t PP2_TYPE_ALPN           = 0x01;
constexpr uint8_t PP2_TYPE_AUTHORITY      = 0x02;
constexpr uint8_t PP2_TYPE_CRC32C         = 0x03;
constexpr uint8_t PP2_TYPE_NOOP           = 0x04;
constexpr uint8_t PP2_TYPE_UNIQUE_ID      = 0x05;
constexpr uint8_t PP2_TYPE_SSL            = 0x20;
constexpr uint8_t PP2_SUBTYPE_SSL_VERSION = 0x21;
constexpr uint8_t PP2_SUBTYPE_SSL_CN      = 0x22;
constexpr uint8_t PP2_SUBTYPE_SSL_CIPHER  = 0x23;
constexpr uint8_t PP2_SUBTYPE_SSL_SIG_ALG = 0x24;
constexpr uint8_t PP2_SUBTYPE_SSL_KEY_ALG = 0x25;
constexpr uint8_t PP2_TYPE_NETNS          = 0x30;

class ProxyProtocol
{
public:
  ProxyProtocol() {}
  ProxyProtocol(ProxyProtocolVersion pp_ver, uint16_t family, IpEndpoint src, IpEndpoint dst)
    : version(pp_ver), ip_family(family), src_addr(src), dst_addr(dst)
  {
  }
  ~ProxyProtocol() { ats_free(additional_data); }
  int  set_additional_data(std::string_view data);
  void set_ipv4_addrs(in_addr_t src_addr, uint16_t src_port, in_addr_t dst_addr, uint16_t dst_port);
  void set_ipv6_addrs(const in6_addr &src_addr, uint16_t src_port, const in6_addr &dst_addr, uint16_t dst_port);

  ProxyProtocolVersion                          version   = ProxyProtocolVersion::UNDEFINED;
  uint16_t                                      ip_family = AF_UNSPEC;
  int                                           type      = 0;
  IpEndpoint                                    src_addr  = {};
  IpEndpoint                                    dst_addr  = {};
  std::unordered_map<uint8_t, std::string_view> tlv;

private:
  char *additional_data = nullptr;
};

const size_t PPv1_CONNECTION_HEADER_LEN_MAX = 108;
const size_t PPv2_CONNECTION_HEADER_LEN     = 16;

extern size_t               proxy_protocol_parse(ProxyProtocol *pp_info, swoc::TextView tv);
extern size_t               proxy_protocol_build(uint8_t *buf, size_t max_buf_len, const ProxyProtocol &pp_info,
                                                 ProxyProtocolVersion force_version = ProxyProtocolVersion::UNDEFINED);
extern ProxyProtocolVersion proxy_protocol_version_cast(int i);
