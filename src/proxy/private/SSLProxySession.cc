/** @file

  Implementation file for SSLProxySession class.

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

#include "SSLProxySession.h"
#include "iocore/net/NetVConnection.h"
#include "iocore/net/TLSSNISupport.h"

class TLSSNISupport;

void
SSLProxySession::init(NetVConnection const &new_vc)
{
  if (auto *snis = new_vc.get_service<TLSSNISupport>(); snis != nullptr) {
    if (char const *name = snis->get_sni_server_name()) {
      _client_sni_server_name.assign(name);
    }
  }

  _client_provided_cert = new_vc.peer_provided_cert();
}
