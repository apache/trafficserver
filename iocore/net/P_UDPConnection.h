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

  P_UDPConnection.h
  Internal UDPConnection holds data members and defines member functions


 ****************************************************************************/
#pragma once

#include "I_UDPNet.h"

class UDPConnectionInternal : public UDPConnection
{
public:
  UDPConnectionInternal() = default;
  ~UDPConnectionInternal() override;

  Continuation *continuation = nullptr;
  int refcount               = 0; // public for assertion

  SOCKET fd = -1;
  IpEndpoint binding{};
  bool binding_valid    = false;
  int tobedestroyed     = 0;
  int sendGenerationNum = 0;

  // this is for doing packet scheduling: we keep two values so that we can
  // implement cancel.  The first value tracks the startTime of the last
  // packet that was sent on this connection; the second value tracks the
  // startTime of the last packet when we are doing scheduling;  whenever the
  // associated continuation cancels a packet, we rest lastPktStartTime to be
  // the same as the lastSentPktStartTime.
  uint64_t lastSentPktStartTime = 0;
  uint64_t lastPktStartTime     = 0;
};

TS_INLINE
UDPConnectionInternal::~UDPConnectionInternal()
{
  continuation = nullptr;
  mutex        = nullptr;
}

TS_INLINE SOCKET
UDPConnection::getFd()
{
  return static_cast<UDPConnectionInternal *>(this)->fd;
}

TS_INLINE void
UDPConnection::setBinding(struct sockaddr const *s)
{
  UDPConnectionInternal *p = static_cast<UDPConnectionInternal *>(this);
  ats_ip_copy(&p->binding, s);
  p->binding_valid = true;
}

TS_INLINE void
UDPConnection::setBinding(IpAddr const &ip, in_port_t port)
{
  UDPConnectionInternal *p = static_cast<UDPConnectionInternal *>(this);
  IpEndpoint addr;
  addr.assign(ip, htons(port));
  ats_ip_copy(&p->binding, addr);
  p->binding_valid = true;
}

TS_INLINE bool
UDPConnection::getBinding(struct sockaddr *s)
{
  UDPConnectionInternal *p = static_cast<UDPConnectionInternal *>(this);
  ats_ip_copy(s, &p->binding);
  return p->binding_valid;
}

TS_INLINE void
UDPConnection::destroy()
{
  static_cast<UDPConnectionInternal *>(this)->tobedestroyed = 1;
}

TS_INLINE int
UDPConnection::shouldDestroy()
{
  return static_cast<UDPConnectionInternal *>(this)->tobedestroyed;
}

TS_INLINE void
UDPConnection::AddRef()
{
  ink_atomic_increment(&((UDPConnectionInternal *)this)->refcount, 1);
}

TS_INLINE int
UDPConnection::GetRefCount()
{
  return static_cast<UDPConnectionInternal *>(this)->refcount;
}

TS_INLINE int
UDPConnection::GetSendGenerationNumber()
{
  return static_cast<UDPConnectionInternal *>(this)->sendGenerationNum;
}

TS_INLINE int
UDPConnection::getPortNum()
{
  return ats_ip_port_host_order(&static_cast<UDPConnectionInternal *>(this)->binding);
}

TS_INLINE void
UDPConnection::setContinuation(Continuation *c)
{
  // it is not safe to switch among continuations that don't share locks
  ink_assert(mutex.get() == nullptr || c->mutex == mutex);
  mutex                                                    = c->mutex;
  static_cast<UDPConnectionInternal *>(this)->continuation = c;
}
