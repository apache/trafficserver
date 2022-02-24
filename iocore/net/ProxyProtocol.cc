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

#include "tscore/ink_assert.h"
#include "tscpp/util/TextView.h"
#include "ProxyProtocol.h"
#include "I_NetVConnection.h"

bool
ssl_has_proxy_v1(NetVConnection *sslvc, char *buffer, int64_t *bytes_r)
{
  ts::TextView tv;

  tv.assign(buffer, *bytes_r);

  // Client must send at least 15 bytes to get a reasonable match.
  if (tv.size() < PROXY_V1_CONNECTION_HEADER_LEN_MIN) {
    Debug("proxyprotocol_v1", "ssl_has_proxy_v1: not enough recv'd");
    return false;
  }

  // if we don't have the PROXY preface, we don't have a ProxyV1 header
  if (0 != memcmp(PROXY_V1_CONNECTION_PREFACE, buffer, PROXY_V1_CONNECTION_PREFACE_LEN)) {
    Debug("proxyprotocol_v1", "ssl_has_proxy_v1: failed the memcmp(%s, %s, %lu)", PROXY_V1_CONNECTION_PREFACE, buffer,
          PROXY_V1_CONNECTION_PREFACE_LEN);
    return false;
  }

  //  Find the terminating newline
  ts::TextView::size_type pos = tv.find('\n');
  if (pos == tv.npos) {
    Debug("proxyprotocol_v1", "ssl_has_proxy_v1: newline not found");
    return false;
  }

  // Parse the TextView before moving the bytes in the buffer
  if (!proxy_protov1_parse(sslvc, tv)) {
    *bytes_r = -EAGAIN;
    return false;
  }
  *bytes_r -= pos + 1;
  if (*bytes_r <= 0) {
    *bytes_r = -EAGAIN;
  } else {
    Debug("ssl", "Moving %" PRId64 " characters remaining in the buffer from %p to %p", *bytes_r, buffer + pos + 1, buffer);
    memmove(buffer, buffer + pos + 1, *bytes_r);
  }
  return true;
}

bool
http_has_proxy_v1(IOBufferReader *reader, NetVConnection *netvc)
{
  char buf[PROXY_V1_CONNECTION_HEADER_LEN_MAX + 1] = {0};
  ts::TextView tv;

  tv.assign(buf, reader->memcpy(buf, sizeof(buf), 0));

  // Client must send at least 15 bytes to get a reasonable match.
  if (tv.size() < PROXY_V1_CONNECTION_HEADER_LEN_MIN) {
    return false;
  }

  if (0 != memcmp(PROXY_V1_CONNECTION_PREFACE, buf, PROXY_V1_CONNECTION_PREFACE_LEN)) {
    return false;
  }

  // Find the terminating LF, which should already be in the buffer.
  ts::TextView::size_type pos = tv.find('\n');
  if (pos == tv.npos) { // not found, it's not a proxy protocol header.
    return false;
  }
  reader->consume(pos + 1); // clear out the header.

  // Now that we know we have a valid PROXY V1 preface, let's parse the
  // remainder of the header

  return proxy_protov1_parse(netvc, tv);
}

bool
proxy_protov1_parse(NetVConnection *netvc, ts::TextView hdr)
{
  static const std::string_view PREFACE{PROXY_V1_CONNECTION_PREFACE, PROXY_V1_CONNECTION_PREFACE_LEN};
  ts::TextView token;
  in_port_t port;

  // All the cases are special and sequence, might as well unroll them.

  // The header should begin with the PROXY preface
  token = hdr.split_prefix_at(' ');
  if (0 == token.size() || token != PREFACE) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: header [%.*s] does not start with preface [%.*s]", static_cast<int>(hdr.size()),
          hdr.data(), static_cast<int>(PREFACE.size()), PREFACE.data());
    return false;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = PREFACE", static_cast<int>(token.size()), token.data());

  // The INET protocol family - TCP4, TCP6 or UNKNOWN
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return false;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = INET Family", static_cast<int>(token.size()), token.data());

  // Next up is the layer 3 source address
  // - 255.255.255.255 or ffff:f...f:ffff ffff:f...f:fff
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return false;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Source Address", static_cast<int>(token.size()), token.data());
  if (0 != netvc->set_proxy_protocol_src_addr(token)) {
    return false;
  }

  // Next is the layer3 destination address
  // - 255.255.255.255 or ffff:f...f:ffff ffff:f...f:fff
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return false;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Destination Address", static_cast<int>(token.size()), token.data());
  if (0 != netvc->set_proxy_protocol_dst_addr(token)) {
    return false;
  }

  // Next is the TCP source port represented as a decimal number in the range of [0..65535] inclusive.
  token = hdr.split_prefix_at(' ');
  if (0 == token.size()) {
    return false;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Source Port", static_cast<int>(token.size()), token.data());

  if (0 == (port = ts::svtoi(token))) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: src port [%d] token [%.*s] failed to parse", port,
          static_cast<int>(token.size()), token.data());
    return false;
  }
  netvc->set_proxy_protocol_src_port(port);

  // Next is the TCP destination port represented as a decimal number in the range of [0..65535] inclusive.
  // Final trailer is CR LF so split at CR.
  token = hdr.split_prefix_at('\r');
  if (0 == token.size()) {
    return false;
  }
  Debug("proxyprotocol_v1", "proxy_protov1_parse: [%.*s] = Destination Port", static_cast<int>(token.size()), token.data());
  if (0 == (port = ts::svtoi(token))) {
    Debug("proxyprotocol_v1", "proxy_protov1_parse: dst port [%d] token [%.*s] failed to parse", port,
          static_cast<int>(token.size()), token.data());
    return false;
  }
  netvc->set_proxy_protocol_dst_port(port);

  netvc->set_proxy_protocol_version(NetVConnection::ProxyProtocolVersion::V1);

  return true;
}
