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

#include "tscore/ink_resolver.h"
#include "SRV.h"

const int DOMAIN_SERVICE_PORT = NAMESERVER_PORT;

const int MAX_DNS_REQUEST_LEN  = NS_PACKETSZ;
const int MAX_DNS_RESPONSE_LEN = 65536;
const int DNS_RR_MAX_COUNT     = (MAX_DNS_RESPONSE_LEN - HFIXEDSZ + RRFIXEDSZ - 1) / RRFIXEDSZ;
const int DNS_MAX_ALIASES      = DNS_RR_MAX_COUNT;
const int DNS_MAX_ADDRS        = DNS_RR_MAX_COUNT;
const int DNS_HOSTBUF_SIZE     = MAX_DNS_RESPONSE_LEN;

/**
  All buffering required to handle a DNS receipt. For asynchronous DNS,
  only one HostEntBuf will exist in the system. For synchronous DNS,
  one will exist per call until the user deletes them.

*/
struct HostEnt : RefCountObj {
  struct hostent ent = {.h_name = nullptr, .h_aliases = nullptr, .h_addrtype = 0, .h_length = 0, .h_addr_list = nullptr};
  uint32_t ttl       = 0;
  int packet_size    = 0;
  char buf[MAX_DNS_RESPONSE_LEN]         = {0};
  u_char *host_aliases[DNS_MAX_ALIASES]  = {nullptr};
  u_char *h_addr_ptrs[DNS_MAX_ADDRS + 1] = {nullptr};
  u_char hostbuf[DNS_HOSTBUF_SIZE]       = {0};
  SRVHosts srv_hosts;
  bool good = true;
  bool isNameError();
  void free() override;
};

extern EventType ET_DNS;

struct DNSHandler;

/** Data for a DNS query.
 * This is either a name for a standard query or an IP address for reverse DNS.
 * Its type should be indicated by other parameters, generally the query type.
 * - T_PTR: IP Address
 * - T_A, T_SRV: Name
 */
union DNSQueryData {
  std::string_view name; ///< Look up a name.
  IpAddr const *addr;    ///< Reverse DNS lookup.

  DNSQueryData(std::string_view tv) : name(tv) {}
  DNSQueryData(IpAddr const *a) : addr(a) {}
};

struct DNSProcessor : public Processor {
  // Public Interface
  //

  /// Options for host name resolution.
  struct Options {
    using self_type = Options; ///< Self reference type.

    /// Query handler to use.
    /// Default: single threaded handler.
    DNSHandler *handler = nullptr;
    /// Query timeout value.
    /// Default: @c DEFAULT_DNS_TIMEOUT (or as set in records.config)
    int timeout = 0; ///< Timeout value for request.
    /// Host resolution style.
    /// Default: IPv4, IPv6 ( @c HOST_RES_IPV4 )
    HostResStyle host_res_style = HOST_RES_IPV4;

    /// Default constructor.
    Options();

    /// Set @a handler option.
    /// @return This object.
    self_type &setHandler(DNSHandler *handler);

    /// Set @a timeout option.
    /// @return This object.
    self_type &setTimeout(int timeout);

    /// Set host query @a style option.
    /// @return This object.
    self_type &setHostResStyle(HostResStyle style);

    /// Reset to default constructed values.
    /// @return This object.
    self_type &reset();
  };

  // DNS lookup
  //   calls: cont->handleEvent( DNS_EVENT_LOOKUP, HostEnt *ent) on success
  //          cont->handleEvent( DNS_EVENT_LOOKUP, nullptr) on failure
  // NOTE: the HostEnt *block is freed when the function returns
  //

  Action *gethostbyname(Continuation *cont, const char *name, Options const &opt);
  Action *gethostbyname(Continuation *cont, std::string_view name, Options const &opt);
  Action *getSRVbyname(Continuation *cont, const char *name, Options const &opt);
  Action *getSRVbyname(Continuation *cont, std::string_view name, Options const &opt);
  Action *gethostbyaddr(Continuation *cont, IpAddr const *ip, Options const &opt);

  // Processor API
  //
  /* currently dns system uses event threads
   * dont pass any value to the call */
  int start(int no_of_extra_dns_threads = 0, size_t stacksize = DEFAULT_STACKSIZE) override;

  // Open/close a link to a 'named' (done in start())
  //
  void open(sockaddr const *ns = nullptr);

  DNSProcessor();

  // private:
  //
  EThread *thread     = nullptr;
  DNSHandler *handler = nullptr;
  ts_imp_res_state l_res;
  IpEndpoint local_ipv6;
  IpEndpoint local_ipv4;

  /** Internal implementation for all getXbyY methods.
      For host resolution queries pass @c T_A for @a type. It will be adjusted
      as needed based on @a opt.host_res_style.

      For address resolution ( @a type is @c T_PTR ), @a x should be a
      @c sockaddr cast to  @c char @c const* .
   */
  Action *getby(DNSQueryData x, int type, Continuation *cont, Options const &opt);

  void dns_init();
};

//
// Global data
//
extern DNSProcessor dnsProcessor;

//
// Inline Functions
//

inline Action *
DNSProcessor::getSRVbyname(Continuation *cont, const char *name, Options const &opt)
{
  return getby(std::string_view(name), T_SRV, cont, opt);
}

inline Action *
DNSProcessor::getSRVbyname(Continuation *cont, std::string_view name, Options const &opt)
{
  return getby(name, T_SRV, cont, opt);
}

inline Action *
DNSProcessor::gethostbyname(Continuation *cont, const char *name, Options const &opt)
{
  return getby(std::string_view(name), T_A, cont, opt);
}

inline Action *
DNSProcessor::gethostbyname(Continuation *cont, std::string_view name, Options const &opt)
{
  return getby(name, T_A, cont, opt);
}

inline Action *
DNSProcessor::gethostbyaddr(Continuation *cont, IpAddr const *addr, Options const &opt)
{
  return getby(addr, T_PTR, cont, opt);
}

inline DNSProcessor::Options::Options() {}

inline DNSProcessor::Options &
DNSProcessor::Options::setHandler(DNSHandler *h)
{
  handler = h;
  return *this;
}

inline DNSProcessor::Options &
DNSProcessor::Options::setTimeout(int t)
{
  timeout = t;
  return *this;
}

inline DNSProcessor::Options &
DNSProcessor::Options::setHostResStyle(HostResStyle style)
{
  host_res_style = style;
  return *this;
}

inline DNSProcessor::Options &
DNSProcessor::Options::reset()
{
  *this = Options();
  return *this;
}

void ink_dns_init(ts::ModuleVersion version);
