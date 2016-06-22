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
enum {
  TTL_OBEY,
  TTL_IGNORE,
  TTL_MIN,
  TTL_MAX,
};
extern int hostdb_ttl_mode;

extern unsigned int hostdb_current_interval;
extern unsigned int hostdb_ip_stale_interval;
extern unsigned int hostdb_ip_timeout_interval;
extern unsigned int hostdb_ip_fail_timeout_interval;
extern int hostdb_size;
extern int hostdb_srv_enabled;
extern char hostdb_filename[PATH_NAME_MAX];

// extern int hostdb_timestamp;
extern int hostdb_sync_frequency;
extern int hostdb_disable_reverse_lookup;

// Static configuration information
extern HostDBCache hostDB;

/** Host DB record mark.

    The records in the host DB are de facto segregated by roughly the
    DNS query type. We use an intermediate type to provide a little flexibility
    although the type is presumed to be a single byte.
 */
enum HostDBMark {
  HOSTDB_MARK_GENERIC, ///< Anything that's not one of the other types.
  HOSTDB_MARK_IPV4,    ///< IPv4 / T_A
  HOSTDB_MARK_IPV6,    ///< IPv6 / T_AAAA
  HOSTDB_MARK_SRV,     ///< Service / T_SRV
};
/** Convert a HostDB @a mark to a string.
    @return A static string.
 */
extern char const *string_for(HostDBMark mark);

inline unsigned int
HOSTDB_CLIENT_IP_HASH(sockaddr const *lhs, sockaddr const *rhs)
{
  unsigned int zret = ~static_cast<unsigned int>(0);
  if (ats_ip_are_compatible(lhs, rhs)) {
    if (ats_is_ip4(lhs)) {
      in_addr_t ip1 = ats_ip4_addr_cast(lhs);
      in_addr_t ip2 = ats_ip4_addr_cast(rhs);
      zret          = (ip1 >> 16) ^ ip1 ^ ip2 ^ (ip2 >> 16);
    } else if (ats_is_ip6(lhs)) {
      uint32_t const *ip1 = ats_ip_addr32_cast(lhs);
      uint32_t const *ip2 = ats_ip_addr32_cast(rhs);
      for (int i = 0; i < 4; ++i, ++ip1, ++ip2) {
        zret ^= (*ip1 >> 16) ^ *ip1 ^ *ip2 ^ (*ip2 >> 16);
      }
    }
  }
  return zret & 0xFFFF;
}

//
// Constants
//

#define HOST_DB_HITS_BITS 3
#define HOST_DB_TAG_BITS 56

#define CONFIGURATION_HISTORY_PROBE_DEPTH 1

// Bump this any time hostdb format is changed
#define HOST_DB_CACHE_MAJOR_VERSION 3
#define HOST_DB_CACHE_MINOR_VERSION 0
// 2.2: IP family split 2.1 : IPv6

#define DEFAULT_HOST_DB_FILENAME "host.db"
#define DEFAULT_HOST_DB_SIZE (1 << 14)
// Resolution of timeouts
#define HOST_DB_TIMEOUT_INTERVAL HRTIME_SECOND
// Timeout DNS every 24 hours by default if ttl_mode is enabled
#define HOST_DB_IP_TIMEOUT (24 * 60 * 60)
// DNS entries should be revalidated every 12 hours
#define HOST_DB_IP_STALE (12 * 60 * 60)
// DNS entries which failed lookup, should be revalidated every hour
#define HOST_DB_IP_FAIL_TIMEOUT (60 * 60)

//#define HOST_DB_MAX_INTERVAL                 (0x7FFFFFFF)
#define HOST_DB_MAX_TTL (0x1FFFFF) // 24 days

//
// Constants
//

// period to wait for a remote probe...
#define HOST_DB_CLUSTER_TIMEOUT HRTIME_MSECONDS(5000)
#define HOST_DB_RETRY_PERIOD HRTIME_MSECONDS(20)
#define HOST_DB_ITERATE_PERIOD HRTIME_MSECONDS(5)

//#define TEST(_x) _x
#define TEST(_x)

#ifdef _HOSTDB_CC_
template struct MultiCache<HostDBInfo>;
#endif /* _HOSTDB_CC_ */

struct ClusterMachine;
struct HostEnt;
struct ClusterConfiguration;

// Stats
enum HostDB_Stats {
  hostdb_total_entries_stat,
  hostdb_total_lookups_stat,
  hostdb_total_hits_stat,  // D == total hits
  hostdb_ttl_stat,         // D average TTL
  hostdb_ttl_expires_stat, // D == TTL Expires
  hostdb_re_dns_on_reload_stat,
  hostdb_bytes_stat,
  HostDB_Stat_Count
};

struct RecRawStatBlock;
extern RecRawStatBlock *hostdb_rsb;

// Stat Macros

#define HOSTDB_DEBUG_COUNT_DYN_STAT(_x, _y) RecIncrRawStatCount(hostdb_rsb, mutex->thread_holding, (int)_x, _y)

#define HOSTDB_INCREMENT_DYN_STAT(_x) RecIncrRawStatSum(hostdb_rsb, mutex->thread_holding, (int)_x, 1)

#define HOSTDB_DECREMENT_DYN_STAT(_x) RecIncrRawStatSum(hostdb_rsb, mutex->thread_holding, (int)_x, -1)

#define HOSTDB_SUM_DYN_STAT(_x, _r) RecIncrRawStatSum(hostdb_rsb, mutex->thread_holding, (int)_x, _r)

#define HOSTDB_READ_DYN_STAT(_x, _count, _sum)        \
  do {                                                \
    RecGetRawStatSum(hostdb_rsb, (int)_x, &_sum);     \
    RecGetRawStatCount(hostdb_rsb, (int)_x, &_count); \
  } while (0)

#define HOSTDB_SET_DYN_COUNT(_x, _count) RecSetRawStatCount(hostdb_rsb, _x, _count);

#define HOSTDB_INCREMENT_THREAD_DYN_STAT(_s, _t) RecIncrRawStatSum(hostdb_rsb, _t, (int)_s, 1);

#define HOSTDB_DECREMENT_THREAD_DYN_STAT(_s, _t) RecIncrRawStatSum(hostdb_rsb, _t, (int)_s, -1);

struct CmpConstBuffferCaseInsensitive {
  bool
  operator()(ts::ConstBuffer a, ts::ConstBuffer b) const
  {
    return ptr_len_casecmp(a._ptr, a._size, b._ptr, b._size) < 0;
  }
};

// Our own typedef for the host file mapping
typedef std::map<ts::ConstBuffer, IpAddr, CmpConstBuffferCaseInsensitive> HostsFileMap;
// A to hold a ref-counted map
struct RefCountedHostsFileMap : public RefCountObj {
  HostsFileMap hosts_file_map;
  ats_scoped_str HostFileText;
  ink_time_t next_sync_time; // time of the next sync
};

//
// HostDBCache (Private)
//
struct HostDBCache : public MultiCache<HostDBInfo> {
  int rebuild_callout(HostDBInfo *e, RebuildMC &r);
  int start(int flags = 0);
  MultiCacheBase *
  dup()
  {
    return new HostDBCache;
  }

  // This accounts for an average of 2 HostDBInfo per DNS cache (for round-robin etc.)
  // In addition, we can do a padding for additional SRV records storage.
  // In addition, we add 120 for hostname storage (since we now always do that)
  virtual size_t
  estimated_heap_bytes_per_entry() const
  {
    return sizeof(HostDBInfo) * 2 + 512 * hostdb_srv_enabled + 120;
  }

  // Map to contain all of the host file overrides, initialize it to empty
  Ptr<RefCountedHostsFileMap> hosts_file_ptr;

  Queue<HostDBContinuation, Continuation::Link_link> pending_dns[MULTI_CACHE_PARTITIONS];
  Queue<HostDBContinuation, Continuation::Link_link> &pending_dns_for_hash(INK_MD5 &md5);
  HostDBCache();
};

inline int
HostDBRoundRobin::index_of(sockaddr const *ip)
{
  bool bad = (rrcount <= 0 || rrcount > HOST_DB_MAX_ROUND_ROBIN_INFO || good <= 0 || good > HOST_DB_MAX_ROUND_ROBIN_INFO);
  if (bad) {
    ink_assert(!"bad round robin size");
    return -1;
  }

  for (int i = 0; i < good; i++) {
    if (ats_ip_addr_eq(ip, info[i].ip())) {
      return i;
    }
  }

  return -1;
}

inline HostDBInfo *
HostDBRoundRobin::find_ip(sockaddr const *ip)
{
  int idx = this->index_of(ip);
  return idx < 0 ? NULL : &info[idx];
}

inline HostDBInfo *
HostDBRoundRobin::select_next(sockaddr const *ip)
{
  HostDBInfo *zret = 0;
  if (good > 1) {
    int idx = this->index_of(ip);
    if (idx >= 0) {
      idx  = (idx + 1) % good;
      zret = &info[idx];
    }
  }
  return zret;
}

inline HostDBInfo *
HostDBRoundRobin::find_target(const char *target)
{
  bool bad = (rrcount <= 0 || rrcount > HOST_DB_MAX_ROUND_ROBIN_INFO || good <= 0 || good > HOST_DB_MAX_ROUND_ROBIN_INFO);
  if (bad) {
    ink_assert(!"bad round robin size");
    return NULL;
  }

  uint32_t key = makeHostHash(target);
  for (int i = 0; i < good; i++) {
    if (info[i].data.srv.key == key && !strcmp(target, info[i].srvname(this)))
      return &info[i];
  }
  return NULL;
}

inline HostDBInfo *
HostDBRoundRobin::select_best_http(sockaddr const *client_ip, ink_time_t now, int32_t fail_window)
{
  bool bad = (rrcount <= 0 || rrcount > HOST_DB_MAX_ROUND_ROBIN_INFO || good <= 0 || good > HOST_DB_MAX_ROUND_ROBIN_INFO);

  if (bad) {
    ink_assert(!"bad round robin size");
    return NULL;
  }

  int best_any = 0;
  int best_up  = -1;

  // Basic round robin, increment current and mod with how many we have
  if (HostDBProcessor::hostdb_strict_round_robin) {
    Debug("hostdb", "Using strict round robin");
    // Check that the host we selected is alive
    for (int i = 0; i < good; i++) {
      best_any = current++ % good;
      if (info[best_any].alive(now, fail_window)) {
        best_up = best_any;
        break;
      }
    }
  } else if (HostDBProcessor::hostdb_timed_round_robin > 0) {
    Debug("hostdb", "Using timed round-robin for HTTP");
    if ((now - timed_rr_ctime) > HostDBProcessor::hostdb_timed_round_robin) {
      Debug("hostdb", "Timed interval expired.. rotating");
      ++current;
      timed_rr_ctime = now;
    }
    for (int i = 0; i < good; i++) {
      best_any = current++ % good;
      if (info[best_any].alive(now, fail_window)) {
        best_up = best_any;
        break;
      }
    }
    Debug("hostdb", "Using %d for best_up", best_up);
  } else {
    Debug("hostdb", "Using default round robin");
    unsigned int best_hash_any = 0;
    unsigned int best_hash_up  = 0;
    sockaddr const *ip;
    for (int i = 0; i < good; i++) {
      ip             = info[i].ip();
      unsigned int h = HOSTDB_CLIENT_IP_HASH(client_ip, ip);
      if (best_hash_any <= h) {
        best_any      = i;
        best_hash_any = h;
      }
      if (info[i].alive(now, fail_window)) {
        if (best_hash_up <= h) {
          best_up      = i;
          best_hash_up = h;
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

inline HostDBInfo *
HostDBRoundRobin::select_best_srv(char *target, InkRand *rand, ink_time_t now, int32_t fail_window)
{
  bool bad = (rrcount <= 0 || rrcount > HOST_DB_MAX_ROUND_ROBIN_INFO || good <= 0 || good > HOST_DB_MAX_ROUND_ROBIN_INFO);

  if (bad) {
    ink_assert(!"bad round robin size");
    return NULL;
  }

#ifdef DEBUG
  for (int i = 1; i < good; ++i) {
    ink_assert(info[i].data.srv.srv_priority >= info[i - 1].data.srv.srv_priority);
  }
#endif

  int i = 0, len = 0;
  uint32_t weight = 0, p = INT32_MAX;
  HostDBInfo *result = NULL;
  HostDBInfo *infos[HOST_DB_MAX_ROUND_ROBIN_INFO];

  do {
    if (info[i].app.http_data.last_failure != 0 && (uint32_t)(now - fail_window) < info[i].app.http_data.last_failure) {
      continue;
    }

    if (info[i].app.http_data.last_failure)
      info[i].app.http_data.last_failure = 0;

    if (info[i].data.srv.srv_priority <= p) {
      p = info[i].data.srv.srv_priority;
      weight += info[i].data.srv.srv_weight;
      infos[len++] = &info[i];
    } else
      break;
  } while (++i < good);

  if (len == 0) { // all failed
    result = &info[current++ % good];
  } else if (weight == 0) { // srv weight is 0
    result = &info[current++ % len];
  } else {
    uint32_t xx = rand->random() % weight;
    for (i = 0; i < len && xx >= infos[i]->data.srv.srv_weight; ++i)
      xx -= infos[i]->data.srv.srv_weight;

    result = infos[i];
  }

  if (result) {
    strcpy(target, result->srvname(this));
    return result;
  }
  return NULL;
}

//
// Types
//

/** Container for an MD5 hash and its dependent data.
    This handles both the host name and raw address cases.
*/
struct HostDBMD5 {
  typedef HostDBMD5 self; ///< Self reference type.

  INK_MD5 hash; ///< The hash value.

  char const *host_name; ///< Host name.
  int host_len;          ///< Length of @a _host_name
  IpAddr ip;             ///< IP address.
  in_port_t port;        ///< IP port (host order).
  /// DNS server. Not strictly part of the MD5 data but
  /// it's both used by @c HostDBContinuation and provides access to
  /// MD5 data. It's just handier to store it here for both uses.
  DNSServer *dns_server;
  SplitDNS *pSD;      ///< Hold the container for @a dns_server.
  HostDBMark db_mark; ///< Mark / type of record.

  /// Default constructor.
  HostDBMD5();
  /// Destructor.
  ~HostDBMD5();
  /// Recompute and update the MD5 hash.
  void refresh();
  /** Assign a hostname.
      This updates the split DNS data as well.
  */
  self &set_host(char const *name, int len);
};

//
// Handles a HostDB lookup request
//
struct HostDBContinuation;
typedef int (HostDBContinuation::*HostDBContHandler)(int, void *);

struct HostDBContinuation : public Continuation {
  Action action;
  HostDBMD5 md5;
  //  IpEndpoint ip;
  unsigned int ttl;
  //  HostDBMark db_mark; ///< Target type.
  /// Original IP address family style. Note this will disagree with
  /// @a md5.db_mark when doing a retry on an alternate family. The retry
  /// logic depends on it to avoid looping.
  HostResStyle host_res_style; ///< Address family priority.
  int dns_lookup_timeout;
  //  INK_MD5 md5;
  Event *timeout;
  ClusterMachine *from;
  Continuation *from_cont;
  HostDBApplicationInfo app;
  int probe_depth;
  int current_iterate_pos;
  ClusterMachine *past_probes[CONFIGURATION_HISTORY_PROBE_DEPTH];
  //  char name[MAXDNAME];
  //  int namelen;
  char md5_host_name_store[MAXDNAME + 1]; // used as backing store for @a md5
  char srv_target_name[MAXDNAME];
  //  void *m_pDS;
  Action *pending_action;

  unsigned int missing : 1;
  unsigned int force_dns : 1;
  unsigned int round_robin : 1;

  int probeEvent(int event, Event *e);
  int iterateEvent(int event, Event *e);
  int clusterEvent(int event, Event *e);
  int clusterResponseEvent(int event, Event *e);
  int dnsEvent(int event, HostEnt *e);
  int dnsPendingEvent(int event, Event *e);
  int backgroundEvent(int event, Event *e);
  int retryEvent(int event, Event *e);
  int removeEvent(int event, Event *e);
  int setbyEvent(int event, Event *e);

  /// Recompute the MD5 and update ancillary values.
  void refresh_MD5();
  void do_dns();
  bool
  is_byname()
  {
    return md5.db_mark == HOSTDB_MARK_IPV4 || md5.db_mark == HOSTDB_MARK_IPV6;
  }
  bool
  is_srv()
  {
    return md5.db_mark == HOSTDB_MARK_SRV;
  }
  HostDBInfo *lookup_done(IpAddr const &ip, char const *aname, bool round_robin, unsigned int attl, SRVHosts *s = NULL);
  bool do_get_response(Event *e);
  void do_put_response(ClusterMachine *m, HostDBInfo *r, Continuation *cont);
  int failed_cluster_request(Event *e);
  int key_partition();
  void remove_trigger_pending_dns();
  int set_check_pending_dns();

  ClusterMachine *master_machine(ClusterConfiguration *cc);

  HostDBInfo *insert(unsigned int attl);

  /** Optional values for @c init.
   */
  struct Options {
    typedef Options self; ///< Self reference type.

    int timeout;                 ///< Timeout value. Default 0
    HostResStyle host_res_style; ///< IP address family fallback. Default @c HOST_RES_NONE
    bool force_dns;              ///< Force DNS lookup. Default @c false
    Continuation *cont;          ///< Continuation / action. Default @c NULL (none)

    Options() : timeout(0), host_res_style(HOST_RES_NONE), force_dns(false), cont(0) {}
  };
  static const Options DEFAULT_OPTIONS; ///< Default defaults.
  void init(HostDBMD5 const &md5, Options const &opt = DEFAULT_OPTIONS);
  int make_get_message(char *buf, int len);
  int make_put_message(HostDBInfo *r, Continuation *c, char *buf, int len);

  HostDBContinuation()
    : Continuation(NULL),
      ttl(0),
      host_res_style(DEFAULT_OPTIONS.host_res_style),
      dns_lookup_timeout(DEFAULT_OPTIONS.timeout),
      timeout(0),
      from(0),
      from_cont(0),
      probe_depth(0),
      current_iterate_pos(0),
      missing(false),
      force_dns(DEFAULT_OPTIONS.force_dns),
      round_robin(false)
  {
    ink_zero(md5_host_name_store);
    ink_zero(md5.hash);
    SET_HANDLER((HostDBContHandler)&HostDBContinuation::probeEvent);
  }
};

// extern Queue<HostDBContinuation>  remoteHostDBQueue[MULTI_CACHE_PARTITIONS];

inline unsigned int
master_hash(INK_MD5 const &md5)
{
  return static_cast<int>(md5[1] >> 32);
}

inline bool
is_dotted_form_hostname(const char *c)
{
  return -1 != (int)ink_inet_addr(c);
}

inline Queue<HostDBContinuation> &
HostDBCache::pending_dns_for_hash(INK_MD5 &md5)
{
  return pending_dns[partition_of_bucket((int)(fold_md5(md5) % hostDB.buckets))];
}

inline int
HostDBContinuation::key_partition()
{
  return hostDB.partition_of_bucket(fold_md5(md5.hash) % hostDB.buckets);
}

#endif /* _P_HostDBProcessor_h_ */
