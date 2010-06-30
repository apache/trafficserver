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
#include "ink_unused.h"       /* MAGIC_EDITING_TAG */
#include "P_Net.h"

#define SET_NO_LINGER
// set in the OS
// #define RECV_BUF_SIZE            (1024*64)
// #define SEND_BUF_SIZE            (1024*64)
#define FIRST_RANDOM_PORT        16000
#define LAST_RANDOM_PORT         32000

#define ROUNDUP(x, y) ((((x)+((y)-1))/(y))*(y))

#if !defined(IP_TRANSPARENT)
unsigned int const IP_TRANSPARENT = 19;
#endif

//
// Functions
//
int
Connection::setup_mc_send(unsigned int mc_ip, int mc_port,
                          unsigned int my_ip, int my_port,
                          bool non_blocking, unsigned char mc_ttl, bool mc_loopback, Continuation * c)
{
  (void) c;
  ink_assert(fd == NO_FD);
  int res = 0;
  int enable_reuseaddr = 1;

  if ((res = socketManager.mc_socket(AF_INET, SOCK_DGRAM, 0, non_blocking)) < 0)
    goto Lerror;

  fd = res;

  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable_reuseaddr, sizeof(enable_reuseaddr)) < 0)) {
    goto Lerror;
  }

  struct sockaddr_in bind_sa;
  memset(&bind_sa, 0, sizeof(bind_sa));
  bind_sa.sin_family = AF_INET;
  bind_sa.sin_port = htons(my_port);
  bind_sa.sin_addr.s_addr = my_ip;
  if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &bind_sa, sizeof(bind_sa), IPPROTO_UDP)) < 0) {
    goto Lerror;
  }

  sa.sin_family = AF_INET;
  sa.sin_port = htons(mc_port);
  sa.sin_addr.s_addr = mc_ip;
  memset(&sa.sin_zero, 0, 8);

#ifdef SET_CLOSE_ON_EXEC
  if ((res = safe_fcntl(fd, F_SETFD, 1)) < 0)
    goto Lerror;
#endif

  if (non_blocking)
    if ((res = safe_nonblocking(fd)) < 0)
      goto Lerror;

  // Set MultiCast TTL to specified value
  if ((res = safe_setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (char *) &mc_ttl, sizeof(mc_ttl)) < 0))
    goto Lerror;


  // Disable MultiCast loopback if requested
  if (!mc_loopback) {
    char loop = 0;

    if ((res = safe_setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *) &loop, sizeof(loop)) < 0))
      goto Lerror;
  }
  return 0;

Lerror:
  if (fd != NO_FD)
    close();
  return res;
}


int
Connection::setup_mc_receive(unsigned int mc_ip, int mc_port,
                             bool non_blocking, Connection * sendChan, Continuation * c)
{
  ink_assert(fd == NO_FD);
  (void) sendChan;
  (void) c;
  int res = 0;
  int enable_reuseaddr = 1;

  if ((res = socketManager.socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    goto Lerror;

  fd = res;

#ifdef SET_CLOSE_ON_EXEC
  if ((res = safe_fcntl(fd, F_SETFD, 1)) < 0)
    goto Lerror;
#endif

  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable_reuseaddr, sizeof(enable_reuseaddr)) < 0))
    goto Lerror;

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = mc_ip;
  sa.sin_port = htons(mc_port);

  if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &sa, sizeof(sa), IPPROTO_TCP)) < 0)
    goto Lerror;

  if (non_blocking)
    if ((res = safe_nonblocking(fd)) < 0)
      goto Lerror;

  struct ip_mreq mc_request;
  // Add ourselves to the MultiCast group
  mc_request.imr_multiaddr.s_addr = mc_ip;
  mc_request.imr_interface.s_addr = htonl(INADDR_ANY);

  if ((res = safe_setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &mc_request, sizeof(mc_request)) < 0))
    goto Lerror;
  return 0;

Lerror:
  if (fd != NO_FD)
    close();
  return res;
}

namespace {
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
    T* obj; ///< Object instance.
    typedef void (T::*method)(); ///< Method signature.
    method m;

    cleaner(T* _obj, method  _method) : obj(_obj), m(_method) {}
    ~cleaner() { if (obj) (obj->*m)(); }
    void reset() { obj = 0; }
  };
}

/** Default options.

    @internal This structure is used to reduce the number of places in
    which the defaults are set. Originally the argument defaulted to
    @c NULL which meant that the defaults had to be encoded in any
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
Connection::open(NetVCOptions const& opt)
{
  ink_assert(fd == NO_FD);
  int enable_reuseaddr = 1; // used for sockopt setting
  int res = 0; // temp result
  uint32 local_addr = NetVCOptions::ANY_ADDR == opt.addr_binding
    ? INADDR_ANY
    : opt.local_addr;
  uint16 local_port = NetVCOptions::ANY_PORT == opt.port_binding
    ? 0
    : opt.local_port;
  int sock_type = NetVCOptions::USE_UDP == opt.ip_proto
    ? SOCK_DGRAM
    : SOCK_STREAM;

  res = socketManager.socket(AF_INET, sock_type, 0);
  if (-1 == res) return -errno;

  fd = res;
  // mark fd for close until we succeed.
  cleaner<Connection> cleanup(this, &Connection::_cleanup);

  // Try setting the various socket options, if requested.

  if (-1 == safe_setsockopt(fd,
			    SOL_SOCKET,
			    SO_REUSEADDR,
			    reinterpret_cast<char *>(&enable_reuseaddr),
			    sizeof(enable_reuseaddr)))
    return -errno;

  if (!opt.f_blocking_connect && -1 == safe_nonblocking(fd))
    return -errno;

  if (opt.socket_recv_bufsize > 0) {
    if (socketManager.set_rcvbuf_size(fd, opt.socket_recv_bufsize)) {
      // Round down until success
      int rbufsz = ROUNDUP(opt.socket_recv_bufsize, 1024);
      while (rbufsz && !socketManager.set_rcvbuf_size(fd, rbufsz))
	rbufsz -= 1024;
      NetDebug("socket", "::open: recv_bufsize = %d of %d\n", rbufsz, opt.socket_recv_bufsize);
    }
  }
  if (opt.socket_send_bufsize > 0) {
    if (socketManager.set_sndbuf_size(fd, opt.socket_send_bufsize)) {
      // Round down until success
      int sbufsz = ROUNDUP(opt.socket_send_bufsize, 1024);
      while (sbufsz && !socketManager.set_sndbuf_size(fd, sbufsz))
	sbufsz -= 1024;
      NetDebug("socket", "::open: send_bufsize = %d of %d\n", sbufsz, opt.socket_send_bufsize);
    }
  }

  if (SOCK_STREAM == sock_type) {
    if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_NO_DELAY) {
      safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int));
      NetDebug("socket", "::open: setsockopt() TCP_NODELAY on socket");
    }
    if (opt.sockopt_flags & NetVCOptions::SOCK_OPT_KEEP_ALIVE) {
      safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int));
      NetDebug("socket", "::open: setsockopt() SO_KEEPALIVE on socket");
    }
  }

  if (NetVCOptions::FOREIGN_ADDR == opt.addr_binding && local_addr) {
    int value = 1;
    res = safe_setsockopt(fd, SOL_SOCKET, IP_TRANSPARENT, reinterpret_cast<char*>(&value), sizeof(value));
    if (-1 == res) return -errno;
  }

  // Local address/port.
  struct sockaddr_in bind_sa;
  memset(&bind_sa, 0, sizeof(bind_sa));
  bind_sa.sin_family = AF_INET;
  bind_sa.sin_port = htons(local_port);
  bind_sa.sin_addr.s_addr = local_addr;
  if (-1 == socketManager.ink_bind(fd,
				   reinterpret_cast<struct sockaddr *>(&bind_sa),
				   sizeof(bind_sa)))
    return -errno;

  cleanup.reset();
  is_bound = true;
  return 0;
}

int
Connection::connect(uint32 addr, uint16 port, NetVCOptions const& opt) {
  ink_assert(fd != NO_FD);
  ink_assert(is_bound);
  ink_assert(!is_connected);

  int res;

  this->setRemote(addr, port);

  cleaner<Connection> cleanup(this, &Connection::_cleanup); // mark for close until we succeed.

  res = ::connect(fd,
		  reinterpret_cast<struct sockaddr *>(&sa),
		  sizeof(struct sockaddr_in));
  // It's only really an error if either the connect was blocking
  // or it wasn't blocking and the error was other than EINPROGRESS.
  // (Is EWOULDBLOCK ok? Does that start the connect?)
  // We also want to handle the cases where the connect blocking
  // and IO blocking differ, by turning it on or off as needed.
  if (-1 == res 
      && (opt.f_blocking_connect
	  || ! (EINPROGRESS == errno || EWOULDBLOCK == errno))) {
    return -errno;
  } else if (opt.f_blocking_connect && !opt.f_blocking) {
    if (-1 == safe_nonblocking(fd)) return -errno;
  } else if (!opt.f_blocking_connect && opt.f_blocking) {
    if (-1 == safe_blocking(fd)) return -errno;
  }

  cleanup.reset();
  is_connected = true;
  return 0;
}

void
Connection::_cleanup()
{
  this->close();
}
