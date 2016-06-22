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

#include "P_DNS.h" /* MAGIC_EDITING_TAG */
#include <ts/ink_inet.h>

#ifdef SPLIT_DNS
#include "I_SplitDNS.h"
#endif

#define SRV_COST (RRFIXEDSZ + 0)
#define SRV_WEIGHT (RRFIXEDSZ + 2)
#define SRV_PORT (RRFIXEDSZ + 4)
#define SRV_SERVER (RRFIXEDSZ + 6)
#define SRV_FIXEDSZ (RRFIXEDSZ + 6)

EventType ET_DNS = ET_CALL;

//
// Config
//
int dns_timeout                      = DEFAULT_DNS_TIMEOUT;
int dns_retries                      = DEFAULT_DNS_RETRIES;
int dns_search                       = DEFAULT_DNS_SEARCH;
int dns_failover_number              = DEFAULT_FAILOVER_NUMBER;
int dns_failover_period              = DEFAULT_FAILOVER_PERIOD;
int dns_failover_try_period          = DEFAULT_FAILOVER_TRY_PERIOD;
int dns_max_dns_in_flight            = MAX_DNS_IN_FLIGHT;
int dns_validate_qname               = 0;
unsigned int dns_handler_initialized = 0;
int dns_ns_rr                        = 0;
int dns_ns_rr_init_down              = 1;
char *dns_ns_list                    = NULL;
char *dns_resolv_conf                = NULL;
char *dns_local_ipv6                 = NULL;
char *dns_local_ipv4                 = NULL;
int dns_thread                       = 0;
int dns_prefer_ipv6                  = 0;
namespace
{
// Currently only used for A and AAAA.
inline char const *
QtypeName(int qtype)
{
  return T_AAAA == qtype ? "AAAA" : T_A == qtype ? "A" : "*";
}
inline bool
is_addr_query(int qtype)
{
  return qtype == T_A || qtype == T_AAAA;
}
}

DNSProcessor dnsProcessor;
ClassAllocator<DNSEntry> dnsEntryAllocator("dnsEntryAllocator");
// Users are expected to free these entries in short order!
// We could page align this buffer to enable page flipping for recv...
ClassAllocator<HostEnt> dnsBufAllocator("dnsBufAllocator", 2);

//
// Function Prototypes
//
static bool dns_process(DNSHandler *h, HostEnt *ent, int len);
static DNSEntry *get_dns(DNSHandler *h, uint16_t id);
// returns true when e is done
static void dns_result(DNSHandler *h, DNSEntry *e, HostEnt *ent, bool retry);
static void write_dns(DNSHandler *h);
static bool write_dns_event(DNSHandler *h, DNSEntry *e);

// "reliable" name to try. need to build up first.
static int try_servers         = 0;
static int local_num_entries   = 1;
static int attempt_num_entries = 1;
char try_server_names[DEFAULT_NUM_TRY_SERVER][MAXDNAME];

static inline char *
strnchr(char *s, char c, int len)
{
  while (*s && *s != c && len)
    ++s, --len;
  return *s == c ? s : (char *)NULL;
}

static inline uint16_t
ink_get16(const uint8_t *src)
{
  uint16_t dst;

  NS_GET16(dst, src);
  return dst;
}

void
HostEnt::free()
{
  dnsBufAllocator.free(this);
}

void
make_ipv4_ptr(in_addr_t addr, char *buffer)
{
  char *p          = buffer;
  uint8_t const *u = reinterpret_cast<uint8_t *>(&addr);

  if (u[3] > 99)
    *p++ = (u[3] / 100) + '0';
  if (u[3] > 9)
    *p++ = ((u[3] / 10) % 10) + '0';
  *p++   = u[3] % 10 + '0';
  *p++   = '.';
  if (u[2] > 99)
    *p++ = (u[2] / 100) + '0';
  if (u[2] > 9)
    *p++ = ((u[2] / 10) % 10) + '0';
  *p++   = u[2] % 10 + '0';
  *p++   = '.';
  if (u[1] > 99)
    *p++ = (u[1] / 100) + '0';
  if (u[1] > 9)
    *p++ = ((u[1] / 10) % 10) + '0';
  *p++   = u[1] % 10 + '0';
  *p++   = '.';
  if (u[0] > 99)
    *p++ = (u[0] / 100) + '0';
  if (u[0] > 9)
    *p++ = ((u[0] / 10) % 10) + '0';
  *p++   = u[0] % 10 + '0';
  *p++   = '.';
  ink_strlcpy(p, "in-addr.arpa", MAXDNAME - (p - buffer + 1));
}

void
make_ipv6_ptr(in6_addr const *addr, char *buffer)
{
  const char hex_digit[] = "0123456789abcdef";
  char *p                = buffer;
  uint8_t const *src     = addr->s6_addr;
  int i;

  for (i = TS_IP6_SIZE - 1; i >= 0; --i) {
    *p++ = hex_digit[src[i] & 0x0f];
    *p++ = '.';
    *p++ = hex_digit[src[i] >> 4];
    *p++ = '.';
  }

  ink_strlcpy(p, "ip6.arpa", MAXDNAME - (p - buffer + 1));
}

//  Public functions
//
//  See documentation is header files and Memos
//
int
DNSProcessor::start(int, size_t stacksize)
{
  //
  // Read configuration
  //
  REC_EstablishStaticConfigInt32(dns_retries, "proxy.config.dns.retries");
  REC_EstablishStaticConfigInt32(dns_timeout, "proxy.config.dns.lookup_timeout");
  REC_EstablishStaticConfigInt32(dns_search, "proxy.config.dns.search_default_domains");
  REC_EstablishStaticConfigInt32(dns_failover_number, "proxy.config.dns.failover_number");
  REC_EstablishStaticConfigInt32(dns_failover_period, "proxy.config.dns.failover_period");
  REC_EstablishStaticConfigInt32(dns_max_dns_in_flight, "proxy.config.dns.max_dns_in_flight");
  REC_EstablishStaticConfigInt32(dns_validate_qname, "proxy.config.dns.validate_query_name");
  REC_EstablishStaticConfigInt32(dns_ns_rr, "proxy.config.dns.round_robin_nameservers");
  REC_ReadConfigStringAlloc(dns_ns_list, "proxy.config.dns.nameservers");
  REC_ReadConfigStringAlloc(dns_local_ipv4, "proxy.config.dns.local_ipv4");
  REC_ReadConfigStringAlloc(dns_local_ipv6, "proxy.config.dns.local_ipv6");
  REC_ReadConfigStringAlloc(dns_resolv_conf, "proxy.config.dns.resolv_conf");
  REC_EstablishStaticConfigInt32(dns_thread, "proxy.config.dns.dedicated_thread");

  if (dns_thread > 0) {
    // TODO: Hmmm, should we just get a single thread some other way?
    ET_DNS = eventProcessor.spawn_event_threads(1, "ET_DNS", stacksize);
    initialize_thread_for_net(eventProcessor.eventthread[ET_DNS][0]);
  } else {
    // Initialize the first event thread for DNS.
    ET_DNS = ET_CALL;
  }
  thread = eventProcessor.eventthread[ET_DNS][0];

  dns_failover_try_period = dns_timeout + 1; // Modify the "default" accordingly

  if (SplitDNSConfig::gsplit_dns_enabled) {
    // reconfigure after threads start
    SplitDNSConfig::reconfigure();
  }

  // Setup the default DNSHandler, it's used both by normal DNS, and SplitDNS (for PTR lookups etc.)
  dns_init();
  open();

  return 0;
}

void
DNSProcessor::open(sockaddr const *target)
{
  DNSHandler *h = new DNSHandler;

  h->mutex = thread->mutex;
  h->m_res = &l_res;
  ats_ip_copy(&h->local_ipv4.sa, &local_ipv4.sa);
  ats_ip_copy(&h->local_ipv6.sa, &local_ipv6.sa);

  if (target)
    ats_ip_copy(&h->ip, target);
  else
    ats_ip_invalidate(&h->ip); // marked to use default.

  if (!dns_handler_initialized)
    handler = h;

  SET_CONTINUATION_HANDLER(h, &DNSHandler::startEvent);
  thread->schedule_imm(h);
}

//
// Initialization
//
void
DNSProcessor::dns_init()
{
  gethostname(try_server_names[0], 255);
  Debug("dns", "localhost=%s\n", try_server_names[0]);
  Debug("dns", "Round-robin nameservers = %d\n", dns_ns_rr);

  IpEndpoint nameserver[MAX_NAMED];
  size_t nserv = 0;

  if (dns_ns_list) {
    Debug("dns", "Nameserver list specified \"%s\"\n", dns_ns_list);
    int i;
    char *last;
    char *ns_list = ats_strdup(dns_ns_list);
    char *ns      = (char *)strtok_r(ns_list, " ,;\t\r", &last);

    for (i = 0, nserv = 0; (i < MAX_NAMED) && ns; ++i) {
      Debug("dns", "Nameserver list - parsing \"%s\"\n", ns);
      bool err    = false;
      int prt     = DOMAIN_SERVICE_PORT;
      char *colon = 0; // where the port colon is.
      // Check for IPv6 notation.
      if ('[' == *ns) {
        char *ndx = strchr(ns + 1, ']');
        if (ndx) {
          if (':' == ndx[1])
            colon = ndx + 1;
        } else {
          err = true;
          Warning("Unmatched '[' in address for nameserver '%s', discarding.", ns);
        }
      } else
        colon = strchr(ns, ':');

      if (!err && colon) {
        *colon = '\0';
        // coverity[secure_coding]
        if (sscanf(colon + 1, "%d%*s", &prt) != 1) {
          Debug("dns", "Unable to parse port number '%s' for nameserver '%s', discardin.", colon + 1, ns);
          Warning("Unable to parse port number '%s' for nameserver '%s', discarding.", colon + 1, ns);
          err = true;
        }
      }

      if (!err && 0 != ats_ip_pton(ns, &nameserver[nserv].sa)) {
        Debug("dns", "Invalid IP address given for nameserver '%s', discarding", ns);
        Warning("Invalid IP address given for nameserver '%s', discarding", ns);
        err = true;
      }

      if (!err) {
        ip_port_text_buffer buff;

        ats_ip_port_cast(&nameserver[nserv].sa) = htons(prt);

        Debug("dns", "Adding nameserver %s to nameserver list", ats_ip_nptop(&nameserver[nserv].sa, buff, sizeof(buff)));
        ++nserv;
      }

      ns = (char *)strtok_r(NULL, " ,;\t\r", &last);
    }
    ats_free(ns_list);
  }
  // The default domain (5th param) and search list (6th param) will
  // come from /etc/resolv.conf.
  if (ink_res_init(&l_res, nameserver, nserv, dns_search, NULL, NULL, dns_resolv_conf) < 0)
    Warning("Failed to build DNS res records for the servers (%s).  Using resolv.conf.", dns_ns_list);

  // Check for local forced bindings.

  if (dns_local_ipv6) {
    if (0 != ats_ip_pton(dns_local_ipv6, &local_ipv6)) {
      ats_ip_invalidate(&local_ipv6);
      Warning("Invalid IP address '%s' for dns.local_ipv6 value, discarding.", dns_local_ipv6);
    } else if (!ats_is_ip6(&local_ipv6.sa)) {
      ats_ip_invalidate(&local_ipv6);
      Warning("IP address '%s' for dns.local_ipv6 value was not IPv6, discarding.", dns_local_ipv6);
    }
  }

  if (dns_local_ipv4) {
    if (0 != ats_ip_pton(dns_local_ipv4, &local_ipv4)) {
      ats_ip_invalidate(&local_ipv4);
      Warning("Invalid IP address '%s' for dns.local_ipv4 value, discarding.", dns_local_ipv4);
    } else if (!ats_is_ip4(&local_ipv4.sa)) {
      ats_ip_invalidate(&local_ipv4);
      Warning("IP address '%s' for dns.local_ipv4 value was not IPv4, discarding.", dns_local_ipv4);
    }
  }
}

/**
  Inter-OS portability for dn_expand.  dn_expand() expands the compressed
  domain name comp_dn to a full domain name. Expanded names are converted
  to upper case. msg is a pointer to the beginning of the message,
  exp_dn is a pointer to a buffer of size length for the result. The
  size of compressed name is returned or -1 if there was an error.

*/
inline int
ink_dn_expand(const u_char *msg, const u_char *eom, const u_char *comp_dn, u_char *exp_dn, int length)
{
  return ::dn_expand((unsigned char *)msg, (unsigned char *)eom, (unsigned char *)comp_dn, (char *)exp_dn, length);
}

DNSProcessor::DNSProcessor() : thread(NULL), handler(NULL)
{
  ink_zero(l_res);
  ink_zero(local_ipv6);
  ink_zero(local_ipv4);
}

void
DNSEntry::init(const char *x, int len, int qtype_arg, Continuation *acont, DNSProcessor::Options const &opt)
{
  qtype          = qtype_arg;
  host_res_style = opt.host_res_style;
  if (is_addr_query(qtype)) {
    // adjust things based on family preference.
    if (HOST_RES_IPV4 == host_res_style || HOST_RES_IPV4_ONLY == host_res_style) {
      qtype = T_A;
    } else if (HOST_RES_IPV6 == host_res_style || HOST_RES_IPV6_ONLY == host_res_style) {
      qtype = T_AAAA;
    }
  }
  submit_time   = Thread::get_hrtime();
  action        = acont;
  submit_thread = acont->mutex->thread_holding;

#ifdef SPLIT_DNS
  if (SplitDNSConfig::gsplit_dns_enabled) {
    dnsH = opt.handler ? opt.handler : dnsProcessor.handler;
  } else {
    dnsH = dnsProcessor.handler;
  }
#else
  dnsH = dnsProcessor.handler;
#endif // SPLIT_DNS

  dnsH->txn_lookup_timeout = opt.timeout;

  mutex = dnsH->mutex;

  if (is_addr_query(qtype) || qtype == T_SRV) {
    if (len) {
      len = len > (MAXDNAME - 1) ? (MAXDNAME - 1) : len;
      memcpy(qname, x, len);
      qname[len]     = 0;
      orig_qname_len = qname_len = len;
    } else {
      qname_len      = ink_strlcpy(qname, x, MAXDNAME);
      orig_qname_len = qname_len;
    }
  } else { // T_PTR
    IpAddr const *ip = reinterpret_cast<IpAddr const *>(x);
    if (ip->isIp6())
      make_ipv6_ptr(&ip->_addr._ip6, qname);
    else if (ip->isIp4())
      make_ipv4_ptr(ip->_addr._ip4, qname);
    else
      ink_assert(!"T_PTR query to DNS must be IP address.");
  }

  SET_HANDLER((DNSEntryHandler)&DNSEntry::mainEvent);
}

/**
  Open (and close) connections as necessary and also assures that the
  epoll fd struct is properly updated.

*/
void
DNSHandler::open_con(sockaddr const *target, bool failed, int icon)
{
  ip_port_text_buffer ip_text;
  PollDescriptor *pd = get_PollDescriptor(dnsProcessor.thread);

  if (!icon && target) {
    ats_ip_copy(&ip, target);
  } else if (!target) {
    target = &ip.sa;
  }

  Debug("dns", "open_con: opening connection %s", ats_ip_nptop(target, ip_text, sizeof ip_text));

  if (con[icon].fd != NO_FD) { // Remove old FD from epoll fd
    con[icon].eio.stop();
    con[icon].close();
  }

  if (con[icon].connect(target, DNSConnection::Options()
                                  .setNonBlockingConnect(true)
                                  .setNonBlockingIo(true)
                                  .setUseTcp(false)
                                  .setBindRandomPort(true)
                                  .setLocalIpv6(&local_ipv6.sa)
                                  .setLocalIpv4(&local_ipv4.sa)) < 0) {
    Debug("dns", "opening connection %s FAILED for %d", ip_text, icon);
    if (!failed) {
      if (dns_ns_rr)
        rr_failure(icon);
      else
        failover();
    }
    return;
  } else {
    ns_down[icon] = 0;
    if (con[icon].eio.start(pd, &con[icon], EVENTIO_READ) < 0) {
      Error("[iocore_dns] open_con: Failed to add %d server to epoll list\n", icon);
    } else {
      con[icon].num = icon;
      Debug("dns", "opening connection %s SUCCEEDED for %d", ip_text, icon);
    }
  }
}

void
DNSHandler::validate_ip()
{
  if (!ip.isValid()) {
    // Invalid, switch to default.
    // seems that res_init always sets m_res.nscount to at least 1!
    if (!m_res->nscount || !ats_ip_copy(&ip.sa, &m_res->nsaddr_list[0].sa)) {
      Warning("bad nameserver config, fallback to loopback");
      ip.setToLoopback(AF_INET);
    }
  }
}
/**
  Initial state of the DNSHandler. Can reinitialize the running DNS
  handler to a new nameserver.

*/
int
DNSHandler::startEvent(int /* event ATS_UNUSED */, Event *e)
{
  //
  // If this is for the default server, get it
  Debug("dns", "DNSHandler::startEvent: on thread %d\n", e->ethread->id);

  this->validate_ip();

  if (!dns_handler_initialized) {
    //
    // If we are THE handler, open connection and configure for
    // periodic execution.
    //
    dns_handler_initialized = 1;
    SET_HANDLER(&DNSHandler::mainEvent);
    if (dns_ns_rr) {
      int max_nscount = m_res->nscount;
      if (max_nscount > MAX_NAMED)
        max_nscount = MAX_NAMED;
      n_con         = 0;
      for (int i = 0; i < max_nscount; i++) {
        ip_port_text_buffer buff;
        sockaddr *sa = &m_res->nsaddr_list[i].sa;
        if (ats_is_ip(sa)) {
          open_con(sa, false, n_con);
          ++n_con;
          Debug("dns_pas", "opened connection to %s, n_con = %d", ats_ip_nptop(sa, buff, sizeof(buff)), n_con);
        }
      }
      dns_ns_rr_init_down = 0;
    } else {
      open_con(0); // use current target address.
      n_con = 1;
    }
    e->ethread->schedule_every(this, DNS_PERIOD);

    return EVENT_CONT;
  } else {
    ink_assert(false); // I.e. this should never really happen
    return EVENT_DONE;
  }
}

/**
  Initial state of the DSNHandler. Can reinitialize the running DNS
  hander to a new nameserver.
*/
int
DNSHandler::startEvent_sdns(int /* event ATS_UNUSED */, Event *e)
{
  Debug("dns", "DNSHandler::startEvent_sdns: on thread %d\n", e->ethread->id);
  this->validate_ip();

  SET_HANDLER(&DNSHandler::mainEvent);
  open_con(&ip.sa, false, n_con);
  ++n_con; // TODO should n_con be zeroed?

  e->schedule_every(DNS_PERIOD);
  return EVENT_CONT;
}

static inline int
_ink_res_mkquery(ink_res_state res, char *qname, int qtype, char *buffer)
{
  int r = ink_res_mkquery(res, QUERY, qname, C_IN, qtype, NULL, 0, NULL, (unsigned char *)buffer, MAX_DNS_PACKET_LEN);
  return r;
}

void
DNSHandler::recover()
{
  ip_text_buffer buff;
  Warning("connection to DNS server %s restored", ats_ip_ntop(&ip.sa, buff, sizeof(buff)));
  name_server = 0;
  switch_named(name_server);
}

void
DNSHandler::retry_named(int ndx, ink_hrtime t, bool reopen)
{
  if (reopen && ((t - last_primary_reopen) > DNS_PRIMARY_REOPEN_PERIOD)) {
    Debug("dns", "retry_named: reopening DNS connection for index %d", ndx);
    last_primary_reopen = t;
    con[ndx].close();
    open_con(&m_res->nsaddr_list[ndx].sa, true, ndx);
  }

  char buffer[MAX_DNS_PACKET_LEN];
  Debug("dns", "trying to resolve '%s' from DNS connection, ndx %d", try_server_names[try_servers], ndx);
  int r       = _ink_res_mkquery(m_res, try_server_names[try_servers], T_A, buffer);
  try_servers = (try_servers + 1) % countof(try_server_names);
  ink_assert(r >= 0);
  if (r >= 0) { // looking for a bounce
    int res = socketManager.send(con[ndx].fd, buffer, r, 0);
    Debug("dns", "ping result = %d", res);
  }
}

void
DNSHandler::try_primary_named(bool reopen)
{
  ink_hrtime t = Thread::get_hrtime();
  if (reopen && ((t - last_primary_reopen) > DNS_PRIMARY_REOPEN_PERIOD)) {
    Debug("dns", "try_primary_named: reopening primary DNS connection");
    last_primary_reopen = t;
    open_con(&ip.sa, true, 0);
  }
  if ((t - last_primary_retry) > DNS_PRIMARY_RETRY_PERIOD) {
    char buffer[MAX_DNS_PACKET_LEN];

    last_primary_retry = t;
    Debug("dns", "trying to resolve '%s' from primary DNS connection", try_server_names[try_servers]);
    int r = _ink_res_mkquery(m_res, try_server_names[try_servers], T_A, buffer);
    // if try_server_names[] is not full, round-robin within the
    // filled entries.
    if (local_num_entries < DEFAULT_NUM_TRY_SERVER)
      try_servers = (try_servers + 1) % local_num_entries;
    else
      try_servers = (try_servers + 1) % countof(try_server_names);
    ink_assert(r >= 0);
    if (r >= 0) { // looking for a bounce
      int res = socketManager.send(con[0].fd, buffer, r, 0);
      Debug("dns", "ping result = %d", res);
    }
  }
}

void
DNSHandler::switch_named(int ndx)
{
  for (DNSEntry *e = entries.head; e; e = (DNSEntry *)e->link.next) {
    e->written_flag = 0;
    if (e->retries < dns_retries)
      ++(e->retries); // give them another chance
  }
  in_flight = 0;
  received_one(ndx); // reset failover counters
}

/** Fail over to another name server. */
void
DNSHandler::failover()
{
  Debug("dns", "failover: initiating failover attempt, current name_server=%d", name_server);
  // no hope, if we have only one server
  if (m_res->nscount > 1) {
    ip_text_buffer buff1, buff2;
    int max_nscount = m_res->nscount;

    if (max_nscount > MAX_NAMED)
      max_nscount            = MAX_NAMED;
    sockaddr const *old_addr = &m_res->nsaddr_list[name_server].sa;
    name_server              = (name_server + 1) % max_nscount;
    Debug("dns", "failover: failing over to name_server=%d", name_server);

    IpEndpoint target;
    ats_ip_copy(&target.sa, &m_res->nsaddr_list[name_server].sa);

    Warning("failover: connection to DNS server %s lost, move to %s", ats_ip_ntop(old_addr, buff1, sizeof(buff1)),
            ats_ip_ntop(&target.sa, buff2, sizeof(buff2)));

    if (!target.isValid())
      target.setToLoopback(AF_INET);

    open_con(&target.sa, true, name_server);
    if (n_con <= name_server)
      n_con = name_server + 1;
    switch_named(name_server);
  } else {
    ip_text_buffer buff;
    Warning("failover: connection to DNS server %s lost, retrying", ats_ip_ntop(&ip.sa, buff, sizeof(buff)));
  }
}

/** Mark one of the nameservers as down. */
void
DNSHandler::rr_failure(int ndx)
{
  // no hope, if we have only one server
  if (!ns_down[ndx]) {
    ip_text_buffer buff;
    // mark this nameserver as down
    Debug("dns", "rr_failure: Marking nameserver %d as down", ndx);
    ns_down[ndx] = 1;
    Warning("connection to DNS server %s lost, marking as down", ats_ip_ntop(&m_res->nsaddr_list[ndx].sa, buff, sizeof(buff)));
  }

  int nscount = m_res->nscount;
  if (nscount > MAX_NAMED)
    nscount = MAX_NAMED;

  // See if all nameservers are down
  int all_down = 1;

  for (int i = 0; i < nscount && all_down; i++) {
    Debug("dns", "nsdown[%d]=%d", i, ns_down[i]);
    if (!ns_down[i]) {
      all_down = 0;
    }
  }

  if (all_down && !dns_ns_rr_init_down) {
    Warning("connection to all DNS servers lost, retrying");
    // actual retries will be done in retry_named called from mainEvent
    // mark any outstanding requests as not sent for later retry
    for (DNSEntry *e = entries.head; e; e = (DNSEntry *)e->link.next) {
      e->written_flag = 0;
      if (e->retries < dns_retries)
        ++(e->retries); // give them another chance
      --in_flight;
      DNS_DECREMENT_DYN_STAT(dns_in_flight_stat);
    }
  } else {
    // move outstanding requests that were sent to this nameserver to another
    for (DNSEntry *e = entries.head; e; e = (DNSEntry *)e->link.next) {
      if (e->which_ns == ndx) {
        e->written_flag = 0;
        if (e->retries < dns_retries)
          ++(e->retries); // give them another chance
        --in_flight;
        DNS_DECREMENT_DYN_STAT(dns_in_flight_stat);
      }
    }
  }
}

static inline unsigned int
get_rcode(char *buff)
{
  return reinterpret_cast<HEADER *>(buff)->rcode;
}

static inline unsigned int
get_rcode(HostEnt *ent)
{
  return get_rcode(reinterpret_cast<char *>(ent));
}

static bool
good_rcode(char *buff)
{
  unsigned int r = get_rcode(buff);
  return NOERROR == r || NXDOMAIN == r;
}

void
DNSHandler::recv_dns(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  DNSConnection *dnsc = NULL;
  ip_text_buffer ipbuff1, ipbuff2;

  while ((dnsc = (DNSConnection *)triggered.dequeue())) {
    while (1) {
      IpEndpoint from_ip;
      socklen_t from_length = sizeof(from_ip);

      if (!hostent_cache)
        hostent_cache = dnsBufAllocator.alloc();
      HostEnt *buf    = hostent_cache;

      int res = socketManager.recvfrom(dnsc->fd, buf->buf, MAX_DNS_PACKET_LEN, 0, &from_ip.sa, &from_length);

      if (res == -EAGAIN)
        break;
      if (res <= 0) {
        Debug("dns", "named error: %d", res);
        if (dns_ns_rr)
          rr_failure(dnsc->num);
        else if (dnsc->num == name_server)
          failover();
        break;
      }

      // verify that this response came from the correct server
      if (!ats_ip_addr_eq(&dnsc->ip.sa, &from_ip.sa)) {
        Warning("unexpected DNS response from %s (expected %s)", ats_ip_ntop(&from_ip.sa, ipbuff1, sizeof ipbuff1),
                ats_ip_ntop(&dnsc->ip.sa, ipbuff2, sizeof ipbuff2));
        continue;
      }
      hostent_cache    = 0;
      buf->packet_size = res;
      Debug("dns", "received packet size = %d", res);
      if (dns_ns_rr) {
        Debug("dns", "round-robin: nameserver %d DNS response code = %d", dnsc->num, get_rcode(buf));
        if (good_rcode(buf->buf)) {
          received_one(dnsc->num);
          if (ns_down[dnsc->num]) {
            Warning("connection to DNS server %s restored",
                    ats_ip_ntop(&m_res->nsaddr_list[dnsc->num].sa, ipbuff1, sizeof ipbuff1));
            ns_down[dnsc->num] = 0;
          }
        }
      } else {
        if (!dnsc->num) {
          Debug("dns", "primary DNS response code = %d", get_rcode(buf));
          if (good_rcode(buf->buf)) {
            if (name_server)
              recover();
            else
              received_one(name_server);
          }
        }
      }
      Ptr<HostEnt> protect_hostent = make_ptr(buf);
      if (dns_process(this, buf, res)) {
        if (dnsc->num == name_server)
          received_one(name_server);
      }
    }
  }
}

/** Main event for the DNSHandler. Attempt to read from and write to named. */
int
DNSHandler::mainEvent(int event, Event *e)
{
  recv_dns(event, e);
  if (dns_ns_rr) {
    ink_hrtime t = Thread::get_hrtime();
    if (t - last_primary_retry > DNS_PRIMARY_RETRY_PERIOD) {
      for (int i = 0; i < n_con; i++) {
        if (ns_down[i]) {
          Debug("dns", "mainEvent: nameserver = %d is down", i);
          retry_named(i, t, true);
        }
      }
      last_primary_retry = t;
    }
    for (int i = 0; i < n_con; i++) {
      if (!ns_down[i] && failover_soon(i)) {
        Debug("dns", "mainEvent: nameserver = %d failover soon", name_server);
        if (failover_now(i))
          rr_failure(i);
        else {
          Debug("dns", "mainEvent: nameserver = %d no failover now - retrying", i);
          retry_named(i, t, false);
          ++failover_soon_number[i];
        }
      }
    }
  } else {
    if (failover_soon(name_server)) {
      Debug("dns", "mainEvent: will failover soon");
      if (failover_now(name_server)) {
        Debug("dns", "mainEvent: failing over now to another nameserver");
        failover();
      } else {
        try_primary_named(false);
        ++failover_soon_number[name_server];
      }
    } else if (name_server) // not on the primary named
      try_primary_named(true);
  }

  if (entries.head)
    write_dns(this);

  return EVENT_CONT;
}

/** Find a DNSEntry by id. */
inline static DNSEntry *
get_dns(DNSHandler *h, uint16_t id)
{
  for (DNSEntry *e = h->entries.head; e; e = (DNSEntry *)e->link.next) {
    if (e->once_written_flag) {
      for (int j = 0; j < MAX_DNS_RETRIES; j++) {
        if (e->id[j] == id) {
          return e;
        } else if (e->id[j] < 0) {
          goto Lnext;
        }
      }
    }
  Lnext:;
  }
  return NULL;
}

/** Find a DNSEntry by query name and type. */
inline static DNSEntry *
get_entry(DNSHandler *h, char *qname, int qtype)
{
  for (DNSEntry *e = h->entries.head; e; e = (DNSEntry *)e->link.next) {
    if (e->qtype == qtype) {
      if (is_addr_query(qtype)) {
        if (!strcmp(qname, e->qname))
          return e;
      } else if (0 == memcmp(qname, e->qname, e->qname_len))
        return e;
    }
  }
  return NULL;
}

/** Write up to dns_max_dns_in_flight entries. */
static void
write_dns(DNSHandler *h)
{
  ProxyMutex *mutex = h->mutex;
  DNS_INCREMENT_DYN_STAT(dns_total_lookups_stat);
  int max_nscount = h->m_res->nscount;
  if (max_nscount > MAX_NAMED)
    max_nscount = MAX_NAMED;
  if (max_nscount <= 0) {
    Warning("There is no name server found in the resolv.conf");
    if (h->entries.head) {
      dns_result(h, h->entries.head, NULL, false);
    }
    return;
  }

  if (h->in_write_dns)
    return;
  h->in_write_dns = true;
  // Debug("dns", "in_flight: %d, dns_max_dns_in_flight: %d", h->in_flight, dns_max_dns_in_flight);
  if (h->in_flight < dns_max_dns_in_flight) {
    DNSEntry *e = h->entries.head;
    while (e) {
      DNSEntry *n = (DNSEntry *)e->link.next;
      if (!e->written_flag) {
        if (dns_ns_rr) {
          int ns_start = h->name_server;
          do {
            h->name_server = (h->name_server + 1) % max_nscount;
          } while (h->ns_down[h->name_server] && h->name_server != ns_start);
        }
        if (!write_dns_event(h, e))
          break;
      }
      if (h->in_flight >= dns_max_dns_in_flight)
        break;
      e = n;
    }
  }
  h->in_write_dns = false;
}

uint16_t
DNSHandler::get_query_id()
{
  uint16_t q1, q2;
  q2 = q1 = (uint16_t)(generator.random() & 0xFFFF);
  if (query_id_in_use(q2)) {
    uint16_t i = q2 >> 6;
    while (qid_in_flight[i] == UINT64_MAX) {
      if (++i == sizeof(qid_in_flight) / sizeof(uint64_t)) {
        i = 0;
      }
      if (i == q1 >> 6) {
        Error("[iocore_dns] get_query_id: Exhausted all DNS query ids");
        return q1;
      }
    }
    i <<= 6;
    q2 &= 0x3F;
    while (query_id_in_use(i + q2)) {
      ++q2;
      q2 &= 0x3F;
      if (q2 == (q1 & 0x3F)) {
        Error("[iocore_dns] get_query_id: Exhausted all DNS query ids");
        return q1;
      }
    }
    q2 += i;
  }

  set_query_id_in_use(q2);
  return q2;
}

/**
  Construct and Write the request for a single entry (using send(3N)).

  @return true = keep going, false = give up for now.

*/
static bool
write_dns_event(DNSHandler *h, DNSEntry *e)
{
  ProxyMutex *mutex = h->mutex;
  union {
    HEADER _h;
    char _b[MAX_DNS_PACKET_LEN];
  } blob;
  int r = 0;

  if ((r = _ink_res_mkquery(h->m_res, e->qname, e->qtype, blob._b)) <= 0) {
    Debug("dns", "cannot build query: %s", e->qname);
    dns_result(h, e, NULL, false);
    return true;
  }

  uint16_t i = h->get_query_id();
  blob._h.id = htons(i);
  if (e->id[dns_retries - e->retries] >= 0) {
    // clear previous id in case named was switched or domain was expanded
    h->release_query_id(e->id[dns_retries - e->retries]);
  }
  e->id[dns_retries - e->retries] = i;
  Debug("dns", "send query (qtype=%d) for %s to fd %d", e->qtype, e->qname, h->con[h->name_server].fd);

  int s = socketManager.send(h->con[h->name_server].fd, blob._b, r, 0);
  if (s != r) {
    Debug("dns", "send() failed: qname = %s, %d != %d, nameserver= %d", e->qname, s, r, h->name_server);
    // changed if condition from 'r < 0' to 's < 0' - 8/2001 pas
    if (s < 0) {
      if (dns_ns_rr)
        h->rr_failure(h->name_server);
      else
        h->failover();
    }
    return false;
  }

  e->written_flag      = true;
  e->which_ns          = h->name_server;
  e->once_written_flag = true;
  ++h->in_flight;
  DNS_INCREMENT_DYN_STAT(dns_in_flight_stat);

  e->send_time = Thread::get_hrtime();

  if (e->timeout)
    e->timeout->cancel();

  if (h->txn_lookup_timeout) {
    e->timeout = h->mutex->thread_holding->schedule_in(e, HRTIME_MSECONDS(h->txn_lookup_timeout)); // this is in msec
  } else {
    e->timeout = h->mutex->thread_holding->schedule_in(e, HRTIME_SECONDS(dns_timeout));
  }

  Debug("dns", "sent qname = %s, id = %u, nameserver = %d", e->qname, e->id[dns_retries - e->retries], h->name_server);
  h->sent_one();
  return true;
}

int
DNSEntry::delayEvent(int event, Event *e)
{
  (void)event;
  if (dnsProcessor.handler) {
    SET_HANDLER((DNSEntryHandler)&DNSEntry::mainEvent);
    return handleEvent(EVENT_IMMEDIATE, e);
  }
  e->schedule_in(DNS_DELAY_PERIOD);
  return EVENT_CONT;
}

/** Handle timeout events. */
int
DNSEntry::mainEvent(int event, Event *e)
{
  switch (event) {
  default:
    ink_assert(!"bad case");
    return EVENT_DONE;
  case EVENT_IMMEDIATE: {
    if (!dnsH)
      dnsH = dnsProcessor.handler;
    if (!dnsH) {
      Debug("dns", "handler not found, retrying...");
      SET_HANDLER((DNSEntryHandler)&DNSEntry::delayEvent);
      return handleEvent(event, e);
    }

    // trailing '.' indicates no domain expansion
    if (dns_search && ('.' != qname[orig_qname_len - 1])) {
      domains = dnsH->m_res->dnsrch;
      // start domain expansion straight away
      // if lookup name has no '.'
      if (domains && !strnchr(qname, '.', MAXDNAME)) {
        qname[orig_qname_len] = '.';
        qname_len = orig_qname_len + 1 + ink_strlcpy(qname + orig_qname_len + 1, *domains, MAXDNAME - (orig_qname_len + 1));
        ++domains;
      }
    } else {
      domains = NULL;
    }
    Debug("dns", "enqueing query %s", qname);
    DNSEntry *dup = get_entry(dnsH, qname, qtype);
    if (dup) {
      Debug("dns", "collapsing NS request");
      dup->dups.enqueue(this);
    } else {
      Debug("dns", "adding first to collapsing queue");
      dnsH->entries.enqueue(this);
      write_dns(dnsH);
    }
    return EVENT_DONE;
  }
  case EVENT_INTERVAL:
    Debug("dns", "timeout for query %s", qname);
    if (dnsH->txn_lookup_timeout) {
      timeout = NULL;
      dns_result(dnsH, this, result_ent, false); // do not retry -- we are over TXN timeout on DNS alone!
      return EVENT_DONE;
    }
    if (written_flag) {
      Debug("dns", "marking %s as not-written", qname);
      written_flag = false;
      --(dnsH->in_flight);
      DNS_DECREMENT_DYN_STAT(dns_in_flight_stat);
    }
    timeout = NULL;
    dns_result(dnsH, this, result_ent, true);
    return EVENT_DONE;
  }
}

Action *
DNSProcessor::getby(const char *x, int len, int type, Continuation *cont, Options const &opt)
{
  Debug("dns", "received query %s type = %d, timeout = %d", x, type, opt.timeout);
  if (type == T_SRV) {
    Debug("dns_srv", "DNSProcessor::getby attempting an SRV lookup for %s, timeout = %d", x, opt.timeout);
  }
  DNSEntry *e = dnsEntryAllocator.alloc();
  e->retries  = dns_retries;
  e->init(x, len, type, cont, opt);
  MUTEX_TRY_LOCK(lock, e->mutex, this_ethread());
  if (!lock.is_locked())
    thread->schedule_imm(e);
  else
    e->handleEvent(EVENT_IMMEDIATE, 0);
  return &e->action;
}

/**
  We have a result for an entry, return it to the user or retry if it
  is a retry-able and we have retries left.
*/
static void
dns_result(DNSHandler *h, DNSEntry *e, HostEnt *ent, bool retry)
{
  ProxyMutex *mutex = h->mutex;
  bool cancelled    = (e->action.cancelled ? true : false);

  if (!ent && !cancelled) {
    // try to retry operation
    if (retry && e->retries) {
      Debug("dns", "doing retry for %s", e->qname);

      DNS_INCREMENT_DYN_STAT(dns_retries_stat);

      --(e->retries);
      write_dns(h);
      return;
    } else if (e->domains && *e->domains) {
      do {
        Debug("dns", "domain extending, last tried '%s', original '%.*s'", e->qname, e->orig_qname_len, e->qname);

        // Make sure the next try fits
        if (e->orig_qname_len + strlen(*e->domains) + 2 > MAXDNAME) {
          Debug("dns", "domain too large %.*s + %s", e->orig_qname_len, e->qname, *e->domains);
        } else {
          e->qname[e->orig_qname_len] = '.';
          e->qname_len =
            e->orig_qname_len + 1 + ink_strlcpy(e->qname + e->orig_qname_len + 1, *e->domains, MAXDNAME - (e->orig_qname_len + 1));
          ++(e->domains);
          e->retries = dns_retries;
          Debug("dns", "new name = %s retries = %d", e->qname, e->retries);
          write_dns(h);

          return;
        }

        // Try another one
        ++(e->domains);
      } while (*e->domains);
    } else {
      e->qname[e->qname_len] = 0;
      if (!strchr(e->qname, '.') && !e->last) {
        e->last = true;
        write_dns(h);
        return;
      }
    }
    if (retry) {
      DNS_INCREMENT_DYN_STAT(dns_max_retries_exceeded_stat);
    }
  }
  if (ent == BAD_DNS_RESULT)
    ent = NULL;
  if (!cancelled) {
    if (!ent) {
      DNS_SUM_DYN_STAT(dns_fail_time_stat, Thread::get_hrtime() - e->submit_time);
    } else {
      DNS_SUM_DYN_STAT(dns_success_time_stat, Thread::get_hrtime() - e->submit_time);
    }
  }
  h->entries.remove(e);

  if (is_debug_tag_set("dns")) {
    if (is_addr_query(e->qtype)) {
      ip_text_buffer buff;
      char const *ptr    = "<none>";
      char const *result = "FAIL";
      if (ent) {
        result = "SUCCESS";
        ptr    = inet_ntop(e->qtype == T_AAAA ? AF_INET6 : AF_INET, ent->ent.h_addr_list[0], buff, sizeof(buff));
      }
      Debug("dns", "%s result for %s = %s retry %d", result, e->qname, ptr, retry);
    } else {
      if (ent) {
        Debug("dns", "SUCCESS result for %s = %s af=%d retry %d", e->qname, ent->ent.h_name, ent->ent.h_addrtype, retry);
      } else {
        Debug("dns", "FAIL result for %s = <not found> retry %d", e->qname, retry);
      }
    }
  }

  if (ent) {
    DNS_INCREMENT_DYN_STAT(dns_lookup_success_stat);
  } else {
    DNS_INCREMENT_DYN_STAT(dns_lookup_fail_stat);
  }

  DNSEntry *dup = NULL;
  while ((dup = e->dups.dequeue())) {
    if (dup->post(h, ent)) {
      e->dups.enqueue(dup);
      goto Lretry;
    }
  }

  if (e->timeout) {
    e->timeout->cancel(e);
    e->timeout = NULL;
  }
  e->result_ent = ent;

  if (h->mutex->thread_holding == e->submit_thread) {
    MUTEX_TRY_LOCK(lock, e->action.mutex, h->mutex->thread_holding);
    if (!lock.is_locked()) {
      Debug("dns", "failed lock for result %s", e->qname);
      goto Lretry;
    }
    for (int i = 0; i < MAX_DNS_RETRIES; i++) {
      if (e->id[i] < 0)
        break;
      h->release_query_id(e->id[i]);
    }
    e->postEvent(0, 0);
  } else {
    for (int i = 0; i < MAX_DNS_RETRIES; i++) {
      if (e->id[i] < 0)
        break;
      h->release_query_id(e->id[i]);
    }
    e->mutex = e->action.mutex;
    SET_CONTINUATION_HANDLER(e, &DNSEntry::postEvent);
    e->submit_thread->schedule_imm_signal(e);
  }
  return;
Lretry:
  e->result_ent = ent;
  e->retries    = 0;
  if (e->timeout)
    e->timeout->cancel();
  e->timeout = h->mutex->thread_holding->schedule_in(e, DNS_PERIOD);
}

int
DNSEntry::post(DNSHandler *h, HostEnt *ent)
{
  if (timeout) {
    timeout->cancel(this);
    timeout = NULL;
  }
  result_ent = ent;
  if (h->mutex->thread_holding == submit_thread) {
    MUTEX_TRY_LOCK(lock, action.mutex, h->mutex->thread_holding);
    if (!lock.is_locked()) {
      Debug("dns", "failed lock for result %s", qname);
      return 1;
    }
    postEvent(0, 0);
  } else {
    mutex = action.mutex;
    SET_HANDLER(&DNSEntry::postEvent);
    submit_thread->schedule_imm_signal(this);
  }
  return 0;
}

int
DNSEntry::postEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  if (!action.cancelled) {
    Debug("dns", "called back continuation for %s", qname);
    action.continuation->handleEvent(DNS_EVENT_LOOKUP, result_ent);
  }
  result_ent   = NULL;
  action.mutex = NULL;
  mutex        = NULL;
  dnsEntryAllocator.free(this);
  return EVENT_DONE;
}

/** Decode the reply from "named". */
static bool
dns_process(DNSHandler *handler, HostEnt *buf, int len)
{
  ProxyMutex *mutex = handler->mutex;
  HEADER *h         = (HEADER *)(buf->buf);
  DNSEntry *e       = get_dns(handler, (uint16_t)ntohs(h->id));
  bool retry        = false;
  bool server_ok    = true;
  uint32_t temp_ttl = 0;

  //
  // Do we have an entry for this id?
  //
  if (!e || !e->written_flag) {
    Debug("dns", "unknown DNS id = %u", (uint16_t)ntohs(h->id));
    return false; // cannot count this as a success
  }
  //
  // It is no longer in flight
  //
  e->written_flag = false;
  --(handler->in_flight);
  DNS_DECREMENT_DYN_STAT(dns_in_flight_stat);

  DNS_SUM_DYN_STAT(dns_response_time_stat, Thread::get_hrtime() - e->send_time);

  if (h->rcode != NOERROR || !h->ancount) {
    Debug("dns", "received rcode = %d", h->rcode);
    switch (h->rcode) {
    default:
      Warning("Unknown DNS error %d for [%s]", h->rcode, e->qname);
      retry     = true;
      server_ok = false; // could be server problems
      goto Lerror;
    case SERVFAIL: // recoverable error
      retry = true;
    case FORMERR: // unrecoverable errors
    case REFUSED:
    case NOTIMP:
      Debug("dns", "DNS error %d for [%s]", h->rcode, e->qname);
      server_ok = false; // could be server problems
      goto Lerror;
    case NOERROR:
    case NXDOMAIN:
    case 6:  // YXDOMAIN
    case 7:  // YXRRSET
    case 8:  // NOTAUTH
    case 9:  // NOTAUTH
    case 10: // NOTZONE
      Debug("dns", "DNS error %d for [%s]", h->rcode, e->qname);
      goto Lerror;
    }
  } else {
    //
    // Initialize local data
    //
    //    struct in_addr host_addr;            unused
    u_char tbuf[MAXDNAME + 1];
    buf->ent.h_name = NULL;

    int ancount       = ntohs(h->ancount);
    unsigned char *bp = buf->hostbuf;
    int buflen        = sizeof(buf->hostbuf);
    u_char *cp        = ((u_char *)h) + HFIXEDSZ;
    u_char *eom       = (u_char *)h + len;
    int n;
    ink_assert(buf->srv_hosts.srv_host_count == 0 && buf->srv_hosts.srv_hosts_length == 0);
    buf->srv_hosts.srv_host_count   = 0;
    buf->srv_hosts.srv_hosts_length = 0;
    unsigned &num_srv               = buf->srv_hosts.srv_host_count;
    int rname_len                   = -1;

    //
    // Expand name
    //
    if ((n = ink_dn_expand((u_char *)h, eom, cp, bp, buflen)) < 0)
      goto Lerror;

    // Should we validate the query name?
    if (dns_validate_qname) {
      int qlen = e->qname_len;
      int rlen = strlen((char *)bp);

      rname_len = rlen; // Save for later use
      if ((qlen > 0) && ('.' == e->qname[qlen - 1]))
        --qlen;
      if ((rlen > 0) && ('.' == bp[rlen - 1]))
        --rlen;
      // TODO: At some point, we might want to care about the case here, and use an algorithm
      // to randomly pick upper case characters in the query, and validate the response with
      // case sensitivity.
      if ((qlen != rlen) || (strncasecmp(e->qname, (const char *)bp, qlen) != 0)) {
        // Bad mojo, forged?
        Warning("received DNS response with query name of '%s', but response query name is '%s'", e->qname, bp);
        goto Lerror;
      } else {
        Debug("dns", "query name validated properly for %s", e->qname);
      }
    }

    cp += n + QFIXEDSZ;
    if (is_addr_query(e->qtype)) {
      if (-1 == rname_len)
        n = strlen((char *)bp) + 1;
      else
        n             = rname_len + 1;
      buf->ent.h_name = (char *)bp;
      bp += n;
      buflen -= n;
    }
    //
    // Configure HostEnt data structure
    //
    u_char **ap          = buf->host_aliases;
    buf->ent.h_aliases   = (char **)buf->host_aliases;
    u_char **hap         = (u_char **)buf->h_addr_ptrs;
    *hap                 = NULL;
    buf->ent.h_addr_list = (char **)buf->h_addr_ptrs;

    //
    // INKqa10938: For customer (i.e. USPS) with closed environment, need to
    // build up try_server_names[] with names already successfully resolved.
    // try_server_names[] gets filled up with every success dns response.
    // Once it's full, a new entry get inputted into try_server_names round-
    // robin style every 50 success dns response.

    // TODO: Why do we do strlen(e->qname) ? That should be available in
    // e->qname_len, no ?
    if (local_num_entries >= DEFAULT_NUM_TRY_SERVER) {
      if ((attempt_num_entries % 50) == 0) {
        try_servers = (try_servers + 1) % countof(try_server_names);
        ink_strlcpy(try_server_names[try_servers], e->qname, MAXDNAME);
        memset(&try_server_names[try_servers][strlen(e->qname)], 0, 1);
        attempt_num_entries = 0;
      }
      ++attempt_num_entries;
    } else {
      // fill up try_server_names for try_primary_named
      try_servers = local_num_entries++;
      ink_strlcpy(try_server_names[try_servers], e->qname, MAXDNAME);
      memset(&try_server_names[try_servers][strlen(e->qname)], 0, 1);
    }

    /* added for SRV support [ebalsa]
       this skips the query section (qdcount)
     */
    unsigned char *here = (unsigned char *)buf->buf + HFIXEDSZ;
    if (e->qtype == T_SRV) {
      for (int ctr = ntohs(h->qdcount); ctr > 0; ctr--) {
        int strlen = dn_skipname(here, eom);
        here += strlen + QFIXEDSZ;
      }
    }
    //
    // Decode each answer
    //
    int answer = false, error = false;

    while (ancount-- > 0 && cp < eom && !error) {
      n = ink_dn_expand((u_char *)h, eom, cp, bp, buflen);
      if (n < 0) {
        ++error;
        break;
      }
      cp += n;
      short int type;
      NS_GET16(type, cp);
      cp += NS_INT16SZ;       // NS_GET16(cls, cp);
      NS_GET32(temp_ttl, cp); // NOTE: this is not a "long" but 32-bits (from nameser_compat.h)
      if ((temp_ttl < buf->ttl) || (buf->ttl == 0))
        buf->ttl = temp_ttl;
      NS_GET16(n, cp);

      //
      // Decode cname
      //
      if (is_addr_query(e->qtype) && type == T_CNAME) {
        if (ap >= &buf->host_aliases[DNS_MAX_ALIASES - 1])
          continue;
        n = ink_dn_expand((u_char *)h, eom, cp, tbuf, sizeof(tbuf));
        if (n < 0) {
          ++error;
          break;
        }
        cp += n;
        *ap++ = (unsigned char *)bp;
        n     = strlen((char *)bp) + 1;
        bp += n;
        buflen -= n;
        n = strlen((char *)tbuf) + 1;
        if (n > buflen) {
          ++error;
          break;
        }
        ink_strlcpy((char *)bp, (char *)tbuf, buflen);
        bp += n;
        buflen -= n;
        Debug("dns", "received cname = %s", tbuf);
        continue;
      }
      if (e->qtype != type) {
        ++error;
        break;
      }
      //
      // Decode names
      //
      if (type == T_PTR) {
        n = ink_dn_expand((u_char *)h, eom, cp, bp, buflen);
        if (n < 0) {
          ++error;
          break;
        }
        cp += n;
        if (!answer) {
          buf->ent.h_name = (char *)bp;
          Debug("dns", "received PTR name = %s", bp);
          n = strlen((char *)bp) + 1;
          bp += n;
          buflen -= n;
        } else if (ap < &buf->host_aliases[DNS_MAX_ALIASES - 1]) {
          *ap++ = bp;
          Debug("dns", "received PTR alias = %s", bp);
          n = strlen((char *)bp) + 1;
          bp += n;
          buflen -= n;
        }
      } else if (type == T_SRV) {
        if (num_srv >= HOST_DB_MAX_ROUND_ROBIN_INFO)
          break;
        cp         = here; /* hack */
        int strlen = dn_skipname(cp, eom);
        cp += strlen;
        const unsigned char *srv_off = cp;
        cp += SRV_FIXEDSZ;
        cp += dn_skipname(cp, eom);
        here     = cp; /* hack */
        SRV *srv = &buf->srv_hosts.hosts[num_srv];
        int r    = ink_ns_name_ntop(srv_off + SRV_SERVER, srv->host, MAXDNAME);
        if (r <= 0) {
          /* FIXME: is this really an error? or just a continue; */
          ++error;
          goto Lerror;
        }
        Debug("dns_srv", "Discovered SRV record [from NS lookup] with cost:%d weight:%d port:%d with host:%s",
              ink_get16(srv_off + SRV_COST), ink_get16(srv_off + SRV_WEIGHT), ink_get16(srv_off + SRV_PORT), srv->host);

        srv->port        = ink_get16(srv_off + SRV_PORT);
        srv->priority    = ink_get16(srv_off + SRV_COST);
        srv->weight      = ink_get16(srv_off + SRV_WEIGHT);
        srv->host_len    = r;
        srv->host[r - 1] = '\0';
        srv->key         = makeHostHash(srv->host);

        if (srv->host[0] != '\0')
          buf->srv_hosts.srv_hosts_length += r;
        else
          continue;
        ++num_srv;
      } else if (is_addr_query(type)) {
        if (answer) {
          if (n != buf->ent.h_length) {
            cp += n;
            continue;
          }
        } else {
          int nn;
          buf->ent.h_length   = n;
          buf->ent.h_addrtype = T_A == type ? AF_INET : AF_INET6;
          buf->ent.h_name     = (char *)bp;
          nn                  = strlen((char *)bp) + 1;
          Debug("dns", "received %s name = %s", QtypeName(type), bp);
          bp += nn;
          buflen -= nn;
        }
        // attempt to use the original buffer (if it is word aligned)
        if (!(((uintptr_t)cp) % sizeof(unsigned int))) {
          *hap++ = cp;
          cp += n;
        } else {
          ip_text_buffer ip_string;
          bp = (unsigned char *)align_pointer_forward(bp, sizeof(int));
          if (bp + n >= buf->hostbuf + DNS_HOSTBUF_SIZE) {
            ++error;
            break;
          }
          memcpy((*hap++ = bp), cp, n);
          Debug("dns", "received %s = %s", QtypeName(type),
                inet_ntop(T_AAAA == type ? AF_INET6 : AF_INET, bp, ip_string, sizeof(ip_string)));
          bp += n;
          cp += n;
        }
      } else
        goto Lerror;
      ++answer;
    }
    if (answer) {
      *ap  = NULL;
      *hap = NULL;
      //
      // If the named didn't send us the name, insert the one
      // the user gave us...
      //
      if (!buf->ent.h_name) {
        Debug("dns", "inserting name = %s", e->qname);
        ink_strlcpy((char *)bp, e->qname, sizeof(buf->hostbuf) - (bp - buf->hostbuf));
        buf->ent.h_name = (char *)bp;
      }
      dns_result(handler, e, buf, retry);
      return server_ok;
    }
  }
Lerror:;
  DNS_INCREMENT_DYN_STAT(dns_lookup_fail_stat);
  dns_result(handler, e, NULL, retry);
  return server_ok;
}

RecRawStatBlock *dns_rsb;

void
ink_dns_init(ModuleVersion v)
{
  static int init_called = 0;

  Debug("dns", "ink_dns_init: called with init_called = %d", init_called);

  ink_release_assert(!checkModuleVersion(v, HOSTDB_MODULE_VERSION));
  if (init_called)
    return;

  init_called = 1;
  // do one time stuff
  // create a stat block for HostDBStats
  dns_rsb = RecAllocateRawStatBlock((int)DNS_Stat_Count);

  //
  // Register statistics callbacks
  //
  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.total_dns_lookups", RECD_INT, RECP_PERSISTENT,
                     (int)dns_total_lookups_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.lookup_avg_time", RECD_INT, RECP_PERSISTENT,
                     (int)dns_response_time_stat, RecRawStatSyncHrTimeAvg);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.success_avg_time", RECD_INT, RECP_NON_PERSISTENT,
                     (int)dns_success_time_stat, RecRawStatSyncHrTimeAvg);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.lookup_successes", RECD_INT, RECP_PERSISTENT,
                     (int)dns_lookup_success_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.fail_avg_time", RECD_INT, RECP_PERSISTENT, (int)dns_fail_time_stat,
                     RecRawStatSyncHrTimeAvg);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.lookup_failures", RECD_INT, RECP_PERSISTENT,
                     (int)dns_lookup_fail_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.retries", RECD_INT, RECP_PERSISTENT, (int)dns_retries_stat,
                     RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.max_retries_exceeded", RECD_INT, RECP_PERSISTENT,
                     (int)dns_max_retries_exceeded_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS, "proxy.process.dns.in_flight", RECD_INT, RECP_NON_PERSISTENT, (int)dns_in_flight_stat,
                     RecRawStatSyncSum);
}

#ifdef TS_HAS_TESTS
struct DNSRegressionContinuation;
typedef int (DNSRegressionContinuation::*DNSRegContHandler)(int, void *);

struct DNSRegressionContinuation : public Continuation {
  int hosts;
  const char **hostnames;
  int type;
  int *status;
  int found;
  int tofind;
  int i;
  RegressionTest *test;

  int
  mainEvent(int event, HostEnt *he)
  {
    (void)event;
    if (event == DNS_EVENT_LOOKUP) {
      if (he) {
        struct in_addr in;
        ++found;
        in.s_addr = *(unsigned int *)he->ent.h_addr_list[0];
        rprintf(test, "host %s [%s] = %s\n", hostnames[i - 1], he->ent.h_name, inet_ntoa(in));
      } else {
        rprintf(test, "host %s not found\n", hostnames[i - 1]);
      }
    }
    if (i < hosts) {
      dnsProcessor.gethostbyname(this, hostnames[i], DNSProcessor::Options().setHostResStyle(HOST_RES_IPV4_ONLY));
      ++i;
      return EVENT_CONT;
    } else {
      if (found == tofind)
        *status = REGRESSION_TEST_PASSED;
      else
        *status = REGRESSION_TEST_FAILED;
      return EVENT_DONE;
    }
  }

  DNSRegressionContinuation(int ahosts, int atofind, const char **ahostnames, RegressionTest *t, int atype, int *astatus)
    : Continuation(new_ProxyMutex()),
      hosts(ahosts),
      hostnames(ahostnames),
      type(atype),
      status(astatus),
      found(0),
      tofind(atofind),
      i(0),
      test(t)
  {
    SET_HANDLER((DNSRegContHandler)&DNSRegressionContinuation::mainEvent);
  }
};

static const char *dns_test_hosts[] = {"www.apple.com", "www.ibm.com", "www.microsoft.com", "www.coke.com"};

REGRESSION_TEST(DNS)(RegressionTest *t, int atype, int *pstatus)
{
  eventProcessor.schedule_in(new DNSRegressionContinuation(4, 4, dns_test_hosts, t, atype, pstatus), HRTIME_SECONDS(1));
}

#endif
