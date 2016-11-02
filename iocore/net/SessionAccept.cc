/** @file

  SessionAccept

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

#include "I_Net.h"
#include "I_VConnection.h"
#include "../../proxy/IPAllow.h"

const AclRecord *
SessionAccept::testIpAllowPolicy(sockaddr const *client_ip)
{
  IpAllow::scoped_config ipallow;
  const AclRecord *acl_record = nullptr;
  if (ipallow) {
    acl_record = ipallow->match(client_ip, IpAllow::SRC_ADDR);
    if (acl_record && acl_record->isEmpty() && ipallow->isAcceptCheckEnabled()) {
      acl_record = nullptr;
    }
  }
  return acl_record;
}
