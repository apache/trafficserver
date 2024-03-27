/** @file

  SNI based Configuration in ATS

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

#include "swoc/TextView.h"
#include "swoc/swoc_ip.h"

#include "iocore/eventsystem/EventSystem.h"
#include "iocore/net/SNIActionItem.h"
#include "iocore/net/SSLTypes.h"
#include "iocore/net/YamlSNIConfig.h"

#include "tscore/ink_inet.h"

#include <vector>

class SSLNetVConnection;

class ControlQUIC : public ActionItem
{
public:
#if TS_USE_QUIC == 1
  ControlQUIC(bool turn_on) : enable_quic(turn_on) {}
#else
  ControlQUIC(bool turn_on) {}
#endif
  ~ControlQUIC() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
#if TS_USE_QUIC == 1
  bool enable_quic = false;
#endif
};

class ControlH2 : public ActionItem
{
public:
  ControlH2(bool turn_on) : enable_h2(turn_on) {}
  ~ControlH2() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  bool enable_h2 = false;
};

class HTTP2BufferWaterMark : public ActionItem
{
public:
  HTTP2BufferWaterMark(int value) : value(value) {}
  ~HTTP2BufferWaterMark() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  int value = -1;
};

class HTTP2InitialWindowSizeIn : public ActionItem
{
public:
  HTTP2InitialWindowSizeIn(int value) : value(value) {}
  ~HTTP2InitialWindowSizeIn() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  int value = -1;
};

class HTTP2MaxSettingsFramesPerMinute : public ActionItem
{
public:
  HTTP2MaxSettingsFramesPerMinute(int value) : value(value) {}
  ~HTTP2MaxSettingsFramesPerMinute() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  int value = -1;
};

class HTTP2MaxPingFramesPerMinute : public ActionItem
{
public:
  HTTP2MaxPingFramesPerMinute(int value) : value(value) {}
  ~HTTP2MaxPingFramesPerMinute() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  int value = -1;
};

class HTTP2MaxPriorityFramesPerMinute : public ActionItem
{
public:
  HTTP2MaxPriorityFramesPerMinute(int value) : value(value) {}
  ~HTTP2MaxPriorityFramesPerMinute() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  int value = -1;
};

class HTTP2MaxRstStreamFramesPerMinute : public ActionItem
{
public:
  HTTP2MaxRstStreamFramesPerMinute(int value) : value(value) {}
  ~HTTP2MaxRstStreamFramesPerMinute() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  int value = -1;
};

class TunnelDestination : public ActionItem
{
  // ID of the configured variable. This will be used to know which function
  // should be called when processing the tunnel destination.
  enum OpId : int32_t {
    MATCH_GROUPS,                 // Deal with configured groups.
    MAP_WITH_RECV_PORT,           // Use port from inbound local
    MAP_WITH_PROXY_PROTOCOL_PORT, // Use port from the proxy protocol
    MAX                           // Always at the end and do not change the value of the above items.
  };
  static constexpr std::string_view MAP_WITH_RECV_PORT_STR           = "{inbound_local_port}";
  static constexpr std::string_view MAP_WITH_PROXY_PROTOCOL_PORT_STR = "{proxy_protocol_port}";

public:
  TunnelDestination(const std::string_view &dest, SNIRoutingType type, YamlSNIConfig::TunnelPreWarm prewarm,
                    const std::vector<int> &alpn);
  ~TunnelDestination() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  static bool is_number(std::string_view s);

  /**
   * `tunnel_route` may contain matching groups ie: `$1` which needs to be replaced by the corresponding
   * captured group from the `fqdn`, this function will replace them using proper group string. Matching
   * groups could be at any order.
   */
  static std::string replace_match_groups(std::string_view dst, const ActionItem::Context::CapturedGroupViewVec &groups,
                                          bool &port_is_dynamic);

  std::string destination;

  /// The start position of a tunnel destination variable, such as '{proxy_protocol_port}'.
  size_t var_start_pos{0};
  SNIRoutingType type                         = SNIRoutingType::NONE;
  YamlSNIConfig::TunnelPreWarm tunnel_prewarm = YamlSNIConfig::TunnelPreWarm::UNSET;
  const std::vector<int> &alpn_ids;

  /** The indexes of the mapping functions that need to be called. On
  creation, we decide which functions need to be called, add the coressponding
  indexes and then we call those functions with the relevant data.
  */
  std::vector<OpId> fnArrIndexes;

  /// tunnel_route destination callback array.
  static std::array<std::function<std::string(std::string_view,    // destination view
                                              size_t,              // The start position for any relevant tunnel_route variable.
                                              const Context &,     // Context
                                              SSLNetVConnection *, // Net vc to get the port.
                                              bool &               // Whether the port is derived from information on the wire.
                                              )>,
                    OpId::MAX>
    fix_destination;
};

class VerifyClient : public ActionItem
{
  uint8_t mode;
  std::string ca_file;
  std::string ca_dir;

public:
  VerifyClient(uint8_t param, std::string_view file, std::string_view dir) : mode(param), ca_file(file), ca_dir(dir) {}
  VerifyClient(const char *param, std::string_view file, std::string_view dir) : VerifyClient(atoi(param), file, dir) {}
  ~VerifyClient() override;

  int SNIAction(SSL &ssl, const Context &ctx) const override;

  bool TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &policy) const override;

  // No copying or moving.
  VerifyClient(VerifyClient const &)            = delete;
  VerifyClient &operator=(VerifyClient const &) = delete;
};

class HostSniPolicy : public ActionItem
{
  uint8_t policy;

public:
  HostSniPolicy(const char *param) : policy(atoi(param)) {}
  HostSniPolicy(uint8_t param) : policy(param) {}
  ~HostSniPolicy() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

  bool TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &in_policy) const override;
};

class TLSValidProtocols : public ActionItem
{
  bool unset = true;
  unsigned long protocol_mask;
  int min_ver = -1;
  int max_ver = -1;

public:
#ifdef SSL_OP_NO_TLSv1_3
  static const unsigned long max_mask = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3;
#else
  static const unsigned long max_mask = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2;
#endif
  TLSValidProtocols() : protocol_mask(max_mask) {}
  TLSValidProtocols(unsigned long protocols);
  TLSValidProtocols(int min_ver, int max_ver) : unset(false), protocol_mask(0), min_ver(min_ver), max_ver(max_ver) {}

  int SNIAction(SSL &ssl, const Context & /* ctx */) const override;
};

class SNI_IpAllow : public ActionItem
{
  swoc::IPRangeSet ip_addrs;
  std::string server_name;

public:
  SNI_IpAllow(std::string &ip_allow_list, const std::string &servername);

  int SNIAction(SSL &ssl, const Context &ctx) const override;

  bool TestClientSNIAction(const char *servrername, const IpEndpoint &ep, int &policy) const override;

protected:
  /** Load the map from @a text.
   *
   * @param content A list of IP addresses in text form, separated by commas or newlines.
   * @param server_name Server named, used only for debugging messages.
   */
  void load(swoc::TextView content, swoc::TextView server_name);
};

/**
   Override proxy.config.ssl.client.sni_policy by client_sni_policy in sni.yaml
 */
class OutboundSNIPolicy : public ActionItem
{
public:
  OutboundSNIPolicy(const std::string_view &p) : policy(p) {}
  ~OutboundSNIPolicy() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

private:
  std::string_view policy{};
};

class ServerMaxEarlyData : public ActionItem
{
public:
  ServerMaxEarlyData(uint32_t value)
#if TS_HAS_TLS_EARLY_DATA
    : server_max_early_data(value)
#endif
  {
  }
  ~ServerMaxEarlyData() override {}

  int SNIAction(SSL &ssl, const Context &ctx) const override;

#if TS_HAS_TLS_EARLY_DATA
private:
  uint32_t server_max_early_data = 0;
#endif
};
