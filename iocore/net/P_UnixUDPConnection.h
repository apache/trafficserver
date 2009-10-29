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

#ifndef _IOCORE_WIN32

#include "P_UDPConnection.h"
class UnixUDPConnection:public UDPConnectionInternal
{

public:

  UnixUDPConnection(int fd);
    virtual ~ UnixUDPConnection();

  void init(int fd);

  void setPollvecIndex(int i);
  int getPollvecIndex();
  void clearPollvecIndex();
  void setEthread(EThread * e);

  void errorAndDie(int e);

    Link<UnixUDPConnection> polling_link;

    Link<UnixUDPConnection> callback_link;

    SLink<UnixUDPConnection> newconn_alink;

  int callbackHandler(int event, void *data);
  InkAtomicList inQueue;

  int onCallbackQueue;

  Action *callbackAction;
  EThread *m_ethread;
  struct epoll_data_ptr *eptr;
  virtual void UDPConnection_is_abstract()
  {
  };

private:
  int m_pollvec_index;          // used by nethandler for polling.
  int m_errno;
};

inline
UnixUDPConnection::UnixUDPConnection(int fd)
  :
onCallbackQueue(0)
  ,
callbackAction(NULL)
  ,
m_ethread(NULL)
  ,
m_pollvec_index(-1)
  ,
m_errno(0)
{
  m_fd = fd;
  UDPPacketInternal p;
  ink_atomiclist_init(&inQueue, "Incoming UDP Packet queue", (char *) &p.alink.next - (char *) &p);
  SET_HANDLER(&UnixUDPConnection::callbackHandler);
}

inline void
UnixUDPConnection::init(int fd)
{
  m_fd = fd;
  onCallbackQueue = 0;
  callbackAction = NULL;
  m_ethread = NULL;
  m_pollvec_index = -1;
  m_errno = 0;

  UDPPacketInternal p;
  ink_atomiclist_init(&inQueue, "Incoming UDP Packet queue", (char *) &p.alink.next - (char *) &p);
  SET_HANDLER(&UnixUDPConnection::callbackHandler);
}

inline void
UnixUDPConnection::setPollvecIndex(int i)
{
  m_pollvec_index = i;
}

inline int
UnixUDPConnection::getPollvecIndex()
{
  return m_pollvec_index;
}

inline void
UnixUDPConnection::clearPollvecIndex()
{
  m_pollvec_index = -1;
}

inline void
UnixUDPConnection::setEthread(EThread * e)
{
  m_ethread = e;
}

inline void
UnixUDPConnection::errorAndDie(int e)
{
  m_errno = e;
}

INK_INLINE void
UDPConnection::Release()
{
  UnixUDPConnection *p = (UnixUDPConnection *) this;
  //epoll changes
  //added by YTS Team, yamsat
  struct epoll_event ev;
  PollCont *pc = get_UDPPollCont(p->m_ethread);
  epoll_ctl(pc->pollDescriptor->epoll_fd, EPOLL_CTL_DEL, getFd(), &ev);
  if (p->eptr) {
    free(p->eptr);
    p->eptr = NULL;
  }
  //epoll changes ends here
  if (ink_atomic_increment(&p->m_refcount, -1) == 1) {
    ink_debug_assert(p->callback_link.next == NULL);
    ink_debug_assert(p->callback_link.prev == NULL);
    ink_debug_assert(p->polling_link.next == NULL);
    ink_debug_assert(p->polling_link.prev == NULL);
    ink_debug_assert(p->newconn_alink.next == NULL);

    delete this;
  }
}

INK_INLINE Action *
UDPConnection::recv(Continuation * c)
{
  UnixUDPConnection *p = (UnixUDPConnection *) this;
  // register callback interest.
  p->continuation = c;
  ink_debug_assert(c != NULL);
  mutex = c->mutex;
  p->recvActive = 1;
  return ACTION_RESULT_NONE;
}


INK_INLINE UDPConnection *
new_UDPConnection(int fd)
{
  return (fd >= 0) ? NEW(new UnixUDPConnection(fd)) : 0;
}

#endif //_IOCORE_WIN32
#endif //__UNIXUDPCONNECTION_H_
