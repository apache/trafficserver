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

#include "iocore/net/NetProcessor.h"
#include "P_Connection.h"
#include "Server.h"

#include "records/RecCore.h"

#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/UnixSocket.h"

#include "tsutil/DbgCtl.h"

#include "tscore/Diags.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_config.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_sock.h"

#if TS_USE_HWLOC
#include <hwloc.h>
#endif

#ifdef SO_ACCEPTFILTER
#include <sys/param.h>
#include <sys/linker.h>
#endif

#if TS_USE_NUMA
#include <numa.h>
#include <numaif.h>
#include <linux/bpf.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <queue>
#include <vector>
#include <algorithm>
#endif

namespace
{
DbgCtl dbg_ctl_connection{"connection"};
DbgCtl dbg_ctl_http_tproxy{"http_tproxy"};
DbgCtl dbg_ctl_iocore_net_server{"iocore_net_server"};
DbgCtl dbg_ctl_iocore_thread{"iocore_thread"};
DbgCtl dbg_ctl_proxyprotocol{"proxyprotocol"};

} // end anonymous namespace

extern class EventProcessor eventProcessor;

static int get_listen_backlog();
static int add_http_filter(int fd);

int
Server::accept(Connection *c)
{
  int       res = 0;
  socklen_t sz  = sizeof(c->addr);

  res = sock.accept4(&c->addr.sa, &sz, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (res < 0) {
    return res;
  }
  c->sock = UnixSocket{res};
  if (dbg_ctl_iocore_net_server.on()) {
    ip_port_text_buffer ipb1, ipb2;
    DbgPrint(dbg_ctl_iocore_net_server, "Connection accepted [Server]. %s -> %s", ats_ip_nptop(&c->addr, ipb2, sizeof(ipb2)),
             ats_ip_nptop(&addr, ipb1, sizeof(ipb1)));
  }

  return 0;
}

int
Server::close()
{
  return sock.close();
}

#if TS_USE_NUMA
// Assumes that threads can be assigned to NUMA zones as 0,1,2,3,0,1,2,3,0,1,2 sequence with no gaps.

class NUMASequencer
{
  std::mutex              mutex;
  std::condition_variable convar;
  std::vector<int>        thread_ids;           // To store thread IDs
  size_t                  cur_index    = 0;     // Index to track the current thread to execute
  bool                    initialized  = false; // Flag to ensure initialization happens once
  bool                    ready_to_run = false; // Flag to ensure threads only start executing when all IDs are collected

public:
  template <class T>
  bool
  run_sequential(T func)
  {
    std::unique_lock<std::mutex> lock(mutex);

    int my_thread_id = this_ethread()->id;
    int my_numa_node = this_ethread()->get_numa_node();

    Debug("numa_sequencer", "[NUMASequencer] Thread %d (NUMA node %d) entered run_sequential.", my_thread_id, my_numa_node);

    // Initialize and populate the thread IDs vector
    if (!initialized) {
      initialized = true;
      thread_ids.reserve(eventProcessor.net_threads); // Preallocate space
      Debug("numa_sequencer", "[NUMASequencer] Initialized thread ID vector with capacity %d.", eventProcessor.net_threads);
    }

    // Add the current thread ID to the list if it's not already present
    if (std::find(thread_ids.begin(), thread_ids.end(), my_thread_id) == thread_ids.end()) {
      thread_ids.push_back(my_thread_id);
      Debug("numa_sequencer", "[NUMASequencer] Added Thread %d to the thread ID list. Total threads collected: %zu", my_thread_id,
            thread_ids.size());
    }

    // If all threads have been added (assuming their number is equal to eventProcessor.net_threads), sort the thread IDs and set
    // ready_to_run to true
    if (thread_ids.size() == eventProcessor.net_threads) {
      std::sort(thread_ids.begin(), thread_ids.end());
      Debug("numa_sequencer", "[NUMASequencer] All thread IDs collected. Sorting thread IDs...");
      Debug("numa_sequencer", "[NUMASequencer] Thread IDs sorted. Execution will follow this order:");
      for (size_t i = 0; i < thread_ids.size(); ++i) {
        Debug("numa_sequencer", "[NUMASequencer] Execution order %zu: Thread ID %d", i + 1, thread_ids[i]);
      }
      ready_to_run = true;
      convar.notify_all(); // Notify all threads that execution can begin
    }

    // Wait until all thread IDs are collected and ready_to_run is true
    while (!ready_to_run) {
      Debug("numa_sequencer", "[NUMASequencer] Thread %d is waiting for all thread IDs to be collected.", my_thread_id);
      convar.wait(lock);
    }

    // Logging the current state before entering the wait loop
    Debug("numa_sequencer", "[NUMASequencer] Thread %d (NUMA node %d) waiting to execute. Current sequence index: %zu",
          my_thread_id, my_numa_node, cur_index);

    // Wait until it's this thread's turn based on sorted IDs
    while (cur_index < thread_ids.size() && thread_ids[cur_index] != my_thread_id) {
      Debug("numa_sequencer", "[NUMASequencer] Thread %d is not yet in sequence. Waiting...", my_thread_id);
      convar.wait(lock);
    }

    // Log when the thread has been awakened and is about to execute the function
    Debug("numa_sequencer", "[NUMASequencer] Thread %d (NUMA node %d) awakened. About to execute function.", my_thread_id,
          my_numa_node);

    // Execute the function
    bool result = func();

    // More detailed logging for debugging
    if (result) {
      Debug("numa_sequencer", "[NUMASequencer] Thread %d successfully executed the function on NUMA node %d.", this_ethread()->id,
            my_numa_node);
    } else {
      Error("[NUMASequencer] Thread %d failed to execute the function on NUMA node %d.", this_ethread()->id, my_numa_node);
    }

    // Move to the next thread in the sequence
    cur_index++;
    Debug("numa_sequencer", "[NUMASequencer] Thread %d completed execution. Moving to next thread. New index: %zu.", my_thread_id,
          cur_index);

    // If we've completed one pass through all threads, reset the index and increment the repeat counter
    if (cur_index >= thread_ids.size()) {
      cur_index = 0;
      Debug("numa_sequencer", "[NUMASequencer] Completed a full pass through all threads. Resetting index.");
    }

    // Notify all threads about the change in sequence
    convar.notify_all();

    return result;
  }
};
NUMASequencer numa_sequencer;
#endif

int
Server::listen(bool non_blocking, const NetProcessor::AcceptOptions &opt)
{
  ink_assert(!sock.is_ok());
  int       res = 0;
  socklen_t namelen;
  int       prot = IPPROTO_TCP;
#if TS_USE_NUMA
  int  use_ebpf = 0;
  int  affinity = 1;
  bool success  = false;

  // Define the initialization function that will be run sequentially
  auto init_func = [&]() -> bool {
    // Additional setup after listen
    Debug("numa", "[Server::listen] Attempting to set up fd after listen with options: %d", opt);
    if ((res = setup_fd_after_listen(opt)) < 0) {
      Error("[Server::listen] Failed to setup fd after listen: %d", res);
      return false;
    }

    Debug("numa", "[Server::listen] Thread %d successfully set up the socket.", this_ethread()->id);
    return true;
  };
#endif
  // Set the IP address for binding
  if (!ats_is_ip(&accept_addr)) {
    ats_ip4_set(&addr, INADDR_ANY, 0);
  } else {
    ats_ip_copy(&addr, &accept_addr);
  }
  // Set protocol for MPTCP if enabled
  if (opt.f_mptcp) {
    Dbg(dbg_ctl_connection, "Define socket with MPTCP");
    prot = IPPROTO_MPTCP;
  }

  // Create the socket
  Debug("numa", "[Server::listen] Attempting to create socket with family: %d, type: %d, protocol: %d", addr.sa.sa_family,
        SOCK_STREAM, prot);
  sock = UnixSocket{addr.sa.sa_family, SOCK_STREAM, prot};
  if (!sock.is_ok()) {
    Error("[Server::listen] Failed to create socket: %d", res);
    goto Lerror;
  }

  // Set up the file descriptor for listening
  Debug("numa", "[Server::listen] Attempting to set up fd for listen with non_blocking: %d, options: %d", non_blocking, opt);
  if ((res = setup_fd_for_listen(non_blocking, opt)) < 0) {
    Error("[Server::listen] Failed to setup fd for listen: %d", res);
    goto Lerror;
  }

  // Bind the socket to the specified address and protocol
  Debug("numa", "[Server::listen] Attempting to bind socket with fd: %d, protocol: %d", sock.get_fd(), prot);
  if ((res = sock.bind(&addr.sa, ats_ip_size(&addr.sa))) < 0) {
    Error("[Server::listen] Failed to bind socket: %d", res);
    goto Lerror;
  }

  // Set the socket to listen for incoming connections
  Debug("numa", "[Server::listen] Attempting to listen on socket with fd: %d", sock.get_fd());
  if ((res = safe_listen(sock.get_fd(), get_listen_backlog())) < 0) {
    Error("[Server::listen] Failed to listen on socket: %d", res);
    goto Lerror;
  }

  // sequentialize over thread number / NUMA here:
#if TS_USE_NUMA
  // Read NUMA and eBPF configuration options
  REC_ReadConfigInteger(use_ebpf, "proxy.config.net.use_ebpf");
  REC_ReadConfigInteger(affinity, "proxy.config.exec_thread.affinity");

  Debug("numa", "[Server::listen] NUMA settings: use_ebpf = %d, affinity = %d", use_ebpf, affinity);

  if (use_ebpf && affinity == 1) {
    // Sequentialize the execution of socket initialization and eBPF setup
    Debug("numa", "[Server::listen] Sequentializing socket setup using NUMASequencer.");
    success = numa_sequencer.run_sequential(init_func);
  } else {
    Debug("numa", "[Server::listen] Running socket setup without NUMASequencer.");
    success = init_func();
  }
  if (!success) {
    goto Lerror;
  }
#else

  Debug("numa", "[Server::listen] Attempting to set up fd after listen with options: %d", opt);
  res = setup_fd_after_listen(opt);
  if (res < 0) {
    Error("[Server::listen] Failed to setup fd after listen: %d", res);
    goto Lerror;
  }
#endif

  // Get the name of the socket after binding to ensure it's correct
  namelen = sizeof(addr);
  if ((res = sock.getsockname(&addr.sa, &namelen))) {
    goto Lerror;
  }

  return 0;

Lerror:
  // Handle errors by closing the file descriptor and logging the issue
  if (sock.is_ok()) {
    close();
  }

  Fatal("Could not bind or listen to port %d, mptcp enabled: %d (error: %d) %s %d", ats_ip_port_host_order(&addr),
        prot == IPPROTO_MPTCP, errno, strerror(errno), res);
  return res;
}

int
get_listen_backlog()
{
  int listen_backlog;

  REC_ReadConfigInteger(listen_backlog, "proxy.config.net.listen_backlog");
  return (0 < listen_backlog && listen_backlog <= 65535) ? listen_backlog : ats_tcp_somaxconn();
}

int
Server::setup_fd_for_listen(bool non_blocking, const NetProcessor::AcceptOptions &opt)
{
  int res               = 0;
  int listen_per_thread = 0;

  ink_assert(sock.is_ok());

  if (opt.defer_accept > 0) {
    http_accept_filter = true;
    add_http_filter(sock.get_fd());
  }

  if (opt.recv_bufsize) {
    if (sock.set_rcvbuf_size(opt.recv_bufsize)) {
      // Round down until success
      int rbufsz = ROUNDUP(opt.recv_bufsize, 1024);
      while (rbufsz) {
        if (sock.set_rcvbuf_size(rbufsz)) {
          rbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }

  if (opt.send_bufsize) {
    if (sock.set_sndbuf_size(opt.send_bufsize)) {
      // Round down until success
      int sbufsz = ROUNDUP(opt.send_bufsize, 1024);
      while (sbufsz) {
        if (sock.set_sndbuf_size(sbufsz)) {
          sbufsz -= 1024;
        } else {
          break;
        }
      }
    }
  }

  if (safe_fcntl(sock.get_fd(), F_SETFD, FD_CLOEXEC) < 0) {
    goto Lerror;
  }

  {
    struct linger l;
    l.l_onoff  = 0;
    l.l_linger = 0;
    if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_LINGER_ON) &&
        safe_setsockopt(sock.get_fd(), SOL_SOCKET, SO_LINGER, reinterpret_cast<char *>(&l), sizeof(l)) < 0) {
      goto Lerror;
    }
  }

  if (ats_is_ip6(&addr) && sock.enable_option(IPPROTO_IPV6, IPV6_V6ONLY) < 0) {
    goto Lerror;
  }

  if (sock.enable_option(SOL_SOCKET, SO_REUSEADDR) < 0) {
    goto Lerror;
  }
  REC_ReadConfigInteger(listen_per_thread, "proxy.config.exec_thread.listen");
  if (listen_per_thread == 1) {
    if (sock.enable_option(SOL_SOCKET, SO_REUSEPORT) < 0) {
      goto Lerror;
    }
#ifdef SO_REUSEPORT_LB
    if (sock.enable_option(SOL_SOCKET, SO_REUSEPORT_LB) < 0) {
      goto Lerror;
    }
#endif
  }

#ifdef SO_INCOMING_CPU
  if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_INCOMING_CPU) {
    int      cpu     = 0;
    EThread *ethread = this_ethread();

#if TS_USE_HWLOC
    cpu = ethread->hwloc_obj->os_index;
#else
    cpu = ethread->id;
#endif
    if (safe_setsockopt(sock.get_fd(), SOL_SOCKET, SO_INCOMING_CPU, &cpu, sizeof(cpu)) < 0) {
      goto Lerror;
    }
    Dbg(dbg_ctl_iocore_thread, "SO_INCOMING_CPU - fd=%d cpu=%d", sock.get_fd(), cpu);
  }
#endif
  if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_NO_DELAY) && sock.enable_option(IPPROTO_TCP, TCP_NODELAY) < 0) {
    goto Lerror;
  }

  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((opt.sockopt_flags & NetVCOptions::SOCK_OPT_KEEP_ALIVE) && sock.enable_option(SOL_SOCKET, SO_KEEPALIVE) < 0) {
    goto Lerror;
  }

#ifdef TCP_FASTOPEN
  if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN) {
    if (safe_setsockopt(sock.get_fd(), IPPROTO_TCP, TCP_FASTOPEN, (char *)&opt.tfo_queue_length, sizeof(int))) {
      // EOPNOTSUPP also checked for general safeguarding of unsupported operations of socket functions
      if (opt.f_mptcp && (errno == ENOPROTOOPT || errno == EOPNOTSUPP)) {
        Warning("[Server::listen] TCP_FASTOPEN socket option not valid on MPTCP socket level");
      } else {
        goto Lerror;
      }
    }
  }
#endif

  if (opt.f_inbound_transparent) {
#if TS_USE_TPROXY
    Dbg(dbg_ctl_http_tproxy, "Listen port inbound transparency enabled.");
    if (sock.enable_option(SOL_IP, TS_IP_TRANSPARENT) < 0) {
      Fatal("[Server::listen] Unable to set transparent socket option [%d] %s\n", errno, strerror(errno));
    }
#else
    Error("[Server::listen] Transparency requested but TPROXY not configured\n");
#endif
  }

  if (opt.f_proxy_protocol) {
    Dbg(dbg_ctl_proxyprotocol, "Proxy Protocol enabled.");
  }

#if defined(TCP_MAXSEG)
  if (NetProcessor::accept_mss > 0) {
    if (opt.f_mptcp) {
      Warning("[Server::listen] TCP_MAXSEG socket option not valid on MPTCP socket level");
    } else if (safe_setsockopt(sock.get_fd(), IPPROTO_TCP, TCP_MAXSEG, reinterpret_cast<char *>(&NetProcessor::accept_mss),
                               sizeof(int)) < 0) {
      goto Lerror;
    }
  }
#endif

#ifdef TCP_DEFER_ACCEPT
  // set tcp defer accept timeout if it is configured, this will not trigger an accept until there is
  // data on the socket ready to be read
  if (opt.defer_accept > 0 && setsockopt(sock.get_fd(), IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt.defer_accept, sizeof(int)) < 0) {
    // FIXME: should we go to the error
    // goto error;
    Error("[Server::listen] Defer accept is configured but set failed: %d", errno);
  }
#endif

  if (non_blocking) {
    if (sock.set_nonblocking() < 0) {
      goto Lerror;
    }
  }

  return 0;

Lerror:
  res = -errno;

  // coverity[check_after_sink]
  if (sock.is_ok()) {
    close();
  }

  return res;
}

int
add_http_filter([[maybe_unused]] int fd)
{
  int err = -1;
#if defined(SOL_FILTER) && defined(FIL_ATTACH)
  err = setsockopt(fd, SOL_FILTER, FIL_ATTACH, "httpfilt", 9);
#endif
  return err;
}

int
Server::setup_fd_after_listen([[maybe_unused]] const NetProcessor::AcceptOptions &opt)
{
#ifdef SO_ACCEPTFILTER
  // SO_ACCEPTFILTER needs to be set **after** listen
  if (opt.defer_accept > 0) {
    int file_id = kldfind("accf_data");

    struct kld_file_stat stat;
    stat.version = sizeof(stat);

    if (kldstat(file_id, &stat) < 0) {
      Error("[Server::listen] Ignored defer_accept config. Because accf_data module is not loaded errno=%d", errno);
    } else {
      struct accept_filter_arg afa;

      bzero(&afa, sizeof(afa));
      strcpy(afa.af_name, "dataready");

      if (setsockopt(this->sock.get_fd(), SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(afa)) < 0) {
        Error("[Server::listen] Defer accept is configured but set failed: %d", errno);
        return -errno;
      }
    }
  }
#endif

// Attach EBPF
#if TS_USE_NUMA
  // Assumes that threads can be assigned to NUMA zones as 0,1,2,3,0,1,2,3,0,1,2 sequence with no gaps.
  int use_ebpf = 0;
  REC_ReadConfigInteger(use_ebpf, "proxy.config.net.use_ebpf");

  Debug("numa", "[Server::setup_fd_after_listen] Thread %d checking NUMA and eBPF settings.", this_ethread()->id);

  if (use_ebpf) {
    int no_of_numa_nodes = numa_max_node() + 1;

    int desired_numa_node = (this_ethread()->id) % no_of_numa_nodes;
    int actual_numa_node  = this_ethread()->get_numa_node();
    if (desired_numa_node != actual_numa_node) {
      Error("BPF program will be futile. You need to use proxy.config.exec_thread.affinity = 1 for BPF socket affinity to work "
            "correctly.");
    }

    static char       bpf_log_buf[65536];
    static const char bpf_license[] = "";
    int               bpf_fd;

    Debug("numa", "[Server::setup_fd_after_listen] Loading BPF program.");

    const struct bpf_insn prog[] = {
      // instead of random
      {BPF_JMP | BPF_CALL,        0,         0,         0, BPF_FUNC_ktime_get_ns                        },
      // assuming that threads are on aligned numa nodes
      // for example 8 threads on 2 numa nodes: 01010101
      // for example 16 threads on 4 numa nodes: 0123012301230123
      {BPF_ALU | BPF_K | BPF_MOD, 0,         0,         0, eventProcessor.net_threads / no_of_numa_nodes},
      {BPF_ALU | BPF_K | BPF_MUL, 0,         0,         0, no_of_numa_nodes                             },
      {BPF_MOV | BPF_X | BPF_ALU, BPF_REG_6, BPF_REG_0, 0, 0                                            },
      {BPF_JMP | BPF_CALL,        0,         0,         0, BPF_FUNC_get_numa_node_id                    },
      {BPF_ALU | BPF_X | BPF_ADD, BPF_REG_0, BPF_REG_6, 0, 0                                            },
      {BPF_JMP | BPF_EXIT,        0,         0,         0, 0                                            }
    };

    union bpf_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
    attr.insn_cnt  = sizeof(prog) / sizeof(prog[0]);
    attr.insns     = (unsigned long)&prog;
    attr.license   = (unsigned long)&bpf_license;
    attr.log_buf   = (unsigned long)&bpf_log_buf;
    attr.log_size  = sizeof(bpf_log_buf);
    attr.log_level = 1;
    bpf_fd         = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));

    if (bpf_fd < 0) {
      Error("[Server::setup_fd_after_listen] Failed to load BPF program: %d, %s", errno, strerror(errno));
      return 0;
    }

    if (safe_setsockopt(sock.get_fd(), SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, &bpf_fd, sizeof(bpf_fd))) {
      Error("[Server::setup_fd_after_listen] Failed to set SO_ATTACH_REUSEPORT_EBPF: %d, %s", errno, strerror(errno));
      ::close(bpf_fd);
      return 0;
    }

    ::close(bpf_fd);
  }

  return true;

#endif

  return 0;
}
