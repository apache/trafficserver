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

/****************************************************************************
  P_HostDBProcessor.h
 ****************************************************************************/

#ifndef _P_HostDBProcessor_h_
#define _P_HostDBProcessor_h_

#include "I_HostDBProcessor.h"

inline unsigned int HOSTDB_CLIENT_IP_HASH(
  sockaddr const* lhs,
  sockaddr const* rhs
) {
  unsigned int zret = ~static_cast<unsigned int>(0);
  if (ats_ip_are_compatible(lhs,rhs)) {
    if (ats_is_ip4(lhs)) {
      in_addr_t ip1 = ats_ip4_addr_cast(lhs);
      in_addr_t ip2 = ats_ip4_addr_cast(rhs);
      zret = (ip1 >> 16) ^ ip1 ^ ip2 ^ (ip2 >> 16);
    } else if (ats_is_ip6(lhs)) {
      uint32_t const* ip1 = ats_ip_addr32_cast(lhs);
      uint32_t const* ip2 = ats_ip_addr32_cast(rhs);
      for ( int i = 0 ; i < 4 ; ++i, ++ip1, ++ip2 ) {
        zret ^= (*ip1 >> 16) ^ *ip1 ^ *ip2 ^ (*ip2 >> 16);
      }
    }
  }
  return zret & 0xFFFF;
}

//
// Constants
//

#define HOST_DB_HITS_BITS           3
#define HOST_DB_TAG_BITS            56

#define CONFIGURATION_HISTORY_PROBE_DEPTH   1

// Bump this any time hostdb format is changed
#define HOST_DB_CACHE_MAJOR_VERSION         2
#define HOST_DB_CACHE_MINOR_VERSION         1
// 2.1 : IPv6

#define DEFAULT_HOST_DB_FILENAME             "host.db"
#define DEFAULT_HOST_DB_SIZE                 (1<<14)
// Resolution of timeouts
#define HOST_DB_TIMEOUT_INTERVAL             HRTIME_SECOND
// Timeout DNS every 24 hours by default if ttl_mode is enabled
#define HOST_DB_IP_TIMEOUT                   (24*60*60)
// DNS entries should be revalidated every 12 hours
#define HOST_DB_IP_STALE                     (12*60*60)
// DNS entries which failed lookup, should be revalidated every hour
#define HOST_DB_IP_FAIL_TIMEOUT              (60*60)

//#define HOST_DB_MAX_INTERVAL                 (0x7FFFFFFF)
#define HOST_DB_MAX_TTL                      (0x1FFFFF) //24 days

//
// Constants
//

// period to wait for a remote probe...
#define HOST_DB_CLUSTER_TIMEOUT  HRTIME_MSECONDS(5000)
#define HOST_DB_RETRY_PERIOD     HRTIME_MSECONDS(20)

//#define TEST(_x) _x
#define TEST(_x)


#ifdef _HOSTDB_CC_
template struct MultiCache <HostDBInfo >;
#endif /* _HOSTDB_CC_ */

struct ClusterMachine;
struct HostEnt;
struct ClusterConfiguration;

// Stats
enum HostDB_Stats
{
  hostdb_total_entries_stat,
  hostdb_total_lookups_stat,
  hostdb_total_hits_stat,       // D == total hits
  hostdb_ttl_stat,              // D average TTL
  hostdb_ttl_expires_stat,      // D == TTL Expires
  hostdb_re_dns_on_reload_stat,
  hostdb_bytes_stat,
  HostDB_Stat_Count
};


struct RecRawStatBlock;
extern RecRawStatBlock *hostdb_rsb;

// Stat Macros

#define HOSTDB_DEBUG_COUNT_DYN_STAT(_x, _y) \
RecIncrRawStatCount(hostdb_rsb, mutex->thread_holding, (int)_x, _y)

#define HOSTDB_INCREMENT_DYN_STAT(_x)  \
RecIncrRawStatSum(hostdb_rsb, mutex->thread_holding, (int)_x, 1)

#define HOSTDB_DECREMENT_DYN_STAT(_x) \
RecIncrRawStatSum(hostdb_rsb, mutex->thread_holding, (int)_x, -1)

#define HOSTDB_SUM_DYN_STAT(_x, _r) \
RecIncrRawStatSum(hostdb_rsb, mutex->thread_holding, (int)_x, _r)

#define HOSTDB_READ_DYN_STAT(_x, _count, _sum) do {\
RecGetRawStatSum(hostdb_rsb, (int)_x, &_sum);          \
RecGetRawStatCount(hostdb_rsb, (int)_x, &_count);         \
} while (0)

#define HOSTDB_SET_DYN_COUNT(_x, _count) \
RecSetRawStatCount(hostdb_rsb, _x, _count);

#define HOSTDB_INCREMENT_THREAD_DYN_STAT(_s, _t) \
  RecIncrRawStatSum(hostdb_rsb, _t, (int) _s, 1);

#define HOSTDB_DECREMENT_THREAD_DYN_STAT(_s, _t) \
  RecIncrRawStatSum(hostdb_rsb, _t, (int) _s, -1);


//
// HostDBCache (Private)
//
struct HostDBCache: public MultiCache<HostDBInfo>
{
  int rebuild_callout(HostDBInfo * e, RebuildMC & r);
  int start(int flags = 0);
  MultiCacheBase *dup()
  {
    return NEW(new HostDBCache);
  }

  // This accounts for an average of 2 HostDBInfo per DNS cache (for round-robin etc.)
  virtual size_t estimated_heap_bytes_per_entry() const { return sizeof(HostDBInfo) * 2; }

  Queue<HostDBContinuation, Continuation::Link_link> pending_dns[MULTI_CACHE_PARTITIONS];
  Queue<HostDBContinuation, Continuation::Link_link> &pending_dns_for_hash(INK_MD5 & md5);
  HostDBCache();
};

inline HostDBInfo*
HostDBRoundRobin::find_ip(sockaddr const* ip) {
  bool bad = (n <= 0 || n > HOST_DB_MAX_ROUND_ROBIN_INFO || good <= 0 || good > HOST_DB_MAX_ROUND_ROBIN_INFO);
  if (bad) {
    ink_assert(!"bad round robin size");
    return NULL;
  }

  for (int i = 0; i < good; i++) {
    if (ats_ip_addr_eq(ip, info[i].ip())) {
      return &info[i];
    }
  }

  return NULL;
}

inline HostDBInfo *
HostDBRoundRobin::select_best(sockaddr const* client_ip, HostDBInfo * r)
{
  (void) r;
  bool bad = (n <= 0 || n > HOST_DB_MAX_ROUND_ROBIN_INFO || good <= 0 || good > HOST_DB_MAX_ROUND_ROBIN_INFO);
  if (bad) {
    ink_assert(!"bad round robin size");
    return NULL;
  }
  int best = 0;
  if (HostDBProcessor::hostdb_strict_round_robin) {
    best = current++ % good;
  } else {
    sockaddr const* ip = info[0].ip();
    unsigned int best_hash = HOSTDB_CLIENT_IP_HASH(client_ip, ip);
    for (int i = 1; i < good; i++) {
      ip = info[i].ip();
      unsigned int h = HOSTDB_CLIENT_IP_HASH(client_ip, ip);
      if (best_hash < h) {
        best = i;
        best_hash = h;
      }
    }
  }
  return &info[best];
}

inline HostDBInfo *
HostDBRoundRobin::select_best_http(sockaddr const* client_ip, ink_time_t now, int32_t fail_window)
{
  bool bad = (n <= 0 || n > HOST_DB_MAX_ROUND_ROBIN_INFO || good <= 0 || good > HOST_DB_MAX_ROUND_ROBIN_INFO);

  if (bad) {
    ink_assert(!"bad round robin size");
    return NULL;
  }

  int best_any = 0;
  int best_up = -1;

  if (HostDBProcessor::hostdb_strict_round_robin) {
    Debug("hostdb", "Using strict round robin");
    best_up = current++ % good;
  } else if (HostDBProcessor::hostdb_timed_round_robin > 0) {
    Debug("hostdb", "Using timed round-robin for HTTP");
    if ((now - timed_rr_ctime) > HostDBProcessor::hostdb_timed_round_robin) {
      Debug("hostdb", "Timed interval expired.. rotating");
      ++current;
      timed_rr_ctime = now;
    }
    best_up = current % good;
    Debug("hostdb", "Using %d for best_up", best_up);
  } else {
    Debug("hostdb", "Using default round robin");
    unsigned int best_hash_any = 0;
    unsigned int best_hash_up = 0;
    sockaddr const* ip;
    for (int i = 0; i < good; i++) {
      ip = info[i].ip();
      unsigned int h = HOSTDB_CLIENT_IP_HASH(client_ip, ip);
      if (best_hash_any <= h) {
        best_any = i;
        best_hash_any = h;
      }
      if (info[i].app.http_data.last_failure == 0 ||
          (unsigned int) (now - fail_window) > info[i].app.http_data.last_failure) {
        // Entry is marked up
        if (best_hash_up <= h) {
          best_up = i;
          best_hash_up = h;
        }
      } else {
        // Entry is marked down.  Make sure some nasty clock skew
        //  did not occur.  Use the retry time to set an upper bound
        //  as to how far in the future we should tolerate bogus last
        //  failure times.  This sets the upper bound that we would ever
        //  consider a server down to 2*down_server_timeout
        if (now + fail_window < (int32_t) (info[i].app.http_data.last_failure)) {
#ifdef DEBUG
          // because this region is mmaped, I cann't get anything
          //   useful from the structure in core files,  therefore
          //   copy the revelvant info to the stack so it will
          //   be readble in the core
          HostDBInfo current_info;
          HostDBRoundRobin current_rr;
          memcpy(&current_info, &info[i], sizeof(HostDBInfo));
          memcpy(&current_rr, this, sizeof(HostDBRoundRobin));
#endif
          ink_assert(!"extreme clock skew");
          info[i].app.http_data.last_failure = 0;
        }
      }
    }
  }

  if (best_up != -1) {
    ink_assert(best_up >= 0 && best_up < good);
    return &info[best_up];
  } else {
    ink_assert(best_any >= 0 && best_any < good);
    return &info[best_any];
  }
}

//
// Types
//

//
// Handles a HostDB lookup request
//
struct HostDBContinuation;
typedef int (HostDBContinuation::*HostDBContHandler) (int, void *);

struct HostDBContinuation: public Continuation
{
  Action action;
  IpEndpoint ip;
  unsigned int ttl;
  bool is_srv_lookup;
  int dns_lookup_timeout;
  INK_MD5 md5;
  Event *timeout;
  ClusterMachine *from;
  Continuation *from_cont;
  HostDBApplicationInfo app;
  int probe_depth;
  ClusterMachine *past_probes[CONFIGURATION_HISTORY_PROBE_DEPTH];
  int namelen;
  char name[MAXDNAME];
  void *m_pDS;
  Action *pending_action;

  unsigned int missing:1;
  unsigned int force_dns:1;
  unsigned int round_robin:1;

  int probeEvent(int event, Event * e);
  int clusterEvent(int event, Event * e);
  int clusterResponseEvent(int event, Event * e);
  int dnsEvent(int event, HostEnt * e);
  int dnsPendingEvent(int event, Event * e);
  int backgroundEvent(int event, Event * e);
  int retryEvent(int event, Event * e);
  int removeEvent(int event, Event * e);
  int setbyEvent(int event, Event * e);

  void do_dns();
  bool is_byname()
  {
    return ((*name && !is_srv_lookup) ? true : false);
  }
  bool is_srv()
  {
    return ((*name && is_srv_lookup) ? true : false);
  }
  HostDBInfo *lookup_done(sockaddr const* aip, char *aname, bool round_robin, unsigned int attl, SRVHosts * s = NULL);
  bool do_get_response(Event * e);
  void do_put_response(ClusterMachine * m, HostDBInfo * r, Continuation * cont);
  int failed_cluster_request(Event * e);
  int key_partition();
  void remove_trigger_pending_dns();
  int set_check_pending_dns();

  ClusterMachine *master_machine(ClusterConfiguration * cc);

  HostDBInfo *insert(unsigned int attl);

  void init(const char *hostname, int len, sockaddr const* ip, INK_MD5 & amd5,
            Continuation * cont, void *pDS = 0, bool is_srv = false, int timeout = 0);
  int make_get_message(char *buf, int len);
  int make_put_message(HostDBInfo * r, Continuation * c, char *buf, int len);

HostDBContinuation():
  Continuation(NULL), ttl(0),
    is_srv_lookup(false), dns_lookup_timeout(0),
    timeout(0), from(0),
    from_cont(0), probe_depth(0), namelen(0), missing(false), force_dns(false), round_robin(false) {
    memset(&ip, 0, sizeof ip);
    memset(name, 0, MAXDNAME);
    md5.b[0] = 0;
    md5.b[1] = 0;
    SET_HANDLER((HostDBContHandler) & HostDBContinuation::probeEvent);
  }
};

//
// Data
//

extern int hostdb_enable;
extern int hostdb_migrate_on_demand;
extern int hostdb_cluster;
extern int hostdb_cluster_round_robin;
extern int hostdb_lookup_timeout;
extern int hostdb_insert_timeout;
extern int hostdb_re_dns_on_reload;

// 0 = obey, 1 = ignore, 2 = min(X,ttl), 3 = max(X,ttl)
enum
{ TTL_OBEY, TTL_IGNORE, TTL_MIN, TTL_MAX };
extern int hostdb_ttl_mode;

extern unsigned int hostdb_current_interval;
extern unsigned int hostdb_ip_stale_interval;
extern unsigned int hostdb_ip_timeout_interval;
extern unsigned int hostdb_ip_fail_timeout_interval;
extern int hostdb_size;
extern char hostdb_filename[PATH_NAME_MAX + 1];

//extern int hostdb_timestamp;
extern int hostdb_sync_frequency;
extern int hostdb_disable_reverse_lookup;

// Static configuration information
extern HostDBCache hostDB;

//extern Queue<HostDBContinuation>  remoteHostDBQueue[MULTI_CACHE_PARTITIONS];

inline unsigned int
master_hash(INK_MD5 & md5)
{
  return (int) (md5[1] >> 32);
}

inline bool
is_dotted_form_hostname(const char *c)
{
  return -1 != (int) ink_inet_addr(c);
}

inline Queue<HostDBContinuation> &
HostDBCache::pending_dns_for_hash(INK_MD5 & md5)
{
  return pending_dns[partition_of_bucket((int) (fold_md5(md5) % hostDB.buckets))];
}

inline int
HostDBContinuation::key_partition()
{
  return hostDB.partition_of_bucket(fold_md5(md5) % hostDB.buckets);
}

#endif /* _P_HostDBProcessor_h_ */
