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
#include "ts/InkErrno.h"
#include "ts/ink_sock.h"

// For Stat Pages
#include "StatPages.h"

NetProcessor::AcceptOptions const NetProcessor::DEFAULT_ACCEPT_OPTIONS;

NetProcessor::AcceptOptions &
NetProcessor::AcceptOptions::reset()
{
  local_port = 0;
  local_ip.invalidate();
  accept_threads        = -1;
  ip_family             = AF_INET;
  etype                 = ET_NET;
  f_callback_on_open    = false;
  localhost_only        = false;
  frequent_accept       = true;
  backdoor              = false;
  recv_bufsize          = 0;
  send_bufsize          = 0;
  sockopt_flags         = 0;
  packet_mark           = 0;
  packet_tos            = 0;
  f_inbound_transparent = false;
  return *this;
}

int net_connection_number = 1;

unsigned int
net_next_connection_number()
{
  unsigned int res = 0;
  do {
    res = (unsigned int)ink_atomic_increment(&net_connection_number, 1);
  } while (!res);
  return res;
}

Action *
NetProcessor::accept(Continuation *cont, AcceptOptions const &opt)
{
  Debug("iocore_net_processor", "NetProcessor::accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0x", opt.local_port,
        opt.recv_bufsize, opt.send_bufsize, opt.sockopt_flags);

  return ((UnixNetProcessor *)this)->accept_internal(cont, NO_FD, opt);
}

Action *
NetProcessor::main_accept(Continuation *cont, SOCKET fd, AcceptOptions const &opt)
{
  UnixNetProcessor *this_unp = static_cast<UnixNetProcessor *>(this);
  Debug("iocore_net_processor", "NetProcessor::main_accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0x",
        opt.local_port, opt.recv_bufsize, opt.send_bufsize, opt.sockopt_flags);
  return this_unp->accept_internal(cont, fd, opt);
}

Action *
UnixNetProcessor::accept_internal(Continuation *cont, int fd, AcceptOptions const &opt)
{
  EventType upgraded_etype = opt.etype; // setEtype requires non-const ref.
  EThread *thread          = this_ethread();
  ProxyMutex *mutex        = thread->mutex;
  int accept_threads       = opt.accept_threads; // might be changed.
  IpEndpoint accept_ip;                          // local binding address.
  char thr_name[MAX_THREAD_NAME_LENGTH];

  NetAccept *na = createNetAccept();

  // Potentially upgrade to SSL.
  upgradeEtype(upgraded_etype);

  // Fill in accept thread from configuration if necessary.
  if (opt.accept_threads < 0) {
    REC_ReadConfigInteger(accept_threads, "proxy.config.accept_threads");
  }

  NET_INCREMENT_DYN_STAT(net_accepts_currently_open_stat);

  // We've handled the config stuff at start up, but there are a few cases
  // we must handle at this point.
  if (opt.localhost_only) {
    accept_ip.setToLoopback(opt.ip_family);
  } else if (opt.local_ip.isValid()) {
    accept_ip.assign(opt.local_ip);
  } else {
    accept_ip.setToAnyAddr(opt.ip_family);
  }
  ink_assert(0 < opt.local_port && opt.local_port < 65536);
  accept_ip.port() = htons(opt.local_port);

  na->accept_fn = net_accept; // All callers used this.
  na->server.fd = fd;
  ats_ip_copy(&na->server.accept_addr, &accept_ip);
  na->server.f_inbound_transparent = opt.f_inbound_transparent;
  if (opt.f_inbound_transparent) {
    Debug("http_tproxy", "Marking accept server %p on port %d as inbound transparent", na, opt.local_port);
  }

  int should_filter_int         = 0;
  na->server.http_accept_filter = false;
  REC_ReadConfigInteger(should_filter_int, "proxy.config.net.defer_accept");
  if (should_filter_int > 0 && opt.etype == ET_NET)
    na->server.http_accept_filter = true;

  na->action_          = new NetAcceptAction();
  *na->action_         = cont;
  na->action_->server  = &na->server;
  na->callback_on_open = opt.f_callback_on_open;
  na->recv_bufsize     = opt.recv_bufsize;
  na->send_bufsize     = opt.send_bufsize;
  na->sockopt_flags    = opt.sockopt_flags;
  na->packet_mark      = opt.packet_mark;
  na->packet_tos       = opt.packet_tos;
  na->etype            = upgraded_etype;
  na->backdoor         = opt.backdoor;
  if (na->callback_on_open)
    na->mutex = cont->mutex;
  if (opt.frequent_accept) { // true
    if (accept_threads > 0) {
      if (0 == na->do_listen(BLOCKING, opt.f_inbound_transparent)) {
        for (int i = 1; i < accept_threads; ++i) {
          NetAccept *a = na->clone();

          snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ACCEPT %d:%d]", i - 1, ats_ip_port_host_order(&accept_ip));
          a->init_accept_loop(thr_name);
          Debug("iocore_net_accept", "Created accept thread #%d for port %d", i, ats_ip_port_host_order(&accept_ip));
        }

        // Start the "template" accept thread last.
        Debug("iocore_net_accept", "Created accept thread #%d for port %d", accept_threads, ats_ip_port_host_order(&accept_ip));
        snprintf(thr_name, MAX_THREAD_NAME_LENGTH, "[ACCEPT %d:%d]", accept_threads - 1, ats_ip_port_host_order(&accept_ip));
        na->init_accept_loop(thr_name);
#if !TS_USE_POSIX_CAP
      } else if (fd == ts::NO_FD && opt.local_port < 1024 && 0 != geteuid()) {
        // TS-2054 - we can fail to bind a privileged port if we waited for cache and we tried
        // to open the socket in do_listen and we're not using libcap (POSIX_CAP) and so have reduced
        // privilege. Mention this to the admin.
        Warning("Failed to open reserved port %d due to lack of process privilege. Use POSIX capabilities if possible or disable "
                "wait_for_cache.",
                opt.local_port);
#endif // TS_USE_POSIX_CAP
      }
    } else {
      na->init_accept_per_thread(opt.f_inbound_transparent);
    }
  } else {
    na->init_accept(NULL, opt.f_inbound_transparent);
  }

#ifdef TCP_DEFER_ACCEPT
  // set tcp defer accept timeout if it is configured, this will not trigger an accept until there is
  // data on the socket ready to be read
  if (should_filter_int > 0) {
    setsockopt(na->server.fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &should_filter_int, sizeof(int));
  }
#endif
#ifdef TCP_INIT_CWND
  int tcp_init_cwnd = 0;
  REC_ReadConfigInteger(tcp_init_cwnd, "proxy.config.http.server_tcp_init_cwnd");
  if (tcp_init_cwnd > 0) {
    Debug("net", "Setting initial congestion window to %d", tcp_init_cwnd);
    if (setsockopt(na->server.fd, IPPROTO_TCP, TCP_INIT_CWND, &tcp_init_cwnd, sizeof(int)) != 0) {
      Error("Cannot set initial congestion window to %d", tcp_init_cwnd);
    }
  }
#endif
  return na->action_;
}

Action *
UnixNetProcessor::connect_re_internal(Continuation *cont, sockaddr const *target, NetVCOptions *opt)
{
  ProxyMutex *mutex      = cont->mutex;
  EThread *t             = mutex->thread_holding;
  UnixNetVConnection *vc = (UnixNetVConnection *)this->allocate_vc(t);

  if (opt)
    vc->options = *opt;
  else
    opt = &vc->options;

  // virtual function used to upgrade etype to ET_SSL for SSLNetProcessor.
  upgradeEtype(opt->etype);

  bool using_socks = (socks_conf_stuff->socks_needed && opt->socks_support != NO_SOCKS
#ifdef SOCKS_WITH_TS
                      && (opt->socks_version != SOCKS_DEFAULT_VERSION ||
                          /* This implies we are tunnelling.
                           * we need to connect using socks server even
                           * if this ip is in no_socks list.
                           */
                          !socks_conf_stuff->ip_map.contains(target))
#endif
                        );
  SocksEntry *socksEntry = NULL;

  NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
  vc->id          = net_next_connection_number();
  vc->submit_time = Thread::get_hrtime();
  vc->setSSLClientConnection(true);
  ats_ip_copy(&vc->server_addr, target);
  vc->mutex      = cont->mutex;
  Action *result = &vc->action_;

  if (using_socks) {
    char buff[INET6_ADDRPORTSTRLEN];
    Debug("Socks", "Using Socks ip: %s\n", ats_ip_nptop(target, buff, sizeof(buff)));
    socksEntry = socksAllocator.alloc();
    socksEntry->init(cont->mutex, vc, opt->socks_support, opt->socks_version); /*XXXX remove last two args */
    socksEntry->action_ = cont;
    cont                = socksEntry;
    if (!ats_is_ip(&socksEntry->server_addr)) {
      socksEntry->lerrno = ESOCK_NO_SOCK_SERVER_CONN;
      socksEntry->free();
      return ACTION_RESULT_DONE;
    }
    ats_ip_copy(&vc->server_addr, &socksEntry->server_addr);
    result      = &socksEntry->action_;
    vc->action_ = socksEntry;
  } else {
    Debug("Socks", "Not Using Socks %d \n", socks_conf_stuff->socks_needed);
    vc->action_ = cont;
  }

  if (t->is_event_type(opt->etype)) {
    MUTEX_TRY_LOCK(lock, cont->mutex, t);
    if (lock.is_locked()) {
      MUTEX_TRY_LOCK(lock2, get_NetHandler(t)->mutex, t);
      if (lock2.is_locked()) {
        int ret;
        ret = vc->connectUp(t, NO_FD);
        if ((using_socks) && (ret == CONNECT_SUCCESS))
          return &socksEntry->action_;
        else
          return ACTION_RESULT_DONE;
      }
    }
  }
  t->schedule_imm(vc);
  if (using_socks) {
    return &socksEntry->action_;
  } else
    return result;
}

Action *
UnixNetProcessor::connect(Continuation *cont, UnixNetVConnection ** /* avc */, sockaddr const *target, NetVCOptions *opt)
{
  return connect_re(cont, target, opt);
}

struct CheckConnect : public Continuation {
  UnixNetVConnection *vc;
  Action action_;
  MIOBuffer *buf;
  IOBufferReader *reader;
  int connect_status;
  int recursion;
  ink_hrtime timeout;

  int
  handle_connect(int event, Event *e)
  {
    connect_status = event;
    switch (event) {
    case NET_EVENT_OPEN:
      vc = (UnixNetVConnection *)e;
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
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *)e);
      break;

    case VC_EVENT_WRITE_READY:
      int sl, ret;
      socklen_t sz;
      if (!action_.cancelled) {
        sz  = sizeof(int);
        ret = getsockopt(vc->con.fd, SOL_SOCKET, SO_ERROR, (char *)&sl, &sz);
        if (!ret && sl == 0) {
          Debug("iocore_net_connect", "connection established");
          /* disable write on vc */
          vc->write.enabled = 0;
          vc->cancel_inactivity_timeout();
          // write_disable(get_NetHandler(this_ethread()), vc);
          /* clean up vc fields */
          vc->write.vio.nbytes = 0;
          vc->write.vio.op     = VIO::NONE;
          vc->write.vio.buffer.clear();

          action_.continuation->handleEvent(NET_EVENT_OPEN, vc);
          delete this;
          return EVENT_DONE;
        }
      }
      vc->do_io_close();
      if (!action_.cancelled)
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *)-ENET_CONNECT_FAILED);
      break;
    case VC_EVENT_INACTIVITY_TIMEOUT:
      Debug("iocore_net_connect", "connect timed out");
      vc->do_io_close();
      if (!action_.cancelled)
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *)-ENET_CONNECT_TIMEOUT);
      break;
    default:
      ink_assert(!"unknown connect event");
      if (!action_.cancelled)
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *)-ENET_CONNECT_FAILED);
    }
    if (!recursion)
      delete this;
    return EVENT_DONE;
  }

  Action *
  connect_s(Continuation *cont, sockaddr const *target, int _timeout, NetVCOptions *opt)
  {
    action_ = cont;
    timeout = HRTIME_SECONDS(_timeout);
    recursion++;
    netProcessor.connect_re(this, target, opt);
    recursion--;
    if (connect_status != NET_EVENT_OPEN_FAILED)
      return &action_;
    else {
      delete this;
      return ACTION_RESULT_DONE;
    }
  }

  CheckConnect(ProxyMutex *m = NULL) : Continuation(m), connect_status(-1), recursion(0), timeout(0)
  {
    SET_HANDLER(&CheckConnect::handle_connect);
    buf    = new_empty_MIOBuffer(1);
    reader = buf->alloc_reader();
  }

  ~CheckConnect()
  {
    buf->dealloc_all_readers();
    buf->clear();
    free_MIOBuffer(buf);
  }
};

Action *
NetProcessor::connect_s(Continuation *cont, sockaddr const *target, int timeout, NetVCOptions *opt)
{
  Debug("iocore_net_connect", "NetProcessor::connect_s called");
  CheckConnect *c = new CheckConnect(cont->mutex);
  return c->connect_s(cont, target, timeout, opt);
}

struct PollCont;

// This is a little odd, in that the actual threads are created before calling the processor.
int
UnixNetProcessor::start(int, size_t)
{
  EventType etype = ET_NET;

  netHandler_offset = eventProcessor.allocate(sizeof(NetHandler));
  pollCont_offset   = eventProcessor.allocate(sizeof(PollCont));

  // etype is ET_NET for netProcessor
  // and      ET_SSL for sslNetProcessor
  upgradeEtype(etype);

  n_netthreads = eventProcessor.n_threads_for_type[etype];
  netthreads   = eventProcessor.eventthread[etype];
  for (int i = 0; i < n_netthreads; ++i) {
    initialize_thread_for_net(netthreads[i]);
    extern void initialize_thread_for_http_sessions(EThread * thread, int thread_index);
    initialize_thread_for_http_sessions(netthreads[i], i);
  }

  RecData d;
  d.rec_int = 0;
  change_net_connections_throttle(NULL, RECD_INT, d, NULL);

  // Socks
  if (!netProcessor.socks_conf_stuff) {
    socks_conf_stuff = new socks_conf_struct;
    loadSocksConfiguration(socks_conf_stuff);
    if (!socks_conf_stuff->socks_needed && socks_conf_stuff->accept_enabled) {
      Warning("We can not have accept_enabled and socks_needed turned off"
              " disabling Socks accept\n");
      socks_conf_stuff->accept_enabled = 0;
    } else {
      // this is sslNetprocessor
      socks_conf_stuff = netProcessor.socks_conf_stuff;
    }
  }

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
  extern Action *register_ShowNet(Continuation * c, HTTPHdr * h);
  if (etype == ET_NET)
    statPagesManager.register_http("net", register_ShowNet);
  return 1;
}

// Virtual function allows creation of an
// SSLNetAccept or NetAccept transparent to NetProcessor.
NetAccept *
UnixNetProcessor::createNetAccept()
{
  return new NetAccept;
}

NetVConnection *
UnixNetProcessor::allocate_vc(EThread *t)
{
  UnixNetVConnection *vc;

  if (t) {
    vc = THREAD_ALLOC_INIT(netVCAllocator, t);
  } else {
    if (likely(vc = netVCAllocator.alloc())) {
      vc->from_accept_thread = true;
    }
  }

  return vc;
}

struct socks_conf_struct *NetProcessor::socks_conf_stuff = NULL;
int NetProcessor::accept_mss                             = 0;

UnixNetProcessor unix_netProcessor;
NetProcessor &netProcessor = unix_netProcessor;
