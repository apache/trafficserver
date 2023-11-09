/** @file

  QUICNextProtocolAccept

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

#include "P_QUICNextProtocolAccept.h"

static QUICNetVConnection *
quic_netvc_cast(int event, void *edata)
{
  union {
    VIO *vio;
    NetVConnection *vc;
  } ptr;

  switch (event) {
  case NET_EVENT_ACCEPT:
    ptr.vc = static_cast<NetVConnection *>(edata);
    return dynamic_cast<QUICNetVConnection *>(ptr.vc);
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_ERROR:
    ptr.vio = static_cast<VIO *>(edata);
    return dynamic_cast<QUICNetVConnection *>(ptr.vio->vc_server);
  default:
    return nullptr;
  }
}

int
QUICNextProtocolAccept::mainEvent(int event, void *edata)
{
  QUICNetVConnection *netvc = quic_netvc_cast(event, edata);

  Debug("v_quic", "[%s] event %d netvc %p", netvc->cids().data(), event, netvc);
  switch (event) {
  case NET_EVENT_ACCEPT:
    ink_release_assert(netvc != nullptr);
    netvc->registerNextProtocolSet(&this->protoset, this->protoenabled);
    return EVENT_CONT;
  default:
    netvc->do_io_close();
    return EVENT_DONE;
  }
}

bool
QUICNextProtocolAccept::accept(NetVConnection *, MIOBuffer *, IOBufferReader *)
{
  ink_release_assert(0);
  return false;
}

bool
QUICNextProtocolAccept::registerEndpoint(const char *protocol, Continuation *handler)
{
  return this->protoset.registerEndpoint(protocol, handler);
}

void
QUICNextProtocolAccept::enableProtocols(const SessionProtocolSet &protos)
{
  this->protoenabled = protos;
}

QUICNextProtocolAccept::QUICNextProtocolAccept() : SessionAccept(nullptr)
{
  SET_HANDLER(&QUICNextProtocolAccept::mainEvent);
}

SSLNextProtocolSet *
QUICNextProtocolAccept::getProtoSet()
{
  return &this->protoset;
}

QUICNextProtocolAccept::~QUICNextProtocolAccept() {}
