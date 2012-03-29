/** @file

  SSLNextProtocolAccept

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

#include "P_SSLNextProtocolAccept.h"

static void
send_plugin_event(Continuation * plugin, int event, void * edata)
{
  if (likely(plugin)) {
    EThread * thread(this_ethread());
    MUTEX_TAKE_LOCK(plugin->mutex, thread);
    plugin->handleEvent(event, edata);
    MUTEX_UNTAKE_LOCK(plugin->mutex, thread);
  }
}

static SSLNetVConnection *
ssl_netvc_cast(int event, void * edata)
{
  union {
    VIO * vio;
    NetVConnection * vc;
  } ptr;

  switch (event) {
  case NET_EVENT_ACCEPT:
    ptr.vc = static_cast<NetVConnection *>(edata);
    return dynamic_cast<SSLNetVConnection *>(ptr.vc);
  case VC_EVENT_READ_COMPLETE:
    ptr.vio = static_cast<VIO *>(edata);
    return dynamic_cast<SSLNetVConnection *>(ptr.vio->vc_server);
  default:
    return NULL;
  }
}

int
SSLNextProtocolAccept::mainEvent(int event, void * edata)
{
  SSLNetVConnection * netvc = ssl_netvc_cast(event, edata);
  Continuation * plugin;

  Debug("ssl",
      "[SSLNextProtocolAccept:mainEvent] event %d netvc %p", event, netvc);

  MUTEX_LOCK(lock, this->mutex, this_ethread());

  switch (event) {
  case NET_EVENT_ACCEPT:
    ink_release_assert(netvc != NULL);
    // Register our protocol set with the VC and kick off a zero-length read to
    // force the SSLNetVConnection to complete the SSL handshake. Don't tell
    // the endpoint that there is an accept to handle until the read completes
    // and we know which protocol was negotiated.
    netvc->registerNextProtocolSet(&this->protoset);
    netvc->do_io(VIO::READ, this, 0, this->buffer, 0);
    return EVENT_CONT;
  case VC_EVENT_READ_COMPLETE:
    ink_release_assert(netvc != NULL);
    plugin = netvc->endpoint();
    if (plugin) {
      send_plugin_event(plugin, NET_EVENT_ACCEPT, netvc);
    } else if (this->endpoint) {
      // Route to the default endpoint
      send_plugin_event(this->endpoint, NET_EVENT_ACCEPT, netvc);
    } else {
      // No handler, what should we do? Best to just kill the VC while we can.
      netvc->do_io(VIO::CLOSE);
    }
    return EVENT_CONT;
  default:
    return EVENT_DONE;
  }
}

bool
SSLNextProtocolAccept::registerEndpoint(
    const char * protocol, Continuation * handler)
{
  return this->protoset.registerEndpoint(protocol, handler);
}

bool
SSLNextProtocolAccept::unregisterEndpoint(
    const char * protocol, Continuation * handler)
{
  return this->protoset.unregisterEndpoint(protocol, handler);
}

SSLNextProtocolAccept::SSLNextProtocolAccept(Continuation * ep)
    : Continuation(new_ProxyMutex()), buffer(new_empty_MIOBuffer()), endpoint(ep)
{
  SET_HANDLER(&SSLNextProtocolAccept::mainEvent);
}

SSLNextProtocolAccept::~SSLNextProtocolAccept()
{
  free_MIOBuffer(this->buffer);
}
