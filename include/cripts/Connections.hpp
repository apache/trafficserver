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
#pragma once

namespace Cript
{
class Context;
}

#include "ts/apidefs.h"
#include "ts/ts.h"

#include "cripts/Matcher.hpp"

namespace Cript
{
namespace Net
{
  extern const Matcher::Range::IP Localhost;
  extern const Matcher::Range::IP RFC1918;
} // namespace Net

class IP : public swoc::IPAddr
{
  using super_type = swoc::IPAddr;
  using self_type  = IP;

public:
  using super_type::IPAddr;

  IP(const IP &)             = delete;
  void operator=(const IP &) = delete;

  Cript::string_view getSV(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);
  Cript::string_view
  string(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128)
  {
    return getSV(ipv4_cidr, ipv6_cidr);
  }

  uint64_t hasher(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);
  bool     sample(double rate, uint32_t seed = 0, unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);

private:
  char     _str[INET6_ADDRSTRLEN + 1];
  uint64_t _hash    = 0;
  uint16_t _sampler = 0;
};

} // namespace Cript

class ConnBase
{
  using self_type = ConnBase;
  class Dscp
  {
    using self_type = Dscp;

  public:
    friend class ConnBase;

    Dscp()                       = default;
    void operator=(const Dscp &) = delete;

    // This is not perfect, but there's currently no ATS Get() mechanism to see a connections
    // current DSCP options.
    operator integer() const { return _val; }

    void
    operator=(int val)
    {
      TSAssert(_owner);
      _owner->setDscp(val);
      _val = val;
    }

  private:
    ConnBase *_owner = nullptr;
    integer   _val   = -1;

  }; // End class ConnBase::Dscp

  class Pacing
  {
    using self_type = Pacing;

  public:
    friend class ConnBase;

    Pacing()                       = default;
    void operator=(const Pacing &) = delete;

    // This is not perfect, but there's currently no ATS Get() mechanism to see a connections
    // current PACING options.
    operator integer() const { return _val; }

    void
    operator=(uint32_t val)
    {
      TSAssert(_owner);
      if (val == 0) {
        val = Off;
      }

#ifdef SO_MAX_PACING_RATE
      int connfd = _owner->fd();
      int res    = setsockopt(connfd, SOL_SOCKET, SO_MAX_PACING_RATE, (char *)&val, sizeof(val));

      // EBADF indicates possible client abort
      if ((res < 0) && (errno != EBADF)) {
        TSError("[fq_pacing] Error setting SO_MAX_PACING_RATE, errno=%d", errno);
      }
#endif
      _val = val;
    }

    static constexpr uint32_t Off = std::numeric_limits<uint32_t>::max();

  private:
    ConnBase *_owner = nullptr;
    uint32_t  _val   = Off;

  }; // End class ConnBase::Pacing

  class Congestion
  {
    using self_type = Congestion;

  public:
    friend class ConnBase;

    Congestion()                       = default;
    void operator=(const Congestion &) = delete;

    self_type &
    operator=(Cript::string_view const &str)
    {
      TSAssert(_owner);
#if defined(TCP_CONGESTION)
      int connfd = _owner->fd();
      int res    = setsockopt(connfd, IPPROTO_TCP, TCP_CONGESTION, str.data(), str.size());

      // EBADF indicates possible client abort
      if ((res < 0) && (errno != EBADF)) {
        TSError("[Congestion] Error setting  TCP_CONGESTION, errno=%d", errno);
      }
#endif

      return *this;
    }

  private:
    ConnBase *_owner = nullptr;

  }; // End class ConnBase::Congestion

  class Mark
  {
    using self_type = Mark;

  public:
    friend class ConnBase;

    Mark()                       = default;
    void operator=(const Mark &) = delete;

    // Same here, no API in ATS to Get() the mark on a VC.
    operator integer() const { return _val; }

    void
    operator=(int val)
    {
      TSAssert(_owner);
      _owner->setMark(val);
      _val = val;
    }

  private:
    ConnBase *_owner = nullptr;
    integer   _val   = -1;

  }; // End class ConnBase::Mark

  class Geo
  {
    using self_type = Geo;

  public:
    friend class ConnBase;

    Geo()                       = default;
    void operator=(const Geo &) = delete;

    [[nodiscard]] Cript::string ASN() const;
    [[nodiscard]] Cript::string ASNName() const;
    [[nodiscard]] Cript::string Country() const;
    [[nodiscard]] Cript::string CountryCode() const;

  private:
    ConnBase *_owner = nullptr;
  }; // End class Geo::TcpInfo

  class TcpInfo
  {
    using self_type = TcpInfo;

  public:
    friend class ConnBase;

    TcpInfo()                       = default;
    void operator=(const TcpInfo &) = delete;

    Cript::string_view log();

    [[nodiscard]] bool
    ready() const
    {
      return _ready;
    }

// ToDo: Add more member accesses? Tthe underlying info makes it hard to make it cross platform
#if defined(TCP_INFO) && defined(HAVE_STRUCT_TCP_INFO)
    integer
    rtt()
    {
      initialize();
      return (_ready ? info.tcpi_rtt : 0);
    }

    integer
    rto()
    {
      initialize();
      return (_ready ? info.tcpi_rto : 0);
    }

    integer
    snd_cwnd()
    {
      initialize();
      return (_ready ? info.tcpi_snd_cwnd : 0);
    }

    integer
    retrans()
    {
      initialize();
      return (_ready ? info.tcpi_retrans : 0);
    }

    struct tcp_info info;
    socklen_t       info_len = sizeof(info);

#else
    integer
    rtt()
    {
      return 0;
    }

    integer
    rto()
    {
      return 0;
    }

    integer
    snd_cwnd()
    {
      return 0;
    }

    integer
    retrans()
    {
      return 0;
    }

#endif

  private:
    void initialize();

    ConnBase     *_owner = nullptr;
    bool          _ready = false;
    Cript::string _logging; // Storage for the logformat of the tcpinfo

  }; // End class ConnBase::TcpInfo

public:
  void operator=(const ConnBase &) = delete;

  ConnBase() { dscp._owner = congestion._owner = tcpinfo._owner = geo._owner = pacing._owner = mark._owner = this; }

  [[nodiscard]] virtual int fd() const = 0; // This needs the txnp from the Context

  [[nodiscard]] struct sockaddr const *
  socket() const
  {
    TSAssert(_vc);
    return TSNetVConnRemoteAddrGet(_vc);
  }

  [[nodiscard]] Cript::IP
  ip() const
  {
    TSAssert(initialized());
    return Cript::IP{socket()};
  }

  [[nodiscard]] bool
  initialized() const
  {
    return _state != nullptr;
  }

  [[nodiscard]] bool
  isInternal() const
  {
    return TSHttpTxnIsInternal(_state->txnp);
  }

  [[nodiscard]] virtual int count() const    = 0;
  virtual void              setDscp(int val) = 0;
  virtual void              setMark(int val) = 0;
  Dscp                      dscp;
  Congestion                congestion;
  TcpInfo                   tcpinfo;
  Geo                       geo;
  Pacing                    pacing;
  Mark                      mark;

  Cript::string_view string(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);

protected:
  Cript::Transaction    *_state  = nullptr;
  struct sockaddr const *_socket = nullptr;
  TSVConn                _vc     = nullptr;
  char                   _str[INET6_ADDRSTRLEN + 1];

}; // End class ConnBase

namespace Client
{
class Connection : public ConnBase
{
  using super_type = ConnBase;
  using self_type  = Connection;

public:
  Connection() = default;

  void operator=(const Connection &) = delete;
  Connection(const Connection &)     = delete;

  [[nodiscard]] int  fd() const override;
  [[nodiscard]] int  count() const override;
  static Connection &_get(Cript::Context *context);

  void
  setDscp(int val) override
  {
    TSHttpTxnClientPacketDscpSet(_state->txnp, val);
  }

  void
  setMark(int val) override
  {
    TSHttpTxnClientPacketMarkSet(_state->txnp, val);
  }

}; // End class Client::Connection

} // namespace Client

namespace Server
{
class Connection : public ConnBase
{
  using super_type = ConnBase;
  using self_type  = Connection;

public:
  Connection()                       = default;
  void operator=(const Connection &) = delete;
  Connection(const Connection &)     = delete;

  [[nodiscard]] int  fd() const override;
  [[nodiscard]] int  count() const override;
  static Connection &_get(Cript::Context *context);

  void
  setDscp(int val) override
  {
    TSHttpTxnServerPacketDscpSet(_state->txnp, val);
  }

  void
  setMark(int val) override
  {
    TSHttpTxnServerPacketMarkSet(_state->txnp, val);
  }

}; // End class Server::Connection

} // namespace Server

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<Cript::IP> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(Cript::IP &ip, FormatContext &ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", ip.getSV());
  }
};
} // namespace fmt
