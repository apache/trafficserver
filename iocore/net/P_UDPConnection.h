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
#ifndef __P_UDPCONNECTION_H_
#define __P_UDPCONNECTION_H_

#include "I_UDPNet.h"


class UDPConnectionInternal:public UDPConnection
{

public:
  UDPConnectionInternal();
  virtual ~ UDPConnectionInternal();

  Continuation *continuation;
  int recvActive;               // interested in receiving
  int refcount;               // public for assertion

  SOCKET fd;
  struct sockaddr_in binding;
  int binding_valid;
  int tobedestroyed;
  int sendGenerationNum;
  ink64 lastSentPktTSSeqNum;

  // this is for doing packet scheduling: we keep two values so that we can
  // implement cancel.  The first value tracks the startTime of the last
  // packet that was sent on this connection; the second value tracks the
  // startTime of the last packet when we are doing scheduling;  whenever the
  // associated continuation cancels a packet, we rest lastPktStartTime to be
  // the same as the lastSentPktStartTime.
  inku64 lastSentPktStartTime;
  inku64 lastPktStartTime;
  ink32 pipe_class;
  inku32 nBytesDone;
  inku32 nBytesTodo;
  // flow rate in Bytes per sec.
  double flowRateBps;
  double avgPktSize;
  ink64 allocedbps;

  //this class is abstract
};

TS_INLINE
UDPConnectionInternal::UDPConnectionInternal()
  : continuation(NULL)
  , recvActive(0)
  , refcount(0)
  , fd(-1)
  , binding_valid(0)
  , tobedestroyed(0)
  , nBytesDone(0)
  , nBytesTodo(0)
{
  sendGenerationNum = 0;
  lastSentPktTSSeqNum = -1;
  lastSentPktStartTime = 0;
  lastPktStartTime = 0;
  pipe_class = 0;
  flowRateBps = 0.0;
  avgPktSize = 0.0;
  allocedbps = 0;
  memset(&binding, 0, sizeof binding);
  //SET_HANDLER(&BaseUDPConnection::callbackHandler);
}

TS_INLINE
UDPConnectionInternal::~UDPConnectionInternal()
{
  udpNet.FreeBandwidth(this);
  continuation = NULL;
  mutex = NULL;
}


TS_INLINE SOCKET
UDPConnection::getFd()
{
  return ((UDPConnectionInternal *) this)->fd;
}

TS_INLINE void
UDPConnection::setBinding(struct sockaddr_in *s)
{
  UDPConnectionInternal *p = (UDPConnectionInternal *) this;
  memcpy(&p->binding, s, sizeof(p->binding));
  p->binding_valid = 1;
}

TS_INLINE int
UDPConnection::getBinding(struct sockaddr_in *s)
{
  UDPConnectionInternal *p = (UDPConnectionInternal *) this;
  memcpy(s, &p->binding, sizeof(*s));
  return p->binding_valid;
}

TS_INLINE int
UDPConnection::get_ndone()
{
  return ((UDPConnectionInternal *) this)->nBytesDone;
}

TS_INLINE int
UDPConnection::get_ntodo()
{
  return ((UDPConnectionInternal *) this)->nBytesTodo;
}

// return the b/w allocated to this UDPConnection in Mbps
TS_INLINE double
UDPConnection::get_allocatedBandwidth()
{
  return (((UDPConnectionInternal *) this)->flowRateBps * 8.0) / (1024.0 * 1024.0);
}

TS_INLINE void
UDPConnection::destroy()
{
  ((UDPConnectionInternal *) this)->tobedestroyed = 1;
}

TS_INLINE int
UDPConnection::shouldDestroy()
{
  return ((UDPConnectionInternal *) this)->tobedestroyed;
}

TS_INLINE void
UDPConnection::AddRef()
{
  ink_atomic_increment(&((UDPConnectionInternal *) this)->refcount, 1);
}

TS_INLINE int
UDPConnection::GetRefCount()
{
  return ((UDPConnectionInternal *) this)->refcount;
}

TS_INLINE int
UDPConnection::GetSendGenerationNumber()
{
  return ((UDPConnectionInternal *) this)->sendGenerationNum;
}

TS_INLINE int
UDPConnection::getPortNum(void)
{
  return ((UDPConnectionInternal *) this)->binding.sin_port;
}

TS_INLINE ink64
UDPConnection::cancel(void)
{
  UDPConnectionInternal *p = (UDPConnectionInternal *) this;

  p->sendGenerationNum++;
  p->lastPktStartTime = p->lastSentPktStartTime;
  return p->lastSentPktTSSeqNum;
};

TS_INLINE void
UDPConnection::SetLastSentPktTSSeqNum(ink64 sentSeqNum)
{
  ((UDPConnectionInternal *) this)->lastSentPktTSSeqNum = sentSeqNum;
};

TS_INLINE void
UDPConnection::setContinuation(Continuation * c)
{
  // it is not safe to switch among continuations that don't share locks
  ink_assert(mutex == NULL || c->mutex == mutex);
  mutex = c->mutex;
  ((UDPConnectionInternal *) this)->continuation = c;
}

#endif //__P_UDPCONNECTION_H_
