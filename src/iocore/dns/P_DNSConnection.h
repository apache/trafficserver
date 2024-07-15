/** @file

  A brief file description

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

/**************************************************************************

  P_DNSConnection.h
  Description:
  struct DNSConnection
  **************************************************************************/

#pragma once

#include "iocore/dns/DNSEventIO.h"
#include "iocore/dns/DNSProcessor.h"

#include "iocore/eventsystem/UnixSocket.h"

#include "tscore/ink_platform.h"
#include "tscore/ink_rand.h"
#include "tscore/List.h"
#include "tscore/Ptr.h"

#include <swoc/IPEndpoint.h>

//
// Connection
//
struct DNSHandler;
enum class DNS_CONN_MODE { UDP_ONLY, TCP_RETRY, TCP_ONLY };

struct DNSConnection {
  /// Options for connecting.
  struct Options {
    using self = Options; ///< Self reference type.

    /// Connection is done non-blocking.
    /// Default: @c true.
    bool _non_blocking_connect = true;
    /// Set socket to have non-blocking I/O.
    /// Default: @c true.
    bool _non_blocking_io = true;
    /// Use TCP if @c true, use UDP if @c false.
    /// Default: @c false.
    bool _use_tcp = false;
    /// Bind to a random port.
    /// Default: @c true.
    bool _bind_random_port = true;
    /// Bind to this local address when using IPv6.
    /// Default: unset, bind to IN6ADDR_ANY.
    sockaddr const *_local_ipv6 = nullptr;
    /// Bind to this local address when using IPv4.
    /// Default: unset, bind to INADDRY_ANY.
    sockaddr const *_local_ipv4 = nullptr;

    Options();

    self &setUseTcp(bool p);
    self &setNonBlockingConnect(bool p);
    self &setNonBlockingIo(bool p);
    self &setBindRandomPort(bool p);
    self &setLocalIpv6(sockaddr const *addr);
    self &setLocalIpv4(sockaddr const *addr);
  };

  UnixSocket sock{NO_SOCK};
  IpEndpoint ip;
  int        num = 0;
  Options    opt;
  LINK(DNSConnection, link);
  DNSEventIO  eio{*this};
  InkRand     generator;
  DNSHandler *handler = nullptr;

  /// TCPData structure is to track the reading progress of a TCP connection
  struct TCPData {
    Ptr<HostEnt>   buf_ptr;
    unsigned short total_length = 0;
    unsigned short done_reading = 0;
    void
    reset()
    {
      buf_ptr.clear();
      total_length = 0;
      done_reading = 0;
    }
  } tcp_data;

  int  connect(sockaddr const *addr, Options const &opt = DEFAULT_OPTIONS);
  int  close();
  void trigger();

  virtual ~DNSConnection();
  DNSConnection();

  static Options const DEFAULT_OPTIONS;
};

inline DNSConnection::Options::Options() {}

inline DNSConnection::Options &
DNSConnection::Options::setNonBlockingIo(bool p)
{
  _non_blocking_io = p;
  return *this;
}
inline DNSConnection::Options &
DNSConnection::Options::setNonBlockingConnect(bool p)
{
  _non_blocking_connect = p;
  return *this;
}
inline DNSConnection::Options &
DNSConnection::Options::setUseTcp(bool p)
{
  _use_tcp = p;
  return *this;
}
inline DNSConnection::Options &
DNSConnection::Options::setBindRandomPort(bool p)
{
  _bind_random_port = p;
  return *this;
}
inline DNSConnection::Options &
DNSConnection::Options::setLocalIpv4(sockaddr const *ip)
{
  _local_ipv4 = ip;
  return *this;
}
inline DNSConnection::Options &
DNSConnection::Options::setLocalIpv6(sockaddr const *ip)
{
  _local_ipv6 = ip;
  return *this;
}
