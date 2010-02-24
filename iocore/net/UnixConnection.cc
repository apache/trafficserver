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

#define SET_TCP_NO_DELAY
#define SET_NO_LINGER
// set in the OS
// #define RECV_BUF_SIZE            (1024*64)
// #define SEND_BUF_SIZE            (1024*64)
#define FIRST_RANDOM_PORT        16000
#define LAST_RANDOM_PORT         32000

#define ROUNDUP(x, y) ((((x)+((y)-1))/(y))*(y))

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


int
Connection::bind_connect(unsigned int target_ip, int target_port, unsigned int my_ip,
                         NetVCOptions *opt, int sock, bool non_blocking_connect, bool use_tcp, 
                         bool non_blocking, bool bc_no_connect, bool bc_no_bind)
{
  ink_assert(fd == NO_FD);
  int res = 0;
  ink_hrtime t;
  int enable_reuseaddr = 1;
  int my_port = (opt) ? opt->local_port : 0;

  if (!bc_no_bind) {
    if (sock < 0) {
      if (use_tcp) {
        if ((res = socketManager.socket(AF_INET, SOCK_STREAM, 0)) < 0)
          goto Lerror;
      } else {
        if ((res = socketManager.socket(AF_INET, SOCK_DGRAM, 0)) < 0)
          goto Lerror;
      }
    } else
      res = sock;

    fd = res;

    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable_reuseaddr, sizeof(enable_reuseaddr)) < 0)) 
      goto Lerror;

    struct sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof(bind_sa));
    bind_sa.sin_family = AF_INET;
    bind_sa.sin_port = htons(my_port);
    bind_sa.sin_addr.s_addr = my_ip;
    if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &bind_sa, sizeof(bind_sa))) < 0)
      goto Lerror;

    NetDebug("arm_spoofing", "Passed in options opt=%x client_ip=%x and client_port=%d",
          opt, opt ? opt->spoof_ip : 0, opt ? opt->spoof_port : 0);
  }

  sa.sin_family = AF_INET;
  sa.sin_port = htons(target_port);
  sa.sin_addr.s_addr = target_ip;
  memset(&sa.sin_zero, 0, 8);

  if (bc_no_bind)               // no socket
    return 0;

  if (non_blocking_connect)
    if ((res = safe_nonblocking(fd)) < 0)
      goto Lerror;

  // cannot do this after connection on non-blocking connect
#ifdef SET_TCP_NO_DELAY
  if (use_tcp)
    if ((res = safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int))) < 0)
      goto Lerror;
#endif
#ifdef RECV_BUF_SIZE
  socketManager.set_rcvbuf_size(fd, RECV_BUF_SIZE);
#endif
#ifdef SET_SO_KEEPALIVE
  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int))) < 0)
    goto Lerror;
#endif

  if (opt) {
    if (opt->socket_recv_bufsize > 0) {
      if (socketManager.set_rcvbuf_size(fd, opt->socket_recv_bufsize)) {
        int rbufsz = ROUNDUP(opt->socket_recv_bufsize, 1024);   // Round down until success
        while (rbufsz) {
          if (!socketManager.set_rcvbuf_size(fd, rbufsz))
            break;
          rbufsz -= 1024;
        }
      }
    }
    if (opt->socket_send_bufsize > 0) {
      if (socketManager.set_sndbuf_size(fd, opt->socket_send_bufsize)) {
        int sbufsz = ROUNDUP(opt->socket_send_bufsize, 1024);   // Round down until success
        while (sbufsz) {
          if (!socketManager.set_sndbuf_size(fd, sbufsz))
            break;
          sbufsz -= 1024;
        }
      }
    }
    if (use_tcp) {
      if (opt->sockopt_flags & 1) {
        safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, ON, sizeof(int));
        NetDebug("socket", "::bind_connect: setsockopt() TCP_NODELAY on socket");
      }
      if (opt->sockopt_flags & 2) {
        safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, ON, sizeof(int));
        NetDebug("socket", "::bind_connect: setsockopt() SO_KEEPALIVE on socket");
      }
    }
  }

  if (!bc_no_connect) {
    t = ink_get_hrtime();
    res =::connect(fd, (struct sockaddr *) &sa, sizeof(struct sockaddr_in));
    if (!res || ((res < 0) && errno == EINPROGRESS)) {
      if (!non_blocking_connect && non_blocking)
        if ((res = safe_nonblocking(fd)) < 0)
          goto Lerror;
    } else
      goto Lerror;
  }

  return 0;

Lerror:
  if (fd != NO_FD)
    close();
  return res;
}
