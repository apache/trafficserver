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

#pragma once

#include "swoc/TextView.h"
#include "swoc/swoc_ip.h"

#include "P_EventSystem.h"
#include "Socks.h"
#include "tscpp/util/ts_errata.h"

#include "ParentSelection.h"

enum {
  // types of events for Socks auth handlers
  SOCKS_AUTH_OPEN,
  SOCKS_AUTH_WRITE_COMPLETE,
  SOCKS_AUTH_READ_COMPLETE,
  SOCKS_AUTH_FILL_WRITE_BUF
};

struct socks_conf_struct {
  int socks_needed              = 0;
  int server_connect_timeout    = 0;
  int socks_timeout             = 100;
  unsigned char default_version = 5;
  std::string user_name_n_passwd;

  int per_server_connection_attempts = 1;
  int connection_attempts            = 0;

  // the following ports are used by SocksProxy
  int accept_enabled       = 0;
  int accept_port          = 0;
  unsigned short http_port = 1080;

  swoc::IPRangeSet ip_addrs;

  socks_conf_struct() {}
};

void start_SocksProxy(int port);

int loadSocksAuthInfo(swoc::TextView content, socks_conf_struct *socks_stuff);

swoc::Errata loadSocksIPAddrs(swoc::TextView content, socks_conf_struct *socks_stuff);

// umm.. the following typedef should take _its own_ type as one of the args
// not possible with C
// Right now just use a generic fn ptr and hide casting in an inline fn.
using SocksAuthHandler = int (*)(int, unsigned char *, void (**)());

TS_INLINE int
invokeSocksAuthHandler(SocksAuthHandler &h, int arg1, unsigned char *arg2)
{
  return (h)(arg1, arg2, (void (**)(void))(&h));
}

void loadSocksConfiguration(socks_conf_struct *socks_conf_stuff);
int socks5BasicAuthHandler(int event, unsigned char *p, void (**)(void));
int socks5PasswdAuthHandler(int event, unsigned char *p, void (**)(void));
int socks5ServerAuthHandler(int event, unsigned char *p, void (**)(void));

class UnixNetVConnection;
using SocksNetVC = UnixNetVConnection;

struct SocksEntry : public Continuation {
  MIOBuffer *buf         = nullptr;
  IOBufferReader *reader = nullptr;

  SocksNetVC *netVConnection = nullptr;

  // Changed from @a ip and @a port.
  IpEndpoint target_addr; ///< Original target address.
  // Changed from @a server_ip, @a server_port.
  IpEndpoint server_addr; ///< Origin server address.

  int nattempts = 0;

  Action action_;
  int lerrno            = 0;
  Event *timeout        = nullptr;
  unsigned char version = 5;

  bool write_done = false;

  SocksAuthHandler auth_handler = nullptr;
  unsigned char socks_cmd       = NORMAL_SOCKS;

  // socks server selection:
  ParentConfigParams *server_params = nullptr;
  HttpRequestData req_data; // We dont use any http specific fields.
  ParentResult server_result;

  int startEvent(int event, void *data);
  int mainEvent(int event, void *data);
  void findServer();
  void init(Ptr<ProxyMutex> &m, SocksNetVC *netvc, unsigned char socks_support, unsigned char ver);
  void free();

  SocksEntry()
  {
    memset(&target_addr, 0, sizeof(target_addr));
    memset(&server_addr, 0, sizeof(server_addr));
  }
};

using SocksEntryHandler = int (SocksEntry::*)(int, void *);

extern ClassAllocator<SocksEntry> socksAllocator;

TS_INLINE void
SocksAddrType::reset()
{
  if (type != SOCKS_ATYPE_IPV4 && addr.buf) {
    ats_free(addr.buf);
  }

  addr.buf = nullptr;
  type     = SOCKS_ATYPE_NONE;
}
