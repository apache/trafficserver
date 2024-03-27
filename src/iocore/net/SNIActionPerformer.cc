/** @file

  Implementation of SNIActionPerformer

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

#include "swoc/swoc_file.h"
#include "swoc/BufferWriter.h"
#include "tscore/Layout.h"

#include "SNIActionPerformer.h"

#include "P_SSLNetVConnection.h"

#if TS_USE_QUIC == 1
#include "P_QUICNetVConnection.h"
#endif

int
ControlQUIC::SNIAction(SSL &ssl, const Context &ctx) const
{
#if TS_USE_QUIC == 1
  if (enable_quic) {
    return SSL_TLSEXT_ERR_OK;
  }

  // This action is only available for QUIC connections
  if (QUICSupport::getInstance(&ssl) == nullptr) {
    return SSL_TLSEXT_ERR_OK;
  }

  if (dbg_ctl_ssl_sni.on()) {
    if (auto snis = TLSSNISupport::getInstance(&ssl)) {
      const char *servername = snis->get_sni_server_name();
      Dbg(dbg_ctl_ssl_sni, "Rejecting handshake due to QUIC being disabled for fqdn [%s]", servername);
    }
  }

  return SSL_TLSEXT_ERR_ALERT_FATAL;
#else
  return SSL_TLSEXT_ERR_OK;
#endif
}

int
ControlH2::SNIAction(SSL &ssl, const Context &ctx) const
{
  auto snis  = TLSSNISupport::getInstance(&ssl);
  auto alpns = ALPNSupport::getInstance(&ssl);

  if (snis == nullptr || alpns == nullptr) {
    return SSL_TLSEXT_ERR_OK;
  }

  const char *servername = snis->get_sni_server_name();
  if (!enable_h2) {
    alpns->disableProtocol(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);
    Dbg(dbg_ctl_ssl_sni, "H2 disabled, fqdn [%s]", servername);
  } else {
    alpns->enableProtocol(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);
    Dbg(dbg_ctl_ssl_sni, "H2 enabled, fqdn [%s]", servername);
  }
  return SSL_TLSEXT_ERR_OK;
}

int
HTTP2BufferWaterMark::SNIAction(SSL &ssl, const Context &ctx) const
{
  if (auto snis = TLSSNISupport::getInstance(&ssl)) {
    snis->hints_from_sni.http2_buffer_water_mark = value;
  }
  return SSL_TLSEXT_ERR_OK;
}

int
HTTP2InitialWindowSizeIn::SNIAction(SSL &ssl, const Context &ctx) const
{
  if (auto snis = TLSSNISupport::getInstance(&ssl)) {
    snis->hints_from_sni.http2_initial_window_size_in = value;
  }
  return SSL_TLSEXT_ERR_OK;
}

int
HTTP2MaxSettingsFramesPerMinute::SNIAction(SSL &ssl, const Context &ctx) const
{
  if (auto snis = TLSSNISupport::getInstance(&ssl)) {
    snis->hints_from_sni.http2_max_settings_frames_per_minute = value;
  }
  return SSL_TLSEXT_ERR_OK;
}

int
HTTP2MaxPingFramesPerMinute::SNIAction(SSL &ssl, const Context &ctx) const
{
  if (auto snis = TLSSNISupport::getInstance(&ssl)) {
    snis->hints_from_sni.http2_max_ping_frames_per_minute = value;
  }
  return SSL_TLSEXT_ERR_OK;
}

int
HTTP2MaxPriorityFramesPerMinute::SNIAction(SSL &ssl, const Context &ctx) const
{
  if (auto snis = TLSSNISupport::getInstance(&ssl)) {
    snis->hints_from_sni.http2_max_priority_frames_per_minute = value;
  }
  return SSL_TLSEXT_ERR_OK;
}

int
HTTP2MaxRstStreamFramesPerMinute::SNIAction(SSL &ssl, const Context &ctx) const
{
  if (auto snis = TLSSNISupport::getInstance(&ssl)) {
    snis->hints_from_sni.http2_max_rst_stream_frames_per_minute = value;
  }
  return SSL_TLSEXT_ERR_OK;
}

TunnelDestination::TunnelDestination(const std::string_view &dest, SNIRoutingType type, YamlSNIConfig::TunnelPreWarm prewarm,
                                     const std::vector<int> &alpn)
  : destination(dest), type(type), tunnel_prewarm(prewarm), alpn_ids(alpn)
{
  // Check for port variable specification. Note that this is checked before
  // the match group so that the corresponding function can be applied before
  // the match group expansion(when the var_start_pos is still accurate).
  auto recv_port_start_pos = destination.find(MAP_WITH_RECV_PORT_STR);
  auto pp_port_start_pos   = destination.find(MAP_WITH_PROXY_PROTOCOL_PORT_STR);
  bool has_recv_port_var   = recv_port_start_pos != std::string::npos;
  bool has_pp_port_var     = pp_port_start_pos != std::string::npos;
  if (has_recv_port_var && has_pp_port_var) {
    Error("Invalid destination \"%.*s\" in SNI configuration - Only one port variable can be specified.",
          static_cast<int>(destination.size()), destination.data());
  } else if (has_recv_port_var) {
    fnArrIndexes.push_back(OpId::MAP_WITH_RECV_PORT);
    var_start_pos = recv_port_start_pos;
  } else if (has_pp_port_var) {
    fnArrIndexes.push_back(OpId::MAP_WITH_PROXY_PROTOCOL_PORT);
    var_start_pos = pp_port_start_pos;
  }
  // Check for match groups as well.
  if (destination.find_first_of('$') != std::string::npos) {
    fnArrIndexes.push_back(OpId::MATCH_GROUPS);
  }
}

int
TunnelDestination::SNIAction(SSL &ssl, const Context &ctx) const
{
  auto snis      = TLSSNISupport::getInstance(&ssl);
  auto tuns      = TLSTunnelSupport::getInstance(&ssl);
  auto alpns     = ALPNSupport::getInstance(&ssl);
  auto ssl_netvc = SSLNetVCAccess(&ssl);

  if (snis == nullptr || tuns == nullptr || alpns == nullptr || ssl_netvc == nullptr) {
    return SSL_TLSEXT_ERR_OK;
  }

  const char *servername = snis->get_sni_server_name();
  if (fnArrIndexes.empty()) {
    tuns->set_tunnel_destination(destination, type, !TLSTunnelSupport::PORT_IS_DYNAMIC, tunnel_prewarm);
    Debug("ssl_sni", "Destination now is [%s], fqdn [%s]", destination.c_str(), servername);
  } else {
    bool port_is_dynamic = false;
    auto fixed_dst{destination};
    // Apply mapping functions to get the final destination.
    for (auto fnArrIndex : fnArrIndexes) {
      // Dispatch to the correct tunnel destination port function.
      fixed_dst = fix_destination[fnArrIndex](fixed_dst, var_start_pos, ctx, ssl_netvc, port_is_dynamic);
    }
    tuns->set_tunnel_destination(fixed_dst, type, port_is_dynamic, tunnel_prewarm);
    Debug("ssl_sni", "Destination now is [%s], configured [%s], fqdn [%s]", fixed_dst.c_str(), destination.c_str(), servername);
  }

  if (type == SNIRoutingType::BLIND) {
    ssl_netvc->attributes = HttpProxyPort::TRANSPORT_BLIND_TUNNEL;
  }

  // ALPN
  for (int id : alpn_ids) {
    alpns->enableProtocol(id);
  }

  return SSL_TLSEXT_ERR_OK;
}

bool
TunnelDestination::is_number(std::string_view s)
{
  return !s.empty() &&
         std::find_if(std::begin(s), std::end(s), [](std::string_view::value_type c) { return !std::isdigit(c); }) == std::end(s);
}

/**
 * `tunnel_route` may contain matching groups ie: `$1` which needs to be replaced by the corresponding
 * captured group from the `fqdn`, this function will replace them using proper group string. Matching
 * groups could be at any order.
 */
std::string
TunnelDestination::replace_match_groups(std::string_view dst, const ActionItem::Context::CapturedGroupViewVec &groups,
                                        bool &port_is_dynamic)
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

int
VerifyClient::SNIAction(SSL &ssl, const Context &ctx) const
{
  auto snis   = TLSSNISupport::getInstance(&ssl);
  auto ssl_vc = SSLNetVCAccess(&ssl);

  if (snis == nullptr || ssl_vc == nullptr) {
    return SSL_TLSEXT_ERR_OK;
  }

  const char *servername = snis->get_sni_server_name();
  Dbg(dbg_ctl_ssl_sni, "action verify param %d, fqdn [%s]", this->mode, servername);
  setClientCertLevel(ssl_vc->ssl, this->mode);
  ssl_vc->set_ca_cert_file(ca_file, ca_dir);
  setClientCertCACerts(ssl_vc->ssl, ssl_vc->get_ca_cert_file(), ssl_vc->get_ca_cert_dir());

  return SSL_TLSEXT_ERR_OK;
}

bool
VerifyClient::TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &policy) const
{
  // This action is triggered by a SNI if it was set
  return true;
}

int
HostSniPolicy::SNIAction(SSL &ssl, const Context &ctx) const
{
  // On action this doesn't do anything
  return SSL_TLSEXT_ERR_OK;
}

bool
HostSniPolicy::TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &in_policy) const
{
  // Update the policy when testing
  in_policy = this->policy;
  // But this action didn't really trigger during the action phase
  return false;
}

TLSValidProtocols::TLSValidProtocols(unsigned long protocols) : unset(false), protocol_mask(protocols)
{
  Warning("valid_tls_versions_in is deprecated. Use valid_tls_version_min_in and ivalid_tls_version_max_in instead.");
}

int
TLSValidProtocols::SNIAction(SSL &ssl, const Context & /* ctx */) const
{
  auto snis = TLSSNISupport::getInstance(&ssl);
  auto tbs  = TLSBasicSupport::getInstance(&ssl);

  if (snis == nullptr || tbs == nullptr) {
    return SSL_TLSEXT_ERR_OK;
  }

  if (this->min_ver >= 0 || this->max_ver >= 0) {
    const char *servername = snis->get_sni_server_name();
    Dbg(dbg_ctl_ssl_sni, "TLSValidProtocol min=%d, max=%d, fqdn [%s]", this->min_ver, this->max_ver, servername);
    tbs->set_valid_tls_version_min(this->min_ver);
    tbs->set_valid_tls_version_max(this->max_ver);
  } else {
    if (!unset) {
      const char *servername = snis->get_sni_server_name();
      Dbg(dbg_ctl_ssl_sni, "TLSValidProtocol param 0%x, fqdn [%s]", static_cast<unsigned int>(this->protocol_mask), servername);
      tbs->set_valid_tls_protocols(protocol_mask, TLSValidProtocols::max_mask);
    }
  }

  return SSL_TLSEXT_ERR_OK;
}

SNI_IpAllow::SNI_IpAllow(std::string &ip_allow_list, std::string const &servername) : server_name(servername)
{
  swoc::TextView content{ip_allow_list};
  if (content && content[0] == '@') {
    std::error_code ec;
    swoc::file::path path{content.remove_prefix(1)};
    if (path.is_relative()) {
      path = swoc::file::path(Layout::get()->sysconfdir) / path;
    }
    ip_allow_list = swoc::file::load(path, ec);
    if (ec) {
      swoc::LocalBufferWriter<1024> w;
      w.print("SNIConfig unable to load file {} - {}", path.string(), ec);
      Warning("%.*s", int(w.size()), w.data());
    }
  }
  this->load(ip_allow_list, servername);
}

void
SNI_IpAllow::load(swoc::TextView content, swoc::TextView server_name)
{
  static constexpr swoc::TextView delim{",\n"};

  while (!content.ltrim(delim).empty()) {
    swoc::TextView token{content.take_prefix_at(delim)};
    if (swoc::IPRange r; r.load(token)) {
      Dbg(dbg_ctl_ssl_sni, "%.*s added to the ip_allow token %.*s", static_cast<int>(token.size()), token.data(),
          int(server_name.size()), server_name.data());
      ip_addrs.fill(r);
    } else {
      Dbg(dbg_ctl_ssl_sni, "%.*s is not a valid format", static_cast<int>(token.size()), token.data());
      break;
    }
  }
}

int
SNI_IpAllow::SNIAction(SSL &ssl, ActionItem::Context const &ctx) const
{
  // i.e, ip filtering is not required
  if (ip_addrs.count() == 0) {
    return SSL_TLSEXT_ERR_OK;
  }

  auto ssl_vc = SSLNetVCAccess(&ssl);
  auto ip     = swoc::IPAddr(ssl_vc->get_remote_endpoint());

  // check the allowed ips
  if (ip_addrs.contains(ip)) {
    return SSL_TLSEXT_ERR_OK;
  } else {
    swoc::LocalBufferWriter<256> w;
    w.print("{} is not allowed for {} - denying connection\0", ip, server_name);
    Dbg(dbg_ctl_ssl_sni, "%s", w.data());
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
}

bool
SNI_IpAllow::TestClientSNIAction(char const *servrername, IpEndpoint const &ep, int &policy) const
{
  return ip_addrs.contains(swoc::IPAddr(ep));
}

int
OutboundSNIPolicy::SNIAction(SSL &ssl, const Context &ctx) const
{
  if (!policy.empty()) {
    if (auto snis = TLSSNISupport::getInstance(&ssl)) {
      snis->hints_from_sni.outbound_sni_policy = policy;
    }
  }
  return SSL_TLSEXT_ERR_OK;
}

int
ServerMaxEarlyData::SNIAction(SSL &ssl, const Context &ctx) const
{
#if TS_HAS_TLS_EARLY_DATA
  auto snis = TLSSNISupport::getInstance(&ssl);
  auto eds  = TLSEarlyDataSupport::getInstance(&ssl);

  if (snis == nullptr || eds == nullptr) {
    return SSL_TLSEXT_ERR_OK;
  }

  snis->hints_from_sni.server_max_early_data = server_max_early_data;
  const uint32_t EARLY_DATA_DEFAULT_SIZE     = 16384;
  const uint32_t server_recv_max_early_data =
    server_max_early_data > 0 ? std::max(server_max_early_data, EARLY_DATA_DEFAULT_SIZE) : 0;
  eds->update_early_data_config(&ssl, server_max_early_data, server_recv_max_early_data);
#endif
  return SSL_TLSEXT_ERR_OK;
}
