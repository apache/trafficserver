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
  virtual int SNIAction(Continuation *cont) const = 0;
  virtual ~ActionItem(){};
};

class ControlH2 : public ActionItem
{
public:
  ControlH2(bool turn_on) : enable_h2(turn_on) {}
  ~ControlH2() override {}

  int
  SNIAction(Continuation *cont) const override
  {
    auto ssl_vc = dynamic_cast<SSLNetVConnection *>(cont);
    if (ssl_vc) {
      if (!enable_h2) {
        ssl_vc->disableProtocol(TS_ALPN_PROTOCOL_INDEX_HTTP_2_0);
      } else if (enable_h2) {
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
  TunnelDestination(const std::string_view &dest, bool decrypt) : destination(dest), tunnel_decrypt(decrypt) {}
  ~TunnelDestination() override {}

  int
  SNIAction(Continuation *cont) const override
  {
    // Set the netvc option?
    SSLNetVConnection *ssl_netvc = dynamic_cast<SSLNetVConnection *>(cont);
    if (ssl_netvc) {
      ssl_netvc->set_tunnel_destination(destination, tunnel_decrypt);
    }
    return SSL_TLSEXT_ERR_OK;
  }
  std::string destination;
  bool tunnel_decrypt = false;
};

class VerifyClient : public ActionItem
{
  uint8_t mode;

public:
  VerifyClient(const char *param) : mode(atoi(param)) {}
  VerifyClient(uint8_t param) : mode(param) {}
  ~VerifyClient() override {}
  int
  SNIAction(Continuation *cont) const override
  {
    auto ssl_vc = dynamic_cast<SSLNetVConnection *>(cont);
    Debug("ssl_sni", "action verify param %d", this->mode);
    setClientCertLevel(ssl_vc->ssl, this->mode);
    return SSL_TLSEXT_ERR_OK;
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
  SNIAction(Continuation *cont) const override
  {
    if (!unset) {
      auto ssl_vc = dynamic_cast<SSLNetVConnection *>(cont);
      Debug("ssl_sni", "TLSValidProtocol param 0%x", static_cast<unsigned int>(this->protocol_mask));
      ssl_vc->protocol_mask_set = true;
      ssl_vc->protocol_mask     = protocol_mask;
    }
    return SSL_TLSEXT_ERR_OK;
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
  SNIAction(Continuation *cont) const override
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
};
