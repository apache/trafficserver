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

#ifdef SPLIT_DNS
#include "I_SplitDNS.h"
#endif

#define SRV_COST    (RRFIXEDSZ+0)
#define SRV_WEIGHT  (RRFIXEDSZ+2)
#define SRV_PORT    (RRFIXEDSZ+4)
#define SRV_SERVER  (RRFIXEDSZ+6)
#define SRV_FIXEDSZ (RRFIXEDSZ+6)


//
// Config
//
int dns_timeout = DEFAULT_DNS_TIMEOUT;
int dns_retries = DEFAULT_DNS_RETRIES;
int dns_search = DEFAULT_DNS_SEARCH;
int dns_failover_number = DEFAULT_FAILOVER_NUMBER;
int dns_failover_period = DEFAULT_FAILOVER_PERIOD;
int dns_failover_try_period = DEFAULT_FAILOVER_TRY_PERIOD;
int dns_max_dns_in_flight = MAX_DNS_IN_FLIGHT;
int dns_validate_qname = 0;
unsigned int dns_sequence_number = 0;
unsigned int dns_handler_initialized = 0;
int dns_ns_rr = 0;
int dns_ns_rr_init_down = 1;
char *dns_ns_list = NULL;

DNSProcessor dnsProcessor;
ClassAllocator<DNSEntry> dnsEntryAllocator("dnsEntryAllocator");
// Users are expected to free these entries in short order!
// We could page align this buffer to enable page flipping for recv...
ClassAllocator<HostEnt> dnsBufAllocator("dnsBufAllocator", 2);


//
// Function Prototypes
//
static bool dns_process(DNSHandler * h, HostEnt * ent, int len);
static DNSEntry *get_dns(DNSHandler * h, u_short id);
// returns true when e is done
static void dns_result(DNSHandler * h, DNSEntry * e, HostEnt * ent, bool retry);
static void write_dns(DNSHandler * h);
static bool write_dns_event(DNSHandler * h, DNSEntry * e);

// "reliable" name to try. need to build up first.
static int try_servers = 0;
static int local_num_entries = 1;
static int attempt_num_entries = 1;
char try_server_names[DEFAULT_NUM_TRY_SERVER][MAXDNAME];


static inline char *
strnchr(char *s, char c, int len)
{
  while (*s && *s != c && len)
    ++s, --len;

  return *s == c ? s : (char *) NULL;
}

static inline uint16
ink_get16(const uint8 *src) {
  uint16 dst;

  NS_GET16(dst, src);
  return dst;
}

//
//  Public functions
//
//  See documentation is header files and Memos
//
inline void
DNSProcessor::free_hostent(HostEnt * ent)
{
  dnsBufAllocator.free(ent);
}

int
DNSProcessor::start(int)
{
  // Initialize the first event thread for DNS.
  dnsProcessor.thread = eventProcessor.eventthread[ET_DNS][0];

  //
  // Read configuration
  //
  IOCORE_EstablishStaticConfigInt32(dns_retries, "proxy.config.dns.retries");
  IOCORE_EstablishStaticConfigInt32(dns_timeout, "proxy.config.dns.lookup_timeout");
  IOCORE_EstablishStaticConfigInt32(dns_search, "proxy.config.dns.search_default_domains");
  IOCORE_EstablishStaticConfigInt32(dns_failover_number, "proxy.config.dns.failover_number");
  IOCORE_EstablishStaticConfigInt32(dns_failover_period, "proxy.config.dns.failover_period");
  IOCORE_EstablishStaticConfigInt32(dns_max_dns_in_flight, "proxy.config.dns.max_dns_in_flight");
  IOCORE_EstablishStaticConfigInt32(dns_validate_qname, "proxy.config.dns.validate_query_name");
  IOCORE_EstablishStaticConfigInt32(dns_ns_rr, "proxy.config.dns.round_robin_nameservers");
  IOCORE_ReadConfigStringAlloc(dns_ns_list, "proxy.config.dns.nameservers");

  dns_failover_try_period = dns_timeout + 1;    // Modify the "default" accordingly

  dns_init();
  open();

  return 0;
}

void
DNSProcessor::open(unsigned int aip, int aport, int aoptions)
{
  DNSHandler *h = NEW(new DNSHandler);

  h->options = aoptions;
  h->mutex = thread->mutex;
  h->m_res = &l_res;
  h->ip = aip;
  h->port = aport;

  if (!dns_handler_initialized)
    dnsProcessor.handler = h;

  SET_CONTINUATION_HANDLER(h, &DNSHandler::startEvent);
  thread->schedule_imm(h, ET_DNS);
}

//
// Initialization
//
void
DNSProcessor::dns_init()
{
  int64 sval, cval = 0;

  DNS_READ_DYN_STAT(dns_sequence_number_stat, sval, cval);

  if (cval > 0) {
    dns_sequence_number = (unsigned int)
      (cval + DNS_SEQUENCE_NUMBER_RESTART_OFFSET);
  } else {                      // select a sequence number at random
    dns_sequence_number = (unsigned int) (ink_get_hrtime() / HRTIME_MSECOND);
  }
  Debug("dns", "initial dns_sequence_number = %d\n", (u_short) dns_sequence_number);
  gethostname(try_server_names[0], 255);
  Debug("dns", "localhost=%s\n", try_server_names[0]);
  Debug("dns", "Round-robin nameservers = %d\n", dns_ns_rr);

  if (dns_ns_rr && dns_ns_list) {
    Debug("dns", "Nameserver list specified \"%s\"\n", dns_ns_list);
    unsigned long nameserver_ip[MAX_NAMED];
    int nameserver_port[MAX_NAMED];
    int i, j;
    char *last, *ndx;
    char *ns_list = xstrdup(dns_ns_list);
    char *ns = (char *) ink_strtok_r(ns_list, " ,;\t\r", &last);

    for (i = 0, j = 0; (i < MAX_NAMED) && ns; i++) {
      Debug("dns", "Nameserver list - parsing \"%s\"\n", ns);
      int err = 0;
      int prt = DOMAIN_SERVICE_PORT;
      if ((ndx = strchr(ns, ':'))) {
        *ndx = '\0';
        // coverity[secure_coding]
        if (sscanf(ndx + 1, "%d%*s", &prt) != 1) {
          Debug("dns", "Unable to parse port number '%s' for nameserver '%s', discarding", ndx + 1, ns);
          Warning("Unable to parse port number '%s' for nameserver '%s', discarding", ndx + 1, ns);
          err = 1;
        }
      }
      nameserver_ip[j] = ink_inet_addr(ns);
      nameserver_port[j] = prt;
      if ((int) nameserver_ip[j] == -1) {
        Debug("dns", "Invalid IP address given for nameserver '%s', discarding", ns);
        Warning("Invalid IP address given for nameserver '%s', discarding", ns);
        err = 1;
      }

      if (!err) {
        Debug("dns", "Adding nameserver %d.%d.%d.%d:%d to nameserver list",
              DOT_SEPARATED(nameserver_ip[j]), nameserver_port[j]);
        ++j;
      } else
        nameserver_ip[j] = 0;

      ns = (char *) ink_strtok_r(NULL, " ,;\t\r", &last);
    }
    xfree(ns_list);

    // Terminate the list for ink_res_init
    nameserver_ip[j] = 0;

    // The default domain (4th param) and search list (5th param) will
    // come from /etc/resolv.conf.
    if (ink_res_init(&l_res, &nameserver_ip[0], &nameserver_port[0], NULL, NULL) < 0)
      Warning("Failed to build DNS res records for the servers (%s).  Using resolv.conf.", dns_ns_list);
  } else {
    if (ink_res_init(&l_res, 0, 0, 0, 0) < 0)
      Warning("Failed to build DNS res records for the servers (%s).  Using resolv.conf.", dns_ns_list);
    dns_ns_rr = 0;
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
ink_dn_expand(const u_char * msg, const u_char * eom, const u_char * comp_dn, u_char * exp_dn, int length)
{
  return::dn_expand((unsigned char *) msg, (unsigned char *) eom, (unsigned char *) comp_dn, (char *) exp_dn, length);
}

DNSProcessor::DNSProcessor()
  : thread(NULL), handler(NULL)
{
  memset(&l_res, 0, sizeof(l_res));
}

void
DNSEntry::init(const char *x, int len, int qtype_arg,
               Continuation * acont, HostEnt ** wait, DNSHandler * adnsH, int dns_lookup_timeout)
{
  (void) adnsH;
  qtype = qtype_arg;
  submit_time = ink_get_hrtime();
  action = acont;
  sem_ent = wait;
  submit_thread = acont->mutex->thread_holding;

#ifdef SPLIT_DNS
  dnsH = SplitDNSConfig::gsplit_dns_enabled && adnsH ? adnsH : dnsProcessor.handler;
#else
  dnsH = dnsProcessor.handler;
#endif // SPLIT_DNS

  dnsH->txn_lookup_timeout = dns_lookup_timeout;

  mutex = dnsH->mutex;
#ifdef DNS_PROXY
  if (!proxy) {
#endif
    if (qtype == T_A || qtype == T_SRV) {
      if (len) {
        len = len > (MAXDNAME - 1) ? (MAXDNAME - 1) : len;
        memcpy(qname, x, len);
        qname_len = len;
        qname[len] = 0;
      } else {
        strncpy(qname, x, MAXDNAME);
        qname[MAXDNAME - 1] = '\0';
        qname_len = strlen(qname);
      }
    } else {                    //T_PTR
      char *p = qname;
      unsigned char *u = (unsigned char *) x;
      if (u[3] > 99)
        *p++ = (u[3] / 100) + '0';
      if (u[3] > 9)
        *p++ = ((u[3] / 10) % 10) + '0';
      *p++ = u[3] % 10 + '0';
      *p++ = '.';
      if (u[2] > 99)
        *p++ = (u[2] / 100) + '0';
      if (u[2] > 9)
        *p++ = ((u[2] / 10) % 10) + '0';
      *p++ = u[2] % 10 + '0';
      *p++ = '.';
      if (u[1] > 99)
        *p++ = (u[1] / 100) + '0';
      if (u[1] > 9)
        *p++ = ((u[1] / 10) % 10) + '0';
      *p++ = u[1] % 10 + '0';
      *p++ = '.';
      if (u[0] > 99)
        *p++ = (u[0] / 100) + '0';
      if (u[0] > 9)
        *p++ = ((u[0] / 10) % 10) + '0';
      *p++ = u[0] % 10 + '0';
      *p++ = '.';
      ink_strncpy(p, "in-addr.arpa", MAXDNAME - (p - qname + 1));
    }
#ifdef DNS_PROXY
  } else {
    if (len) {
      len = len > (MAX_DNS_PROXY_PACKET_LEN - 1) ? (MAX_DNS_PROXY_PACKET_LEN - 1) : len;
    } else {
      len = MAX_DNS_PROXY_PACKET_LEN;
    }
    memcpy(request, x, len);
  }
#endif
  if (sem_ent) {
#if defined(darwin)
    static int qnum = 0;
    char sname[NAME_MAX];
    int retval;
    qnum++;
    snprintf(sname,NAME_MAX,"%s%d","DNSEntry",qnum);
    retval = ink_sem_unlink(sname); // FIXME: remove, semaphore should be properly deleted after usage
    sem = ink_sem_open(sname, O_CREAT | O_EXCL, 0777, 0);
#else /* !darwin */
    ink_sem_init(&sem, 0);
#endif /* !darwin */
  }
  SET_HANDLER((DNSEntryHandler) & DNSEntry::mainEvent);
}

/**
  Open (and close) connections as necessary and also assures that the
  epoll fd struct is properly updated.

*/
void
DNSHandler::open_con(unsigned int aip, int aport, bool failed, int icon)
{
  PollDescriptor *pd = get_PollDescriptor(dnsProcessor.thread);

  Debug("dns", "open_con: opening connection %d.%d.%d.%d:%d", DOT_SEPARATED(aip), aport);

  if (!icon) {
    ip = aip;
    port = aport;
  }

  if (con[icon].fd != NO_FD) {  // Remove old FD from epoll fd
    con[icon].eio.stop();
    con[icon].close();
  }

  if (con[icon].connect(aip, aport, NON_BLOCKING_CONNECT, CONNECT_WITH_UDP, NON_BLOCKING, BIND_RANDOM_PORT) < 0) {
    Debug("dns", "opening connection %d.%d.%d.%d:%d FAILED for %d", DOT_SEPARATED(aip), aport, icon);
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
      Error("iocore_dns", "open_con: Failed to add %d server to epoll list\n", icon);
    } else {
      con[icon].num = icon;
      Debug("dns", "opening connection %d.%d.%d.%d:%d SUCCEEDED for %d", DOT_SEPARATED(aip), aport, icon);
    }
  }
}

/**
  Initial state of the DNSHandler. Can reinitialize the running DNS
  hander to a new nameserver.

*/
int
DNSHandler::startEvent(int event, Event * e)
{
  NOWARN_UNUSED(event);
  //
  // If this is for the default server, get it
  //
  Debug("dns", "DNSHandler::startEvent: on thread%d\n", e->ethread->id);
  if (ip == DEFAULT_DOMAIN_NAME_SERVER) {
    // seems that res_init always sets m_res.nscount to at least 1!
    if (!m_res->nscount)
      Warning("bad '/etc/resolv.conf': no nameservers given");
    struct sockaddr_in *sa = &m_res->nsaddr_list[0];
    ip = sa->sin_addr.s_addr;
    if (!ip)
      ip = ink_inet_addr("127.0.0.1");
    port = ntohs(sa->sin_port);
  }

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
      n_con = 0;
      for (int i = 0; i < max_nscount; i++) {
        struct sockaddr_in *sa = &m_res->nsaddr_list[i];
        ip = sa->sin_addr.s_addr;
        if (ip) {
          port = ntohs(sa->sin_port);
          dnsProcessor.handler->open_con(ip, port, false, n_con);
          ++n_con;
          Debug("dns_pas", "opened connection to %d.%d.%d.%d:%d, n_con = %d", DOT_SEPARATED(ip), port, n_con);
        }
      }
      dns_ns_rr_init_down = 0;
    } else {
      dnsProcessor.handler->open_con(ip, port);
      n_con = 1;
    }
    dnsProcessor.thread->schedule_every(this, DNS_PERIOD);

    return EVENT_CONT;
  } else {
    ink_assert(false);          // I.e. this should never really happen
    return EVENT_DONE;
  }
}

/**
  Initial state of the DSNHandler. Can reinitialize the running DNS
  hander to a new nameserver.
*/
int
DNSHandler::startEvent_sdns(int event, Event * e)
{
  NOWARN_UNUSED(event);
  //
  // If this is for the default server, get it
  //

  //added by YTS Team, yamsat
  Debug("dns", "DNSHandler::startEvent_sdns: on thread%d\n", e->ethread->id);

  if (ip == DEFAULT_DOMAIN_NAME_SERVER) {
    // seems that res_init always sets m_res.nscount to at least 1!
    if (!m_res->nscount)
      Warning("bad '/etc/resolv.conf': no nameservers given");
    struct sockaddr_in *sa = &m_res->nsaddr_list[0];
    ip = sa->sin_addr.s_addr;
    if (!ip)
      ip = ink_inet_addr("127.0.0.1");
    port = ntohs(sa->sin_port);
  }

  SET_HANDLER(&DNSHandler::mainEvent);
  open_con(ip, port, false, n_con);
  ++n_con;                      // TODO should n_con be zeroed?

  e->schedule_every(DNS_PERIOD);
  return EVENT_CONT;
}

static inline int
_ink_res_mkquery(ink_res_state res, char *qname, int qtype, char *buffer)
{
  int r = ink_res_mkquery(res, QUERY, qname, C_IN, qtype,
                          NULL, 0, NULL, (unsigned char *) buffer,
                          MAX_DNS_PACKET_LEN);
  return r;
}

void
DNSHandler::recover()
{
  Warning("connection to DNS server %d.%d.%d.%d restored", DOT_SEPARATED(ip));
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
    struct sockaddr_in *sa;
    sa = &m_res->nsaddr_list[ndx];
    ip = sa->sin_addr.s_addr;
    port = ntohs(sa->sin_port);

    open_con(ip, port, true, ndx);
  }

  char buffer[MAX_DNS_PACKET_LEN];
  Debug("dns", "trying to resolve '%s' from DNS connection, ndx %d", try_server_names[try_servers], ndx);
  int r = _ink_res_mkquery(m_res, try_server_names[try_servers], T_A, buffer);
  try_servers = (try_servers + 1) % SIZE(try_server_names);
  ink_assert(r >= 0);
  if (r >= 0) {                 // looking for a bounce
    int res = socketManager.send(con[ndx].fd, buffer, r, 0);
    Debug("dns", "ping result = %d", res);
  }
}

void
DNSHandler::try_primary_named(bool reopen)
{
  ink_hrtime t = ink_get_hrtime();
  if (reopen && ((t - last_primary_reopen) > DNS_PRIMARY_REOPEN_PERIOD)) {
    Debug("dns", "try_primary_named: reopening primary DNS connection");
    last_primary_reopen = t;
    open_con(ip, port, true, 0);
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
      try_servers = (try_servers + 1) % SIZE(try_server_names);
    ink_assert(r >= 0);
    if (r >= 0) {               // looking for a bounce
      int res = socketManager.send(con[0].fd, buffer, r, 0);
      Debug("dns", "ping result = %d", res);
    }
  }
}


void
DNSHandler::switch_named(int ndx)
{
  for (DNSEntry * e = entries.head; e; e = (DNSEntry *) e->link.next) {
    e->written_flag = 0;
    if (e->retries < dns_retries)
      ++(e->retries);           // give them another chance
  }
  in_flight = 0;
  received_one(ndx);            // reset failover counters
}

/** Fail over to another name server. */
void
DNSHandler::failover()
{
  Debug("dns", "failover: initiating failover attempt, current name_server=%d", name_server);
  // no hope, if we have only one server
  if (m_res->nscount > 1) {
    int max_nscount = m_res->nscount;

    if (max_nscount > MAX_NAMED)
      max_nscount = MAX_NAMED;
    unsigned int old_ip = m_res->nsaddr_list[name_server].sin_addr.s_addr;
    name_server = (name_server + 1) % max_nscount;
    Debug("dns", "failover: failing over to name_server=%d", name_server);

    struct sockaddr_in *sa = &m_res->nsaddr_list[name_server];

    Warning("failover: connection to DNS server %d.%d.%d.%d lost, move to %d.%d.%d.%d",
            DOT_SEPARATED(old_ip), DOT_SEPARATED(sa->sin_addr.s_addr));

    unsigned int tip = sa->sin_addr.s_addr;

    if (!tip)
      tip = ink_inet_addr("127.0.0.1");
    open_con(tip, ntohs(sa->sin_port), true, name_server);
    if (n_con <= name_server)
      n_con = name_server + 1;
    switch_named(name_server);
  } else
    Warning("failover: connection to DNS server %d.%d.%d.%d lost, retrying", DOT_SEPARATED(ip));
}

/** Mark one of the nameservers as down. */
void
DNSHandler::rr_failure(int ndx)
{
  // no hope, if we have only one server
  if (!ns_down[ndx]) {
    // mark this nameserver as down
    Debug("dns", "rr_failure: Marking nameserver %d as down", ndx);
    ns_down[ndx] = 1;

    struct sockaddr_in *sa = &m_res->nsaddr_list[ndx];
    unsigned int tip = sa->sin_addr.s_addr;
    Warning("connection to DNS server %d.%d.%d.%d lost, marking as down", DOT_SEPARATED(tip));
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
    for (DNSEntry * e = entries.head; e; e = (DNSEntry *) e->link.next) {
      e->written_flag = 0;
      if (e->retries < dns_retries)
        ++(e->retries);         // give them another chance
      --in_flight;
      DNS_DECREMENT_DYN_STAT(dns_in_flight_stat);
    }
  } else {
    // move outstanding requests that were sent to this nameserver to another
    for (DNSEntry * e = entries.head; e; e = (DNSEntry *) e->link.next) {
      if (e->which_ns == ndx) {
        e->written_flag = 0;
        if (e->retries < dns_retries)
          ++(e->retries);       // give them another chance
        --in_flight;
        DNS_DECREMENT_DYN_STAT(dns_in_flight_stat);
      }
    }
  }
}

static bool
good_rcode(char *buf)
{
  HEADER *h = (HEADER *) buf;
  switch (h->rcode) {
  default:
    return false;
  case NOERROR:
  case NXDOMAIN:
    return true;
  }
}


//changed by YTS Team, yamsat
void
DNSHandler::recv_dns(int event, Event * e)
{
  NOWARN_UNUSED(event);
  NetHandler *nh = get_NetHandler(e->ethread);  //added by YTS Team, yamsat
  DNSConnection *dnsc = NULL;   //added by YTS Team, yamsat

  while ((dnsc = (DNSConnection *) nh->dnsqueue.dequeue())) {
    while (1) {
      struct sockaddr_in sa_from;
      socklen_t sa_length = sizeof(sa_from);
      if (!hostent_cache)
        hostent_cache = dnsBufAllocator.alloc();
      HostEnt *buf = hostent_cache;
      int res =
        socketManager.recvfrom(dnsc->fd, buf->buf, MAX_DNS_PACKET_LEN, 0, (struct sockaddr *) &sa_from, &sa_length);
      // verify that this response came from the correct server
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
      if (dnsc->sa.sin_addr.s_addr != sa_from.sin_addr.s_addr) {
        Warning("received DNS response from unexpected named %d.%d.%d.%d", DOT_SEPARATED(sa_from.sin_addr.s_addr));
        continue;
      }
      hostent_cache = 0;
      buf->ref_count = 1;
      buf->packet_size = res;
      Debug("dns", "received packet size = %d", res);
      if (dns_ns_rr) {
        Debug("dns", "round-robin: nameserver %d DNS response code = %d", dnsc->num, ((HEADER *) buf->buf)->rcode);
        if (good_rcode(buf->buf)) {
          received_one(dnsc->num);
          if (ns_down[dnsc->num]) {
            struct sockaddr_in *sa = &m_res->nsaddr_list[dnsc->num];
            Warning("connection to DNS server %d.%d.%d.%d restored", DOT_SEPARATED(sa->sin_addr.s_addr));
            ns_down[dnsc->num] = 0;
          }
        }
      } else {
        if (!dnsc->num) {
          Debug("dns", "primary DNS response code = %d", ((HEADER *) buf->buf)->rcode);
          if (good_rcode(buf->buf)) {
            if (name_server)
              recover();
            else
              received_one(name_server);
          }
        }
      }
      if (dns_process(this, buf, res)) {
        if (dnsc->num == name_server)
          received_one(name_server);
      }
    }                           /* end of while(1) */
  }
}

/** Main event for the DNSHandler. Attempt to read from and write to named. */
int
DNSHandler::mainEvent(int event, Event * e)
{
  recv_dns(event, e);
  if (dns_ns_rr) {
    ink_hrtime t = ink_get_hrtime();
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
    } else if (name_server)     // not on the primary named
      try_primary_named(true);
  }

  if (entries.head)
    write_dns(this);

  return EVENT_CONT;
}

/** Find a DNSEntry by id. */
inline static DNSEntry *
get_dns(DNSHandler * h, u_short id)
{
  for (DNSEntry * e = h->entries.head; e; e = (DNSEntry *) e->link.next) {
    if (e->once_written_flag)
      for (int j = 0; j < MAX_DNS_RETRIES; j++)
        if (e->id[j] == id)
          return e;
        else if (e->id[j] < 0)
          goto Lnext;
  Lnext:;
  }
  return NULL;
}

inline static DNSEntry *
get_entry(DNSHandler * h, char *qname, int qtype)
{
  for (DNSEntry * e = h->entries.head; e; e = (DNSEntry *) e->link.next) {
    if (e->qtype == qtype) {
      if (qtype == T_A) {
        if (!strcmp(qname, e->qname))
          return e;
      } else if (*(unsigned int *) qname == *(unsigned int *) e->qname)
        return e;
    }
  }
  return NULL;
}

/** Write up to dns_max_dns_in_flight entries. */
static void
write_dns(DNSHandler * h)
{
  ProxyMutex *mutex = h->mutex;
  DNS_INCREMENT_DYN_STAT(dns_total_lookups_stat);
  int max_nscount = h->m_res->nscount;
  if (max_nscount > MAX_NAMED)
    max_nscount = MAX_NAMED;

  if (h->in_write_dns)
    return;
  h->in_write_dns = true;
  Debug("dns", "in_flight: %d, dns_max_dns_in_flight: %d", h->in_flight, dns_max_dns_in_flight);
  if (h->in_flight < dns_max_dns_in_flight) {
    DNSEntry *e = h->entries.head;
    while (e) {
      DNSEntry *n = (DNSEntry *) e->link.next;
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

/**
  Construct and Write the request for a single entry (using send(3N)).

  @return true = keep going, false = give up for now.

*/
static bool
write_dns_event(DNSHandler * h, DNSEntry * e)
{
  ProxyMutex *mutex = h->mutex;
  char buffer[MAX_DNS_PACKET_LEN];
  int r = 0;

#ifdef DNS_PROXY
  if (!e->proxy) {
#endif
    if ((r = _ink_res_mkquery(h->m_res, e->qname, e->qtype, buffer)) <= 0) {
      Debug("dns", "cannot build query: %s", e->qname);
      dns_result(h, e, NULL, false);
      return true;
    }
#ifdef DNS_PROXY
  }
#endif

  int id = dns_retries - e->retries;
  if (id >= MAX_DNS_RETRIES)
    id = MAX_DNS_RETRIES - 1;   // limit id history

  ++dns_sequence_number;

  DNS_SET_DYN_COUNT(dns_sequence_number_stat, dns_sequence_number);

#ifdef DNS_PROXY
  if (!e->proxy) {
#endif
    u_short i = (u_short) dns_sequence_number;
    ((HEADER *) (buffer))->id = htons(i);
    e->id[dns_retries - e->retries] = i;
#ifdef DNS_PROXY
  } else {
    e->id[dns_retries - e->retries] = ntohs(((HEADER *) (e->request))->id);
    memcpy(buffer, e->request, MAX_DNS_PROXY_PACKET_LEN);
    r = MAX_DNS_PROXY_PACKET_LEN;
  }
#endif
  Debug("dns", "send query for %s to fd %d", e->qname, h->con[h->name_server].fd);

  int s = socketManager.send(h->con[h->name_server].fd, buffer, r, 0);
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

  e->written_flag = true;
  e->which_ns = h->name_server;
  e->once_written_flag = true;
  ++h->in_flight;
  DNS_INCREMENT_DYN_STAT(dns_in_flight_stat);

  e->send_time = ink_get_hrtime();

  if (e->timeout)
    e->timeout->cancel();

  if (h->txn_lookup_timeout) {
    e->timeout = h->mutex->thread_holding->schedule_in(e, HRTIME_MSECONDS(h->txn_lookup_timeout));      //this is in msec
  } else {
    e->timeout = h->mutex->thread_holding->schedule_in(e, HRTIME_SECONDS(dns_timeout));
  }

  Debug("dns", "sent qname = %s, id = %u, nameserver = %d", e->qname, e->id[dns_retries - e->retries], h->name_server);
  h->sent_one();
  return true;
}


int
DNSEntry::delayEvent(int event, Event * e)
{
  (void) event;
  if (dnsProcessor.handler) {
    SET_HANDLER((DNSEntryHandler) & DNSEntry::mainEvent);
    return handleEvent(EVENT_IMMEDIATE, e);
  }
  e->schedule_in(DNS_DELAY_PERIOD);
  return EVENT_CONT;
}

/** Handle timeout events. */
int
DNSEntry::mainEvent(int event, Event * e)
{
  switch (event) {
  default:
    ink_assert(!"bad case");
    return EVENT_DONE;
  case EVENT_IMMEDIATE:{
      if (!dnsH)
        dnsH = dnsProcessor.handler;
      if (!dnsH) {
        Debug("dns", "handler not found, retrying...");
        SET_HANDLER((DNSEntryHandler) & DNSEntry::delayEvent);
        return handleEvent(event, e);
      }
#ifdef DNS_PROXY
      if (!proxy) {
#endif
        //if (dns_search && !strnchr(qname,'.',MAXDNAME)){
        if (dns_search)
          domains = dnsH->m_res->dnsrch;
        if (domains && !strnchr(qname, '.', MAXDNAME)) {
          qname[qname_len] = '.';
          ink_strncpy(qname + qname_len + 1, *domains, MAXDNAME - (qname_len + 1));
          ++domains;
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
#ifdef DNS_PROXY
      } else {
        dnsH->entries.enqueue(this);
        write_dns(dnsH);
      }
#endif
      return EVENT_DONE;
    }
  case EVENT_INTERVAL:
    Debug("dns", "timeout for query %s", qname);
    if (dnsH->txn_lookup_timeout) {
      timeout = NULL;
      dns_result(dnsH, this, result_ent, false);        //do not retry -- we are over TXN timeout on DNS alone!
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
DNSProcessor::getby(const char *x, int len, int type,
                    Continuation * cont, HostEnt ** wait, DNSHandler * adnsH, bool proxy, bool proxy_cache, int timeout)
{
  Debug("dns", "received query %s type = %d, timeout = %d", x, type, timeout);
  if (type == T_SRV) {
    Debug("dns_srv", "DNSProcessor::getby attempting an SRV lookup for %s, timeout = %d", x, timeout);
  }
  DNSEntry *e = dnsEntryAllocator.alloc();
  e->retries = dns_retries;
  if (proxy)
    e->proxy = true;
  if (proxy_cache)
    e->proxy_cache = true;
  e->init(x, len, type, cont, wait, adnsH, timeout);
  MUTEX_TRY_LOCK(lock, e->mutex, this_ethread());
  if (!lock)
    dnsProcessor.thread->schedule_imm(e);
  else
    e->handleEvent(EVENT_IMMEDIATE, 0);
  if (wait) {
#if defined(darwin)
    ink_sem_wait(e->sem);
#else
    ink_sem_wait(&e->sem);
#endif
  }
  return wait ? ACTION_RESULT_DONE : &e->action;
}

/**
  We have a result for an entry, return it to the user or retry if it
  is a retry-able and we have retries left.
*/
static void
dns_result(DNSHandler * h, DNSEntry * e, HostEnt * ent, bool retry)
{
  ProxyMutex *mutex = h->mutex;
  bool cancelled = (e->action.cancelled ? true : false);

  if (!ent && !cancelled) {
    // try to retry operation
    if (retry && e->retries && !e->proxy && !e->proxy_cache) {
      Debug("dns", "doing retry for %s", e->qname);

      DNS_INCREMENT_DYN_STAT(dns_retries_stat);

      --(e->retries);
      write_dns(h);
      return;
    } else if (e->domains && *e->domains && !e->proxy && !e->proxy_cache) {
      do {
        Debug("dns", "domain extending %s", e->qname);
        //int l = _strlen(e->qname);
        char *dot = strchr(e->qname, '.');
        if (dot) {
          if (e->qname_len + strlen(*e->domains) + 2 > MAXDNAME) {
            Debug("dns", "domain too large %s + %s", e->qname, *e->domains);
            goto LnextDomain;
          }
          if (e->qname[e->qname_len - 1] != '.') {
            e->qname[e->qname_len] = '.';
            ink_strncpy(e->qname + e->qname_len + 1, *e->domains, MAXDNAME - (e->qname_len + 1));
          } else {
            ink_strncpy(e->qname + e->qname_len, *e->domains, MAXDNAME - e->qname_len);
          }
        } else {
          if (e->qname_len + strlen(*e->domains) + 2 > MAXDNAME) {
            Debug("dns", "domain too large %s + %s", e->qname, *e->domains);
            goto LnextDomain;
          }
          e->qname[e->qname_len] = '.';
          ink_strncpy(e->qname + e->qname_len + 1, *e->domains, MAXDNAME - (e->qname_len + 1));
        }
        ++(e->domains);
        e->retries = dns_retries;
        Debug("dns", "new name = %s retries = %d", e->qname, e->retries);
        write_dns(h);
        return;
      LnextDomain:
        ++(e->domains);
      } while (*e->domains);
    } else if (!e->proxy && !e->proxy_cache) {
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
      DNS_SUM_DYN_STAT(dns_fail_time_stat, ink_get_hrtime() - e->submit_time);
    } else {
      DNS_SUM_DYN_STAT(dns_success_time_stat, ink_get_hrtime() - e->submit_time);
    }
  }
  h->entries.remove(e);

  if (e->qtype == T_A) {
    unsigned int tip = ent != NULL ? *(unsigned int *) ent->ent.h_addr_list[0] : 0;
    Debug("dns", "%s result for %s = %d.%d.%d.%d retry %d",
          ent ? "SUCCESS" : "FAIL", e->qname, DOT_SEPARATED(tip), retry);
  } else {
    Debug("dns", "%s result for %s = %s retry %d",
          ent ? "SUCCESS" : "FAIL", e->qname, (ent != NULL ? ent->ent.h_name : "<not found>"), retry);
  }

  if (ent) {
    DNS_INCREMENT_DYN_STAT(dns_lookup_success_stat);
  } else {
    DNS_INCREMENT_DYN_STAT(dns_lookup_fail_stat);
  }

  DNSEntry *dup = NULL;

  while ((dup = e->dups.dequeue())) {
    if (dup->post(h, ent, false)) {
      e->dups.enqueue(dup);
      goto Lretry;
    }
  }
  if (!e->post(h, ent, true))
    return;
Lretry:
  e->result_ent = ent;
  e->retries = 0;
  if (e->timeout)
    e->timeout->cancel();
  e->timeout = h->mutex->thread_holding->schedule_in(e, DNS_PERIOD);
}

int
DNSEntry::post(DNSHandler * h, HostEnt * ent, bool freeable)
{
  NOWARN_UNUSED(freeable);
  if (timeout) {
    timeout->cancel(this);
    timeout = NULL;
  }
  if (sem_ent) {
    // If this call was synchronous, post to the semaphore
    *sem_ent = ent;
#if defined(darwin)
    ink_sem_post(sem);
#else
    ink_sem_post(&sem);
#endif
  } else {
    result_ent = ent;
    if (h->mutex->thread_holding == submit_thread) {
      MUTEX_TRY_LOCK(lock, action.mutex, h->mutex->thread_holding);
      if (!lock) {
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
  action.mutex = NULL;
  mutex = NULL;
  dnsEntryAllocator.free(this);
  return 0;
}

int
DNSEntry::postEvent(int event, Event * e)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(e);
  if (!action.cancelled) {
    Debug("dns", "called back continuation for %s", qname);
    action.continuation->handleEvent(DNS_EVENT_LOOKUP, result_ent);
  }
  if (result_ent)
    if (ink_atomic_increment(&result_ent->ref_count, -1) == 1)
      dnsProcessor.free_hostent(result_ent);
  action.mutex = NULL;
  mutex = NULL;
  dnsEntryAllocator.free(this);
  return EVENT_DONE;
}

/** Decode the reply from "named". */
static bool
dns_process(DNSHandler * handler, HostEnt * buf, int len)
{
  ProxyMutex *mutex = handler->mutex;
  HEADER *h = (HEADER *) (buf->buf);
  DNSEntry *e = get_dns(handler, (u_short) ntohs(h->id));
  bool retry = false;
  bool server_ok = true;
  uint32 temp_ttl = 0;

  //
  // Do we have an entry for this id?
  //
  if (!e || !e->written_flag) {
    Debug("dns", "unknown DNS id = %u", (u_short) ntohs(h->id));
    if (!handler->hostent_cache)
      handler->hostent_cache = buf;
    else
      dnsBufAllocator.free(buf);
    return false;               // cannot count this as a success
  }
  //
  // It is no longer in flight
  //
  e->written_flag = false;
  --(handler->in_flight);
  DNS_DECREMENT_DYN_STAT(dns_in_flight_stat);

  if (e->proxy) {
    Debug("dns", "using proxy");
    dns_result(handler, e, buf, retry);
    return server_ok;
  }

  DNS_SUM_DYN_STAT(dns_response_time_stat, ink_get_hrtime() - e->send_time);

  if (h->rcode != NOERROR || !h->ancount) {
    Debug("dns", "received rcode = %d", h->rcode);
    switch (h->rcode) {
    default:
      Warning("Unknown DNS error %d for [%s]", h->rcode, e->qname);
      retry = true;
      server_ok = false;        // could be server problems
      goto Lerror;
    case SERVFAIL:             // recoverable error
      retry = true;
    case FORMERR:              // unrecoverable errors
    case REFUSED:
    case NOTIMP:
      Debug("dns", "DNS error %d for [%s]", h->rcode, e->qname);
      server_ok = false;        // could be server problems
      goto Lerror;
    case NOERROR:
    case NXDOMAIN:
    case 6:                    // YXDOMAIN
    case 7:                    // YXRRSET
    case 8:                    // NOTAUTH
    case 9:                    // NOTAUTH
    case 10:                   // NOTZONE
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

    int ancount = ntohs(h->ancount);
    unsigned char *bp = buf->hostbuf;
    int buflen = sizeof(buf->hostbuf);
    u_char *cp = ((u_char *) h) + HFIXEDSZ;
    u_char *eom = (u_char *) h + len;
    int n;
    unsigned char *srv[50];
    int num_srv = 0;
    int rname_len = -1;

    //
    // Expand name
    //
    if ((n = ink_dn_expand((u_char *) h, eom, cp, bp, buflen)) < 0)
      goto Lerror;

    // Should we validate the query name?
    if (dns_validate_qname) {
      int qlen = e->qname_len;
      int rlen = strlen((char *)bp);

      rname_len = rlen; // Save for later use
      if ((qlen > 0) && ('.' == e->qname[qlen-1]))
        --qlen;
      if ((rlen > 0) && ('.' == bp[rlen-1]))
        --rlen;
      // TODO: At some point, we might want to care about the case here, and use an algorithm
      // to randomly pick upper case characters in the query, and validate the response with
      // case sensitivity.
      if ((qlen != rlen) || (strncasecmp(e->qname, (const char*)bp, qlen) != 0)) {
        // Bad mojo, forged?
        Warning("received DNS response with query name of %s, but response query name is %s", e->qname, bp);
        goto Lerror;
      } else {
        Debug("dns", "query name validated properly for %s", e->qname);
      }
    }

    cp += n + QFIXEDSZ;
    if (e->qtype == T_A) {
      if (-1 == rname_len)
        n = strlen((char *)bp) + 1;
      else
        n = rname_len + 1;
      buf->ent.h_name = (char *) bp;
      bp += n;
      buflen -= n;
    }
    //
    // Configure HostEnt data structure
    //
    u_char **ap = buf->host_aliases;
    buf->ent.h_aliases = (char **) buf->host_aliases;
    u_char **hap = (u_char **) buf->h_addr_ptrs;
    *hap = NULL;
    buf->ent.h_addr_list = (char **) buf->h_addr_ptrs;

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
        try_servers = (try_servers + 1) % SIZE(try_server_names);
        strncpy(try_server_names[try_servers], e->qname, strlen(e->qname));
        memset(&try_server_names[try_servers][strlen(e->qname)], 0, 1);
        attempt_num_entries = 0;
      }
      ++attempt_num_entries;
    } else {
      // fill up try_server_names for try_primary_named
      try_servers = local_num_entries++;
      strncpy(try_server_names[try_servers], e->qname, strlen(e->qname));
      memset(&try_server_names[try_servers][strlen(e->qname)], 0, 1);
    }

    /* added for SRV support [ebalsa]
       this skips the query section (qdcount)
     */
    unsigned char *here = (unsigned char *) buf->buf + HFIXEDSZ;
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
      n = ink_dn_expand((u_char *) h, eom, cp, bp, buflen);
      if (n < 0) {
        ++error;
        break;
      }
      cp += n;
      short int type, cls;
      GETSHORT(type, cp);
      GETSHORT(cls, cp); // NOTE: Don't eliminate this, it'll break badly.
      GETLONG(temp_ttl, cp);
      if ((temp_ttl < buf->ttl) || (buf->ttl == 0))
        buf->ttl = temp_ttl;
      GETSHORT(n, cp);

      //
      // Decode cname
      //
      if (e->qtype == T_A && type == T_CNAME) {
        if (ap >= &buf->host_aliases[DNS_MAX_ALIASES - 1])
          continue;
        n = ink_dn_expand((u_char *) h, eom, cp, tbuf, sizeof(tbuf));
        if (n < 0) {
          ++error;
          break;
        }
        cp += n;
        *ap++ = (unsigned char *) bp;
        n = strlen((char *) bp) + 1;
        bp += n;
        buflen -= n;
        n = strlen((char *) tbuf) + 1;
        if (n > buflen) {
          ++error;
          break;
        }
        ink_strncpy((char *) bp, (char *) tbuf, buflen);
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
        n = ink_dn_expand((u_char *) h, eom, cp, bp, buflen);
        if (n < 0) {
          ++error;
          break;
        }
        cp += n;
        if (!answer) {
          buf->ent.h_name = (char *) bp;
          Debug("dns", "received PTR name = %s", bp);
          n = strlen((char *) bp) + 1;
          bp += n;
          buflen -= n;
        } else if (ap < &buf->host_aliases[DNS_MAX_ALIASES - 1]) {
          *ap++ = bp;
          Debug("dns", "received PTR alias = %s", bp);
          n = strlen((char *) bp) + 1;
          bp += n;
          buflen -= n;
        }
      } else if (type == T_SRV) {
        cp = here;              /* hack */
        int strlen = dn_skipname(cp, eom);
        cp += strlen;
        srv[num_srv] = cp;
        cp += SRV_FIXEDSZ;
        cp += dn_skipname(cp, eom);
        here = cp;              /* hack */
        char srvname[MAXDNAME];
        int r = ink_ns_name_ntop(srv[num_srv] + SRV_SERVER, srvname, MAXDNAME);
        if (r <= 0) {
          /* FIXME: is this really an error? or just a continue; */
          ++error;
          goto Lerror;
        }
        Debug("dns_srv", "Discovered SRV record [from NS lookup] with cost:%d weight:%d port:%d with host:%s",
              ink_get16(srv[num_srv] + SRV_COST),
              ink_get16(srv[num_srv] + SRV_WEIGHT), ink_get16(srv[num_srv] + SRV_PORT), srvname);

        SRV *s = SRVAllocator.alloc();
        s->setPort(ink_get16(srv[num_srv] + SRV_PORT));
        s->setPriority(ink_get16(srv[num_srv] + SRV_COST));
        s->setWeight(ink_get16(srv[num_srv] + SRV_WEIGHT));
        s->setHost(srvname);

        buf->srv_hosts.insert(s);
        ++num_srv;
      } else if (type == T_A) {
        if (answer) {
          if (n != buf->ent.h_length) {
            cp += n;
            continue;
          }
        } else {
          int nn;
          buf->ent.h_length = n;
          buf->ent.h_addrtype = C_IN;
          buf->ent.h_name = (char *) bp;
          nn = strlen((char *) bp) + 1;
          Debug("dns", "received A name = %s", bp);
          bp += nn;
          buflen -= nn;
        }
        // attempt to use the original buffer (if it is word aligned)
        // FIXME: is this alignment check correct?
        if (!(((unsigned long) cp) % sizeof(unsigned int))) {
          *hap++ = cp;
          cp += n;
        } else {
          bp = (unsigned char *) align_pointer_forward(bp, sizeof(int));
          if (bp + n >= buf->hostbuf + DNS_HOSTBUF_SIZE) {
            ++error;
            break;
          }
          memcpy((*hap++ = bp), cp, n);
          Debug("dns", "received A = %d.%d.%d.%d", DOT_SEPARATED(*(unsigned int *) bp));
          bp += n;
          cp += n;
        }
      } else
        goto Lerror;
      ++answer;
    }
    if (answer) {
      *ap = NULL;
      *hap = NULL;
      //
      // If the named didn't send us the name, insert the one
      // the user gave us...
      //
      if (!buf->ent.h_name) {
        Debug("dns", "inserting name = %s", e->qname);
        ink_strncpy((char *) bp, e->qname, sizeof(buf->hostbuf) - (bp - buf->hostbuf));
        buf->ent.h_name = (char *) bp;
      }
      dns_result(handler, e, buf, retry);
      return server_ok;
    }
  }
Lerror:;
  DNS_INCREMENT_DYN_STAT(dns_lookup_fail_stat);
  if (!handler->hostent_cache)
    handler->hostent_cache = buf;
  else
    dnsBufAllocator.free(buf);
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
  dns_rsb = RecAllocateRawStatBlock((int) DNS_Stat_Count);

  //
  // Register statistics callbacks
  //
  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.total_dns_lookups",
                     RECD_INT, RECP_NULL, (int) dns_total_lookups_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.lookup_avg_time",
                     RECD_INT, RECP_NULL, (int) dns_response_time_stat, RecRawStatSyncHrTimeAvg);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.success_avg_time",
                     RECD_INT, RECP_NON_PERSISTENT, (int) dns_success_time_stat, RecRawStatSyncHrTimeAvg);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.lookup_successes",
                     RECD_INT, RECP_NULL, (int) dns_lookup_success_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.fail_avg_time",
                     RECD_INT, RECP_NULL, (int) dns_fail_time_stat, RecRawStatSyncHrTimeAvg);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.lookup_failures",
                     RECD_INT, RECP_NULL, (int) dns_lookup_fail_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.retries", RECD_INT, RECP_NULL, (int) dns_retries_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.max_retries_exceeded",
                     RECD_INT, RECP_NULL, (int) dns_max_retries_exceeded_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.in_flight",
                     RECD_INT, RECP_NON_PERSISTENT, (int) dns_in_flight_stat, RecRawStatSyncSum);

  RecRegisterRawStat(dns_rsb, RECT_PROCESS,
                     "proxy.process.dns.sequence_number",
                     RECD_INT, RECP_NULL, (int) dns_sequence_number_stat, RecRawStatSyncCount);
}



struct DNSRegressionContinuation;
typedef int (DNSRegressionContinuation::*DNSRegContHandler) (int, void *);

struct DNSRegressionContinuation: public Continuation
{
  int hosts;
  const char **hostnames;
  int type;
  int *status;
  int found;
  int tofind;
  int i;
  RegressionTest *test;

  int mainEvent(int event, HostEnt * he)
  {
    (void) event;
    if (event == DNS_EVENT_LOOKUP) {
      if (he)
        ++found;
      if (he)
      {
        struct in_addr in;
          in.s_addr = *(unsigned int *) he->ent.h_addr_list[0];
          rprintf(test, "host %s [%s] = %s\n", hostnames[i - 1], he->ent.h_name, inet_ntoa(in));
      } else
          rprintf(test, "host %s not found\n", hostnames[i - 1]);
    }
    if (i < hosts) {
      dnsProcessor.gethostbyname(this, hostnames[i]);
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

  DNSRegressionContinuation(int ahosts, int atofind, const char **ahostnames, RegressionTest * t, int atype, int *astatus)
:  Continuation(new_ProxyMutex()), hosts(ahosts), hostnames(ahostnames), type(atype),
    status(astatus), found(0), tofind(atofind), i(0), test(t) {
    SET_HANDLER((DNSRegContHandler) & DNSRegressionContinuation::mainEvent);
  }
};

static const char *dns_test_hosts[] = {
  "www.apple.com",
  "www.ibm.com",
  "www.microsoft.com",
  "www.coke.com"
};

REGRESSION_TEST(DNS) (RegressionTest * t, int atype, int *pstatus) {
  eventProcessor.schedule_in(NEW(new DNSRegressionContinuation(4, 4, dns_test_hosts, t, atype, pstatus)),
                             HRTIME_SECONDS(1));
}
