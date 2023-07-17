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

#include <vector>
#include <optional>
#include "TLSSNISupport.h"
#include "tscore/ink_inet.h"
#include "ts/DbgCtl.h"

class ActionItem
{
public:
  /**
   * Context should contain extra data needed to be passed to the actual SNIAction.
   */
  struct Context {
    using CapturedGroupViewVec = std::vector<std::string_view>;
    /**
     * if any, fqdn_wildcard_captured_groups will hold the captured groups from the `fqdn`
     * match which will be used to construct the tunnel destination. This vector contains only
     * partial views of the original server name, group views are valid as long as the original
     * string from where the groups were obtained lives.
     */
    std::optional<CapturedGroupViewVec> _fqdn_wildcard_captured_groups;
  };

  virtual int SNIAction(SSL &ssl, const Context &ctx) const = 0;

  /**
    This method tests whether this action would have been triggered by a
    particularly SNI value and IP address combination.  This is run after the
    TLS exchange finished to see if the client used an SNI name different from
    the host name to avoid SNI-based policy
  */
  virtual bool
  TestClientSNIAction(const char *servername, const IpEndpoint &ep, int &policy) const
  {
    return false;
  }
  virtual ~ActionItem(){};

protected:
  inline static DbgCtl dbg_ctl_ssl_sni{"ssl_sni"};
};
