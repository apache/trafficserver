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

#if !defined (_P_DNSProcessor_h_)
#define _P_DNSProcessor_h_

#define DNS_PROXY 1
/*
  #include "I_DNS.h"
  #include "inktomi++.h"
  #include <arpa/nameser.h>
  #include "I_Cache.h"
  #include "P_Net.h"
*/
#include "I_EventSystem.h"

#define MAX_NAMED                           32
#define DEFAULT_DNS_RETRIES                 3
#define MAX_DNS_RETRIES                     5
#define DEFAULT_DNS_TIMEOUT                 30
#define MAX_DNS_IN_FLIGHT                   60
#define DEFAULT_FAILOVER_NUMBER             (DEFAULT_DNS_RETRIES + 1)
#define DEFAULT_FAILOVER_PERIOD             (DEFAULT_DNS_TIMEOUT + 30)
// how many seconds before FAILOVER_PERIOD to try the primary with
// a well known address
#define DEFAULT_FAILOVER_TRY_PERIOD         (DEFAULT_DNS_TIMEOUT + 1)
#define DEFAULT_DNS_SEARCH           1
#define FAILOVER_SOON_RETRY          5
#define MAX_DNS_PROXY_PACKET_LEN     1024
#define NO_NAMESERVER_SELECTED       -1

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
extern unsigned int dns_sequence_number;

//
// Constants
//

#define DNS_PERIOD                          HRTIME_MSECONDS(-100)
#define DNS_DELAY_PERIOD                    HRTIME_MSECONDS(10)
#define DNS_SEQUENCE_NUMBER_RESTART_OFFSET  4000
#define DNS_PRIMARY_RETRY_PERIOD            HRTIME_SECONDS(5)
#define DNS_PRIMARY_REOPEN_PERIOD           HRTIME_SECONDS(60)
#define BAD_DNS_RESULT                      ((HostEnt*)(long)-1)
#define DEFAULT_NUM_TRY_SERVER              8

// these are from nameser.h
#ifndef HFIXEDSZ
#define HFIXEDSZ 12
#endif
#ifndef QFIXEDSZ
#define QFIXEDSZ 4
#endif


// Events

#define DNS_EVENT_LOOKUP             DNS_EVENT_EVENTS_START

extern int dns_fd;

void dns_cache_Init(void);
void *dns_udp_receiver(void *arg);


//Stats
enum DNS_Stats
{
  dns_total_lookups_stat,
  dns_response_time_stat,
  dns_success_time_stat,
  dns_lookup_success_stat,
  dns_lookup_fail_stat,
  dns_fail_time_stat,
  dns_retries_stat,
  dns_max_retries_exceeded_stat,
  dns_sequence_number_stat,
  dns_in_flight_stat,
  DNS_Stat_Count
};

struct HostEnt;
struct DNSHandler;

struct RecRawStatBlock;
extern RecRawStatBlock *dns_rsb;

// Stat Macros

#define DNS_DEBUG_COUNT_DYN_STAT(_x, _y)                            \
  RecIncrRawStatCount(dns_rsb, mutex->thread_holding, (int)_x, _y)

#define DNS_INCREMENT_DYN_STAT(_x)                              \
  RecIncrRawStatSum(dns_rsb, mutex->thread_holding, (int)_x, 1)

#define DNS_DECREMENT_DYN_STAT(_x)                                \
  RecIncrRawStatSum(dns_rsb, mutex->thread_holding, (int)_x, -1)

#define DNS_SUM_DYN_STAT(_x, _r)                                  \
  RecIncrRawStatSum(dns_rsb, mutex->thread_holding, (int)_x, _r)

#define DNS_READ_DYN_STAT(_x, _count, _sum) do {    \
    RecGetRawStatSum(dns_rsb, (int)_x, &_sum);      \
    RecGetRawStatCount(dns_rsb, (int)_x, &_count);  \
  } while (0)

#define DNS_SET_DYN_COUNT(_x, _count)           \
  RecSetRawStatCount(dns_rsb, _x, _count);

#define DNS_INCREMENT_THREAD_DYN_STAT(_s, _t)   \
  RecIncrRawStatSum(dns_rsb, _t, (int) _s, 1);

#define DNS_DECREMENT_THREAD_DYN_STAT(_s, _t)   \
  RecIncrRawStatSum(dns_rsb, _t, (int) _s, -1);

/**
  One DNSEntry is allocated per outstanding request. This continuation
  handles TIMEOUT events for the request as well as storing all
  information about the request and its status.

*/
struct DNSEntry:Continuation
{
  int id[MAX_DNS_RETRIES];
  int qtype;
  int retries;
  int which_ns;
  ink_hrtime submit_time;
  ink_hrtime send_time;
  char qname[MAXDNAME];
  int qname_len;
  char **domains;
  bool proxy_cache;

#ifdef DNS_PROXY
  bool proxy;
  unsigned char request[MAX_DNS_PROXY_PACKET_LEN];
#endif

  Action action;

  Event *timeout;
  HostEnt *result_ent;

  HostEnt **sem_ent;
#if (HOST_OS == darwin)
  ink_sem *sem;
#else
  ink_sem sem;
#endif
  DNSHandler *dnsH;


  bool written_flag;
  bool once_written_flag;
  bool last;
  LINK(DNSEntry, dup_link);
  Que(DNSEntry, dup_link) dups;

  int mainEvent(int event, Event * e);
  int delayEvent(int event, Event * e);
  int post(DNSHandler * h, HostEnt * ent, bool freeable);
  int postEvent(int event, Event * e);

  void init(char *x, int len, int qtype_arg, Continuation * acont, HostEnt ** wait, DNSHandler * adnsH, int timeout);

    DNSEntry()
  : Continuation(NULL),
    qtype(0),
    retries(DEFAULT_DNS_RETRIES),
    which_ns(NO_NAMESERVER_SELECTED), submit_time(0), send_time(0), qname_len(0), domains(0), proxy_cache(0),
#ifdef DNS_PROXY
    proxy(0),
#endif
    timeout(0), result_ent(0), sem_ent(0), dnsH(0), written_flag(false), once_written_flag(false), last(false)
  {
    for (int i = 0; i < MAX_DNS_RETRIES; i++)
      id[i] = -1;

    memset(qname, 0, MAXDNAME);
    memset(request, 0, MAX_DNS_PROXY_PACKET_LEN);
    memset(&sem, 0, sizeof sem);
  }
};


typedef int (DNSEntry::*DNSEntryHandler) (int, void *);

struct DNSEntry;

/**
  One DNSHandler is allocated to handle all DNS traffic by polling a
  UDP port.

*/
struct DNSHandler:Continuation
{
  unsigned int ip;
  int port;
  int ifd[MAX_NAMED];
  int n_con;
  DNSConnection con[MAX_NAMED];
  int options;
  Queue<DNSEntry> entries;
  int in_flight;
  int name_server;
  int in_write_dns;
  HostEnt *hostent_cache;

  int ns_down[MAX_NAMED];
  int failover_number[MAX_NAMED];
  int failover_soon_number[MAX_NAMED];
  ink_hrtime crossed_failover_number[MAX_NAMED];
  ink_hrtime last_primary_retry;
  ink_hrtime last_primary_reopen;

  ink_res_state m_res;
  int txn_lookup_timeout;

  void received_one(int i)
  {
    failover_number[i] = failover_soon_number[i] = crossed_failover_number[i] = 0;
  }

  void sent_one()
  {
    ++failover_number[name_server];
    Debug("dns", "sent_one: failover_number for resolver %d is %d", name_server, failover_number[name_server]);
    if (failover_number[name_server] >= dns_failover_number && !crossed_failover_number[name_server])
      crossed_failover_number[name_server] = ink_get_hrtime();
  }

  bool failover_now(int i)
  {
    if (is_debug_tag_set("dns")) {
      Debug("dns", "failover_now: Considering immediate failover, target time is %lld",
            HRTIME_SECONDS(dns_failover_period));
      Debug("dns", "\tdelta time is %lld", (ink_get_hrtime() - crossed_failover_number[i]));
    }
    return (crossed_failover_number[i] &&
            ((ink_get_hrtime() - crossed_failover_number[i]) > HRTIME_SECONDS(dns_failover_period)));
  }

  bool failover_soon(int i) {
    return (crossed_failover_number[i] &&
            ((ink_get_hrtime() - crossed_failover_number[i]) >
             (HRTIME_SECONDS(dns_failover_try_period + failover_soon_number[i] * FAILOVER_SOON_RETRY))));
  }

  void recv_dns(int event, Event * e);
  int startEvent(int event, Event * e);
  int startEvent_sdns(int event, Event * e);
  int mainEvent(int event, Event * e);

  void open_con(unsigned int aip, int aport, bool failed = false, int icon = 0);
  void failover();
  void rr_failure(int ndx);
  void recover();
  void retry_named(int ndx, ink_hrtime t, bool reopen = true);
  void try_primary_named(bool reopen = true);
  void switch_named(int ndx);

  DNSHandler();
};


TS_INLINE DNSHandler::DNSHandler()
 :Continuation(NULL),
  ip(0),
  port(0),
  n_con(0),
  options(0),
  in_flight(0), name_server(0), in_write_dns(0), hostent_cache(0), last_primary_retry(0), last_primary_reopen(0),
  m_res(0), txn_lookup_timeout(0)
{
  for (int i = 0; i < MAX_NAMED; i++) {
    ifd[i] = -1;
    failover_number[i] = 0;
    failover_soon_number[i] = 0;
    crossed_failover_number[i] = 0;
    ns_down[i] = 1;
  }
  SET_HANDLER(&DNSHandler::startEvent);
  Debug("net_epoll", "inline DNSHandler::DNSHandler()");
}

#define DOT_SEPARATED(_x)                                   \
  ((unsigned char*)&(_x))[0], ((unsigned char*)&(_x))[1],   \
    ((unsigned char*)&(_x))[2], ((unsigned char*)&(_x))[3]

#endif
