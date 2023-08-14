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

#include "I_EventSystem.h"
#include "tscore/PendingAction.h"

#define MAX_NAMED 32
#define DEFAULT_DNS_RETRIES 5
#define MAX_DNS_RETRIES 9
#define DEFAULT_DNS_TIMEOUT 30
#define MAX_DNS_IN_FLIGHT 2048
#define MAX_DNS_TCP_CONTINUOUS_FAILURES 10
#define DEFAULT_FAILOVER_NUMBER (DEFAULT_DNS_RETRIES + 1)
#define DEFAULT_FAILOVER_PERIOD (DEFAULT_DNS_TIMEOUT + 30)
// how many seconds before FAILOVER_PERIOD to try the primary with
// a well known address
#define DEFAULT_FAILOVER_TRY_PERIOD (DEFAULT_DNS_TIMEOUT + 1)
#define DEFAULT_DNS_SEARCH 1
#define FAILOVER_SOON_RETRY 5
#define NO_NAMESERVER_SELECTED -1

//
// Config
//
extern int dns_timeout;
extern int dns_retries;
extern int dns_search;
extern int dns_failover_number;
extern int dns_failover_period;
extern int dns_failover_try_period;
extern int dns_max_dns_in_flight;
extern int dns_max_tcp_continuous_failures;
extern unsigned int dns_sequence_number;

//
// Constants
//

#define DNS_PERIOD HRTIME_MSECONDS(100)
#define DNS_DELAY_PERIOD HRTIME_MSECONDS(10)
#define DNS_SEQUENCE_NUMBER_RESTART_OFFSET 4000
#define DNS_PRIMARY_RETRY_PERIOD HRTIME_SECONDS(5)
#define DNS_PRIMARY_REOPEN_PERIOD HRTIME_SECONDS(60)
#define BAD_DNS_RESULT (reinterpret_cast<HostEnt *>((uintptr_t)-1))
#define DEFAULT_NUM_TRY_SERVER 8

// these are from nameser.h
#ifndef HFIXEDSZ
#define HFIXEDSZ 12
#endif
#ifndef QFIXEDSZ
#define QFIXEDSZ 4
#endif

// Events
#define DNS_EVENT_LOOKUP DNS_EVENT_EVENTS_START

// Stats
enum DNS_Stats {
  dns_total_lookups_stat,
  dns_response_time_stat,
  dns_success_time_stat,
  dns_lookup_success_stat,
  dns_lookup_fail_stat,
  dns_fail_time_stat,
  dns_retries_stat,
  dns_max_retries_exceeded_stat,
  dns_in_flight_stat,
  dns_tcp_retries_stat,
  dns_tcp_reset_stat,
  DNS_Stat_Count
};

struct HostEnt;
struct DNSHandler;

struct RecRawStatBlock;
extern RecRawStatBlock *dns_rsb;

// Stat Macros
#define DNS_INCREMENT_DYN_STAT(_x) RecIncrRawStatSum(dns_rsb, mutex->thread_holding, (int)_x, 1)

#define DNS_DECREMENT_DYN_STAT(_x) RecIncrRawStatSum(dns_rsb, mutex->thread_holding, (int)_x, -1)

#define DNS_SUM_DYN_STAT(_x, _r) RecIncrRawStatSum(dns_rsb, mutex->thread_holding, (int)_x, _r)

/**
  One DNSEntry is allocated per outstanding request. This continuation
  handles TIMEOUT events for the request as well as storing all
  information about the request and its status.

*/
struct DNSEntry : public Continuation {
  int id[MAX_DNS_RETRIES];
  int qtype                   = 0;             ///< Type of query to send.
  HostResStyle host_res_style = HOST_RES_NONE; ///< Preferred IP address family.
  int retries                 = DEFAULT_DNS_RETRIES;
  int which_ns                = NO_NAMESERVER_SELECTED;
  ink_hrtime submit_time      = 0;
  ink_hrtime send_time        = 0;
  char qname[MAXDNAME + 1];
  int qname_len          = 0;
  int orig_qname_len     = 0;
  char **domains         = nullptr;
  EThread *submit_thread = nullptr;
  Action action;
  Event *timeout = nullptr;
  Ptr<HostEnt> result_ent;
  DNSHandler *dnsH       = nullptr;
  bool written_flag      = false;
  bool once_written_flag = false;
  bool last              = false;
  LINK(DNSEntry, dup_link);
  Que(DNSEntry, dup_link) dups;

  int mainEvent(int event, Event *e);
  int delayEvent(int event, Event *e);
  int postAllEvent(int event, Event *e);
  int post(DNSHandler *h, HostEnt *ent);
  int postOneEvent(int event, Event *e);
  void init(DNSQueryData target, int qtype_arg, Continuation *acont, DNSProcessor::Options const &opt);

  DNSEntry()
  {
    for (int &i : id)
      i = -1;
    memset(qname, 0, MAXDNAME);
  }
};

typedef int (DNSEntry::*DNSEntryHandler)(int, void *);

struct DNSEntry;

/**
  One DNSHandler is allocated to handle all DNS traffic by polling a
  UDP port.

*/
struct DNSHandler : public Continuation {
  /// This is used as the target if round robin isn't set.
  IpEndpoint ip;
  IpEndpoint local_ipv6; ///< Local V6 address if set.
  IpEndpoint local_ipv4; ///< Local V4 address if set.
  int ifd[MAX_NAMED];
  int n_con = 0;
  DNSConnection tcpcon[MAX_NAMED];
  DNSConnection udpcon[MAX_NAMED];
  Queue<DNSEntry> entries;
  Queue<DNSConnection> triggered;
  int in_flight    = 0;
  int name_server  = 0;
  int in_write_dns = 0;

  HostEnt *hostent_cache = nullptr;

  int ns_down[MAX_NAMED];
  int failover_number[MAX_NAMED];
  int failover_soon_number[MAX_NAMED];
  int tcp_continuous_failures[MAX_NAMED];
  ink_hrtime crossed_failover_number[MAX_NAMED];
  ink_hrtime last_primary_retry  = 0;
  ink_hrtime last_primary_reopen = 0;

  ink_res_state m_res    = nullptr;
  int txn_lookup_timeout = 0;

  InkRand generator;
  // bitmap of query ids in use
  uint64_t qid_in_flight[(USHRT_MAX + 1) / 64];

  void
  received_one(int i)
  {
    failover_number[i] = failover_soon_number[i] = crossed_failover_number[i] = 0;
  }

  void
  sent_one()
  {
    ++failover_number[name_server];
    Debug("dns", "sent_one: failover_number for resolver %d is %d", name_server, failover_number[name_server]);
    if (failover_number[name_server] >= dns_failover_number && !crossed_failover_number[name_server])
      crossed_failover_number[name_server] = ink_get_hrtime();
  }

  bool
  failover_now(int i)
  {
    if (is_debug_tag_set("dns")) {
      Debug("dns", "failover_now: Considering immediate failover, target time is %" PRId64 "",
            (ink_hrtime)HRTIME_SECONDS(dns_failover_period));
      Debug("dns", "\tdelta time is %" PRId64 "", (ink_get_hrtime() - crossed_failover_number[i]));
    }
    return ns_down[i] ||
           (crossed_failover_number[i] && ((ink_get_hrtime() - crossed_failover_number[i]) > HRTIME_SECONDS(dns_failover_period)));
  }

  bool
  failover_soon(int i)
  {
    return ns_down[i] || (crossed_failover_number[i] &&
                          ((ink_get_hrtime() - crossed_failover_number[i]) >
                           (HRTIME_SECONDS(dns_failover_try_period + failover_soon_number[i] * FAILOVER_SOON_RETRY))));
  }

  void recv_dns(int event, Event *e);
  int startEvent(int event, Event *e);
  int startEvent_sdns(int event, Event *e);
  int mainEvent(int event, Event *e);

  void open_cons(sockaddr const *addr, bool failed = false, int icon = 0);
  bool open_con(sockaddr const *addr, bool failed = false, int icon = 0, bool over_tcp = false);
  void failover();
  void rr_failure(int ndx);
  void recover();
  void retry_named(int ndx, ink_hrtime t, bool reopen = true);
  void try_primary_named(bool reopen = true);
  void switch_named(int ndx);
  uint16_t get_query_id();

  void
  release_query_id(uint16_t qid)
  {
    qid_in_flight[qid >> 6] &= (uint64_t) ~(0x1ULL << (qid & 0x3F));
  };

  void
  set_query_id_in_use(uint16_t qid)
  {
    qid_in_flight[qid >> 6] |= (uint64_t)(0x1ULL << (qid & 0x3F));
  };

  bool
  query_id_in_use(uint16_t qid)
  {
    return (qid_in_flight[(uint16_t)(qid) >> 6] & (uint64_t)(0x1ULL << ((uint16_t)(qid)&0x3F))) != 0;
  };

  DNSHandler();

private:
  // Check the IP address and switch to default if needed.
  void validate_ip();
  // Check tcp connection for TCP_RETRY mode
  void check_and_reset_tcp_conn();
  bool reset_tcp_conn(int ndx);

  /** The event used for the periodic retry of connectivity to any down name
   * servers. */
  PendingAction _dns_retry_event;
};

/* --------------------------------------------------------------
   **                struct DNSServer

   A record for an single server
   -------------------------------------------------------------- */
struct DNSServer {
  IpEndpoint x_server_ip[MAXNS];
  char x_dns_ip_line[MAXDNAME * 2];

  char x_def_domain[MAXDNAME];
  char x_domain_srch_list[MAXDNAME];

  DNSHandler *x_dnsH = nullptr;

  DNSServer()
  {
    memset(x_server_ip, 0, sizeof(x_server_ip));

    memset(x_def_domain, 0, MAXDNAME);
    memset(x_domain_srch_list, 0, MAXDNAME);
    memset(x_dns_ip_line, 0, MAXDNAME * 2);
  }
};

TS_INLINE
DNSHandler::DNSHandler()
  : Continuation(nullptr),

    generator((uint32_t)((uintptr_t)time(nullptr) ^ (uintptr_t)this))
{
  ats_ip_invalidate(&ip);
  for (int i = 0; i < MAX_NAMED; i++) {
    ifd[i]                     = -1;
    failover_number[i]         = 0;
    failover_soon_number[i]    = 0;
    crossed_failover_number[i] = 0;
    tcp_continuous_failures[i] = 0;
    ns_down[i]                 = 1;
    tcpcon[i].handler          = this;
    udpcon[i].handler          = this;
  }
  memset(&qid_in_flight, 0, sizeof(qid_in_flight));
  SET_HANDLER(&DNSHandler::startEvent);
  Debug("net_epoll", "inline DNSHandler::DNSHandler()");
}
