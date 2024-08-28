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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"
#include <arpa/inet.h>

constexpr unsigned NORMALIZED_TIME_QUANTUM = 3600; // 1 hour

// Some common network ranges
const cripts::Matcher::Range::IP cripts::Net::Localhost({"127.0.0.1", "::1"});
const cripts::Matcher::Range::IP cripts::Net::RFC1918({"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"});

void
detail::ConnBase::Pacing::operator=(uint32_t val)
{
  TSAssert(_owner);
  if (val == 0) {
    val = Off;
  }

#ifdef SO_MAX_PACING_RATE
  int connfd = _owner->FD();
  int res    = setsockopt(connfd, SOL_SOCKET, SO_MAX_PACING_RATE, (char *)&val, sizeof(val));

  // EBADF indicates possible client abort
  if ((res < 0) && (errno != EBADF)) {
    TSError("[fq_pacing] Error setting SO_MAX_PACING_RATE, errno=%d", errno);
  }
#endif
  _val = val;
}

void
detail::ConnBase::TcpInfo::initialize()
{
#if defined(TCP_INFO) && defined(HAVE_STRUCT_TCP_INFO)
  if (!_ready) {
    int connfd = _owner->FD();

    TSAssert(_owner->_state->txnp);
    if (connfd < 0 || TSHttpTxnIsInternal(_owner->_state->txnp)) { // No TCPInfo for internal transactions
      _ready = false;
      // ToDo: Deal with errors?
    } else {
      if (getsockopt(connfd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
        _ready = (info_len > 0);
      }
    }
  }
#endif
}

cripts::string_view
detail::ConnBase::TcpInfo::Log()
{
  initialize();
  // We intentionally do not use the old tcpinfo that may be stored, since we may
  // request this numerous times (measurements). Also make sure there's always a value.
  _logging = "-";

  if (_ready) {
    // A lot of this is taken verbatim from header_rewrite, may want to rewrite this with sstreams
#if HAVE_STRUCT_TCP_INFO_TCPI_TOTAL_RETRANS
    _logging = fmt::format("{};{};{};{}", info.tcpi_rtt, info.tcpi_rto, info.tcpi_snd_cwnd, info.tcpi_retrans);
#elif HAVE_STRUCT_TCP_INFO___TCPI_RETRANS
    _logging = fmt::format("{};{};{};{}", info.tcpi_rtt, info.tcpi_rto, info.tcpi_snd_cwnd, info.__tcpi_retrans);
#endif
  }

  return _logging;
}

namespace cripts
{

// This is mostly copied out of header_rewrite of course
cripts::string_view
IP::GetSV(unsigned ipv4_cidr, unsigned ipv6_cidr)
{
  if (is_ip4()) {
    auto      addr = this->_addr._ip4.network_order();
    in_addr_t ip   = addr & htonl(UINT32_MAX << (32 - ipv4_cidr));

    if (inet_ntop(AF_INET, &ip, _str, INET_ADDRSTRLEN)) {
      return {_str, strlen(_str)};
    }
  } else if (is_ip6()) {
    unsigned int v6_zero_bytes = (128 - ipv6_cidr) / 8;
    int          v6_mask       = 0xff >> ((128 - ipv6_cidr) % 8);
    auto         addr          = this->_addr._ip6.network_order();

    // For later:
    //   swoc::FixedBuffer w(buff, buff_size);
    //   w.print("{}", addr & IPMask(addr.is_ip4() ? 24 : 64));
    //   addr &= IPMask(addr.is_ip4() ? 24 : 64);
    //

    if (v6_zero_bytes > 0) {
      memset(&addr.s6_addr[16 - v6_zero_bytes], 0, v6_zero_bytes);
    }
    if (v6_mask != 0xff) {
      addr.s6_addr[16 - v6_zero_bytes] &= v6_mask;
    }
    if (inet_ntop(AF_INET6, &addr, _str, INET6_ADDRSTRLEN)) {
      return {_str, strlen(_str)};
    }
  }

  return "";
}

uint64_t
IP::Hasher(unsigned ipv4_cidr, unsigned ipv6_cidr)
{
  if (_hash == 0) {
    if (is_ip4()) {
      auto      addr = this->_addr._ip4.network_order();
      in_addr_t ip   = addr & htonl(UINT32_MAX << (32 - ipv4_cidr));

      _hash = (0xffffffff00000000 | ip);
    } else if (is_ip6()) {
      unsigned int v6_zero_bytes = (128 - ipv6_cidr) / 8;
      int          v6_mask       = 0xff >> ((128 - ipv6_cidr) % 8);
      auto         addr          = this->_addr._ip6.network_order();

      if (v6_zero_bytes > 0) {
        memset(&addr.s6_addr[16 - v6_zero_bytes], 0, v6_zero_bytes);
      }
      if (v6_mask != 0xff) {
        addr.s6_addr[16 - v6_zero_bytes] &= v6_mask;
      }

      _hash =
        (*reinterpret_cast<uint64_t const *>(addr.s6_addr) ^ *reinterpret_cast<uint64_t const *>(addr.s6_addr + sizeof(uint64_t)));
    } else {
      // Clearly this shouldn't happen, but lets not assert
    }
  }

  return _hash;
}

bool
IP::Sample(double rate, uint32_t seed, unsigned ipv4_cidr, unsigned ipv6_cidr)
{
  CAssert(rate >= 0.0 && rate <= 1.0); // For detecting bugs in a Cript, 0.0 and 1.0 are valid though

  if (_sampler == 0) {
    // This only works until 2038
    uint32_t now = (Time::Local::Now().Epoch() / NORMALIZED_TIME_QUANTUM) * NORMALIZED_TIME_QUANTUM;

    if (is_ip4()) {
      auto      addr = this->_addr._ip4.network_order();
      in_addr_t ip   = addr & htonl(UINT32_MAX << (32 - ipv4_cidr));

      _sampler = (ip >> 16) ^ (ip & 0x00ff); // Fold them to 16 bits, mixing net and host

    } else if (is_ip6()) {
      unsigned int v6_zero_bytes = (128 - ipv6_cidr) / 8;
      int          v6_mask       = 0xff >> ((128 - ipv6_cidr) % 8);
      auto         addr          = this->_addr._ip6.network_order();

      if (v6_zero_bytes > 0) {
        memset(&addr.s6_addr[16 - v6_zero_bytes], 0, v6_zero_bytes);
      }
      if (v6_mask != 0xff) {
        addr.s6_addr[16 - v6_zero_bytes] &= v6_mask;
      }

      _sampler = *reinterpret_cast<uint16_t const *>(addr.s6_addr) ^
                 *reinterpret_cast<uint16_t const *>(addr.s6_addr + sizeof(uint16_t)) ^
                 *reinterpret_cast<uint16_t const *>(addr.s6_addr + 2 * sizeof(uint16_t)) ^
                 *reinterpret_cast<uint16_t const *>(addr.s6_addr + 3 * sizeof(uint16_t));
    } else {
      // Clearly this shouldn't happen, but lets not assert
    }

    _sampler ^= (now >> 16) ^ (now & 0x00ff); // Fold in the hourly normalized timestamp
    if (seed) {
      _sampler ^= (seed >> 16) ^ (seed & 0x00ff); // Fold in the seed as well
    }
  }

  // To avoid picking sequential folded IPs bucketize them with a modulo of the sampler (just in case)
  return (_sampler % static_cast<uint16_t>(rate * UINT16_MAX)) == _sampler;
}

Client::Connection &
Client::Connection::_get(cripts::Context *context)
{
  Client::Connection *conn = &context->_client_conn;

  if (!conn->Initialized()) {
    TSAssert(context->state.ssnp);
    TSAssert(context->state.txnp);

    conn->_state  = &context->state;
    conn->_vc     = TSHttpSsnClientVConnGet(context->state.ssnp);
    conn->_socket = TSHttpTxnClientAddrGet(context->state.txnp);
  }

  return context->_client_conn;
}

int
Client::Connection::FD() const
{
  int connfd = -1;

  TSAssert(_state->txnp);
  TSHttpTxnClientFdGet(_state->txnp, &connfd);

  return connfd;
}

int
Client::Connection::Count() const
{
  TSHttpSsn ssn = TSHttpTxnSsnGet(_state->txnp);

  TSAssert(_state->txnp);

  return ssn ? TSHttpSsnTransactionCount(ssn) : -1;
}

Server::Connection &
Server::Connection::_get(cripts::Context *context)
{
  cripts::Server::Connection *conn = &context->_server_conn;

  if (!conn->Initialized()) {
    TSAssert(context->state.ssnp);

    conn->_state  = &context->state;
    conn->_vc     = TSHttpSsnServerVConnGet(context->state.ssnp);
    conn->_socket = TSHttpTxnServerAddrGet(context->state.txnp);
  }

  return context->_server_conn;
}

int
Server::Connection::FD() const
{
  int connfd = -1;

  TSAssert(_state->txnp);
  TSHttpTxnServerFdGet(_state->txnp, &connfd);

  return connfd;
}

int
Server::Connection::Count() const
{
  return TSHttpTxnServerSsnTransactionCount(_state->txnp);
}

} // namespace cripts
