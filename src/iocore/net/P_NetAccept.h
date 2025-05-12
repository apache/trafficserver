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
#pragma once

#include "iocore/net/Net.h"
#include "iocore/net/NetProcessor.h"
#include "iocore/net/NetAcceptEventIO.h"
#include "Server.h"

#include <vector>

struct NetAccept;
struct HttpProxyPort;
class Event;
class SSLNextProtocolAccept;
//
// Default accept function
//   Accepts as many connections as possible, returning the number accepted
//   or -1 to stop accepting.
//
using AcceptFunction    = int(NetAccept *, void *, bool);
using AcceptFunctionPtr = AcceptFunction *;
AcceptFunction net_accept;

class UnixNetVConnection;

// TODO fix race between cancel accept and call back
struct NetAcceptAction : public Action, public RefCountObjInHeap {
  Server *server;

  void
  cancel(Continuation *cont = nullptr) override
  {
    Action::cancel(cont);
    server->close();
  }

  Continuation *
  operator=(Continuation *acont)
  {
    return Action::operator=(acont);
  }

  ~NetAcceptAction() override
  {
    static DbgCtl dbg_ctl{"net_accept"};
    Dbg(dbg_ctl, "NetAcceptAction dying");
  }
};

//
// NetAccept
// Handles accepting connections.
//
struct NetAccept : public Continuation {
  ink_hrtime             period = 0;
  Server                 server;
  int                    ifd = NO_FD;
  int                    id  = -1;
  Ptr<NetAcceptAction>   action_;
  SSLNextProtocolAccept *snpa = nullptr;
  NetAcceptEventIO       ep;

  HttpProxyPort *proxyPort = nullptr;
  AcceptOptions  opt;

  virtual NetProcessor *getNetProcessor() const;

  virtual void       init_accept_periodic(EThread *t = nullptr);
  void               init_accept_dedicated_threads();
  void               init_accept_per_thread();
  virtual void       stop_accept();
  virtual NetAccept *clone() const;

  /** Listen without blocking.
   *
   * For a blocking listen, use do_blocking_listen.
   *
   * @see do_blocking_listen
   */
  int do_nonblocking_listen();
  int do_blocking_listen();
  int do_blocking_accept(EThread *t);
  int do_accept_periodic(void *ep, bool blockable);

  int  acceptEventPeriodic(int event, void *e);
  int  acceptEventPerThread(int event, void *e);
  int  listenEventPerThread(int event, void *e);
  int  acceptEventDedicatedThread(int event, Event *e);
  void cancel();

  explicit NetAccept(const NetProcessor::AcceptOptions &);
  ~NetAccept() override { action_ = nullptr; }

private:
  int                 do_listen_impl(bool non_blocking);
  UnixNetVConnection *initialize_vc_from_con(Connection &con, NetAcceptAction *action, EThread *ethread);
};

extern Ptr<ProxyMutex>          naVecMutex;
extern std::vector<NetAccept *> naVec;
