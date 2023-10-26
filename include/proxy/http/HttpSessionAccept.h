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

#pragma once

#include "tscore/ink_platform.h"
#include "records/RecHttp.h"
#include "iocore/eventsystem/EventSystem.h"
#include "proxy/http/HttpConfig.h"
#include "proxy/hdrs/HTTP.h"
#include "iocore/net/Net.h"
#include "iocore/net/SessionAccept.h"

namespace detail
{
/** Options for @c HttpSessionAccept.

    @internal This is done as a separate class for two reasons.

    The first is that in current usage many instances are created
    with the same options so (for the client) this is easier and
    more efficient than passing options directly to the @c
    HttpSessionAccept or calling setters.

    The second is that @c HttpSessionAccept is not provided with any thread
    safety because it is intended as an immutable object. Putting
    the setters here and not there makes that clearer.

    We don't do this directly as nested class because we want to
    inherit the data members rather than duplicate the declarations
    and initializations.
 */
class HttpSessionAcceptOptions
{
private:
  using self = HttpSessionAcceptOptions; ///< Self reference type.
public:
  HttpSessionAcceptOptions();

  // Connection type (HttpProxyPort::TransportType)
  int transport_type = 0;
  /// Set the transport type.
  self &setTransportType(int);
  /// Local address to bind for outbound connections.
  ts::IPAddrPair outbound;
  /// Set the outbound IP address to @a ip.
  self &setOutboundIp(IpAddr &ip);
  /// Set the outbound IP address to @a ip.
  self &setOutboundIp(IpEndpoint *ip);
  /// Local port for outbound connection.
  uint16_t outbound_port = 0;
  /// Set outbound port.
  self &setOutboundPort(uint16_t);
  /// Outbound transparent.
  bool f_outbound_transparent = false;
  /// Set outbound transparency.
  self &setOutboundTransparent(bool);
  /// Transparent pass-through.
  bool f_transparent_passthrough = false;
  /// Set transparent passthrough.
  self &setTransparentPassthrough(bool);
  /// Host address resolution preference order.
  HostResPreferenceOrder host_res_preference;
  /// Set the host query preference.
  self &setHostResPreference(HostResPreferenceOrder const &);
  /// Acceptable session protocols.
  SessionProtocolSet session_protocol_preference;
  /// Set the session protocol preference.
  self &setSessionProtocolPreference(SessionProtocolSet const &);
};

inline HttpSessionAcceptOptions::HttpSessionAcceptOptions()
{
  host_res_preference = host_res_default_preference_order;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setTransportType(int type)
{
  transport_type = type;
  return *this;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setOutboundIp(IpAddr &ip)
{
  if (ip.isIp4())
    outbound = swoc::IP4Addr(ip._addr._ip4);
  else if (ip.isIp6())
    outbound = swoc::IP6Addr(ip._addr._ip6);
  return *this;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setOutboundIp(IpEndpoint *ip)
{
  outbound = swoc::IPAddr(&(ip->sa));
  return *this;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setOutboundPort(uint16_t port)
{
  outbound_port = port;
  return *this;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setOutboundTransparent(bool flag)
{
  f_outbound_transparent = flag;
  return *this;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setTransparentPassthrough(bool flag)
{
  f_transparent_passthrough = flag;
  return *this;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setHostResPreference(HostResPreferenceOrder const &order)
{
  host_res_preference = order;
  return *this;
}

inline HttpSessionAcceptOptions &
HttpSessionAcceptOptions::setSessionProtocolPreference(SessionProtocolSet const &sp_set)
{
  session_protocol_preference = sp_set;
  return *this;
}
} // namespace detail

/**
   The continuation mutex is NULL to allow parellel accepts in NT. No
   state is recorded by the handler and values are required to be set
   during construction via the @c Options struct and never changed. So
   a NULL mutex is safe.

   Most of the state is simply passed on to the @c ClientSession after
   an accept. It is done here because this is the least bad pathway
   from the top level configuration to the HTTP session.
*/

class HttpSessionAccept : public SessionAccept, private detail::HttpSessionAcceptOptions
{
private:
  using self = HttpSessionAccept; ///< Self reference type.
public:
  /** Construction options.
      Provide an easier to remember typedef for clients.
  */
  using Options = detail::HttpSessionAcceptOptions;

  /** Default constructor.
      @internal We don't use a static default options object because of
      initialization order issues. It is important to pick up data that is read
      from the config file and a static is initialized long before that point.
  */
  HttpSessionAccept(Options const &opt = Options()) : SessionAccept(nullptr), detail::HttpSessionAcceptOptions(opt) // copy these.
  {
    SET_HANDLER(&HttpSessionAccept::mainEvent);
    return;
  }

  ~HttpSessionAccept() override { return; }
  bool accept(NetVConnection *, MIOBuffer *, IOBufferReader *) override;
  int mainEvent(int event, void *netvc) override;

  // noncopyable
  HttpSessionAccept(const HttpSessionAccept &)            = delete;
  HttpSessionAccept &operator=(const HttpSessionAccept &) = delete;
};
