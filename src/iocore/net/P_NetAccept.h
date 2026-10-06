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

#include <atomic>
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

struct NetAcceptAction : public Action, public RefCountObjInHeap {
  NetAcceptAction(Continuation *cont, Server *s)
  {
    continuation = cont;
    if (cont != nullptr) {
      mutex = cont->mutex;
    }
    _server.store(s, std::memory_order_release);
  }

  /** Whether this action still owns an open listening socket.

    Accept paths must consult this before dispatching EVENT_ERROR. It is
    cleared by cancel() before Action::cancel() sets @c cancelled, so it goes
    false no later than the cancellation the continuation is aware of, and it
    is safe to read from any thread. Reading @c cancelled instead is both a
    data race on a plain bool and too late: accept() reports EBADF as soon as
    the socket closes, which is before @c cancelled is set.

  */
  bool
  is_listening() const
  {
    return _server.load(std::memory_order_acquire) != nullptr;
  }

  void
  cancel(Continuation *cont = nullptr) override
  {
    // Use atomic exchange so only one thread closes the server, preventing
    // use-after-free races between cancel() and acceptEvent() cleanup. This
    // must stay ahead of Action::cancel(), see is_listening().
    Server *s = _server.exchange(nullptr, std::memory_order_acq_rel);
    if (s != nullptr) {
      s->close();
    }
    if (!cancelled) {
      Action::cancel(cont);
    }
  }

  ~NetAcceptAction() override
  {
    static DbgCtl dbg_ctl{"net_accept"};
    Dbg(dbg_ctl, "NetAcceptAction dying");
  }

private:
  /// Cleared exactly once, by cancel(). Private so the ordering above is the
  /// only way this transitions to null.
  std::atomic<Server *> _server{nullptr};
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

  virtual void       init_accept(EThread *t = nullptr);
  void               init_accept_loop();
  void               init_accept_per_thread();
  virtual void       stop_accept();
  virtual NetAccept *clone() const;

  /** Listen without blocking.
   *
   * For a blocking listen, use do_blocking_listen.
   *
   * @see do_blocking_listen
   */
  int do_listen();
  int do_blocking_listen();
  int do_blocking_accept(EThread *t);

  virtual int acceptEvent(int event, void *e);
  virtual int acceptFastEvent(int event, void *e);
  virtual int accept_per_thread(int event, void *e);
  int         acceptLoopEvent(int event, Event *e);
  void        cancel();

  explicit NetAccept(const NetProcessor::AcceptOptions &);
  ~NetAccept() override { action_ = nullptr; }

private:
  int do_listen_impl(bool non_blocking);
};

extern Ptr<ProxyMutex>          naVecMutex;
extern std::vector<NetAccept *> naVec;
