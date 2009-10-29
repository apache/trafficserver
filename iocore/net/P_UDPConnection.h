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
  int m_refcount;               // public for assertion

  SOCKET m_fd;
  struct sockaddr_in m_binding;
  int m_binding_valid;
  int m_tobedestroyed;
  int m_sendGenerationNum;
  ink64 m_lastSentPktTSSeqNum;

  // this is for doing packet scheduling: we keep two values so that we can
  // implement cancel.  The first value tracks the startTime of the last
  // packet that was sent on this connection; the second value tracks the
  // startTime of the last packet when we are doing scheduling;  whenever the
  // associated continuation cancels a packet, we rest lastPktStartTime to be
  // the same as the lastSentPktStartTime.
  inku64 m_lastSentPktStartTime;
  inku64 m_lastPktStartTime;
  ink32 m_pipe_class;
  inku32 m_nBytesDone;
  inku32 m_nBytesTodo;
  // flow rate in Bytes per sec.
  double m_flowRateBps;
  double m_avgPktSize;
  ink64 m_allocedbps;

  //this class is abstract
};

inline
UDPConnectionInternal::UDPConnectionInternal()
  :
continuation(NULL)
  ,
recvActive(0)
  ,
m_refcount(0)
  ,
m_fd(-1)
  ,
m_binding_valid(0)
  ,
m_tobedestroyed(0)
  ,
m_nBytesDone(0)
  ,
m_nBytesTodo(0)
{
  m_sendGenerationNum = 0;
  m_lastSentPktTSSeqNum = -1;
  m_lastSentPktStartTime = 0;
  m_lastPktStartTime = 0;
  m_pipe_class = 0;
  m_flowRateBps = 0.0;
  m_avgPktSize = 0.0;
  m_allocedbps = 0;
  memset(&m_binding, 0, sizeof m_binding);
  //SET_HANDLER(&BaseUDPConnection::callbackHandler);
}

inline
UDPConnectionInternal::~
UDPConnectionInternal()
{
  udpNet.FreeBandwidth(this);
  continuation = NULL;
  mutex = NULL;
}


INK_INLINE SOCKET
UDPConnection::getFd()
{
  return ((UDPConnectionInternal *) this)->m_fd;
}

INK_INLINE void
UDPConnection::setBinding(struct sockaddr_in *s)
{
  UDPConnectionInternal *p = (UDPConnectionInternal *) this;
  memcpy(&p->m_binding, s, sizeof(p->m_binding));
  p->m_binding_valid = 1;
}

INK_INLINE int
UDPConnection::getBinding(struct sockaddr_in *s)
{
  UDPConnectionInternal *p = (UDPConnectionInternal *) this;
  memcpy(s, &p->m_binding, sizeof(*s));
  return p->m_binding_valid;
}

INK_INLINE int
UDPConnection::get_ndone()
{
  return ((UDPConnectionInternal *) this)->m_nBytesDone;
}

INK_INLINE int
UDPConnection::get_ntodo()
{
  return ((UDPConnectionInternal *) this)->m_nBytesTodo;
}

// return the b/w allocated to this UDPConnection in Mbps
INK_INLINE double
UDPConnection::get_allocatedBandwidth()
{
  return (((UDPConnectionInternal *) this)->m_flowRateBps * 8.0) / (1024.0 * 1024.0);
}

INK_INLINE void
UDPConnection::destroy()
{
  ((UDPConnectionInternal *) this)->m_tobedestroyed = 1;
}

INK_INLINE int
UDPConnection::shouldDestroy()
{
  return ((UDPConnectionInternal *) this)->m_tobedestroyed;
}

INK_INLINE void
UDPConnection::AddRef()
{
  ink_atomic_increment(&((UDPConnectionInternal *) this)->m_refcount, 1);
}

INK_INLINE int
UDPConnection::GetRefCount()
{
  return ((UDPConnectionInternal *) this)->m_refcount;
}

INK_INLINE int
UDPConnection::GetSendGenerationNumber()
{
  return ((UDPConnectionInternal *) this)->m_sendGenerationNum;
}

INK_INLINE int
UDPConnection::getPortNum(void)
{
  return ((UDPConnectionInternal *) this)->m_binding.sin_port;
}

INK_INLINE ink64
UDPConnection::cancel(void)
{
  UDPConnectionInternal *p = (UDPConnectionInternal *) this;

  p->m_sendGenerationNum++;
  p->m_lastPktStartTime = p->m_lastSentPktStartTime;
  return p->m_lastSentPktTSSeqNum;
};

INK_INLINE void
UDPConnection::SetLastSentPktTSSeqNum(ink64 sentSeqNum)
{
  ((UDPConnectionInternal *) this)->m_lastSentPktTSSeqNum = sentSeqNum;
};

INK_INLINE void
UDPConnection::setContinuation(Continuation * c)
{
  // it is not safe to switch among continuations that don't share locks
  ink_assert(mutex == NULL || c->mutex == mutex);
  mutex = c->mutex;
  ((UDPConnectionInternal *) this)->continuation = c;
}

#endif //__P_UDPCONNECTION_H_
