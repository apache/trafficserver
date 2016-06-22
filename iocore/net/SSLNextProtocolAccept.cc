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
send_plugin_event(Continuation *plugin, int event, void *edata)
{
  if (plugin->mutex) {
    EThread *thread(this_ethread());
    MUTEX_TAKE_LOCK(plugin->mutex, thread);
    plugin->handleEvent(event, edata);
    MUTEX_UNTAKE_LOCK(plugin->mutex, thread);
  } else {
    plugin->handleEvent(event, edata);
  }
}

static SSLNetVConnection *
ssl_netvc_cast(int event, void *edata)
{
  union {
    VIO *vio;
    NetVConnection *vc;
  } ptr;

  switch (event) {
  case NET_EVENT_ACCEPT:
    ptr.vc = static_cast<NetVConnection *>(edata);
    return dynamic_cast<SSLNetVConnection *>(ptr.vc);
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_ERROR:
    ptr.vio = static_cast<VIO *>(edata);
    return dynamic_cast<SSLNetVConnection *>(ptr.vio->vc_server);
  default:
    return NULL;
  }
}

// SSLNextProtocolTrampoline is the receiver of the I/O event generated when we perform a 0-length read on the new SSL
// connection. The 0-length read forces the SSL handshake, which allows us to bind an endpoint that is selected by the
// NPN extension. The Continuation that receives the read event *must* have a mutex, but we don't want to take a global
// lock across the handshake, so we make a trampoline to bounce the event from the SSL acceptor to the ultimate session
// acceptor.
struct SSLNextProtocolTrampoline : public Continuation {
  explicit SSLNextProtocolTrampoline(const SSLNextProtocolAccept *npn, ProxyMutex *mutex) : Continuation(mutex), npnParent(npn)
  {
    SET_HANDLER(&SSLNextProtocolTrampoline::ioCompletionEvent);
  }

  int
  ioCompletionEvent(int event, void *edata)
  {
    VIO *vio;
    Continuation *plugin;
    SSLNetVConnection *netvc;

    vio   = static_cast<VIO *>(edata);
    netvc = dynamic_cast<SSLNetVConnection *>(vio->vc_server);
    ink_assert(netvc != NULL);

    switch (event) {
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_ACTIVE_TIMEOUT:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      // Cancel the read before we have a chance to delete the continuation
      netvc->do_io_read(NULL, 0, NULL);
      netvc->do_io(VIO::CLOSE);
      delete this;
      return EVENT_ERROR;
    case VC_EVENT_READ_COMPLETE:
      break;
    default:
      return EVENT_ERROR;
    }

    // Cancel the action, so later timeouts and errors don't try to
    // send the event to the Accept object.  After this point, the accept
    // object does not care.
    netvc->set_action(NULL);

    // Cancel the read before we have a chance to delete the continuation
    netvc->do_io_read(NULL, 0, NULL);
    plugin = netvc->endpoint();
    if (plugin) {
      send_plugin_event(plugin, NET_EVENT_ACCEPT, netvc);
    } else if (npnParent->endpoint) {
      // Route to the default endpoint
      send_plugin_event(npnParent->endpoint, NET_EVENT_ACCEPT, netvc);
    } else {
      // No handler, what should we do? Best to just kill the VC while we can.
      netvc->do_io(VIO::CLOSE);
    }

    delete this;
    return EVENT_CONT;
  }

  const SSLNextProtocolAccept *npnParent;
};

int
SSLNextProtocolAccept::mainEvent(int event, void *edata)
{
  SSLNetVConnection *netvc = ssl_netvc_cast(event, edata);

  Debug("ssl", "[SSLNextProtocolAccept:mainEvent] event %d netvc %p", event, netvc);

  switch (event) {
  case NET_EVENT_ACCEPT:
    ink_release_assert(netvc != NULL);

    netvc->setTransparentPassThrough(transparent_passthrough);

    // Register our protocol set with the VC and kick off a zero-length read to
    // force the SSLNetVConnection to complete the SSL handshake. Don't tell
    // the endpoint that there is an accept to handle until the read completes
    // and we know which protocol was negotiated.
    netvc->registerNextProtocolSet(&this->protoset);
    netvc->do_io(VIO::READ, new SSLNextProtocolTrampoline(this, netvc->mutex), 0, this->buffer, 0);
    netvc->set_session_accept_pointer(this);
    return EVENT_CONT;
  default:
    netvc->do_io(VIO::CLOSE);
    return EVENT_DONE;
  }
}

void
SSLNextProtocolAccept::accept(NetVConnection *, MIOBuffer *, IOBufferReader *)
{
  ink_release_assert(0);
}

bool
SSLNextProtocolAccept::registerEndpoint(const char *protocol, Continuation *handler)
{
  return this->protoset.registerEndpoint(protocol, handler);
}

bool
SSLNextProtocolAccept::unregisterEndpoint(const char *protocol, Continuation *handler)
{
  return this->protoset.unregisterEndpoint(protocol, handler);
}

SSLNextProtocolAccept::SSLNextProtocolAccept(Continuation *ep, bool transparent_passthrough)
  : SessionAccept(NULL), buffer(new_empty_MIOBuffer()), endpoint(ep), transparent_passthrough(transparent_passthrough)
{
  SET_HANDLER(&SSLNextProtocolAccept::mainEvent);
}

SSLNextProtocolAccept::~SSLNextProtocolAccept()
{
  free_MIOBuffer(this->buffer);
}
