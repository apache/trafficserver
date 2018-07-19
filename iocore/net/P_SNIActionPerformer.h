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
#include "ts/Map.h"
//#include"P_UnixNetProcessor.h"
#include <vector>
#include "P_SSLNextProtocolAccept.h"
#include "ts/ink_inet.h"

extern Map<int, SSLNextProtocolSet *> snpsMap;
// enum of all the actions
enum AllActions {
  TS_DISABLE_H2 = 0,
  TS_VERIFY_CLIENT, // this applies to server side vc only
  TS_TUNNEL_ROUTE,  // blind tunnel action
};

/** action for setting next hop properties should be listed in the following enum*/
enum PropertyActions { TS_VERIFY_SERVER = 200, TS_CLIENT_CERT };

class ActionItem
{
public:
  virtual int SNIAction(Continuation *cont) = 0;
  virtual ~ActionItem(){};
};

class DisableH2 : public ActionItem
{
public:
  DisableH2() {}
  ~DisableH2() override {}

  int
  SNIAction(Continuation *cont) override
  {
    auto ssl_vc     = reinterpret_cast<SSLNetVConnection *>(cont);
    auto accept_obj = ssl_vc ? ssl_vc->accept_object : nullptr;
    if (accept_obj && accept_obj->snpa && ssl_vc) {
      auto nps = snpsMap.get(accept_obj->id);
      ssl_vc->registerNextProtocolSet(reinterpret_cast<SSLNextProtocolSet *>(nps));
    }
    return SSL_TLSEXT_ERR_OK;
  }
};

class VerifyClient : public ActionItem
{
  uint8_t mode;

public:
  VerifyClient(const char *param) : mode(atoi(param)) {}
  VerifyClient(uint8_t param) : mode(param) {}
  ~VerifyClient() override {}
  int
  SNIAction(Continuation *cont) override
  {
    auto ssl_vc = reinterpret_cast<SSLNetVConnection *>(cont);
    Debug("ssl_sni", "action verify param %d", this->mode);
    setClientCertLevel(ssl_vc->ssl, this->mode);
    return SSL_TLSEXT_ERR_OK;
  }
};

class SNI_IpAllow : public ActionItem
{
  IpMap ip_map;

public:
  SNI_IpAllow(std::string const &ip_allow_list, cchar *servername)
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
          Debug("ssl_sni", "%.*s added to the ip_allow list %s", static_cast<int>(list.size()), list.data(), servername);
          ip_map.fill(IpEndpoint().assign(addr1), IpEndpoint().assign(addr2), reinterpret_cast<void *>(1));
        }
      }
    }
  } // end function SNI_IpAllow

  int
  SNIAction(Continuation *cont) override
  {
    // i.e, ip filtering is not required
    if (ip_map.count() == 0) {
      return SSL_TLSEXT_ERR_OK;
    }

    auto ssl_vc = reinterpret_cast<SSLNetVConnection *>(cont);
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

class SNIActionPerformer
{
public:
  SNIActionPerformer() = default;
  static int PerformAction(Continuation *cont, cchar *servername);
};
