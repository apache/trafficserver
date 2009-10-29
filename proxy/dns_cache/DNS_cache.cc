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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "DNS_cache.h"

ClassAllocator<DNS_Cache> DNS_cache_Allocator("DNS_cache_Allocator");

int NoRecurse = 0;
int dns_fd = 0;


void
DNS_Cache::init(char *pkt_buf, int pkt_size, struct sockaddr_in *saddr_in, int saddr_in_length)
{
  mutex = new_ProxyMutex();
  buflen = MAX_DNS_PROXY_PACKET_LEN;
  msglen = pkt_size;
  if ((request = (unsigned char *) xmalloc(msglen)) != NULL) {
    bcopy((const char *) pkt_buf, (char *) request, msglen);
    bcopy((const char *) saddr_in, (char *) &sa_from, saddr_in_length);
    SET_HANDLER(&DNS_Cache::mainEvent);
    eventProcessor.schedule_imm(this);
  }
}

int
DNS_Cache::mainEvent(int event, void *data)
{
  switch (event) {

  case EVENT_IMMEDIATE:
    Debug("dns_cache", "received a new dns query");
    DNS_CACHE_INCREMENT_DYN_STAT(dns_proxy_requests_received_stat);
    process_dns_query();
    break;

  case EVENT_HOST_DB_LOOKUP:
    pending_action = NULL;
    process_hostdb_info((HostDBInfo *) data);
    send_dns_response();
    free();
    DNS_cache_Allocator.free(this);
    break;

  default:
    Fatal("dns_cache: unexpected event %d", event);
  }

  return EVENT_CONT;
}

int
DNS_Cache::process_dns_query()
{
  register HEADER *hp = (HEADER *) request;
  unsigned char *cp, *eom;

  Debug("dns_cache", "qr=%d, opcode=%d, aa=%d, tc=%d, rd=%d, ra=%d, rcode=%d",
        hp->qr, hp->opcode, hp->aa, hp->tc, hp->rd, hp->ra, hp->rcode);

  //
  // sanity check for qdcount equals 1
  //
  if (hp->qdcount != htons(1)) {
    Fatal("Received DNS request contains %d question.", ntohs(hp->qdcount));
  }
  //
  // we are dealing with dns quest, so these bits
  // are set to 0.
  //
  hp->aa = hp->ra = 0;

  cp = request + HFIXEDSZ;
  eom = request + msglen;
  buflen -= HFIXEDSZ;

  switch (hp->opcode) {

  case QUERY:
    req_query(hp, &cp, eom);
    break;

  default:
    Error("Opcode is not of type Query!");
    Debug("dns_cache", "dns_cache: Opcode %d not implemented", hp->opcode);
    hp->rcode = NOTIMP;
    send_dns_response();
  }
  return 1;
}

int
DNS_Cache::send_dns_response()
{
  HEADER *tmp_hp = (HEADER *) msg;

  Debug("dns_cache", "reply back to = %d.%d.%d.%d, port=%d\n, with rcode=%d",
        DOT_SEPARATED(*(unsigned int *) (&(sa_from.sin_addr.s_addr))), sa_from.sin_port, tmp_hp->rcode);

  ink_assert(msg);
  ink_assert(&sa_from);
  int n = sendto(dns_fd, (char *) msg, msglen, 0, (struct sockaddr *) &sa_from,
                 sizeof(struct sockaddr_in));

  if (n < 0) {
#ifdef DEBUG
    if (errno != ECONNREFUSED) {
      Warning("DNS sendto return errno %d\n", errno);
    }
#endif
  }

  Debug("dns_cache", "sent %d bytes response back", n);

  return 1;
}

int
DNS_Cache::req_query(HEADER * hp, unsigned char **cpp, unsigned char *eom)
{
  int n;
  unsigned char *tmp_cpp;
  short type;
  char tmp_dname[MAXDNAME];

  n = dn_expand(request, eom, *cpp, tmp_dname, MAXDNAME);

  dname = (char *) xmalloc(n + 1);
  strncpy(dname, tmp_dname, n);
  dname[n] = '\0';

  tmp_cpp = *cpp + n;

  GETSHORT(type, tmp_cpp);

  if (type == T_A) {
    Debug("dns_cache", "got request for dname = %s\n", dname);

    Action *dns_lookup_action_handle = hostDBProcessor.getbyname_imm(this,
                                                                     (process_hostdb_info_pfn) & DNS_Cache::
                                                                     process_hostdb_info,
                                                                     dname, 0, server_port,
                                                                     HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS
                                                                     | HostDBProcessor::HOSTDB_DNS_PROXY);
    if (dns_lookup_action_handle != ACTION_RESULT_DONE) {
      Debug("dns_cache", "hostlookup return pending event");
      ink_assert(!pending_action);
      pending_action = dns_lookup_action_handle;
    } else {
      send_dns_response();
      free();
      DNS_cache_Allocator.free(this);
    }
  } else {
    Debug("dns_cache", "got request of type %d", type);
    SET_HANDLER((DNS_CacheContHandler) & DNS_Cache::dnsEvent);
    pending_action = dnsProcessor.getproxyresult(this, (char *) request, NULL);
  }
  return 1;
}

/*
 * DNS proxy result state
 */
int
DNS_Cache::dnsEvent(int event, HostEnt * e)
{
  if (event == DNS_EVENT_LOOKUP) {
    if (e) {
      ink_assert(e->buf);
      ink_assert(e->packet_size);
      msg = (unsigned char *) xmalloc(e->packet_size);
      bcopy(e->buf, msg, e->packet_size);
      msglen = e->packet_size;
      send_dns_response();
      Debug("dns_cache", "sent non_type A response with %d bytes", msglen);
      free();
      DNS_cache_Allocator.free(this);
    } else {
      // the dns request probably timed out. so forget everything
      // it's up to the dns resolver to retry the request.
      free();
      DNS_cache_Allocator.free(this);
    }
  }
  return EVENT_DONE;
}

/*
 * Process HostDBInfo, and construct rr response.
 */
int
DNS_Cache::process_hostdb_info(HostDBInfo * r)
{
  msg = (unsigned char *)::malloc(MAX_DNS_PROXY_PACKET_LEN);
  bcopy((const char *) request, (char *) msg, msglen);

  HEADER *hp = (HEADER *) msg;
  int class_type = 1;
  int n;
  u_char *sp;

  hp->qr = 1;                   /* set response flag */

  if (r) {
    // following field should be filled regardless whether r is
    // round robin or not.
    hp->rcode = NOERROR;
    hp->qdcount = htons(1);

    if (r->hits > 0) {
      DNS_CACHE_INCREMENT_DYN_STAT(dns_proxy_cache_hits_stat);
    } else {
      DNS_CACHE_INCREMENT_DYN_STAT(dns_proxy_cache_misses_stat);
    }

    // round_robin indicates multiple ip's associate
    // with host alias. 
    if (!r->round_robin) {
      hp->rd = 1;
      //          hp->ra = (NoRecurse == 0);
      hp->ancount = htons(1);

      // Get ttl from r
      ttl = r->ip_time_remaining() * 60;

      // Jump over header section
      u_char *cp = msg + HFIXEDSZ;

      // Jump over question section
      n = dn_comp(dname, cp, buflen, 0, 0);
      cp = cp + n + QFIXEDSZ;

      // Now we reached answer portion
      // start filling in RR section
      buflen -= n + QFIXEDSZ;
      n = dn_comp(dname, cp, buflen, 0, 0);

      cp += n;
      PUTSHORT(T_A, cp);
      PUTSHORT((unsigned int) class_type, cp);
      PUTLONG(ttl, cp);

      /* need to save current position via sp, so later we can insert
       * resource data length after resource data has been filled in.
       */
      sp = cp;
      cp += INT16SZ;
      unsigned int temp_ip = r->data.ip;
      Debug("dns_cache", "DNS lookup succeeded for '%s'", dname);
      Debug("dns_cache", "ip = %d.%d.%d.%d\n", DOT_SEPARATED(*(unsigned int *) (&temp_ip)));
      //n = dn_comp((char *)&(temp_ip), cp , buflen, 0, 0);
      //ink_assert(n > 0);
      memcpy(cp, (unsigned int *) &temp_ip, 4);
      cp += 4;
      PUTSHORT(4, sp);          /* put length of data in */
      Debug("dns_cache", "ttl %d %d %d %d, sp values %d %d\n", DOT_SEPARATED(ttl), sp[-2], sp[-1]);
      msglen = cp - msg;
    } else {
      Debug("dns_cache", "HostDB has a ROUNDROBIN entries for hostname %s\n", dname);
      HostDBRoundRobin *rr = r->rr();

      // First, set up the hostname in reply with type being CNAME,
      // Then need to put additional canonical name 

      // Get ttl from r
      // Currently, the only ttl that is valid is from the initial
      // HostDBInfo structure, all HostDBInfo structs from HostDBInfoRoundRobin
      // are invalid. For now, just use ttl in initial HostDBInfo structures
      // for all ttl in RR answers. 
      ttl = r->ip_time_remaining() * 60;
      Debug("dns_cache", "first entries in ROUNDROBIN has ttl %d\n", ttl);
      // Jump over header section
      u_char *cp = msg + HFIXEDSZ;

      // Jump over question section
      n = dn_comp(dname, cp, buflen, 0, 0);
      cp = cp + n + QFIXEDSZ;
      buflen -= n + QFIXEDSZ;

      Debug("dns_cache", "HostDB has a ROUNDROBIN entries for hostname %s\n", dname);
      // Now we reached answer portion
      // start filling in RR section
      // First need to check if a canonical name is given. Fill it
      // in only if it's given

      int answers = 0;
      char tmp_cname[MAXDNAME];
      memset(tmp_cname, 0, MAXDNAME);

#if 0
      strcpy(tmp_cname, rr->cname((char *) rr));
      if (*tmp_cname) {
#endif
        // for now, in case canonical name is not given, use
        // dname to fill in names for round-robin servers.
        strncpy(tmp_cname, dname, sizeof(tmp_cname));
        // Fill in canonical name
        n = dn_comp(dname, cp, buflen, 0, 0);
        cp += n;
        PUTSHORT((unsigned int) T_CNAME, cp);
        PUTSHORT((unsigned int) class_type, cp);
        PUTLONG(ttl, cp);
        sp = cp;
        cp += INT16SZ;

        n = dn_comp(tmp_cname, cp, buflen, 0, 0);
        PUTSHORT((unsigned int) n, sp);

        cp += n;
        answers++;
#if 0
      } else {
      }
#endif
      // increment the round-robin count
      rr->increment_round_robin();
      int current = rr->current % rr->good;

      // put in all answers for multiple ips for hostname
      for (int i = current; i < rr->good; i++) {
        ttl = r->ip_time_remaining() * 60;
        n = dn_comp(tmp_cname, cp, buflen, 0, 0);
        cp += n;
        PUTSHORT((unsigned int) T_A, cp);
        PUTSHORT((unsigned int) class_type, cp);
        PUTLONG(ttl, cp);
        sp = cp;
        cp += INT16SZ;
        unsigned int temp_ip = rr->info[i].data.ip;
        Debug("dns_cache", "DNS lookup succeeded for '%s'", dname);
        Debug("dns_cache", "ip = %d.%d.%d.%d\n", DOT_SEPARATED(*(unsigned int *) (&temp_ip)));
        memcpy(cp, (unsigned int *) &temp_ip, 4);
        n = 4;
        cp += n;
        PUTSHORT((unsigned int) n, sp); /* put length of data in */
        answers++;
      }
      if (current > 0) {
        for (int i = 0; i < current; i++) {
          ttl = r->ip_time_remaining() * 60;
          n = dn_comp(tmp_cname, cp, buflen, 0, 0);
          cp += n;
          PUTSHORT((unsigned int) T_A, cp);
          PUTSHORT((unsigned int) class_type, cp);
          PUTLONG(ttl, cp);
          sp = cp;
          cp += INT16SZ;
          unsigned int temp_ip = rr->info[i].data.ip;
          Debug("dns_cache", "DNS lookup succeeded for '%s'", dname);
          Debug("dns_cache", "ip = %d.%d.%d.%d\n", DOT_SEPARATED(*(unsigned int *) (&temp_ip)));
          memcpy(cp, (unsigned int *) &temp_ip, 4);
          n = 4;
          cp += n;
          PUTSHORT((unsigned int) n, sp);       /* put length of data in */
          answers++;
        }
      }
      msglen = cp - msg;
      hp->rd = 1;
      //          hp->ra = (NoRecurse == 0);
      hp->ancount = htons(answers);
    }
  } else {
    Debug("dns_cache", "DNS lookup failed for '%s'", dname);
    hp->rcode = NXDOMAIN;
    hp->rd = 1;
    msglen = HFIXEDSZ;
  }

  return 1;
}

void
start_dns_Proxy(int dns_proxy_fd)
{
  extern void *dns_udp_receiver(void *);

  int dns_proxy_port = 0;

  Debug("dns_cache", "dns_proxy_fd = %d", dns_proxy_fd);
  dns_fd = dns_proxy_fd;

  // dns_proxy_fd should not be a non-valid number. For now,
  // in case it is NO_FD, we will set up the udp port again from
  // the configuration file.
  if (dns_proxy_fd == NO_FD) {
    IOCORE_EstablishStaticConfigInt32(dns_proxy_port, "proxy.config.dns.proxy_port");
  }

  if (dns_proxy_port == 0)
    dns_proxy_port = DEFAULT_DNS_PROXY_PORT;

  ink_thread_create(dns_udp_receiver, (void *) dns_proxy_port);
}

void *
dns_udp_receiver(void *varg)
{
  int port = (int) varg;

  char *pkt_buf = (char *) xmalloc(MAX_DNS_PACKET_LEN);

  if (!pkt_buf) {
    perror("malloc()");
    return 0;
  }

  if (dns_fd == NO_FD) {
    dns_fd = socket(PF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in saddr;

    bzero((void *) &saddr, sizeof(sockaddr_in));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = INADDR_ANY;

    if ((bind(dns_fd, (struct sockaddr *) &saddr, sizeof(saddr))) < 0) {
      perror("bind(udp_fd)");
      xfree(pkt_buf);
      return 0;
    }
  }

  struct sockaddr_in saddr_in;
  int saddr_in_length = sizeof(saddr_in);

  while (1) {
    int pkt_size = recvfrom(dns_fd, pkt_buf, MAX_DNS_PACKET_LEN, 0,
                            (struct sockaddr *) &saddr_in,
                            (socklen_t *) & saddr_in_length);

    /* create one DNS Cache continuation to deal with each dns query */
    if (pkt_size > 0 && pkt_size <= MAX_DNS_PACKET_LEN) {
      (DNS_cache_Allocator.alloc())->init(pkt_buf, pkt_size, &saddr_in, saddr_in_length);
      //(NEW(new DNS_Cache()))->init(pkt_buf,pkt_size,&saddr_in,saddr_in_length);
    }
  }
}

RecRawStatBlock *dns_cache_rsb;

void
ink_dns_cache_init(ModuleVersion v)
{
  static int init_called = 0;

  ink_release_assert(!checkModuleVersion(v, DNS_CACHE_MODULE_VERSION));
  if (init_called)
    return;

  init_called = 1;
  // do one time stuff
  // create a stat block for HostDBStats
  dns_cache_rsb = RecAllocateRawStatBlock((int) DNS_Cache_Stat_Count);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.dns.proxy_port", 5353, RECU_DYNAMIC, RECC_NULL, NULL);

  //
  // Register statistics callbacks
  //
  RecRegisterRawStat(dns_cache_rsb, RECT_PROCESS,
                     "proxy.process.dns.proxy.requests.received",
                     RECD_INT, RECP_NULL, (int) dns_proxy_requests_received_stat, RecRawStatSyncCount);

  RecRegisterRawStat(dns_cache_rsb, RECT_PROCESS,
                     "proxy.process.dns.proxy.cache.hits",
                     RECD_INT, RECP_NULL, (int) dns_proxy_cache_hits_stat, RecRawStatSyncCount);

  RecRegisterRawStat(dns_cache_rsb, RECT_PROCESS,
                     "proxy.process.dns.proxy.cache.misses",
                     RECD_INT, RECP_NULL, (int) dns_proxy_cache_misses_stat, RecRawStatSyncCount);
}
