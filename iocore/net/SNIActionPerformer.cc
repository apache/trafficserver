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
 SNIActionPerformer.cc
   Created On      : 05/02/2017

   Description:
   SNI based Configuration in ATS
 ****************************************************************************/
#include "P_SNIActionPerformer.h"
#include "tscore/ink_memory.h"
#include "P_SSLSNI.h"
#include "P_Net.h"
#include "P_SSLNextProtocolAccept.h"
#include "P_SSLUtils.h"

extern std::unordered_map<int, SSLNextProtocolSet *> snpsMap;

int
SNIActionPerformer::PerformAction(Continuation *cont, cchar *servername)
{
  SNIConfig::scoped_config params;
  auto actionvec = params->get(servername);
  if (!actionvec) {
    Debug("ssl_sni", "%s not available in the map", servername);
  } else {
    for (auto it : *actionvec) {
      if (it) {
        auto ret = it->SNIAction(cont);
        if (ret != SSL_TLSEXT_ERR_OK) {
          return ret;
        }
      }
    }
  }
  return SSL_TLSEXT_ERR_OK;
}
