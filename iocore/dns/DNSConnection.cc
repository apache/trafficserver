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

  Commonality across all platforms -- move out as required.

**************************************************************************/
#include "P_DNS.h"
#include "P_DNSConnection.h"
#include "P_DNSProcessor.h"

#define SET_TCP_NO_DELAY
#define SET_NO_LINGER
#define SET_SO_KEEPALIVE
// set in the OS
// #define RECV_BUF_SIZE            (1024*64)
// #define SEND_BUF_SIZE            (1024*64)
#define FIRST_RANDOM_PORT (16000)
#define LAST_RANDOM_PORT (60000)

#define ROUNDUP(x, y) ((((x) + ((y)-1)) / (y)) * (y))

DNSConnection::Options const DNSConnection::DEFAULT_OPTIONS;

//
// Functions
//

DNSConnection::DNSConnection() : fd(NO_FD), generator((uint32_t)((uintptr_t)time(nullptr) ^ (uintptr_t)this))
{
  memset(&ip, 0, sizeof(ip));
}

DNSConnection::~DNSConnection()
{
  close();
}

int
DNSConnection::close()
{
  eio.stop();
  // don't close any of the standards
  if (fd >= 2) {
    int fd_save = fd;
    fd          = NO_FD;
    return socketManager.close(fd_save);
  } else {
    fd = NO_FD;
    return -EBADF;
  }
}

void
DNSConnection::trigger()
{
  handler->triggered.enqueue(this);

  // Since the periodic check is removed, we need to call
  // this when it's triggered by EVENTIO_DNS_CONNECTION.
  // The handler should be pointing to DNSHandler::mainEvent.
  // We can schedule an immediate event or call the handler
  // directly, and since both arguments are not being used
  // passing in 0 and nullptr will do the job.
  handler->handleEvent(0, nullptr);
}

int
DNSConnection::connect(sockaddr const *addr, Options const &opt)
//                       bool non_blocking_connect, bool use_tcp, bool non_blocking, bool bind_random_port)
{
  ink_assert(fd == NO_FD);
  ink_assert(ats_is_ip(addr));
  this->opt = opt;
  this->tcp_data.reset();

  int res = 0;
  short Proto;
  uint8_t af = addr->sa_family;
  IpEndpoint bind_addr;
  size_t bind_size = 0;

  if (opt._use_tcp) {
    Proto = IPPROTO_TCP;
    if ((res = socketManager.socket(af, SOCK_STREAM, 0)) < 0) {
      goto Lerror;
    }
  } else {
    Proto = IPPROTO_UDP;
    if ((res = socketManager.socket(af, SOCK_DGRAM, 0)) < 0) {
      goto Lerror;
    }
  }

  fd = res;

  memset(&bind_addr, 0, sizeof bind_addr);
  bind_addr.sa.sa_family = af;

  if (AF_INET6 == af) {
    if (ats_is_ip6(opt._local_ipv6)) {
      ats_ip_copy(&bind_addr.sa, opt._local_ipv6);
    } else {
      bind_addr.sin6.sin6_addr = in6addr_any;
    }
    bind_size = sizeof(sockaddr_in6);
  } else if (AF_INET == af) {
    if (ats_is_ip4(opt._local_ipv4)) {
      ats_ip_copy(&bind_addr.sa, opt._local_ipv4);
    } else {
      bind_addr.sin.sin_addr.s_addr = INADDR_ANY;
    }
    bind_size = sizeof(sockaddr_in);
  } else {
    ink_assert(!"Target DNS address must be IP.");
  }

  if (opt._bind_random_port) {
    int retries = 0;
    IpEndpoint bind_addr;
    size_t bind_size = 0;
    memset(&bind_addr, 0, sizeof bind_addr);
    bind_addr.sa.sa_family = af;
    if (AF_INET6 == af) {
      bind_addr.sin6.sin6_addr = in6addr_any;
      bind_size                = sizeof bind_addr.sin6;
    } else {
      bind_addr.sin.sin_addr.s_addr = INADDR_ANY;
      bind_size                     = sizeof bind_addr.sin;
    }
    while (retries++ < 10000) {
      ip_port_text_buffer b;
      uint32_t p                      = generator.random();
      p                               = static_cast<uint16_t>((p % (LAST_RANDOM_PORT - FIRST_RANDOM_PORT)) + FIRST_RANDOM_PORT);
      ats_ip_port_cast(&bind_addr.sa) = htons(p); // stuff port in sockaddr.
      Debug("dns", "random port = %s", ats_ip_nptop(&bind_addr.sa, b, sizeof b));
      if ((res = socketManager.ink_bind(fd, &bind_addr.sa, bind_size, Proto)) < 0) {
        continue;
      }
      goto Lok;
    }
    Warning("unable to bind random DNS port");
  Lok:;
  } else if (ats_is_ip(&bind_addr.sa)) {
    ip_text_buffer b;
    res = socketManager.ink_bind(fd, &bind_addr.sa, bind_size, Proto);
    if (res < 0) {
      Warning("Unable to bind local address to %s.", ats_ip_ntop(&bind_addr.sa, b, sizeof b));
    }
  }

  if (opt._non_blocking_connect) {
    if ((res = safe_nonblocking(fd)) < 0) {
      goto Lerror;
    }
  }

// cannot do this after connection on non-blocking connect
#ifdef SET_TCP_NO_DELAY
  if (opt._use_tcp) {
    if ((res = safe_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, SOCKOPT_ON, sizeof(int))) < 0) {
      goto Lerror;
    }
  }
#endif
#ifdef RECV_BUF_SIZE
  socketManager.set_rcvbuf_size(fd, RECV_BUF_SIZE);
#endif
#ifdef SET_SO_KEEPALIVE
  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, SOCKOPT_ON, sizeof(int))) < 0) {
    goto Lerror;
  }
#endif

  ats_ip_copy(&ip.sa, addr);
  res = ::connect(fd, addr, ats_ip_size(addr));

  if (!res || ((res < 0) && (errno == EINPROGRESS || errno == EWOULDBLOCK))) {
    if (!opt._non_blocking_connect && opt._non_blocking_io) {
      if ((res = safe_nonblocking(fd)) < 0) {
        goto Lerror;
      }
    }
    // Shouldn't we turn off non-blocking when it's a non-blocking connect
    // and blocking IO?
  } else {
    goto Lerror;
  }

  return 0;

Lerror:
  if (fd != NO_FD) {
    close();
  }
  return res;
}
