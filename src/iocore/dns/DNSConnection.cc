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
#include <tscore/ink_defs.h>
#include "P_DNSConnection.h"
#include "P_DNSProcessor.h"

#include "iocore/eventsystem/UnixSocket.h"

#define SET_TCP_NO_DELAY
#define SET_NO_LINGER
#define SET_SO_KEEPALIVE
#define FIRST_RANDOM_PORT (16000)
#define LAST_RANDOM_PORT  (60000)

DNSConnection::Options const DNSConnection::DEFAULT_OPTIONS;

namespace
{

DbgCtl dbg_ctl_dns{"dns"};

} // end anonymous namespace

//
// Functions
//

DNSConnection::DNSConnection()
  : generator(static_cast<uint32_t>(static_cast<uintptr_t>(time(nullptr)) ^ reinterpret_cast<uintptr_t>(this)))
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
  return this->sock.close();
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
{
  ink_assert(!this->sock.is_ok());
  ink_assert(ats_is_ip(addr));
  this->opt = opt;
  this->tcp_data.reset();

  int        res = 0;
  uint8_t    af  = addr->sa_family;
  IpEndpoint bind_addr;
  size_t     bind_size = 0;

  if (opt._use_tcp) {
    this->sock = UnixSocket{af, SOCK_STREAM, 0};
  } else {
    this->sock = UnixSocket{af, SOCK_DGRAM, 0};
  }

  if (!this->sock.is_ok()) {
    goto Lerror;
  }

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
    while (retries++ < 10000) {
      ip_port_text_buffer b;
      uint32_t            p           = generator.random();
      p                               = static_cast<uint16_t>((p % (LAST_RANDOM_PORT - FIRST_RANDOM_PORT)) + FIRST_RANDOM_PORT);
      ats_ip_port_cast(&bind_addr.sa) = htons(p); // stuff port in sockaddr.
      Dbg(dbg_ctl_dns, "random port = %s", ats_ip_nptop(&bind_addr.sa, b, sizeof b));
      if (this->sock.bind(&bind_addr.sa, bind_size) < 0) {
        continue;
      }
      goto Lok;
    }
    Warning("unable to bind random DNS port");
  Lok:;
  } else if (ats_is_ip(&bind_addr.sa)) {
    ip_text_buffer b;
    res = this->sock.bind(&bind_addr.sa, bind_size);
    if (res < 0) {
      Warning("Unable to bind local address to %s.", ats_ip_ntop(&bind_addr.sa, b, sizeof b));
    }
  }

  if (opt._non_blocking_connect) {
    if ((res = this->sock.set_nonblocking()) < 0) {
      goto Lerror;
    }
  }

// cannot do this after connection on non-blocking connect
#ifdef SET_TCP_NO_DELAY
  if (opt._use_tcp) {
    if ((res = this->sock.enable_option(IPPROTO_TCP, TCP_NODELAY)) < 0) {
      goto Lerror;
    }
  }
#endif
#ifdef SET_SO_KEEPALIVE
  // enables 2 hour inactivity probes, also may fix IRIX FIN_WAIT_2 leak
  if ((res = this->sock.enable_option(SOL_SOCKET, SO_KEEPALIVE)) < 0) {
    goto Lerror;
  }
#endif

  ats_ip_copy(&ip.sa, addr);
  res = this->sock.connect(addr, ats_ip_size(addr));

  if (!res || ((res < 0) && (errno == EINPROGRESS || errno == EWOULDBLOCK))) {
    if (!opt._non_blocking_connect && opt._non_blocking_io) {
      if ((res = this->sock.set_nonblocking()) < 0) {
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
  if (this->sock.is_ok()) {
    close();
  }
  return res;
}
