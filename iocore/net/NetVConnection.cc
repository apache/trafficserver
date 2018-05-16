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

/****************************************************************************

  NetVConnection.cc

  This file implements an I/O Processor for network I/O.


 ****************************************************************************/

#include "P_Net.h"
#include "ts/apidefs.h"

Action *
NetVConnection::send_OOB(Continuation *, char *, int)
{
  return ACTION_RESULT_DONE;
}

void
NetVConnection::cancel_OOB()
{
  return;
}

std::string_view
NetVCOptions::get_proto_string() const
{
  switch (ip_proto) {
  case USE_TCP:
    return IP_PROTO_TAG_TCP;
  case USE_UDP:
    return IP_PROTO_TAG_UDP;
  default:
    break;
  }
  return {};
}

std::string_view
NetVCOptions::get_family_string() const
{
  switch (ip_family) {
  case AF_INET:
    return IP_PROTO_TAG_IPV4;
  case AF_INET6:
    return IP_PROTO_TAG_IPV6;
  default:
    break;
  }
  return {};
}
