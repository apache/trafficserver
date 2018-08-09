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
#include "P_SSLNextProtocolAccept.h"

// For Stat Pages
#include "StatPages.h"

EventType ET_ACCEPT = ET_NET;
int net_accept_number = 0;
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
  tfo_queue_length      = 0;
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
  ProxyMutex *mutex  = this_ethread()->mutex.get();
  int accept_threads = opt.accept_threads; // might be changed.
  IpEndpoint accept_ip;                    // local binding address.

  NetAccept *na = createNetAccept(opt);
  na->id        = ink_atomic_increment(&net_accept_number, 1);
  Debug("iocore_net_accept", "creating new net accept number %d", na->id);

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

  if (opt.f_inbound_transparent) {
    Debug("http_tproxy", "Marked accept server %p on port %d as inbound transparent", na, opt.local_port);
  }

  int should_filter_int         = 0;
  na->server.http_accept_filter = false;
  REC_ReadConfigInteger(should_filter_int, "proxy.config.net.defer_accept");
  if (should_filter_int > 0 && opt.etype == ET_NET) {
    na->server.http_accept_filter = true;
  }

  SessionAccept *sa = dynamic_cast<SessionAccept *>(cont);
  na->proxyPort     = sa ? sa->proxyPort : nullptr;
  na->snpa          = dynamic_cast<SSLNextProtocolAccept *>(cont);

  na->action_         = new NetAcceptAction();
  *na->action_        = cont;
  na->action_->server = &na->server;

  if (na->opt.f_callback_on_open) {
    na->mutex = cont->mutex;
  }

  if (opt.frequent_accept) { // true
    if (accept_threads > 0) {
      na->init_accept_loop();
    } else {
      na->init_accept_per_thread();
    }
#if !TS_USE_POSIX_CAP
    if (fd == ts::NO_FD && opt.local_port < 1024 && 0 != geteuid()) {
      // TS-2054 - we can fail to bind a privileged port if we waited for cache and we tried
      // to open the socket in do_listen and we're not using libcap (POSIX_CAP) and so have reduced
      // privilege. Mention this to the admin.
      Warning("Failed to open reserved port %d due to lack of process privilege. Use POSIX capabilities if possible or disable "
              "wait_for_cache.",
              opt.local_port);
    }
#endif // TS_USE_POSIX_CAP
  } else {
    na->init_accept(nullptr);
  }

  {
    SCOPED_MUTEX_LOCK(lock, naVecMutex, this_ethread());
    naVec.push_back(na);
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

  return na->action_.get();
}

void
NetProcessor::stop_accept()
{
  for (auto &na : naVec) {
    na->stop_accept();
  }
}

Action *
UnixNetProcessor::connect_re_internal(Continuation *cont, sockaddr const *target, NetVCOptions *opt)
{
  if (unlikely(shutdown_event_system == true)) {
    return ACTION_RESULT_NONE;
  }
  EThread *t             = cont->mutex->thread_holding;
  UnixNetVConnection *vc = (UnixNetVConnection *)this->allocate_vc(t);

  if (opt) {
    vc->options = *opt;
  } else {
    opt = &vc->options;
  }

  vc->set_context(NET_VCONNECTION_OUT);
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
  SocksEntry *socksEntry = nullptr;

  vc->id          = net_next_connection_number();
  vc->submit_time = Thread::get_hrtime();
  vc->mutex       = cont->mutex;
  Action *result  = &vc->action_;
  // Copy target to con.addr,
  //   then con.addr will copy to vc->remote_addr by set_remote_addr()
  vc->con.setRemote(target);

  if (using_socks) {
    char buff[INET6_ADDRPORTSTRLEN];
    Debug("Socks", "Using Socks ip: %s", ats_ip_nptop(target, buff, sizeof(buff)));
    socksEntry = socksAllocator.alloc();
    // The socksEntry->init() will get the origin server addr by vc->get_remote_addr(),
    //   and save it to socksEntry->req_data.dest_ip.
    socksEntry->init(cont->mutex, vc, opt->socks_support, opt->socks_version); /*XXXX remove last two args */
    socksEntry->action_ = cont;
    cont                = socksEntry;
    if (!ats_is_ip(&socksEntry->server_addr)) {
      socksEntry->lerrno = ESOCK_NO_SOCK_SERVER_CONN;
      socksEntry->free();
      return ACTION_RESULT_DONE;
    }
    // At the end of socksEntry->init(), a socks server will be selected and saved to socksEntry->server_addr.
    // Therefore, we should set the remote to socks server in order to establish a connection with socks server.
    vc->con.setRemote(&socksEntry->server_addr.sa);
    result      = &socksEntry->action_;
    vc->action_ = socksEntry;
  } else {
    Debug("Socks", "Not Using Socks %d ", socks_conf_stuff->socks_needed);
    vc->action_ = cont;
  }

  if (t->is_event_type(opt->etype)) {
    MUTEX_TRY_LOCK(lock, cont->mutex, t);
    if (lock.is_locked()) {
      MUTEX_TRY_LOCK(lock2, get_NetHandler(t)->mutex, t);
      if (lock2.is_locked()) {
        int ret;
        ret = vc->connectUp(t, NO_FD);
        if ((using_socks) && (ret == CONNECT_SUCCESS)) {
          return &socksEntry->action_;
        } else {
          return ACTION_RESULT_DONE;
        }
      }
    }
  }
  // Try to stay on the current thread if it is the right type
  if (t->is_event_type(opt->etype)) {
    t->schedule_imm(vc);
  } else { // Otherwise, pass along to another thread of the right type
    eventProcessor.schedule_imm(vc, opt->etype);
  }
  if (using_socks) {
    return &socksEntry->action_;
  } else {
    return result;
  }
}

Action *
UnixNetProcessor::connect(Continuation *cont, UnixNetVConnection ** /* avc */, sockaddr const *target, NetVCOptions *opt)
{
  return connect_re(cont, target, opt);
}

struct PollCont;

// This needs to be called before the ET_NET threads are started.
void
UnixNetProcessor::init()
{
  EventType etype = ET_NET;

  netHandler_offset = eventProcessor.allocate(sizeof(NetHandler));
  pollCont_offset   = eventProcessor.allocate(sizeof(PollCont));

  if (0 == accept_mss) {
    REC_ReadConfigInteger(accept_mss, "proxy.config.net.sock_mss_in");
  }

  // NetHandler - do the global configuration initialization and then
  // schedule per thread start up logic. Global init is done only here.
  NetHandler::init_for_process();
  NetHandler::active_thread_types[ET_NET] = true;
  eventProcessor.schedule_spawn(&initialize_thread_for_net, etype);

  RecData d;
  d.rec_int = 0;
  change_net_connections_throttle(nullptr, RECD_INT, d, nullptr);

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

  /*
   * Stat pages
   */
  extern Action *register_ShowNet(Continuation * c, HTTPHdr * h);
  if (etype == ET_NET) {
    statPagesManager.register_http("net", register_ShowNet);
  }
}

int
UnixNetProcessor::start_accept(int n_accept_threads, size_t stacksize)
{
  ET_ACCEPT = eventProcessor.register_event_type("ET_ACCEPT");
  NetHandler::active_thread_types[ET_ACCEPT] = true;
  eventProcessor.schedule_spawn(&initialize_thread_for_accept, ET_ACCEPT);
  eventProcessor.spawn_event_threads(ET_ACCEPT, n_accept_threads, stacksize);

  return 0;
}

// Virtual function allows creation of an
// SSLNetAccept or NetAccept transparent to NetProcessor.
NetAccept *
UnixNetProcessor::createNetAccept(const NetProcessor::AcceptOptions &opt)
{
  return new NetAccept(opt);
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

struct socks_conf_struct *NetProcessor::socks_conf_stuff = nullptr;
int NetProcessor::accept_mss                             = 0;

UnixNetProcessor unix_netProcessor;
NetProcessor &netProcessor = unix_netProcessor;
