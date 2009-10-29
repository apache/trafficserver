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



#include "diags.i"

class Tunnel;

#define MYCALL 12345

#define MAX_TUNNELS 10
ProxyMutexPtr tlock;
Tunnel *tunnels[MAX_TUNNELS];

#define MAX_CLIENTS 300
int verbose = 0;
int clients = 0;

// for debugging
#define LOCK_FAIL_RATE 0.05

#define MAYBE_FAIL_LOCK(_l) \
  if ((inku32)this_ethread()->generator.random() < \
     (inku32)(UINT_MAX * LOCK_FAIL_RATE)) \
    _l.release()

class Tunnel:public MTInteractor
{
public:
  Tunnel(ProxyMutex * amutex = NULL)
:  MTInteractor(amutex) {
  };
  virtual ~ Tunnel() {
  };
  virtual int attachClient(MTClient * m)
  {
    if (try_lock()) {
      m_clients.enqueue(m);
      m->setMTInteractor(this);
      unlock();
      return 1;
    } else {
      return 0;
    }
  };
  virtual int detachClient(MTClient * m)
  {
    if (try_lock()) {
      m_clients.remove(m);
      m->unsetMTInteractor();
      unlock();
      return 1;
    } else {
      return 0;
    }
  };
  int noClients()
  {
    return m_clients.head == NULL;
  };

  void callAllClients()
  {
    if (try_lock()) {
      MTClient *p;
      for (p = m_clients.head; p; p = p->link.next) {
        if (p->try_lock()) {
          p->handleEvent(MYCALL, 0);
          p->unlock();
        }
      }
      unlock();
    }
  };
private:
  Queue<MTClient> m_clients;
};


class Client:public MTClient
{
public:
  Client(ProxyMutex * amutex = NULL, int tun = 0)
:  MTClient(amutex)
    , m_join(NULL)
    , m_leave(NULL)
    , m_call(NULL)
    , m_die(NULL) {
    ink_atomic_increment(&clients, 1);
    m_thetun = tun;
    // we are a client that needs to join with the tunnel.
    SET_HANDLER(handleDetached);
    ink_hrtime rand = lrand48() % 300000 + 1;
      m_join = eventProcessor.schedule_in(this, HRTIME_MSECONDS(rand));
  };
  virtual ~ Client() {
    ink_atomic_increment(&clients, -1);
    if (clients < 5) {
      printf("%d clients left\n", clients);
    }
  };

  void clearTimers(int event, void *data)
  {
    if (event == EVENT_INTERVAL && data == m_join)
      m_join = NULL;
    if (event == EVENT_INTERVAL && data == m_leave)
      m_leave = NULL;
    if (event == EVENT_INTERVAL && data == m_call)
      m_call = NULL;
    if (event == EVENT_INTERVAL && data == m_die)
      m_die = NULL;
  }

  virtual int handleAttached(int event, void *data)
  {
    ink_release_assert(event != MYCALL);
    ink_release_assert(event == MTClient::e_attached);
    // we joined.
    clearTimers(event, data);
    if (verbose > 1)
      printf("%x got %d\n", this, event);
    if (verbose > 1)
      printf("%x joined %d\n", this, m_thetun);

    ink_release_assert(m_call == NULL);
    SET_HANDLER(handleCalls);
    ink_hrtime rand = lrand48() % 1000 + 1;
    m_call = eventProcessor.schedule_in(this, HRTIME_MSECONDS(rand));
    return EVENT_CONT;
  };

  int handleCalls(int event, void *data)
  {
    ink_release_assert(event == EVENT_INTERVAL && data == m_call || event == MYCALL);
    clearTimers(event, data);
    if (event == MYCALL) {
      if (verbose > 1)
        printf("%x got %d\n", this, event);
      return EVENT_CONT;
    }
    // call clients over random time intervals and then decide to leave.
    ink_hrtime leave_rand = lrand48() % 10;
    if (leave_rand <= 0) {      // 1/10 chance of leaving
      if (m_call) {
        m_call->cancel();       // cancel this.
        m_call = NULL;
      }
      SET_HANDLER(handleLeave);
      ink_hrtime rand = lrand48() % 1000 + 1;
      m_leave = eventProcessor.schedule_in(this, HRTIME_MSECONDS(rand));
      return EVENT_CONT;
    }
    ink_release_assert(m_call == NULL);
    // iterate over clients
    Tunnel *t = (Tunnel *) m_mti;
    ink_assert(t);
    t->callAllClients();
    ink_hrtime rand = lrand48() % 1000 + 1;
    m_call = eventProcessor.schedule_in(this, HRTIME_MSECONDS(rand));
    return EVENT_CONT;
  };

  int handleLeave(int event, void *data)
  {
    ink_release_assert(event == EVENT_INTERVAL && data == m_leave || event == MYCALL);
    ink_release_assert(m_call == NULL);
    // ignore MYCALLs
    if (event == MYCALL)
      return EVENT_CONT;

    clearTimers(event, data);
    if (verbose > 1)
      printf("%x left %d\n", this, m_thetun);
    return startDetach();
  };

  virtual int handleDetached(int event, void *data)
  {
    ink_release_assert(event != MYCALL);
    ink_release_assert(event == MTClient::e_detached || event == EVENT_INTERVAL && data == m_join);
    clearTimers(event, data);
    if (verbose > 1)
      printf("%x got %d\n", this, event);
    MUTEX_TRY_LOCK(l, tlock, this_ethread());
    MAYBE_FAIL_LOCK(l);
    if (!l) {
      m_join = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
      return EVENT_CONT;
    }
    if (tunnels[m_thetun]) {
      if (verbose > 1)
        printf("%x starting attach\n", this, event);
      return startAttach((MTInteractor *) tunnels[m_thetun]);
    } else {
      // no longer exists
      if (verbose > 0)
        printf("  %x: tunnel %d DEAD\n", this, m_thetun);
      SET_HANDLER(handleDie);
      m_die = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
      return EVENT_CONT;
    }
  };
  virtual int handleDie(int event, void *data)
  {
    ink_release_assert(event != MYCALL);
    ink_release_assert(event == EVENT_INTERVAL && data == m_die);
    clearTimers(event, data);
    ink_release_assert(m_join == NULL);
    ink_release_assert(m_leave == NULL);
    ink_release_assert(m_call == NULL);
    ink_release_assert(m_die == NULL);
    delete this;
    return EVENT_CONT;
  };
private:
  Action * m_join;
  Action *m_leave;
  Action *m_call;
  Action *m_die;
  int m_thetun;
};

class C:public Continuation
{
public:
  C(ProxyMutex * amutex, int tnum)
  : Continuation(amutex)
   , m_join(NULL)
   , m_leave(NULL)
   , m_tunnel(NULL)
  {
    m_thetun = tnum;
    m_tunnel = NEW(new Tunnel(amutex));
    SET_HANDLER(handleDie);
    ink_hrtime rand = lrand48() % 300000 + 1;
      m_die = eventProcessor.schedule_in(this, HRTIME_MSECONDS(rand));
  };
  virtual ~ C() {
    if (m_tunnel)
      delete m_tunnel;
    m_tunnel = NULL;
  };
  Tunnel *getTunnel()
  {
    return m_tunnel;
  }

  int handleDie(int event, void *data)
  {
    // we are tunnel, so we need to go through orderly teardown.
    MUTEX_TRY_LOCK(l, tlock, this_ethread());
    MAYBE_FAIL_LOCK(l);
    if (!l) {
      // retry until we get tunnel table lock
      m_die = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
      return EVENT_CONT;
    }
    // remove ourselves from table.
    tunnels[m_thetun] = NULL;
    printf("tunnel %d dying\n", m_thetun);
    // now signal death to the clients.
    SET_HANDLER(handleDieWaitClients);
    m_die = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    return EVENT_CONT;
  };
  int handleDieWaitClients(int event, void *data)
  {
    if (!m_tunnel->noClients()) {
      m_die = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
      return EVENT_CONT;
    }
    // no more clients
    printf("tunnel %d dead\n", m_thetun);
    delete this;
    return EVENT_DONE;
  };
private:
  int m_thetun;
  Action *m_join;
  Action *m_leave;
  Action *m_die;

  // only for servers
  Tunnel *m_tunnel;
};

int
main(int argc, char *argv[])
{
  (void) argc;
  (void) argv;
  int i;
  int num_net_threads = ink_number_of_processors();
  RecProcessInit(RECM_STAND_ALONE);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(num_net_threads);
  RecProcessStart();
  srand48(time(NULL));

  int tnum = 0;
  tlock = new_ProxyMutex();
  // set up tunnels that other clients should join with
  for (i = 0; i < MAX_TUNNELS; i++) {
    MUTEX_TAKE_LOCK(tlock, this_ethread());
    ProxyMutex *p = new_ProxyMutex();
    C *source = NEW(new C(p, i));
    tunnels[i] = source->getTunnel();
    MUTEX_UNTAKE_LOCK(tlock, this_ethread());
  }

  ink_release_assert(clients == 0);
  // now set up clients with particular tunnels to join
  for (i = 0; i < MAX_CLIENTS; i++) {
    ProxyMutex *p = new_ProxyMutex();
    Client *dest = NEW(new Client(p, i % MAX_TUNNELS));
  }
  ink_release_assert(clients == MAX_CLIENTS);

  this_thread()->execute();
}
