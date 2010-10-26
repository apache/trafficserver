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

#ifndef _I_DNSProcessor_h_
#define _I_DNSProcessor_h_

#include "SRV.h"

/*
  #include "I_EventSystem.h"
  #include "I_HostDB.h"
  #include "I_Net.h"
*/

#define  MAX_DNS_PACKET_LEN         8192
#define  DNS_MAX_ALIASES              35
#define  DNS_MAX_ADDRS                35
#define  DNS_HOSTBUF_SIZE           8192
#define  DOMAIN_SERVICE_PORT          53
#define  DEFAULT_DOMAIN_NAME_SERVER    0        // use the default server


/**
  All buffering required to handle a DNS receipt. For asynchronous DNS,
  only one HostEntBuf will exist in the system. For synchronous DNS,
  one will exist per call until the user deletes them.

*/
struct HostEnt
{
  struct hostent ent;
  uint32 ttl;
  int ref_count;
  int packet_size;
  char buf[MAX_DNS_PACKET_LEN];
  u_char *host_aliases[DNS_MAX_ALIASES];
  u_char *h_addr_ptrs[DNS_MAX_ADDRS + 1];
  u_char hostbuf[DNS_HOSTBUF_SIZE];

  SRVHosts srv_hosts;

    HostEnt()
  {
    memset(this, 0, sizeof(*this));
  }
};

extern EventType ET_DNS;

struct DNSHandler;

struct DNSProcessor: public Processor
{
  //
  // Public Interface
  //

  // Non-blocking DNS lookup
  //   calls: cont->handleEvent( DNS_EVENT_LOOKUP, HostEnt * ent) on success
  //          cont->handleEvent( DNS_EVENT_LOOKUP, NULL) on failure
  // NOTE: the HostEnt * block is freed when the function returns
  //

  Action *gethostbyname(Continuation * cont, const char *name, DNSHandler * adnsH = 0, int timeout = 0);
  Action *getSRVbyname(Continuation * cont, const char *name, DNSHandler * adnsH = 0, int timeout = 0);
  Action *gethostbyname(Continuation * cont, const char *name, int len, int timeout = 0);
  Action *gethostbyaddr(Continuation * cont, unsigned int ip, int timeout = 0);

  // Blocking DNS lookup, user must free the return HostEnt *
  // NOTE: this HostEnt is big, please free these ASAP
  //
  HostEnt *gethostbyname(const char *name);
  HostEnt *gethostbyaddr(unsigned int addr);
  HostEnt *getSRVbyname(const char *name);

  /** Free the returned HostEnt (only for Blocking versions). */
  void free_hostent(HostEnt * ent);

  // Processor API
  //
  /* currently dns system uses event threads
   * dont pass any value to the call */
  int start(int no_of_extra_dns_threads = 0);

  // Open/close a link to a 'named' (done in start())
  //
  void open(unsigned int ip = DEFAULT_DOMAIN_NAME_SERVER, int port = DOMAIN_SERVICE_PORT, int options = _res.options);

  DNSProcessor();

  //
  // private:
  //
  EThread *thread;
  DNSHandler *handler;
  __ink_res_state l_res;
  Action *getby(const char *x, int len, int type, Continuation * cont,
                HostEnt ** wait, DNSHandler * adnsH = NULL, int timeout = 0);
  void dns_init();
};


//
// Global data
//
extern DNSProcessor dnsProcessor;


//
// Inline Functions
//

inline HostEnt *
DNSProcessor::getSRVbyname(const char *name)
{
  HostEnt *ent = NULL;

  getby(name, 0, T_SRV, NULL, &ent);
  return ent;
}


inline Action *
DNSProcessor::getSRVbyname(Continuation * cont, const char *name, DNSHandler * adnsH, int timeout)
{
  return getby(name, 0, T_SRV, cont, 0, adnsH, timeout);
}


inline Action *
DNSProcessor::gethostbyname(Continuation * cont, const char *name, DNSHandler * adnsH, int timeout)
{
  return getby(name, 0, T_A, cont, 0, adnsH, timeout);
}


inline Action *
DNSProcessor::gethostbyname(Continuation * cont, const char *name, int len, int timeout)
{
  return getby(name, len, T_A, cont, 0, NULL, timeout);
}


inline Action *
DNSProcessor::gethostbyaddr(Continuation * cont, unsigned int addr, int timeout)
{
  return getby((char *) &addr, sizeof(addr), T_PTR, cont, 0, NULL, timeout);
}


inline HostEnt *
DNSProcessor::gethostbyname(const char *name)
{
  HostEnt *ent = NULL;
  getby(name, 0, T_A, NULL, &ent);
  return ent;
}


inline HostEnt *
DNSProcessor::gethostbyaddr(unsigned int addr)
{
  HostEnt *ent = NULL;
  getby((char *) &addr, sizeof(addr), T_PTR, NULL, &ent);
  return ent;
}

void ink_dns_init(ModuleVersion version);

#endif
