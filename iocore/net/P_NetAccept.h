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

  NetAccept.h


   NetAccept is a generalized facility which allows
   Connections of different classes to be accepted either
   from a blockable thread or by adaptive polling.

   It is used by the NetProcessor and the ClusterProcessor
   and should be considered PRIVATE to processor implementations.



 ****************************************************************************/
#ifndef __P_NETACCEPT_H__
#define __P_NETACCEPT_H__

#include "ts/ink_platform.h"
#include "P_Connection.h"

struct NetAccept;
class Event;
//
// Default accept function
//   Accepts as many connections as possible, returning the number accepted
//   or -1 to stop accepting.
//
typedef int(AcceptFunction)(NetAccept *na, void *e, bool blockable);
typedef AcceptFunction *AcceptFunctionPtr;
AcceptFunction net_accept;

class UnixNetVConnection;

// TODO fix race between cancel accept and call back
struct NetAcceptAction : public Action, public RefCountObj {
  Server *server;

  void
  cancel(Continuation *cont = NULL)
  {
    Action::cancel(cont);
    server->close();
  }

  Continuation *
  operator=(Continuation *acont)
  {
    return Action::operator=(acont);
  }

  ~NetAcceptAction() { Debug("net_accept", "NetAcceptAction dying\n"); }
};

//
// NetAccept
// Handles accepting connections.
//
struct NetAccept : public Continuation {
  ink_hrtime period;
  Server server;
  AcceptFunctionPtr accept_fn;
  int ifd;
  bool callback_on_open;
  bool backdoor;
  Ptr<NetAcceptAction> action_;
  int recv_bufsize;
  int send_bufsize;
  uint32_t sockopt_flags;
  uint32_t packet_mark;
  uint32_t packet_tos;
  EventType etype;
  UnixNetVConnection *epoll_vc; // only storage for epoll events
  EventIO ep;

  virtual EventType getEtype() const;
  virtual NetProcessor *getNetProcessor() const;

  void init_accept_loop(const char *);
  virtual void init_accept(EThread *t = NULL, bool isTransparent = false);
  virtual void init_accept_per_thread(bool isTransparent);
  virtual NetAccept *clone() const;
  // 0 == success
  int do_listen(bool non_blocking, bool transparent = false);

  int do_blocking_accept(EThread *t);
  virtual int acceptEvent(int event, void *e);
  virtual int acceptFastEvent(int event, void *e);
  int acceptLoopEvent(int event, Event *e);
  void cancel();

  NetAccept();
  virtual ~NetAccept() { action_ = NULL; };
};

#endif
