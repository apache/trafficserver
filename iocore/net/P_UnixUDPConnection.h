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

  P_UnixUDPConnection.h
  Unix UDPConnection implementation


 ****************************************************************************/
#ifndef __UNIXUDPCONNECTION_H_
#define __UNIXUDPCONNECTION_H_

#include "P_UDPConnection.h"
#include "P_UDPPacket.h"

class UnixUDPConnection : public UDPConnectionInternal
{
public:
  void init(int the_fd);
  void setEthread(EThread *e);
  void errorAndDie(int e);
  int callbackHandler(int event, void *data);

  LINK(UnixUDPConnection, polling_link);
  LINK(UnixUDPConnection, callback_link);
  SLINK(UnixUDPConnection, newconn_alink);

  InkAtomicList inQueue;
  int onCallbackQueue;
  Action *callbackAction;
  EThread *ethread;
  EventIO ep;

  UnixUDPConnection(int the_fd);
  virtual ~UnixUDPConnection();

private:
  int m_errno;
  virtual void UDPConnection_is_abstract(){};
};

TS_INLINE
UnixUDPConnection::UnixUDPConnection(int the_fd) : onCallbackQueue(0), callbackAction(NULL), ethread(NULL), m_errno(0)
{
  fd = the_fd;
  UDPPacketInternal p;
  ink_atomiclist_init(&inQueue, "Incoming UDP Packet queue", (char *)&p.alink.next - (char *)&p);
  SET_HANDLER(&UnixUDPConnection::callbackHandler);
}

TS_INLINE void
UnixUDPConnection::init(int the_fd)
{
  fd              = the_fd;
  onCallbackQueue = 0;
  callbackAction  = NULL;
  ethread         = NULL;
  m_errno         = 0;

  UDPPacketInternal p;
  ink_atomiclist_init(&inQueue, "Incoming UDP Packet queue", (char *)&p.alink.next - (char *)&p);
  SET_HANDLER(&UnixUDPConnection::callbackHandler);
}

TS_INLINE void
UnixUDPConnection::setEthread(EThread *e)
{
  ethread = e;
}

TS_INLINE void
UnixUDPConnection::errorAndDie(int e)
{
  m_errno = e;
}

TS_INLINE Action *
UDPConnection::recv(Continuation *c)
{
  UnixUDPConnection *p = (UnixUDPConnection *)this;
  // register callback interest.
  p->continuation = c;
  ink_assert(c != NULL);
  mutex         = c->mutex;
  p->recvActive = 1;
  return ACTION_RESULT_NONE;
}

TS_INLINE UDPConnection *
new_UDPConnection(int fd)
{
  return (fd >= 0) ? new UnixUDPConnection(fd) : 0;
}

#endif //__UNIXUDPCONNECTION_H_
