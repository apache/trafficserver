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

/**************************************************************************
  Connections

**************************************************************************/
#include "P_Net.h"
#include "tscore/ink_defs.h"

#define SET_NO_LINGER
// set in the OS
// #define RECV_BUF_SIZE            (1024*64)
// #define SEND_BUF_SIZE            (1024*64)
#define FIRST_RANDOM_PORT 16000
#define LAST_RANDOM_PORT 32000

#if TS_USE_TPROXY
#if !defined(IP_TRANSPARENT)
unsigned int const IP_TRANSPARENT = 19;
#endif
#endif

namespace
{
/** Struct to make cleaning up resources easier.

    By default, the @a method is invoked on the @a object when
    this object is destructed. This can be prevented by calling
    the @c reset method.

    This is not overly useful in the allocate, check, return case
    but very handy if there are
    - multiple resources (each can have its own cleaner)
    - multiple checks against the resource
    In such cases, rather than trying to track all the resources
    that might need cleaned up, you can set up a cleaner at allocation
    and only have to deal with them on success, which is generally
    singular.

    @code
    self::some_method (...) {
      /// allocate resource
      cleaner<self> clean_up(this, &self::cleanup);
      // modify or check the resource
      if (fail) return FAILURE; // cleanup() is called
      /// success!
      clean_up.reset(); // cleanup() not called after this
      return SUCCESS;
    @endcode
 */
template <typename T> struct cleaner {
  T *obj;                       ///< Object instance.
  using method = void (T::*)(); ///< Method signature.
  method m;

  cleaner(T *_obj, method _method) : obj(_obj), m(_method) {}
  ~cleaner()
  {
    if (obj) {
      (obj->*m)();
    }
  }
  void
  reset()
  {
    obj = nullptr;
  }
};
} // namespace

/** Default options.

    @internal This structure is used to reduce the number of places in
    which the defaults are set. Originally the argument defaulted to
    @c nullptr which meant that the defaults had to be encoded in any
    methods that used it as well as the @c NetVCOptions
    constructor. Now they are controlled only in the latter and not in
    any of the methods. This makes handling global default values
    (such as @c RECV_BUF_SIZE) more robust. It doesn't have to be
    checked in the method, only in the @c NetVCOptions constructor.

    The methods are simpler because they never have to check for the
    presence of the options, yet the clients aren't inconvenienced
    because a default value for the argument is provided. Further,
    clients can pass temporaries and not have to declare a variable in
    order to tweak options.
 */
NetVCOptions const Connection::DEFAULT_OPTIONS;

int
Connection::open(NetVCOptions const &opt)
{
  ink_assert(fd == NO_FD);
  int enable_reuseaddr = 1; // used for sockopt setting
  int res              = 0; // temp result
  IpEndpoint local_addr;
  sock_type = NetVCOptions::USE_UDP == opt.ip_proto ? SOCK_DGRAM : SOCK_STREAM;
  int family;

  // Need to do address calculations first, so we can determine the
  // address family for socket creation.
  ink_zero(local_addr);

  bool is_any_address = false;
  if (NetVCOptions::FOREIGN_ADDR == opt.addr_binding || NetVCOptions::INTF_ADDR == opt.addr_binding) {
    // Same for now, transparency for foreign addresses must be handled
    // *after* the socket is created, and we need to do this calculation
    // before the socket to get the IP family correct.
    ink_release_assert(opt.local_ip.isValid());
    local_addr.assign(opt.local_ip, htons(opt.local_port));
    family = opt.local_ip.family();
  } else {
    // No local address specified, so use family option if possible.
    family = ats_is_ip(opt.ip_family) ? opt.ip_family : AF_INET;
    local_addr.setToAnyAddr(family);
    is_any_address                  = true;
    local_addr.network_order_port() = htons(opt.local_port);
  }

  res = socketManager.socket(family, sock_type, 0);
  if (-1 == res) {
    return -errno;
  }

  fd = res;
  // mark fd for close until we succeed.
  cleaner<Connection> cleanup(this, &Connection::_cleanup);

  // Try setting the various socket options, if requested.

  if (-1 == safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&enable_reuseaddr), sizeof(enable_reuseaddr))) {
    return -errno;
  }

  if (NetVCOptions::FOREIGN_ADDR == opt.addr_binding) {
    static char const *const DEBUG_TEXT = "::open setsockopt() IP_TRANSPARENT";
#if TS_USE_TPROXY
    int value = 1;
    if (-1 == safe_setsockopt(fd, SOL_IP, TS_IP_TRANSPARENT, reinterpret_cast<char *>(&value), sizeof(value))) {
      Debug("socket", "%s - fail %d:%s", DEBUG_TEXT, errno, strerror(errno));
      return -errno;
    } else {
      Debug("socket", "%s set", DEBUG_TEXT);
    }
#else
    Debug("socket", "%s - requested but TPROXY not configured", DEBUG_TEXT);
#endif
  }

  if (!opt.f_blocking_connect && -1 == safe_nonblocking(fd)) {
    return -errno;
  }

  if (opt.socket_recv_bufsize > 0) {
    if (socketManager.set_rcvbuf_size(fd, opt.socket_recv_bufsize)) {
      // Round down until success
      int rbufsz = ROUNDUP(opt.socket_recv_bufsize, 1024);
      while (rbufsz && !socketManager.set_rcvbuf_size(fd, rbufsz)) {
        rbufsz -= 1024;
      }
      Debug("socket", "::open: recv_bufsize = %d of %d", rbufsz, opt.socket_recv_bufsize);
    }
  }
  if (opt.socket_send_bufsize > 0) {
    if (socketManager.set_sndbuf_size(fd, opt.socket_send_bufsize)) {
      // Round down until success
      int sbufsz = ROUNDUP(opt.socket_send_bufsize, 1024);
      while (sbufsz && !socketManager.set_sndbuf_size(fd, sbufsz)) {
        sbufsz -= 1024;
      }
      Debug("socket", "::open: send_bufsize = %d of %d", sbufsz, opt.socket_send_bufsize);
    }
  }

  // apply dynamic options
  apply_options(opt);

  if (local_addr.network_order_port() || !is_any_address) {
    if (-1 == socketManager.ink_bind(fd, &local_addr.sa, ats_ip_size(&local_addr.sa))) {
      return -errno;
    }
  }

  cleanup.reset();
  is_bound = true;
  return 0;
}

int
Connection::connect(sockaddr const *target, NetVCOptions const &opt)
{
  ink_assert(fd != NO_FD);
  ink_assert(is_bound);
  ink_assert(!is_connected);

  int res;

  if (target != nullptr) {
    this->setRemote(target);
  }

  // apply dynamic options with this.addr initialized
  apply_options(opt);

  cleaner<Connection> cleanup(this, &Connection::_cleanup); // mark for close until we succeed.

  if (opt.f_tcp_fastopen && !opt.f_blocking_connect) {
    // TCP Fast Open is (effectively) a non-blocking connect, so set the
    // return value we would see in that case.
    errno = EINPROGRESS;
    res   = -1;
  } else {
    res = ::connect(fd, &this->addr.sa, ats_ip_size(&this->addr.sa));
  }

  // It's only really an error if either the connect was blocking
  // or it wasn't blocking and the error was other than EINPROGRESS.
  // (Is EWOULDBLOCK ok? Does that start the connect?)
  // We also want to handle the cases where the connect blocking
  // and IO blocking differ, by turning it on or off as needed.
  if (-1 == res && (opt.f_blocking_connect || !(EINPROGRESS == errno || EWOULDBLOCK == errno))) {
    return -errno;
  } else if (opt.f_blocking_connect && !opt.f_blocking) {
    if (-1 == safe_nonblocking(fd)) {
      return -errno;
    }
  } else if (!opt.f_blocking_connect && opt.f_blocking) {
    if (-1 == safe_blocking(fd)) {
      return -errno;
    }
  }

  cleanup.reset();

  // Only mark this connection as connected if we successfully called connect(2). When we
  // do the TCP Fast Open later, we need to track this accurately.
  is_connected = !(opt.f_tcp_fastopen && !opt.f_blocking_connect);
  return 0;
}

void
Connection::_cleanup()
{
  this->close();
}

void
Connection::apply_options(NetVCOptions const &opt)
{
  // Set options which can be changed after a connection is established
  // ignore other changes
  if (SOCK_STREAM == sock_type) {
    if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_NO_DELAY) {
      safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int));
      Debug("socket", "::open: setsockopt() TCP_NODELAY on socket");
    }
    if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_KEEP_ALIVE) {
      safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, SOCKOPT_ON, sizeof(int));
      Debug("socket", "::open: setsockopt() SO_KEEPALIVE on socket");
    }
    if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_LINGER_ON) {
      struct linger l;
      l.l_onoff  = 1;
      l.l_linger = 0;
      safe_setsockopt(fd, SOL_SOCKET, SO_LINGER, reinterpret_cast<char *>(&l), sizeof(l));
      Debug("socket", "::open:: setsockopt() turn on SO_LINGER on socket");
    }
#ifdef TCP_NOTSENT_LOWAT
    if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_TCP_NOTSENT_LOWAT) {
      uint32_t lowat = opt.packet_notsent_lowat;
      safe_setsockopt(fd, IPPROTO_TCP, TCP_NOTSENT_LOWAT, reinterpret_cast<char *>(&lowat), sizeof(lowat));
      Debug("socket", "::open:: setsockopt() set TCP_NOTSENT_LOWAT to %d", lowat);
    }
#endif
  }

#if TS_HAS_SO_MARK
  if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_PACKET_MARK) {
    uint32_t mark = opt.packet_mark;
    safe_setsockopt(fd, SOL_SOCKET, SO_MARK, reinterpret_cast<char *>(&mark), sizeof(uint32_t));
  }
#endif

#if TS_HAS_IP_TOS
  if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_PACKET_TOS) {
    uint32_t tos = opt.packet_tos;
    if (addr.isIp4()) {
      safe_setsockopt(fd, IPPROTO_IP, IP_TOS, reinterpret_cast<char *>(&tos), sizeof(uint32_t));
    } else if (addr.isIp6()) {
      safe_setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, reinterpret_cast<char *>(&tos), sizeof(uint32_t));
    }
  }
#endif
}
