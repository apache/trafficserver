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
#include <vector>
#include "P_SSLNextProtocolAccept.h"
#include "tscore/ink_inet.h"

class ActionItem
{
public:
  /**
   * Context should contain extra data needed to be passed to the actual SNIAction.
   */
  struct Context {
    /**
     * if any, fqdn_wildcard_captured_groups will hold the captured groups from the `fqdn`
     * match which will be used to construct the tunnel destination.
     */
    std::optional<std::vector<std::string>> _fqdn_wildcard_captured_groups;
  };

  virtual int SNIAction(Continuation *cont, const Context &ctx) const = 0;

  /**
    This method tests whether this action would have been triggered by a
    particuarly SNI value and IP address combination.  This is run after the
    TLS exchange finished to see if the client used an SNI name different from
    the host name to avoid SNI-based policy
  */
  virtual bool
  TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &policy) const
  {
    return false;
  }
  virtual ~ActionItem(){};
};

class ControlH2 : public ActionItem
{
public:
  ControlH2(bool turn_on) : enable_h2(turn_on) {}
  ~ControlH2() override {}

  int
  SNIAction(Continuation *cont, const Context &ctx) const override
  {
    auto ssl_vc = dynamic_cast<SSLNetVConnection *>(cont);
    if (ssl_vc) {
      if (!enable_h2) {
        ssl_vc->disableProtocol(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);
      } else {
        ssl_vc->enableProtocol(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);
      }
    }
    return SSL_TLSEXT_ERR_OK;
  }

private:
  bool enable_h2 = false;
};

class TunnelDestination : public ActionItem
{
public:
  TunnelDestination(const std::string_view &dest, bool decrypt, bool tls_upstream)
    : destination(dest), tunnel_decrypt(decrypt), tls_upstream(tls_upstream)
  {
    need_fix = (destination.find_first_of('$') != std::string::npos);
  }
  ~TunnelDestination() override {}

  int
  SNIAction(Continuation *cont, const Context &ctx) const override
  {
    // Set the netvc option?
    SSLNetVConnection *ssl_netvc = dynamic_cast<SSLNetVConnection *>(cont);
    if (ssl_netvc) {
      // If needed, we will try to amend the tunnel destination.
      if (ctx._fqdn_wildcard_captured_groups && need_fix) {
        const auto &fixed_dst = replace_match_groups(destination, *ctx._fqdn_wildcard_captured_groups);
        ssl_netvc->set_tunnel_destination(fixed_dst, tunnel_decrypt, tls_upstream);
        Debug("TunnelDestination", "Destination now is [%s], configured [%s]", fixed_dst.c_str(), destination.c_str());
      } else {
        ssl_netvc->set_tunnel_destination(destination, tunnel_decrypt, tls_upstream);
      }
    }
    return SSL_TLSEXT_ERR_OK;
  }

private:
  bool
  is_number(const std::string &s) const
  {
    return !s.empty() &&
           std::find_if(std::begin(s), std::end(s), [](std::string::value_type c) { return !std::isdigit(c); }) == std::end(s);
  }

  /**
   * `tunnel_route` may contain matching groups ie: `$1` which needs to be replaced by the corresponding
   * captured group from the `fqdn`, this function will replace them using proper group string. Matching
   * groups could be at any order.
   */
  std::string
  replace_match_groups(const std::string &dst, const std::vector<std::string> &groups) const
  {
    if (dst.empty() || groups.empty()) {
      return dst;
    }
    std::string real_dst;
    std::string::size_type pos{0};

    const auto end = std::end(dst);
    // We need to split the tunnel string and place each corresponding match on the
    // configured one, so we need to first, get the match, then get the match number
    // making sure that it does exist in the captured group.
    for (auto c = std::begin(dst); c != end; c++, pos++) {
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
        const auto &number_str = dst.substr(pos + 1, to);
        if (!is_number(number_str)) {
          // it may be some issue on the configured string, place the char and keep going.
          real_dst += *c;
          continue;
        }
        const std::size_t group_index = std::stoi(number_str);
        if ((group_index - 1) < groups.size()) {
          // place the captured group.
          real_dst += groups[group_index - 1];
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

  bool tunnel_decrypt;
  bool need_fix;
  bool tls_upstream;
};

class VerifyClient : public ActionItem
{
  uint8_t mode;

public:
  VerifyClient(const char *param) : mode(atoi(param)) {}
  VerifyClient(uint8_t param) : mode(param) {}
  ~VerifyClient() override {}
  int
  SNIAction(Continuation *cont, const Context &ctx) const override
  {
    auto ssl_vc = dynamic_cast<SSLNetVConnection *>(cont);
    Debug("ssl_sni", "action verify param %d", this->mode);
    setClientCertLevel(ssl_vc->ssl, this->mode);
    return SSL_TLSEXT_ERR_OK;
  }
  bool
  TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &policy) const override
  {
    // This action is triggered by a SNI if it was set
    return true;
  }
};

class HostSniPolicy : public ActionItem
{
  uint8_t policy;

public:
  HostSniPolicy(const char *param) : policy(atoi(param)) {}
  HostSniPolicy(uint8_t param) : policy(param) {}
  ~HostSniPolicy() override {}
  int
  SNIAction(Continuation *cont, const Context &ctx) const override
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
  SNIAction(Continuation *cont, const Context &ctx) const override
  {
    if (!unset) {
      auto ssl_vc = dynamic_cast<SSLNetVConnection *>(cont);
      Debug("ssl_sni", "TLSValidProtocol param 0%x", static_cast<unsigned int>(this->protocol_mask));
      ssl_vc->protocol_mask_set = true;
      ssl_vc->protocol_mask     = protocol_mask;
    }
    return SSL_TLSEXT_ERR_OK;
  }
  bool
  TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &policy) const override
  {
    return !unset;
  }
};

class SNI_IpAllow : public ActionItem
{
  IpMap ip_map;

public:
  SNI_IpAllow(std::string &ip_allow_list, const std::string &servername)
  {
    // the server identified by item.fqdn requires ATS to do IP filtering
    if (ip_allow_list.length()) {
      IpAddr addr1;
      IpAddr addr2;
      // check format first
      // check if the input is a comma separated list of IPs
      ts::TextView content(ip_allow_list);
      while (!content.empty()) {
        ts::TextView list{content.take_prefix_at(',')};
        if (0 != ats_ip_range_parse(list, addr1, addr2)) {
          Debug("ssl_sni", "%.*s is not a valid format", static_cast<int>(list.size()), list.data());
          break;
        } else {
          Debug("ssl_sni", "%.*s added to the ip_allow list %s", static_cast<int>(list.size()), list.data(), servername.c_str());
          ip_map.fill(IpEndpoint().assign(addr1), IpEndpoint().assign(addr2), reinterpret_cast<void *>(1));
        }
      }
    }
  } // end function SNI_IpAllow

  int
  SNIAction(Continuation *cont, const Context &ctx) const override
  {
    // i.e, ip filtering is not required
    if (ip_map.count() == 0) {
      return SSL_TLSEXT_ERR_OK;
    }

    auto ssl_vc = dynamic_cast<SSLNetVConnection *>(cont);
    auto ip     = ssl_vc->get_remote_endpoint();

    // check the allowed ips
    if (ip_map.contains(ip)) {
      return SSL_TLSEXT_ERR_OK;
    } else {
      char buff[256];
      ats_ip_ntop(&ip.sa, buff, sizeof(buff));
      Debug("ssl_sni", "%s is not allowed. Denying connection", buff);
      return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
  }
  bool
  TestClientSNIAction(const char *servrername, const IpEndpoint &ep, int &policy) const override
  {
    bool retval = false;
    if (ip_map.contains(ep)) {
      retval = true;
    }
    return retval;
  }
};
