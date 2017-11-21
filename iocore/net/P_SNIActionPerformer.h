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
#ifndef __P_ACTIONPROCESSOR_H__
#define __P_ACTIONPROCESSOR_H__

#include "I_EventSystem.h"
#include "ts/Map.h"
//#include"P_UnixNetProcessor.h"
#include <vector>
#include "P_SSLNextProtocolAccept.h"

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
  virtual void SNIAction(Continuation *cont) = 0;
  virtual ~ActionItem(){};
};

class DisableH2 : public ActionItem
{
public:
  DisableH2() {}
  ~DisableH2() override {}

  void
  SNIAction(Continuation *cont) override
  {
    auto ssl_vc     = reinterpret_cast<SSLNetVConnection *>(cont);
    auto accept_obj = ssl_vc ? ssl_vc->accept_object : nullptr;
    if (accept_obj && accept_obj->snpa && ssl_vc) {
      auto nps = snpsMap.get(accept_obj->id);
      ssl_vc->registerNextProtocolSet(reinterpret_cast<SSLNextProtocolSet *>(nps));
    }
  }
};

class VerifyClient : public ActionItem
{
  uint8_t mode;

public:
  VerifyClient(const char *param) : mode(atoi(param)) {}
  VerifyClient(uint8_t param) : mode(param) {}
  ~VerifyClient() override {}
  void
  SNIAction(Continuation *cont) override
  {
    auto ssl_vc = reinterpret_cast<SSLNetVConnection *>(cont);
    Debug("ssl_sni", "action verify param %d", this->mode);
    setClientCertLevel(ssl_vc->ssl, this->mode);
  }
};

class SNIActionPerformer
{
public:
  SNIActionPerformer() = default;
  static void PerformAction(Continuation *cont, cchar *servername);
};

#endif
