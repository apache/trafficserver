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

  UnixUDPConnection.cc
  Unix UDPConnection implementation


 ****************************************************************************/

#include "P_Net.h"
#include "P_UDPNet.h"

UnixUDPConnection::~UnixUDPConnection()
{
  UDPPacketInternal *p = (UDPPacketInternal *)ink_atomiclist_popall(&inQueue);

  if (!tobedestroyed) {
    tobedestroyed = 1;
  }

  if (p) {
    UDPPacketInternal *pnext = nullptr;
    while (p) {
      pnext         = p->alink.next;
      p->alink.next = nullptr;
      p->free();
      p = pnext;
    }
  }
  if (callbackAction) {
    callbackAction->cancel();
    callbackAction = nullptr;
  }
  Debug("udpnet", "Destroying udp port = %d", getPortNum());
  if (fd != NO_FD) {
    socketManager.close(fd);
  }
  fd = NO_FD;
}

// called with continuation lock taken out
// We call Release because AddRef was called before entering here.
int
UnixUDPConnection::callbackHandler(int event, void *data)
{
  (void)event;
  (void)data;
  callbackAction = nullptr;
  if (continuation == nullptr) {
    return EVENT_CONT;
  }

  if (m_errno) {
    if (!shouldDestroy()) {
      continuation->handleEvent(NET_EVENT_DATAGRAM_ERROR, this);
    }
    destroy(); // don't destroy until after calling back with error
    Release();
    return EVENT_CONT;
  } else {
    UDPPacketInternal *p = (UDPPacketInternal *)ink_atomiclist_popall(&inQueue);
    if (p) {
      Debug("udpnet", "UDPConnection::callbackHandler");
      UDPPacketInternal *pnext = nullptr;
      Queue<UDPPacketInternal> result;
      while (p) {
        pnext         = p->alink.next;
        p->alink.next = nullptr;
        result.push(p);
        p = pnext;
      }
      if (!shouldDestroy()) {
        continuation->handleEvent(NET_EVENT_DATAGRAM_READ_READY, &result);
      } else {
        while ((p = result.dequeue())) {
          p->free();
        }
      }
    }
  }
  Release();
  return EVENT_CONT;
}

void
UDPConnection::bindToThread(Continuation *c)
{
  UnixUDPConnection *uc = (UnixUDPConnection *)this;
  // add to new connections queue for EThread.
  EThread *t = eventProcessor.assign_thread(ET_UDP);
  ink_assert(t);
  ink_assert(get_UDPNetHandler(t));
  uc->ethread = t;
  AddRef();
  uc->continuation = c;
  mutex            = c->mutex;
  ink_atomiclist_push(&get_UDPNetHandler(t)->udpNewConnections, uc);
}

Action *
UDPConnection::send(Continuation *c, UDPPacket *xp)
{
  UDPPacketInternal *p    = (UDPPacketInternal *)xp;
  UnixUDPConnection *conn = (UnixUDPConnection *)this;

  if (shouldDestroy()) {
    ink_assert(!"freeing packet sent on dead connection");
    p->free();
    return ACTION_RESULT_NONE;
  }

  ink_assert(mutex == c->mutex);
  p->setContinuation(c);
  p->setConnection(this);
  conn->continuation = c;
  ink_assert(conn->continuation != nullptr);
  mutex               = c->mutex;
  p->reqGenerationNum = conn->sendGenerationNum;
  get_UDPNetHandler(conn->ethread)->udpOutQueue.send(p);
  return ACTION_RESULT_NONE;
}

void
UDPConnection::Release()
{
  UnixUDPConnection *p = (UnixUDPConnection *)this;

  p->ep.stop();

  if (ink_atomic_increment(&p->refcount, -1) == 1) {
    ink_assert(p->callback_link.next == nullptr);
    ink_assert(p->callback_link.prev == nullptr);
    ink_assert(p->polling_link.next == nullptr);
    ink_assert(p->polling_link.prev == nullptr);
    ink_assert(p->newconn_alink.next == nullptr);

    delete this;
  }
}
