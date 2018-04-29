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

#include "I_EventSystem.h"
#include "I_DNSProcessor.h"

//
// Connection
//
struct DNSHandler;
enum class DNS_CONN_MODE { UDP_ONLY, TCP_RETRY, TCP_ONLY };

struct DNSConnection {
  /// Options for connecting.
  struct Options {
    typedef Options self; ///< Self reference type.

    /// Connection is done non-blocking.
    /// Default: @c true.
    bool _non_blocking_connect;
    /// Set socket to have non-blocking I/O.
    /// Default: @c true.
    bool _non_blocking_io;
    /// Use TCP if @c true, use UDP if @c false.
    /// Default: @c false.
    bool _use_tcp;
    /// Bind to a random port.
    /// Default: @c true.
    bool _bind_random_port;
    /// Bind to this local address when using IPv6.
    /// Default: unset, bind to IN6ADDR_ANY.
    sockaddr const *_local_ipv6;
    /// Bind to this local address when using IPv4.
    /// Default: unset, bind to INADDRY_ANY.
    sockaddr const *_local_ipv4;

    Options();

    self &setUseTcp(bool p);
    self &setNonBlockingConnect(bool p);
    self &setNonBlockingIo(bool p);
    self &setBindRandomPort(bool p);
    self &setLocalIpv6(sockaddr const *addr);
    self &setLocalIpv4(sockaddr const *addr);
  };

  int fd;
  IpEndpoint ip;
  int num;
  Options opt;
  LINK(DNSConnection, link);
  EventIO eio;
  InkRand generator;
  DNSHandler *handler;

  /// TCPData structure is to track the reading progress of a TCP connection
  struct TCPData {
    Ptr<HostEnt> buf_ptr;
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

  int connect(sockaddr const *addr, Options const &opt = DEFAULT_OPTIONS);
  /*
                bool non_blocking_connect = NON_BLOCKING_CONNECT,
                bool use_tcp = CONNECT_WITH_TCP, bool non_blocking = NON_BLOCKING, bool bind_random_port = BIND_ANY_PORT);
  */
  int close();
  void trigger();

  virtual ~DNSConnection();
  DNSConnection();

  static Options const DEFAULT_OPTIONS;
};

inline DNSConnection::Options::Options()
  : _non_blocking_connect(true),
    _non_blocking_io(true),
    _use_tcp(false),
    _bind_random_port(true),
    _local_ipv6(nullptr),
    _local_ipv4(nullptr)
{
}

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
