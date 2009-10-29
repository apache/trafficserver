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

#ifndef H_DNS_CACHE_H
#define H_DNS_CACHE_H

#include <arpa/nameser.h>
#include "P_DNS.h"
//#include "NetVConnection.h"
//#include "CacheInternal.h"
//#include "OneWayTunnel.h"
//#include "HttpTransact.h"
//#include "Allocator.h"

#define server_port 80
#define DEFAULT_DNS_PROXY_PORT  28888
#define MAX_DNS_PROXY_PACKET_LEN 1024

void start_dns_Proxy(int);
void *dns_udp_receiver(void *arg);

#define DNS_CACHE_MODULE_MAJOR_VERSION 1
#define DNS_CACHE_MODULE_MINOR_VERSION 0
#define DNS_CACHE_MODULE_VERSION makeModuleVersion(                    \
                                       DNS_CACHE_MODULE_MAJOR_VERSION, \
                                       DNS_CACHE_MODULE_MINOR_VERSION, \
                                       PUBLIC_MODULE_HEADER)

struct DNS_Cache;
typedef int (DNS_Cache::*DNS_CacheContHandler) (int, void *);

class DNS_Cache:public Continuation
{

public:

  DNS_Cache();

  int mainEvent(int, void *);
  int process_dns_query();
  int req_query(HEADER *, unsigned char **, unsigned char *);
  int process_hostdb_info(HostDBInfo *);
  int send_dns_response();
  int dnsEvent(int, HostEnt *);
  void init(char *pkt_buf, int pkt_size, struct sockaddr_in *saddr_in, int saddr_in_length);
  void free()
  {
    mutex = NULL;
    ::free(dname);
    ::free(msg);
    ::free(request);
  }

private:

  struct sockaddr_in sa_from;
  unsigned long ttl;
  unsigned char *request;
  unsigned char *msg;
  int msglen, buflen;
  char *dname;
  Action *pending_action;
};


inline
DNS_Cache::DNS_Cache()
  :
Continuation(0),
ttl(0),
msglen(0),
buflen(0),
pending_action(0)
{
  bzero((void *) &sa_from, sizeof(struct sockaddr_in));
}


void ink_dns_cache_init(ModuleVersion version);

//Stats
enum DNS_Cache_Stats
{
  dns_proxy_requests_received_stat,
  dns_proxy_cache_hits_stat,
  dns_proxy_cache_misses_stat,
  DNS_Cache_Stat_Count
};

struct RecRawStatBlock;
extern RecRawStatBlock *dns_cache_rsb;

// Stat Macros

#define DNS_CACHE_DEBUG_COUNT_DYN_STAT(_x, _y) \
RecIncrRawStatCount(dns_cache_rsb, mutex->thread_holding, (int)_x, _y)

#define DNS_CACHE_INCREMENT_DYN_STAT(_x)  \
RecIncrRawStatSum(dns_cache_rsb, mutex->thread_holding, (int)_x, 1)

#define DNS_CACHE_DECREMENT_DYN_STAT(_x) \
RecIncrRawStatSum(dns_cache_rsb, mutex->thread_holding, (int)_x, -1)

#define DNS_CACHE_SUM_DYN_STAT(_x, _r) \
RecIncrRawStatSum(dns_cache_rsb, mutex->thread_holding, (int)_x, _r)

#define DNS_CACHE_READ_DYN_STAT(_x, _count, _sum) do {\
RecGetRawStatSum(dns_cache_rsb, (int)_x, &_sum);          \
RecGetRawStatCount(dns_cache_rsb, (int)_x, &_count);         \
} while (0)

#define DNS_CACHE_SET_DYN_COUNT(_x, _count) \
RecSetRawStatCount(dns_cache_rsb, _x, _count);

#define DNS_CACHE_INCREMENT_THREAD_DYN_STAT(_s, _t) \
  RecIncrRawStatSum(dns_cache_rsb, _t, (int) _s, 1);

#define DNS_CACHE_DECREMENT_THREAD_DYN_STAT(_s, _t) \
  RecIncrRawStatSum(dns_cache_rsb, _t, (int) _s, -1);

#endif /* #ifndef H_DNS_CACHE_H */
