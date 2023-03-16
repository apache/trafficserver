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

/*************************** -*- Mod: C++ -*- ******************************
  P_ActionProcessor.h
   Created On      : 05/02/2017

   Description:
   SNI based Configuration in ATS
 ****************************************************************************/
#pragma once

#include "I_EventSystem.h"
#include "P_SSLNextProtocolAccept.h"
#include "P_SSLNetVConnection.h"
#include "SNIActionPerformer.h"
#include "SSLTypes.h"
#include "swoc/TextView.h"

#include "tscore/ink_inet.h"
#include "swoc/TextView.h"

#include <vector>

class ControlH2 : public ActionItem
{
public:
  ControlH2(bool turn_on) : enable_h2(turn_on) {}
  ~ControlH2() override {}

  int
  SNIAction(TLSSNISupport *snis, const Context &ctx) const override
  {
    auto ssl_vc            = dynamic_cast<SSLNetVConnection *>(snis);
    const char *servername = snis->get_sni_server_name();
    if (ssl_vc) {
      if (!enable_h2) {
        ssl_vc->disableProtocol(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);
        Debug("ssl_sni", "H2 disabled, fqdn [%s]", servername);
      } else {
        ssl_vc->enableProtocol(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);
        Debug("ssl_sni", "H2 enabled, fqdn [%s]", servername);
      }
    }
    return SSL_TLSEXT_ERR_OK;
  }

private:
  bool enable_h2 = false;
};

class HTTP2BufferWaterMark : public ActionItem
{
public:
  HTTP2BufferWaterMark(int value) : value(value) {}
  ~HTTP2BufferWaterMark() override {}

  int
  SNIAction(TLSSNISupport *snis, const Context &ctx) const override
  {
    auto ssl_vc = dynamic_cast<SSLNetVConnection *>(snis);
    if (ssl_vc) {
      ssl_vc->hints_from_sni.http2_buffer_water_mark = value;
    }
    return SSL_TLSEXT_ERR_OK;
  }

private:
  int value = -1;
};

class TunnelDestination : public ActionItem
{
  // ID of the configured variable. This will be used to know which function
  // should be called when processing the tunnel destination.
  enum OpId : int32_t {
    DEFAULT = -1,                 // No specific variable set.
    MATCH_GROUPS,                 // Deal with configured groups.
    MAP_WITH_RECV_PORT,           // Use port from inbound local
    MAP_WITH_PROXY_PROTOCOL_PORT, // Use port from the proxy protocol
    MAX                           // Always at the end and do not change the value of the above items.
  };
  static constexpr std::string_view MAP_WITH_RECV_PORT_STR           = "{inbound_local_port}";
  static constexpr std::string_view MAP_WITH_PROXY_PROTOCOL_PORT_STR = "{proxy_protocol_port}";

public:
  TunnelDestination(const std::string_view &dest, SNIRoutingType type, YamlSNIConfig::TunnelPreWarm prewarm,
                    const std::vector<int> &alpn)
    : destination(dest), type(type), tunnel_prewarm(prewarm), alpn_ids(alpn)
  {
    if (destination.find_first_of('$') != std::string::npos) {
      fnArrIndex = OpId::MATCH_GROUPS;
    } else if (var_start_pos = destination.find(MAP_WITH_RECV_PORT_STR); var_start_pos != std::string::npos) {
      fnArrIndex = OpId::MAP_WITH_RECV_PORT;
    } else if (var_start_pos = destination.find(MAP_WITH_PROXY_PROTOCOL_PORT_STR); var_start_pos != std::string::npos) {
      fnArrIndex = OpId::MAP_WITH_PROXY_PROTOCOL_PORT;
    }
  }
  ~TunnelDestination() override {}

  int
  SNIAction(TLSSNISupport *snis, const Context &ctx) const override
  {
    // Set the netvc option?
    SSLNetVConnection *ssl_netvc = dynamic_cast<SSLNetVConnection *>(snis);
    const char *servername       = snis->get_sni_server_name();
    if (ssl_netvc) {
      if (fnArrIndex == OpId::DEFAULT) {
        ssl_netvc->set_tunnel_destination(destination, type, !TLSTunnelSupport::PORT_IS_DYNAMIC, tunnel_prewarm);
        Debug("ssl_sni", "Destination now is [%s], fqdn [%s]", destination.c_str(), servername);
      } else {
        // Dispatch to the correct tunnel destination port function.
        bool port_is_dynamic  = false;
        const auto &fixed_dst = fix_destination[fnArrIndex](destination, var_start_pos, ctx, ssl_netvc, port_is_dynamic);
        ssl_netvc->set_tunnel_destination(fixed_dst, type, port_is_dynamic, tunnel_prewarm);
        Debug("ssl_sni", "Destination now is [%s], configured [%s], fqdn [%s]", fixed_dst.c_str(), destination.c_str(), servername);
      }

      if (type == SNIRoutingType::BLIND) {
        ssl_netvc->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
      }

      // ALPN
      for (int id : alpn_ids) {
        ssl_netvc->enableProtocol(id);
      }
    }

    return SSL_TLSEXT_ERR_OK;
  }

private:
  static bool
  is_number(std::string_view s)
  {
    return !s.empty() &&
           std::find_if(std::begin(s), std::end(s), [](std::string_view::value_type c) { return !std::isdigit(c); }) == std::end(s);
  }

  /**
   * `tunnel_route` may contain matching groups ie: `$1` which needs to be replaced by the corresponding
   * captured group from the `fqdn`, this function will replace them using proper group string. Matching
   * groups could be at any order.
   */
  static std::string
  replace_match_groups(std::string_view dst, const ActionItem::Context::CapturedGroupViewVec &groups, bool &port_is_dynamic)
  {
    port_is_dynamic = false;
    if (dst.empty() || groups.empty()) {
      return std::string{dst};
    }
    std::string real_dst;
    std::string::size_type pos{0};

    const auto end = std::end(dst);
    // We need to split the tunnel string and place each corresponding match on the
    // configured one, so we need to first, get the match, then get the match number
    // making sure that it does exist in the captured group.
    bool is_writing_port = false;
    for (auto c = std::begin(dst); c != end; c++, pos++) {
      if (*c == ':') {
        is_writing_port = true;
      }
      if (*c == '$') {
        // find the next '.' so we can get the group number.
        const auto dot            = dst.find('.', pos);
        std::string::size_type to = std::string::npos;
        if (dot != std::string::npos) {
          to = dot - (pos + 1);
        } else {
          // It may not have a dot, which could be because it's the last part. In that case
          // we should check for the port separator.
          if (const auto port = dst.find(':', pos); port != std::string::npos) {
            to = (port - pos) - 1;
          }
        }
        std::string_view number_str{dst.substr(pos + 1, to)};
        if (!is_number(number_str)) {
          // it may be some issue on the configured string, place the char and keep going.
          real_dst += *c;
          continue;
        }
        const std::size_t group_index = swoc::svtoi(number_str);
        if ((group_index - 1) < groups.size()) {
          // place the captured group.
          real_dst += groups[group_index - 1];
          if (is_writing_port) {
            port_is_dynamic = true;
          }
          // if it was the last match, then ...
          if (dot == std::string::npos && to == std::string::npos) {
            // that's it.
            break;
          }
          pos += number_str.size() + 1;
          std::advance(c, number_str.size() + 1);
        }
        // If there is no match for a specific group, then we keep the `$#` as defined in the string.
      }
      real_dst += *c;
    }

    return real_dst;
  }

  std::string destination;

  /// The start position of a tunnel destination variable, such as '{proxy_protocol_port}'.
  size_t var_start_pos{0};
  SNIRoutingType type                         = SNIRoutingType::NONE;
  YamlSNIConfig::TunnelPreWarm tunnel_prewarm = YamlSNIConfig::TunnelPreWarm::UNSET;
  const std::vector<int> &alpn_ids;

  OpId fnArrIndex{OpId::DEFAULT}; /// On creation, we decide which function needs to be called, set the index and then we
                                  /// call it with the relevant data

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

  int
  SNIAction(TLSSNISupport *snis, const Context &ctx) const override
  {
    auto ssl_vc            = dynamic_cast<SSLNetVConnection *>(snis);
    const char *servername = snis->get_sni_server_name();
    Debug("ssl_sni", "action verify param %d, fqdn [%s]", this->mode, servername);
    setClientCertLevel(ssl_vc->ssl, this->mode);
    ssl_vc->set_ca_cert_file(ca_file, ca_dir);
    setClientCertCACerts(ssl_vc->ssl, ssl_vc->get_ca_cert_file(), ssl_vc->get_ca_cert_dir());

    return SSL_TLSEXT_ERR_OK;
  }

  bool
  TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &policy) const override
  {
    // This action is triggered by a SNI if it was set
    return true;
  }

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

  int
  SNIAction(TLSSNISupport *snis, const Context &ctx) const override
  {
    // On action this doesn't do anything
    return SSL_TLSEXT_ERR_OK;
  }

  bool
  TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &in_policy) const override
  {
    // Update the policy when testing
    in_policy = this->policy;
    // But this action didn't really trigger during the action phase
    return false;
  }
};

class TLSValidProtocols : public ActionItem
{
  bool unset = true;
  unsigned long protocol_mask;

public:
#ifdef SSL_OP_NO_TLSv1_3
  static const unsigned long max_mask = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3;
#else
  static const unsigned long max_mask = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2;
#endif
  TLSValidProtocols() : protocol_mask(max_mask) {}
  TLSValidProtocols(unsigned long protocols) : unset(false), protocol_mask(protocols) {}

  int
  SNIAction(TLSSNISupport *snis, const Context & /* ctx */) const override
  {
    if (!unset) {
      auto ssl_vc            = dynamic_cast<SSLNetVConnection *>(snis);
      const char *servername = snis->get_sni_server_name();
      Debug("ssl_sni", "TLSValidProtocol param 0%x, fqdn [%s]", static_cast<unsigned int>(this->protocol_mask), servername);
      ssl_vc->set_valid_tls_protocols(protocol_mask, TLSValidProtocols::max_mask);
    }

    return SSL_TLSEXT_ERR_OK;
  }
};

class SNI_IpAllow : public ActionItem
{
  IpMap ip_map;

public:
  SNI_IpAllow(std::string &ip_allow_list, const std::string &servername);

  int SNIAction(TLSSNISupport *snis, const Context &ctx) const override;

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

  int
  SNIAction(TLSSNISupport *snis, const Context &ctx) const override
  {
    // TODO: change design to avoid this dynamic_cast
    auto ssl_vc = dynamic_cast<SSLNetVConnection *>(snis);
    if (ssl_vc && !policy.empty()) {
      ssl_vc->options.outbound_sni_policy = policy;
    }
    return SSL_TLSEXT_ERR_OK;
  }

private:
  std::string_view policy{};
};
