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
#include "P_NetAccept.h"
#include "P_Socks.h"
#include "P_UnixNet.h"
#include "P_UnixNetProcessor.h"
#include "P_UnixNetVConnection.h"
#include "iocore/net/SessionAccept.h"
#include "tscore/InkErrno.h"
#include "tscore/TSSystemState.h"
#include "P_SSLNextProtocolAccept.h"
#include "tscore/ink_atomic.h"

// For Stat Pages

// naVecMutext protects access to naVec.
Ptr<ProxyMutex> naVecMutex;

std::vector<NetAccept *> naVec;

unsigned int
net_next_connection_number()
{
  static int net_connection_number = 1;

  unsigned int res = 0;
  do {
    res = ink_atomic_increment(&net_connection_number, 1);
  } while (!res);
  return res;
}

NetProcessor::AcceptOptions const NetProcessor::DEFAULT_ACCEPT_OPTIONS;

namespace
{

DbgCtl dbg_ctl_iocore_net_processor{"iocore_net_processor"};
DbgCtl dbg_ctl_iocore_net_accept{"iocore_net_accept"};
DbgCtl dbg_ctl_http_tproxy{"http_tproxy"};
DbgCtl dbg_ctl_Socks{"Socks"};

} // end anonymous namespace

Action *
UnixNetProcessor::accept(Continuation *cont, AcceptOptions const &opt)
{
  Dbg(dbg_ctl_iocore_net_processor, "NetProcessor::accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0x",
      opt.local_port, opt.recv_bufsize, opt.send_bufsize, opt.sockopt_flags);

  return accept_internal(cont, NO_FD, opt);
}

Action *
UnixNetProcessor::main_accept(Continuation *cont, SOCKET fd, AcceptOptions const &opt)
{
  Dbg(dbg_ctl_iocore_net_processor, "NetProcessor::main_accept - port %d,recv_bufsize %d, send_bufsize %d, sockopt 0x%0x",
      opt.local_port, opt.recv_bufsize, opt.send_bufsize, opt.sockopt_flags);
  return accept_internal(cont, fd, opt);
}

Action *
UnixNetProcessor::accept_internal(Continuation *cont, int fd, AcceptOptions const &opt)
{
  static int net_accept_number = 0;
  int        accept_threads    = opt.accept_threads; // might be changed.
  int        listen_per_thread = 0;
  IpEndpoint accept_ip; // local binding address.

  NetAccept *na = createNetAccept(opt);
  na->id        = ink_atomic_increment(&net_accept_number, 1);
  Dbg(dbg_ctl_iocore_net_accept, "creating new net accept number %d", na->id);

  // Fill in accept thread from configuration if necessary.
  if (opt.accept_threads < 0) {
    accept_threads = RecGetRecordInt("proxy.config.accept_threads").value_or(0);
  }
  listen_per_thread = RecGetRecordInt("proxy.config.exec_thread.listen").value_or(0);
  if (accept_threads > 0 && listen_per_thread > 0) {
    Fatal("Please disable accept_threads or exec_thread.listen");
  }

  Metrics::Gauge::increment(net_rsb.accepts_currently_open);

  // We've handled the config stuff at start up, but there are a few cases
  // we must handle at this point.
  if (opt.ip_family == AF_UNIX) {
    accept_ip.assign(opt.local_path);
  } else if (opt.localhost_only) {
    accept_ip.setToLoopback(opt.ip_family);
  } else if (opt.local_ip.isValid()) {
    accept_ip.assign(opt.local_ip);
  } else {
    accept_ip.setToAnyAddr(opt.ip_family);
  }
  ink_assert(opt.ip_family == AF_UNIX || (0 < opt.local_port && opt.local_port < 65536));
  accept_ip.network_order_port() = htons(opt.local_port);

  na->accept_fn   = net_accept; // All callers used this.
  na->server.sock = UnixSocket{fd};
  ats_ip_copy(&na->server.accept_addr, &accept_ip);

  if (opt.f_inbound_transparent) {
    Dbg(dbg_ctl_http_tproxy, "Marked accept server %p on port %d as inbound transparent", na, opt.local_port);
  }

  if (opt.f_proxy_protocol) {
    Dbg(dbg_ctl_http_tproxy, "Marked accept server %p on port %d for proxy protocol", na, opt.local_port);
  }

  SessionAccept *sa = dynamic_cast<SessionAccept *>(cont);
  na->proxyPort     = sa ? sa->proxyPort : nullptr;
  na->snpa          = dynamic_cast<SSLNextProtocolAccept *>(cont);

  na->action_         = new NetAcceptAction();
  *na->action_        = cont;
  na->action_->server = &na->server;

  if (opt.frequent_accept) { // true
    if (accept_threads > 0 && listen_per_thread == 0) {
      na->init_accept_loop();
    } else {
      na->init_accept_per_thread();
    }
#if !TS_USE_POSIX_CAP
    if (fd == ts::NO_FD && opt.local_port < 1024 && 0 != geteuid() && opt.ip_family != AF_UNIX) {
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

  return na->action_.get();
}

void
UnixNetProcessor::stop_accept()
{
  SCOPED_MUTEX_LOCK(lock, naVecMutex, this_ethread());
  for (auto &na : naVec) {
    na->stop_accept();
  }
}

Action *
UnixNetProcessor::connect_re(Continuation *cont, sockaddr const *target, NetVCOptions const &opt)
{
  if (TSSystemState::is_event_system_shut_down()) {
    return nullptr;
  }

  EThread            *t  = eventProcessor.assign_affinity_by_type(cont, opt.etype);
  UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(this->allocate_vc(t));

  vc->options = opt;

  vc->set_context(NET_VCONNECTION_OUT);

  const bool using_socks = (socks_conf_stuff->socks_needed && opt.socks_support != NO_SOCKS &&
                            (opt.socks_version != SOCKS_DEFAULT_VERSION ||
                             /* This implies we are tunnelling.
                              * we need to connect using socks server even
                              * if this ip is in no_socks list.
                              */
                             !socks_conf_stuff->ip_addrs.contains(swoc::IPAddr(target))));

  SocksEntry *socksEntry = nullptr;

  vc->id          = net_next_connection_number();
  vc->submit_time = ink_get_hrtime();
  vc->mutex       = cont->mutex;
  Action *result  = &vc->action_;
  // Copy target to con.addr,
  //   then con.addr will copy to vc->remote_addr by set_remote_addr()
  vc->con.setRemote(target);

  if (using_socks) {
    char buff[INET6_ADDRPORTSTRLEN];
    Dbg(dbg_ctl_Socks, "Using Socks ip: %s", ats_ip_nptop(target, buff, sizeof(buff)));
    socksEntry = socksAllocator.alloc();
    // The socksEntry->init() will get the origin server addr by vc->get_remote_addr(),
    //   and save it to socksEntry->req_data.dest_ip.
    socksEntry->init(cont->mutex, vc, opt.socks_support, opt.socks_version); /*XXXX remove last two args */
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
    Dbg(dbg_ctl_Socks, "Not Using Socks %d ", socks_conf_stuff->socks_needed);
    vc->action_ = cont;
  }

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

  t->schedule_imm(vc);

  if (using_socks) {
    return &socksEntry->action_;
  } else {
    return result;
  }
}

// This needs to be called before the ET_NET threads are started.
void
UnixNetProcessor::init()
{
  naVecMutex = new_ProxyMutex();

  netHandler_offset = eventProcessor.allocate(sizeof(NetHandler));
  pollCont_offset   = eventProcessor.allocate(sizeof(PollCont));

  if (0 == accept_mss) {
    accept_mss = RecGetRecordInt("proxy.config.net.sock_mss_in").value_or(0);
  }

  // NetHandler - do the global configuration initialization and then
  // schedule per thread start up logic. Global init is done only here.
  NetHandler::init_for_process();
  NetHandler::active_thread_types[ET_NET] = true;
  eventProcessor.schedule_spawn(&initialize_thread_for_net, ET_NET);

  RecData d;
  d.rec_int = 0;
  change_net_connections_throttle(nullptr, RECD_INT, d, nullptr);
}

void
UnixNetProcessor::init_socks()
{
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
int                       NetProcessor::accept_mss       = 0;

UnixNetProcessor unix_netProcessor;
NetProcessor    &netProcessor = unix_netProcessor;
