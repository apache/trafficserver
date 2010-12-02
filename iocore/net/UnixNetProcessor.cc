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

#include "P_Net.h"

// For Stat Pages
#ifdef NON_MODULAR
#include "StatPages.h"
#endif

NetProcessor::AcceptOptions const NetProcessor::DEFAULT_ACCEPT_OPTIONS;

NetProcessor::AcceptOptions&
NetProcessor::AcceptOptions::reset()
{
  port = 0;
  accept_threads = 0;
  domain = AF_INET;
  etype = ET_NET;
  f_callback_on_open = false;
  recv_bufsize = 0;
  send_bufsize = 0;
  sockopt_flags = 0;
  f_outbound_transparent = false;
  f_inbound_transparent = false;
  return *this;
}


int net_connection_number = 1;
unsigned int
net_next_connection_number()
{
  unsigned int res = 0;
  do {
    res = (unsigned int)
      ink_atomic_increment(&net_connection_number, 1);
  } while (!res);
  return res;
}

Action *
NetProcessor::accept(Continuation * cont,
                     int port,
                     int domain,
                     bool frequent_accept,
                     unsigned int accept_ip,
                     char *accept_ip_str,
                     bool callback_on_open,
                     SOCKET listen_socket_in,
                     int accept_pool_size,
                     bool accept_only,
                     sockaddr * bound_sockaddr,
                     int *bound_sockaddr_size,
                     int recv_bufsize, int send_bufsize, uint32 sockopt_flags, EventType etype)
{
  (void) listen_socket_in;      // NT only
  (void) accept_pool_size;      // NT only
  (void) accept_only;           // NT only
  (void) bound_sockaddr;        // NT only
  (void) bound_sockaddr_size;   // NT only
  Debug("iocore_net_processor",
           "NetProcessor::accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0lX",
           port, recv_bufsize, send_bufsize, sockopt_flags
           );

  AcceptOptions opt;
  opt.port = port;
  opt.domain = domain;
  opt.etype = etype;
  opt.f_callback_on_open = callback_on_open;
  opt.recv_bufsize = recv_bufsize;
  opt.send_bufsize = send_bufsize;
  opt.sockopt_flags = opt.sockopt_flags;
  return ((UnixNetProcessor *) this)->accept_internal(cont, NO_FD,
                                                      bound_sockaddr,
                                                      bound_sockaddr_size,
                                                      frequent_accept,
                                                      net_accept,
                                                      accept_ip,
                                                      accept_ip_str,
                                                      opt
                                                      );
}

Action *
NetProcessor::main_accept(Continuation * cont, SOCKET fd,
                          sockaddr * bound_sockaddr, int *bound_sockaddr_size,
                          bool accept_only,
                          AcceptOptions const& opt
                          )
{
  (void) accept_only;           // NT only
  Debug("iocore_net_processor", "NetProcessor::main_accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0lX",
        opt.port, opt.recv_bufsize, opt.send_bufsize, opt.sockopt_flags);
  return ((UnixNetProcessor *) this)->accept_internal(cont, fd,
                                                      bound_sockaddr,
                                                      bound_sockaddr_size,
                                                      true,
                                                      net_accept,
                                                      ((UnixNetProcessor *) this)->incoming_ip_to_bind_saddr,
                                                      ((UnixNetProcessor *) this)->incoming_ip_to_bind,
                                                      opt
                                                      );
}



Action *
UnixNetProcessor::accept_internal(Continuation * cont,
                                  int fd,
                                  struct sockaddr * bound_sockaddr,
                                  int *bound_sockaddr_size,
                                  bool frequent_accept,
                                  AcceptFunction fn,
                                  unsigned int accept_ip,
                                  char *accept_ip_str,
                                  AcceptOptions const& opt
                                  )
{
  EventType et = opt.etype; // setEtype requires non-const ref.
  NetAccept *na = createNetAccept();
  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  // Potentially upgrade to SSL.
  upgradeEtype(et);

  NET_INCREMENT_DYN_STAT(net_accepts_currently_open_stat);
  na->port = opt.port;
  na->domain = opt.domain;
  na->accept_fn = fn;
  na->server.fd = fd;
  na->server.accept_ip = accept_ip;
  na->server.accept_ip_str = accept_ip_str;
  na->server.f_outbound_transparent = opt.f_outbound_transparent;
  na->server.f_inbound_transparent = opt.f_inbound_transparent;
  if (opt.f_outbound_transparent) Debug("http_tproxy", "Marking accept server %x on port %d as outbound transparent.\n", na, opt.port);
  na->action_ = NEW(new NetAcceptAction());
  *na->action_ = cont;
  na->action_->server = &na->server;
  na->callback_on_open = opt.f_callback_on_open;
  na->recv_bufsize = opt.recv_bufsize;
  na->send_bufsize = opt.send_bufsize;
  na->sockopt_flags = opt.sockopt_flags;
  na->etype = opt.etype;
  if (na->callback_on_open)
    na->mutex = cont->mutex;
  if (frequent_accept) { // true
    if (opt.accept_threads > 0)  {
      if (0 == na->do_listen(BLOCKING)) {
        NetAccept *a;

        for (int i=1; i < opt.accept_threads; ++i) {
          a = NEW(new NetAccept);
          *a = *na;
          a->init_accept_loop();
          Debug("iocore_net_accept", "Created accept thread #%d for port %d", i, opt.port);
        }
        // Start the "template" accept thread last.
        Debug("iocore_net_accept", "Created accept thread #%d for port %d", opt.accept_threads, opt.port);
        na->init_accept_loop();
      }
    } else {
      na->init_accept_per_thread();
    }
  } else
    na->init_accept();
  if (bound_sockaddr && bound_sockaddr_size)
    safe_getsockname(na->server.fd, bound_sockaddr, bound_sockaddr_size);

#ifdef TCP_DEFER_ACCEPT
  // set tcp defer accept timeout if it is configured, this will not trigger an accept until there is
  // data on the socket ready to be read
  int accept_timeout = 0;
  IOCORE_ReadConfigInteger(accept_timeout, "proxy.config.net.defer_accept");
  if (accept_timeout > 0) {
    setsockopt(na->server.fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &accept_timeout, sizeof(int));
  }
#endif
  return na->action_;
}

Action *
UnixNetProcessor::connect_re_internal(Continuation * cont,
                                      unsigned int ip, int port,  NetVCOptions * opt)
{
  ProxyMutex *mutex = cont->mutex;
  EThread *t = mutex->thread_holding;
  UnixNetVConnection *vc = allocateThread(t);

  if (opt)
    vc->options = *opt;
  else
    opt = &vc->options;

  // virtual function used to upgrade etype to ET_SSL for SSLNetProcessor.
  upgradeEtype(opt->etype);

#ifndef INK_NO_SOCKS
  bool using_socks = (socks_conf_stuff->socks_needed && opt->socks_support != NO_SOCKS
#ifdef SOCKS_WITH_TS
                      && (opt->socks_version != SOCKS_DEFAULT_VERSION ||
                          /* This implies we are tunnelling.
                           * we need to connect using socks server even
                           * if this ip is in no_socks list.
                           */
                          !socks_conf_stuff->ip_range.match(ip))
#endif
    );
  SocksEntry *socksEntry = NULL;
#endif
  NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
  vc->id = net_next_connection_number();
  vc->submit_time = ink_get_hrtime();
  vc->setSSLClientConnection(true);
  vc->ip = ip;
  vc->port = port;
  vc->mutex = cont->mutex;
  Action *result = &vc->action_;
#ifndef INK_NO_SOCKS
  if (using_socks) {
    Debug("Socks", "Using Socks ip: %u.%u.%u.%u:%d\n", PRINT_IP(ip), port);
    socksEntry = socksAllocator.alloc();
    socksEntry->init(cont->mutex, vc, opt->socks_support, opt->socks_version);        /*XXXX remove last two args */
    socksEntry->action_ = cont;
    cont = socksEntry;
    if (socksEntry->server_ip == (uint32) - 1) {
      socksEntry->lerrno = ESOCK_NO_SOCK_SERVER_CONN;
      socksEntry->free();
      return ACTION_RESULT_DONE;
    }
    vc->ip = socksEntry->server_ip;
    vc->port = socksEntry->server_port;
    result = &socksEntry->action_;
    vc->action_ = socksEntry;
  } else {
    Debug("Socks", "Not Using Socks %d \n", socks_conf_stuff->socks_needed);
    vc->action_ = cont;
  }
#else
  vc->action_ = cont;
#endif /*INK_NO_SOCKS */

  if (t->is_event_type(opt->etype)) {
    MUTEX_TRY_LOCK(lock, cont->mutex, t);
    if (lock) {
      MUTEX_TRY_LOCK(lock2, get_NetHandler(t)->mutex, t);
      if (lock2) {
        int ret;
        ret = vc->connectUp(t);
#ifndef INK_NO_SOCKS
        if ((using_socks) && (ret == CONNECT_SUCCESS))
          return &socksEntry->action_;
        else
#endif
          return ACTION_RESULT_DONE;
      }
    }
  }
  eventProcessor.schedule_imm(vc, opt->etype);
#ifndef INK_NO_SOCKS
  if (using_socks) {
    return &socksEntry->action_;
  } else
#endif
    return result;
}

Action *
UnixNetProcessor::connect(Continuation * cont,
                          UnixNetVConnection ** avc,
                          unsigned int ip, int port, NetVCOptions * opt)
{
  NOWARN_UNUSED(avc);
  return connect_re(cont, ip, port, opt);
}

struct CheckConnect:public Continuation
{
  UnixNetVConnection *vc;
  Action action_;
  MIOBuffer *buf;
  IOBufferReader *reader;
  int connect_status;
  int recursion;
  ink_hrtime timeout;

  int handle_connect(int event, Event * e)
  {
    connect_status = event;
    switch (event) {
    case NET_EVENT_OPEN:
      vc = (UnixNetVConnection *) e;
      Debug("iocore_net_connect", "connect Net open");
      vc->do_io_write(this, 10, /* some non-zero number just to get the poll going */
                      reader);
      /* dont wait for more than timeout secs */
      vc->set_inactivity_timeout(timeout);
      return EVENT_CONT;
      break;

      case NET_EVENT_OPEN_FAILED:
        Debug("iocore_net_connect", "connect Net open failed");
      if (!action_.cancelled)
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) e);
      break;

      case VC_EVENT_WRITE_READY:int sl, ret;
      socklen_t sz;
      if (!action_.cancelled)
      {
        sz = sizeof(int);
          ret = getsockopt(vc->con.fd, SOL_SOCKET, SO_ERROR, (char *) &sl, &sz);
        if (!ret && sl == 0)
        {
          Debug("iocore_net_connect", "connection established");
          /* disable write on vc */
          vc->write.enabled = 0;
          vc->cancel_inactivity_timeout();
          //write_disable(get_NetHandler(this_ethread()), vc);
          /* clean up vc fields */
          vc->write.vio.nbytes = 0;
          vc->write.vio.op = VIO::NONE;
          vc->write.vio.buffer.clear();


          action_.continuation->handleEvent(NET_EVENT_OPEN, vc);
          delete this;
            return EVENT_DONE;
        }
      }
      vc->do_io_close();
      if (!action_.cancelled)
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) -ENET_CONNECT_FAILED);
      break;
    case VC_EVENT_INACTIVITY_TIMEOUT:
      Debug("iocore_net_connect", "connect timed out");
      vc->do_io_close();
      if (!action_.cancelled)
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) -ENET_CONNECT_TIMEOUT);
      break;
    default:
      ink_debug_assert(!"unknown connect event");
      if (!action_.cancelled)
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) -ENET_CONNECT_FAILED);

    }
    if (!recursion)
      delete this;
    return EVENT_DONE;
  }

  Action *connect_s(Continuation * cont, unsigned int ip, int port,
                    int _timeout, NetVCOptions * opt)
  {
    action_ = cont;
    timeout = HRTIME_MSECONDS(_timeout);
    recursion++;
    netProcessor.connect_re(this, ip, port, opt);
    recursion--;
    if (connect_status != NET_EVENT_OPEN_FAILED)
      return &action_;
    else {
      delete this;
      return ACTION_RESULT_DONE;
    }
  }

  CheckConnect(ProxyMutex * m = NULL):Continuation(m), connect_status(-1), recursion(0), timeout(0) {
    SET_HANDLER(&CheckConnect::handle_connect);
    buf = new_empty_MIOBuffer(1);
    reader = buf->alloc_reader();
  }

  ~CheckConnect() {
    buf->dealloc_all_readers();
    buf->clear();
    free_MIOBuffer(buf);
  }
};

Action *
NetProcessor::connect_s(Continuation * cont, unsigned int ip,
                        int port, int timeout, NetVCOptions * opt)
{
  Debug("iocore_net_connect", "NetProcessor::connect_s called");
  CheckConnect *c = NEW(new CheckConnect(cont->mutex));
  return c->connect_s(cont, ip, port, timeout, opt);
}



struct PollCont;

int
UnixNetProcessor::start(int)
{
  EventType etype = ET_NET;

  netHandler_offset = eventProcessor.allocate(sizeof(NetHandler));
  pollCont_offset = eventProcessor.allocate(sizeof(PollCont));

  // etype is ET_NET for netProcessor
  // and      ET_SSL for sslNetProcessor
  upgradeEtype(etype);

  n_netthreads = eventProcessor.n_threads_for_type[etype];
  netthreads = eventProcessor.eventthread[etype];
  for (int i = 0; i < n_netthreads; i++) {
    initialize_thread_for_net(netthreads[i], i);
  }

  if ((incoming_ip_to_bind = IOCORE_ConfigReadString("proxy.local.incoming_ip_to_bind")) != 0)
    incoming_ip_to_bind_saddr = inet_addr(incoming_ip_to_bind);
  else
    incoming_ip_to_bind_saddr = 0;

  RecData d;
  d.rec_int = 0;
  change_net_connections_throttle(NULL, RECD_INT, d, NULL);

  // Socks
#ifndef INK_NO_SOCKS
  if (!netProcessor.socks_conf_stuff) {
    socks_conf_stuff = NEW(new socks_conf_struct);
    loadSocksConfiguration(socks_conf_stuff);
    if (!socks_conf_stuff->socks_needed && socks_conf_stuff->accept_enabled) {
      Warning("We can not have accept_enabled and socks_needed turned off" " disabling Socks accept\n");
      socks_conf_stuff->accept_enabled = 0;
    } else {
      // this is sslNetprocessor
      socks_conf_stuff = netProcessor.socks_conf_stuff;
    }
  }
#endif /*INK_NO_SOCKS */
  // commented by vijay -  bug 2489945
  /*if (use_accept_thread) // 0
     { NetAccept * na = createNetAccept();
     SET_CONTINUATION_HANDLER(na,&NetAccept::acceptLoopEvent);
     accept_thread_event = eventProcessor.spawn_thread(na);
     if (!accept_thread_event) delete na;
     } */


/*
 * Stat pages
 */
#ifdef NON_MODULAR
  extern Action *register_ShowNet(Continuation * c, HTTPHdr * h);
  if (etype == ET_NET)
    statPagesManager.register_http("net", register_ShowNet);
#endif
  return 1;
}

// Functions all THREAD_FREE and THREAD_ALLOC to be performed
// for both SSL and regular UnixNetVConnection transparent to
// netProcessor connect functions. Yes it looks goofy to
// have them in both places, but it saves a bunch of
// code from being duplicated.
UnixNetVConnection *
UnixNetProcessor::allocateThread(EThread * t)
{
  return ((UnixNetVConnection *) THREAD_ALLOC(netVCAllocator, t));
}

void
UnixNetProcessor::freeThread(UnixNetVConnection * vc, EThread * t)
{
  ink_assert(!vc->from_accept_thread);
  THREAD_FREE(vc, netVCAllocator, t);
}

// Virtual function allows creation of an
// SSLNetAccept or NetAccept transparent to NetProcessor.
NetAccept *
UnixNetProcessor::createNetAccept()
{
  return (NEW(new NetAccept));
}

struct socks_conf_struct *
NetProcessor::socks_conf_stuff = NULL;
int NetProcessor::accept_mss = 0;

UnixNetProcessor unix_netProcessor;
NetProcessor & netProcessor = unix_netProcessor;
