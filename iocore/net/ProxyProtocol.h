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
#include <tscpp/util/TextView.h>

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

struct ProxyProtocol {
  ProxyProtocolVersion version = ProxyProtocolVersion::UNDEFINED;
  uint16_t ip_family           = AF_UNSPEC;
  IpEndpoint src_addr          = {};
  IpEndpoint dst_addr          = {};
};

const size_t PPv1_CONNECTION_HEADER_LEN_MAX = 108;
const size_t PPv2_CONNECTION_HEADER_LEN     = 16;

extern size_t proxy_protocol_parse(ProxyProtocol *pp_info, ts::TextView tv);
extern size_t proxy_protocol_build(uint8_t *buf, size_t max_buf_len, const ProxyProtocol &pp_info,
                                   ProxyProtocolVersion force_version = ProxyProtocolVersion::UNDEFINED);
extern ProxyProtocolVersion proxy_protocol_version_cast(int i);
