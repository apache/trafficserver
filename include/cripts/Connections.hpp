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

namespace cripts
{
class Context;
}

#include "ts/apidefs.h"
#include "ts/ts.h"

#include "cripts/Lulu.hpp"
#include "cripts/Matcher.hpp"

namespace cripts
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
  IP(const self_type &)             = delete;
  void operator=(const self_type &) = delete;

  cripts::string_view GetSV(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);
  cripts::string_view
  string(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128)
  {
    return GetSV(ipv4_cidr, ipv6_cidr);
  }

  uint64_t Hasher(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);
  bool     Sample(double rate, uint32_t seed = 0, unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);

private:
  char     _str[INET6_ADDRSTRLEN + 1];
  uint64_t _hash    = 0;
  uint16_t _sampler = 0;
};

} // namespace cripts

namespace detail
{

class ConnBase
{
  using self_type = ConnBase;
  class Dscp
  {
    using self_type = Dscp;

  public:
    friend class ConnBase;

    Dscp()                            = default;
    void operator=(const self_type &) = delete;

    // This is not perfect, but there's currently no ATS Get() mechanism to see a connections
    // current DSCP options.
    operator integer() const { return _val; }

    void
    operator=(int val)
    {
      TSAssert(_owner);
      _owner->SetDscp(val);
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

    Pacing()                          = default;
    void operator=(const self_type &) = delete;

    // This is not perfect, but there's currently no ATS Get() mechanism to see a connections
    // current PACING options.
    operator integer() const { return _val; }

    void operator=(uint32_t val);

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

    Congestion()                      = default;
    void operator=(const self_type &) = delete;

    self_type &
    operator=([[maybe_unused]] cripts::string_view const &str)
    {
      TSAssert(_owner);
#if defined(TCP_CONGESTION)
      int connfd = _owner->FD();
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

    Mark()                            = default;
    void operator=(const self_type &) = delete;

    // Same here, no API in ATS to Get() the mark on a VC.
    operator integer() const { return _val; }

    void
    operator=(int val)
    {
      TSAssert(_owner);
      _owner->SetMark(val);
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

    Geo()                             = default;
    void operator=(const self_type &) = delete;

    [[nodiscard]] cripts::string ASN() const;
    [[nodiscard]] cripts::string ASNName() const;
    [[nodiscard]] cripts::string Country() const;
    [[nodiscard]] cripts::string CountryCode() const;

  private:
    ConnBase *_owner = nullptr;
  }; // End class ConnBase::Geo

  class TcpInfo
  {
    using self_type = TcpInfo;

  public:
    friend class ConnBase;

    TcpInfo()                         = default;
    TcpInfo(const self_type &)        = delete;
    void operator=(const self_type &) = delete;

    cripts::string_view Log();

    [[nodiscard]] bool
    Ready() const
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

    ConnBase      *_owner = nullptr;
    bool           _ready = false;
    cripts::string _logging; // Storage for the logformat of the tcpinfo

  }; // End class ConnBase::TcpInfo

public:
  ConnBase() { dscp._owner = congestion._owner = tcpinfo._owner = geo._owner = pacing._owner = mark._owner = this; }

  ConnBase(const self_type &)       = delete;
  void operator=(const self_type &) = delete;

  [[nodiscard]] virtual int FD() const = 0; // This needs the txnp from the Context

  [[nodiscard]] struct sockaddr const *
  Socket() const
  {
    TSAssert(_vc);
    return TSNetVConnRemoteAddrGet(_vc);
  }

  [[nodiscard]] cripts::IP
  IP() const
  {
    TSAssert(Initialized());
    return cripts::IP{Socket()};
  }

  [[nodiscard]] bool
  Initialized() const
  {
    return _state != nullptr;
  }

  [[nodiscard]] bool
  IsInternal() const
  {
    return TSHttpTxnIsInternal(_state->txnp);
  }

  [[nodiscard]] virtual cripts::IP LocalIP() const  = 0;
  [[nodiscard]] virtual int        Count() const    = 0;
  virtual void                     SetDscp(int val) = 0;
  virtual void                     SetMark(int val) = 0;

  Dscp       dscp;
  Congestion congestion;
  TcpInfo    tcpinfo;
  Geo        geo;
  Pacing     pacing;
  Mark       mark;

  cripts::string_view string(unsigned ipv4_cidr = 32, unsigned ipv6_cidr = 128);

protected:
  cripts::Transaction   *_state  = nullptr;
  struct sockaddr const *_socket = nullptr;
  TSVConn                _vc     = nullptr;
  char                   _str[INET6_ADDRSTRLEN + 1];

}; // End class ConnBase

} // namespace detail

namespace cripts
{

namespace Client
{
  class Connection : public detail::ConnBase
  {
    using pe        = detail::ConnBase;
    using self_type = Connection;

  public:
    Connection()                      = default;
    Connection(const self_type &)     = delete;
    void operator=(const self_type &) = delete;

    [[nodiscard]] int  FD() const override;
    [[nodiscard]] int  Count() const override;
    static Connection &_get(cripts::Context *context);

    void
    SetDscp(int val) override
    {
      TSHttpTxnClientPacketDscpSet(_state->txnp, val);
    }

    void
    SetMark(int val) override
    {
      TSHttpTxnClientPacketMarkSet(_state->txnp, val);
    }

    [[nodiscard]] cripts::IP
    LocalIP() const override
    {
      return cripts::IP{TSHttpTxnIncomingAddrGet(_state->txnp)};
    }

  }; // End class Client::Connection

} // namespace Client

namespace Server
{
  class Connection : public detail::ConnBase
  {
    using pe        = detail::ConnBase;
    using self_type = Connection;

  public:
    Connection()                      = default;
    Connection(const self_type &)     = delete;
    void operator=(const self_type &) = delete;

    [[nodiscard]] int  FD() const override;
    [[nodiscard]] int  Count() const override;
    static Connection &_get(cripts::Context *context);

    void
    SetDscp(int val) override
    {
      TSHttpTxnServerPacketDscpSet(_state->txnp, val);
    }

    void
    SetMark(int val) override
    {
      TSHttpTxnServerPacketMarkSet(_state->txnp, val);
    }

    [[nodiscard]] cripts::IP
    LocalIP() const override
    {
      return cripts::IP{TSHttpTxnOutgoingAddrGet(_state->txnp)};
    }

  }; // End class Server::Connection

} // namespace Server

} // namespace cripts

// Formatters for {fmt}
namespace fmt
{
template <> struct formatter<cripts::IP> {
  constexpr auto
  parse(format_parse_context &ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(cripts::IP &ip, FormatContext &ctx) -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", ip.GetSV());
  }
};
} // namespace fmt
