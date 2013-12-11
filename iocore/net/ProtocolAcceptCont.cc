/** @file

  ProtocolAcceptCont

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

#include "P_ProtocolAcceptCont.h"
#include "P_SSLNextProtocolAccept.h"
#include "P_Net.h"
#include "I_Machine.h"
#include "Error.h"

void *
ProtocolAcceptCont::createNetAccept()
{
  return ((NetAccept *) NEW(new ProtocolNetAccept));
}

void
ProtocolAcceptCont::registerEndpoint(TSProtoType type, Continuation *ep)
{
  endpoint[type] = ep;
}

int
ProtocolAcceptCont::mainEvent(int event, void *netvc)
{
  ink_release_assert(event == NET_EVENT_ACCEPT || event == EVENT_ERROR);
  ink_release_assert((event == NET_EVENT_ACCEPT) ? (netvc!= 0) : (1));

  if (event == NET_EVENT_ACCEPT) {
    TSProtoType proto_type;
    UnixNetVConnection *vc = (UnixNetVConnection *)netvc;

    if (vc->proto_stack & (1u << TS_PROTO_TLS)) {
      proto_type = TS_PROTO_TLS;
    } else if (vc->proto_stack & (1u << TS_PROTO_HTTP)) {
      proto_type = TS_PROTO_HTTP;
    } else if (vc->proto_stack & (1u << TS_PROTO_SPDY)) {
      proto_type = TS_PROTO_SPDY;
    } else {
      Warning("Invalid protocol stack:%x", vc->proto_stack);
      return EVENT_CONT;
    }

    if (endpoint[proto_type])
      endpoint[proto_type]->handleEvent(NET_EVENT_ACCEPT, netvc);
    else
      Warning("Unregistered protocol type:%x", proto_type);

    return EVENT_CONT;
  }

  MachineFatal("Protocol Accept received fatal error: errno = %d", -((int)(intptr_t)netvc));
  return EVENT_CONT;
}
