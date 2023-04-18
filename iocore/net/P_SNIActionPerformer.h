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

#include "swoc/TextView.h"
#include "swoc/swoc_ip.h"

#include "I_EventSystem.h"
#include "P_SSLNextProtocolAccept.h"
#include "P_SSLNetVConnection.h"
#include "SNIActionPerformer.h"
#include "SSLTypes.h"

#include "tscore/ink_inet.h"

#include "swoc/bwf_base.h"

#include <vector>
#include <optional>

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
  /// Context used for name binding on tunnel destination.
  struct BWContext {
    Context const &_action_ctx; ///< @c ActionItem context.
    SSLNetVConnection *_vc;     ///< Connection object.
  };
  /// The container for bound names.
  using bwf_map_type = swoc::bwf::ContextNames<BWContext const>;
  /// Argument pack for capture groups.
  class CaptureArgs : public swoc::bwf::ArgPack
  {
  public:
    CaptureArgs(std::optional<ActionItem::Context::CapturedGroupViewVec> const &groups)
      : _groups(groups.has_value() ? groups.value() : NO_GROUPS)
    {
    }

    virtual std::any capture(unsigned idx) const override;

    /// Call out from formatting when a replace group is referenced.
    virtual swoc::BufferWriter &print(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, unsigned idx) const override;

    /// Number of arguments in the pack.
    virtual unsigned count() const override;

  private:
    /// Empty group for when no captures exist.
    static inline const ActionItem::Context::CapturedGroupViewVec NO_GROUPS;
    ActionItem::Context::CapturedGroupViewVec const &_groups;
  };

  static constexpr std::string_view MAP_WITH_RECV_PORT_STR           = "inbound_local_port";
  static constexpr std::string_view MAP_WITH_PROXY_PROTOCOL_PORT_STR = "proxy_protocol_port";

public:
  TunnelDestination(const std::string_view &dest, SNIRoutingType type, YamlSNIConfig::TunnelPreWarm prewarm,
                    const std::vector<int> &alpn);
  ~TunnelDestination() override {}

  int SNIAction(TLSSNISupport *snis, const Context &ctx) const override;

  static void static_initialization();

private:
  std::string destination; ///< Persistent storage for format.
  SNIRoutingType type                         = SNIRoutingType::NONE;
  YamlSNIConfig::TunnelPreWarm tunnel_prewarm = YamlSNIConfig::TunnelPreWarm::UNSET;
  const std::vector<int> &alpn_ids;

  std::optional<swoc::bwf::Format> _fmt; ///< Format used to render destination.

  static bwf_map_type bwf_map; ///< Names available in the configuration strings.
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
  int min_ver = -1;
  int max_ver = -1;

public:
#ifdef SSL_OP_NO_TLSv1_3
  static const unsigned long max_mask = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_3;
#else
  static const unsigned long max_mask = SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2;
#endif
  TLSValidProtocols() : protocol_mask(max_mask) {}
  TLSValidProtocols(unsigned long protocols) : unset(false), protocol_mask(protocols) {}
  TLSValidProtocols(int min_ver, int max_ver) : unset(false), protocol_mask(0), min_ver(min_ver), max_ver(max_ver) {}

  int
  SNIAction(TLSSNISupport *snis, const Context & /* ctx */) const override
  {
    if (this->min_ver >= 0 || this->max_ver >= 0) {
      const char *servername = snis->get_sni_server_name();
      Debug("ssl_sni", "TLSValidProtocol min=%d, max=%d, fqdn [%s]", this->min_ver, this->max_ver, servername);
      auto ssl_vc = dynamic_cast<SSLNetVConnection *>(snis);
      ssl_vc->set_valid_tls_version_min(this->min_ver);
      ssl_vc->set_valid_tls_version_max(this->max_ver);
    } else {
      if (!unset) {
        auto ssl_vc            = dynamic_cast<SSLNetVConnection *>(snis);
        const char *servername = snis->get_sni_server_name();
        Debug("ssl_sni", "TLSValidProtocol param 0%x, fqdn [%s]", static_cast<unsigned int>(this->protocol_mask), servername);
        ssl_vc->set_valid_tls_protocols(protocol_mask, TLSValidProtocols::max_mask);
        Warning("valid_tls_versions_in is deprecated. Use valid_tls_version_min_in and ivalid_tls_version_max_in instead.");
      }
    }

    return SSL_TLSEXT_ERR_OK;
  }
};

class SNI_IpAllow : public ActionItem
{
  swoc::IPRangeSet ip_addrs;

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
