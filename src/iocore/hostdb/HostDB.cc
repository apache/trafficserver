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

#include "swoc/swoc_file.h"
#include "tsutil/ts_bw_format.h"

#include "P_HostDB.h"
#include "tscore/Layout.h"
#include "tscore/ink_apidefs.h"
#include "tscore/MgmtDefs.h" // MgmtInt, MgmtFloat, etc
#include "iocore/hostdb/HostFile.h"

#include <utility>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <shared_mutex>

using std::chrono::duration_cast;
using swoc::round_down;
using swoc::round_up;
using swoc::TextView;

HostDBProcessor                   hostDBProcessor;
int                               HostDBProcessor::hostdb_strict_round_robin = 0;
int                               HostDBProcessor::hostdb_timed_round_robin  = 0;
HostDBProcessor::Options const    HostDBProcessor::DEFAULT_OPTIONS;
HostDBContinuation::Options const HostDBContinuation::DEFAULT_OPTIONS;
int                               hostdb_enable                     = true;
int                               hostdb_migrate_on_demand          = true;
int                               hostdb_lookup_timeout             = 30;
int                               hostdb_re_dns_on_reload           = false;
int                               hostdb_ttl_mode                   = TTL_OBEY;
unsigned int                      hostdb_round_robin_max_count      = 16;
unsigned int                      hostdb_ip_stale_interval          = HOST_DB_IP_STALE;
unsigned int                      hostdb_ip_timeout_interval        = HOST_DB_IP_TIMEOUT;
unsigned int                      hostdb_ip_fail_timeout_interval   = HOST_DB_IP_FAIL_TIMEOUT;
unsigned int                      hostdb_serve_stale_but_revalidate = 0;
static ts_seconds                 hostdb_hostfile_check_interval{std::chrono::hours(24)};
// Epoch timestamp of the current hosts file check. This also functions as a
// cached version of ts_clock::now().
std::atomic<ts_time> hostdb_current_timestamp{TS_TIME_ZERO};
// Epoch timestamp of the last time we actually checked for a hosts file update.
static ts_time hostdb_last_timestamp{TS_TIME_ZERO};
// Epoch timestamp when we updated the hosts file last.
static ts_time          hostdb_hostfile_update_timestamp{TS_TIME_ZERO};
int                     hostdb_max_count = DEFAULT_HOST_DB_SIZE;
static swoc::file::path hostdb_hostfile_path;
int                     hostdb_disable_reverse_lookup = 0;
int                     hostdb_max_iobuf_index        = BUFFER_SIZE_INDEX_32K;

ClassAllocator<HostDBContinuation> hostDBContAllocator("hostDBContAllocator");

namespace
{
DbgCtl dbg_ctl_hostdb{"hostdb"};
DbgCtl dbg_ctl_dns_srv{"dns_srv"};

unsigned int
HOSTDB_CLIENT_IP_HASH(sockaddr const *lhs, IpAddr const &rhs)
{
  unsigned int zret = ~static_cast<unsigned int>(0);
  if (lhs->sa_family == rhs.family()) {
    if (rhs.isIp4()) {
      in_addr_t ip1 = ats_ip4_addr_cast(lhs);
      in_addr_t ip2 = rhs._addr._ip4;
      zret          = (ip1 >> 16) ^ ip1 ^ ip2 ^ (ip2 >> 16);
    } else if (rhs.isIp6()) {
      uint32_t const *ip1 = ats_ip_addr32_cast(lhs);
      uint32_t const *ip2 = rhs._addr._u32;
      for (int i = 0; i < 4; ++i, ++ip1, ++ip2) {
        zret ^= (*ip1 >> 16) ^ *ip1 ^ *ip2 ^ (*ip2 >> 16);
      }
    }
  }
  return zret & 0xFFFF;
}

} // namespace

char const *
name_of(HostDBType t)
{
  switch (t) {
  case HostDBType::UNSPEC:
    return "*";
  case HostDBType::ADDR:
    return "Address";
  case HostDBType::SRV:
    return "SRV";
  case HostDBType::HOST:
    return "Reverse DNS";
  }
  return "";
}
// Static configuration information

HostDBCache hostDB;

void UpdateHostsFile(swoc::file::path const &path, ts_seconds interval);

static inline bool
is_addr_valid(uint8_t af, ///< Address family (format of data)
              void   *ptr ///< Raw address data (not a sockaddr variant!)
)
{
  return (AF_INET == af && INADDR_ANY != *(reinterpret_cast<in_addr_t *>(ptr))) ||
         (AF_INET6 == af && !IN6_IS_ADDR_UNSPECIFIED(reinterpret_cast<in6_addr *>(ptr)));
}

inline void
hostdb_cont_free(HostDBContinuation *cont)
{
  if (cont->timeout) {
    cont->timeout->cancel();
    cont->timeout = nullptr;
  }
  cont->mutex        = nullptr;
  cont->action.mutex = nullptr;
  hostDBContAllocator.free(cont);
}

/* Check whether a resolution fail should lead to a retry.
   The @a mark argument is updated if appropriate.
   @return @c true if @a mark was updated, @c false if no retry should be done.
*/
static inline bool
check_for_retry(HostDBMark &mark, HostResStyle style)
{
  bool zret = true;
  if (HOSTDB_MARK_IPV4 == mark && HOST_RES_IPV4 == style) {
    mark = HOSTDB_MARK_IPV6;
  } else if (HOSTDB_MARK_IPV6 == mark && HOST_RES_IPV6 == style) {
    mark = HOSTDB_MARK_IPV4;
  } else {
    zret = false;
  }
  return zret;
}

const char *
string_for(HostDBMark mark)
{
  static const char *STRING[] = {"Generic", "IPv4", "IPv6", "SRV"};
  return STRING[mark];
}

//
// Function Prototypes
//
HostDBHash &
HostDBHash::set_host(TextView name)
{
  host_name = name;

  if (!host_name.empty() && SplitDNSConfig::isSplitDNSEnabled()) {
    if (TS_SUCCESS != ip.load(host_name)) {
      // config is released in the destructor, because we must make sure values we
      // get out of it don't evaporate while @a this is still around.
      if (!pSD) {
        pSD = SplitDNSConfig::acquire();
      }
      if (pSD) {
        dns_server = static_cast<DNSServer *>(pSD->getDNSRecord(host_name));
      }
    } else {
      dns_server = nullptr;
    }
  }

  return *this;
}

void
HostDBHash::refresh()
{
  CryptoContext ctx;

  if (host_name) {
    const char *server_line = dns_server ? dns_server->x_dns_ip_line : nullptr;
    uint8_t     m           = static_cast<uint8_t>(db_mark); // be sure of the type.

    ctx.update(host_name.data(), host_name.size());
    ctx.update(reinterpret_cast<uint8_t *>(&port), sizeof(port));
    ctx.update(&m, sizeof(m));
    if (server_line) {
      ctx.update(server_line, strlen(server_line));
    }
  } else {
    // CryptoHash the ip, pad on both sizes with 0's
    // so that it does not intersect the string space
    //
    char buff[TS_IP6_SIZE + 4];
    int  n = ip.isIp6() ? sizeof(in6_addr) : sizeof(in_addr_t);
    memset(buff, 0, 2);
    memcpy(buff + 2, ip._addr._byte, n);
    memset(buff + 2 + n, 0, 2);
    ctx.update(buff, n + 4);
  }
  ctx.finalize(hash);
}

HostDBHash::~HostDBHash()
{
  if (pSD) {
    SplitDNSConfig::release(pSD);
  }
}

HostDBCache::HostDBCache() {}

bool
HostDBCache::is_pending_dns_for_hash(const CryptoHash &hash)
{
  ts::shared_mutex                  &bucket_lock = hostDB.refcountcache->lock_for_key(hash.fold());
  std::shared_lock<ts::shared_mutex> lock{bucket_lock};
  bool                               retval = false;
  Queue<HostDBContinuation>         &q      = pending_dns_for_hash(hash);
  for (HostDBContinuation *c = q.head; c; c = static_cast<HostDBContinuation *>(c->link.next)) {
    if (hash == c->hash.hash) {
      retval = true;
      break;
    }
  }
  return retval;
}

bool
HostDBCache::remove_from_pending_dns_for_hash(const CryptoHash &hash, HostDBContinuation *c)
{
  bool                               retval      = false;
  ts::shared_mutex                  &bucket_lock = hostDB.refcountcache->lock_for_key(hash.fold());
  std::unique_lock<ts::shared_mutex> lock{bucket_lock};
  Queue<HostDBContinuation>         &q = pending_dns_for_hash(hash);
  if (q.in(c)) {
    q.remove(c);
    retval = true;
  }
  return retval;
}

std::shared_ptr<HostFile>
HostDBCache::acquire_host_file()
{
  std::shared_lock lock(host_file_mutex);
  auto             zret = host_file;
  return zret;
}

HostDBCache *
HostDBProcessor::cache()
{
  return &hostDB;
}

struct HostDBBackgroundTask : public Continuation {
  ts_seconds frequency;
  ts_hr_time start_time;

  virtual int sync_event(int event, void *edata) = 0;
  int         wait_event(int event, void *edata);

  HostDBBackgroundTask(ts_seconds frequency);
};

HostDBBackgroundTask::HostDBBackgroundTask(ts_seconds frequency) : Continuation(new_ProxyMutex()), frequency(frequency)
{
  SET_HANDLER(&HostDBBackgroundTask::sync_event);
}

int
HostDBCache::start(int flags)
{
  (void)flags; // unused
  MgmtInt hostdb_max_size   = 0;
  int     hostdb_partitions = 64;

  // Read configuration
  // Command line overrides manager configuration.
  //
  REC_ReadConfigInt32(hostdb_enable, "proxy.config.hostdb.enabled");

  // Max number of items
  REC_ReadConfigInt32(hostdb_max_count, "proxy.config.hostdb.max_count");
  // max size allowed to use
  REC_ReadConfigInteger(hostdb_max_size, "proxy.config.hostdb.max_size");
  // number of partitions
  REC_ReadConfigInt32(hostdb_partitions, "proxy.config.hostdb.partitions");

  REC_EstablishStaticConfigInt32(hostdb_max_iobuf_index, "proxy.config.hostdb.io.max_buffer_index");

  if (hostdb_max_size == 0) {
    Fatal("proxy.config.hostdb.max_size must be a non-zero number");
  }

  // Setup the ref-counted cache (this must be done regardless of syncing or not).
  this->refcountcache = new RefCountCache<HostDBRecord>(hostdb_partitions, hostdb_max_size, hostdb_max_count, HostDBRecord::Version,
                                                        "proxy.process.hostdb.cache.");
  this->pending_dns   = new Queue<HostDBContinuation, Continuation::Link_link>[hostdb_partitions];
  this->remoteHostDBQueue = new Queue<HostDBContinuation, Continuation::Link_link>[hostdb_partitions];
  return 0;
}

int
HostDBProcessor::clear_and_start(int, size_t)
{
  if (hostDB.start(0) < 0) {
    return -1;
  }

  hostDB.refcountcache->clear();
  return init();
}

// Start up the Host Database processor.
// Load configuration, register configuration and statistics and
// open the cache. This doesn't create any threads, so those
// parameters are ignored.
//
int
HostDBProcessor::start(int, size_t)
{
  if (hostDB.start(0) < 0) {
    return -1;
  }

  return init();
}

int
HostDBProcessor::init()
{
  //
  // Register configuration callback, and establish configuration links
  //
  REC_EstablishStaticConfigInt32(hostdb_ttl_mode, "proxy.config.hostdb.ttl_mode");
  REC_EstablishStaticConfigInt32(hostdb_disable_reverse_lookup, "proxy.config.cache.hostdb.disable_reverse_lookup");
  REC_EstablishStaticConfigInt32(hostdb_re_dns_on_reload, "proxy.config.hostdb.re_dns_on_reload");
  REC_EstablishStaticConfigInt32(hostdb_migrate_on_demand, "proxy.config.hostdb.migrate_on_demand");
  REC_EstablishStaticConfigInt32(hostdb_strict_round_robin, "proxy.config.hostdb.strict_round_robin");
  REC_EstablishStaticConfigInt32(hostdb_timed_round_robin, "proxy.config.hostdb.timed_round_robin");
  REC_EstablishStaticConfigInt32(hostdb_lookup_timeout, "proxy.config.hostdb.lookup_timeout");
  REC_EstablishStaticConfigInt32U(hostdb_ip_timeout_interval, "proxy.config.hostdb.timeout");
  REC_EstablishStaticConfigInt32U(hostdb_ip_stale_interval, "proxy.config.hostdb.verify_after");
  REC_EstablishStaticConfigInt32U(hostdb_ip_fail_timeout_interval, "proxy.config.hostdb.fail.timeout");
  REC_EstablishStaticConfigInt32U(hostdb_serve_stale_but_revalidate, "proxy.config.hostdb.serve_stale_for");
  REC_EstablishStaticConfigInt32U(hostdb_round_robin_max_count, "proxy.config.hostdb.round_robin_max_count");
  const char *interval_config = "proxy.config.hostdb.host_file.interval";
  {
    RecInt tmp_interval{};

    REC_ReadConfigInteger(tmp_interval, interval_config);
    hostdb_hostfile_check_interval = std::chrono::seconds(tmp_interval);
  }
  RecRegisterConfigUpdateCb(
    interval_config,
    [&](const char *, RecDataT, RecData data, void *) {
      hostdb_hostfile_check_interval = std::chrono::seconds(data.rec_int);

      return REC_ERR_OKAY;
    },
    nullptr);

  //
  // Initialize hostdb_current_timestamp which is our cached version of
  // ts_clock::now().
  //
  hostdb_current_timestamp = ts_clock::now();

  HostDBContinuation *b = hostDBContAllocator.alloc();
  SET_CONTINUATION_HANDLER(b, &HostDBContinuation::backgroundEvent);
  b->mutex = new_ProxyMutex();
  eventProcessor.schedule_every(b, HRTIME_SECONDS(1), ET_DNS);

  return 0;
}

void
HostDBContinuation::init(HostDBHash const &the_hash, Options const &opt)
{
  hash           = the_hash;
  hash.host_name = hash.host_name.prefix(static_cast<int>(sizeof(hash_host_name_store) - 1));
  if (!hash.host_name.empty()) {
    // copy to backing store.
    memcpy(hash_host_name_store, hash.host_name);
  }
  hash_host_name_store[hash.host_name.size()] = 0;
  hash.host_name.assign(hash_host_name_store, hash.host_name.size());

  host_res_style     = opt.host_res_style;
  dns_lookup_timeout = opt.timeout;
  mutex              = new_ProxyMutex();
  timeout            = nullptr;
  if (opt.cont) {
    action = opt.cont;
  } else {
    // ink_assert(!"this sucks");
    ink_zero(action);
    action.mutex = mutex;
  }
}

void
HostDBContinuation::refresh_hash()
{
  // We're not pending DNS anymore.
  remove_and_trigger_pending_dns();
  hash.refresh();
}

static bool
reply_to_cont(Continuation *cont, HostDBRecord *r, bool is_srv = false)
{
  if (r == nullptr || r->is_srv() != is_srv || r->is_failed()) {
    cont->handleEvent(is_srv ? EVENT_SRV_LOOKUP : EVENT_HOST_DB_LOOKUP, nullptr);
    return false;
  }

  if (r->record_type != HostDBType::HOST) {
    if (!r->name()) {
      ink_assert(!"missing hostname");
      cont->handleEvent(is_srv ? EVENT_SRV_LOOKUP : EVENT_HOST_DB_LOOKUP, nullptr);
      Warning("bogus entry deleted from HostDB: missing hostname");
      hostDB.refcountcache->erase(r->key);
      return false;
    }
    Dbg(dbg_ctl_hostdb, "hostname = %s", r->name());
  }

  cont->handleEvent(is_srv ? EVENT_SRV_LOOKUP : EVENT_HOST_DB_LOOKUP, r);

  return true;
}

inline HostResStyle
host_res_style_for(sockaddr const *ip)
{
  return ats_is_ip6(ip) ? HOST_RES_IPV6_ONLY : HOST_RES_IPV4_ONLY;
}

inline HostResStyle
host_res_style_for(HostDBMark mark)
{
  return HOSTDB_MARK_IPV4 == mark ? HOST_RES_IPV4_ONLY : HOSTDB_MARK_IPV6 == mark ? HOST_RES_IPV6_ONLY : HOST_RES_NONE;
}

inline HostDBMark
db_mark_for(HostResStyle style)
{
  HostDBMark zret = HOSTDB_MARK_GENERIC;
  if (HOST_RES_IPV4 == style || HOST_RES_IPV4_ONLY == style) {
    zret = HOSTDB_MARK_IPV4;
  } else if (HOST_RES_IPV6 == style || HOST_RES_IPV6_ONLY == style) {
    zret = HOSTDB_MARK_IPV6;
  }
  return zret;
}

inline HostDBMark
db_mark_for(sockaddr const *ip)
{
  return ats_is_ip6(ip) ? HOSTDB_MARK_IPV6 : HOSTDB_MARK_IPV4;
}

inline HostDBMark
db_mark_for(IpAddr const &ip)
{
  return ip.isIp6() ? HOSTDB_MARK_IPV6 : HOSTDB_MARK_IPV4;
}

HostDBRecord::Handle
probe_ip(HostDBHash const &hash)
{
  HostDBRecord::Handle result;

  if (hash.is_byname()) {
    Dbg(dbg_ctl_hostdb, "DNS %.*s", int(hash.host_name.size()), hash.host_name.data());
    IpAddr tip;
    if (0 == tip.load(hash.host_name)) {
      result            = HostDBRecord::Handle{HostDBRecord::alloc(hash.host_name, 1)};
      result->af_family = tip.family();
      auto &info        = result->rr_info()[0];
      info.assign(tip);
    }
  }

  return result;
}

HostDBRecord::Handle
probe_hostfile(HostDBHash const &hash)
{
  HostDBRecord::Handle result;
  // Check if this can be fulfilled by the host file
  //
  if (auto static_hosts = hostDB.acquire_host_file(); static_hosts) {
    result = static_hosts->lookup(hash);
  }

  return result;
}

HostDBRecord::Handle
probe(HostDBHash const &hash, bool ignore_timeout)
{
  static const Ptr<HostDBRecord> NO_RECORD;

  // If hostdb is disabled, don't return anything
  if (!hostdb_enable) {
    return NO_RECORD;
  }

  // Otherwise HostDB is enabled, so we'll do our thing
  uint64_t          folded_hash = hash.hash.fold();
  ts::shared_mutex &bucket_lock = hostDB.refcountcache->lock_for_key(folded_hash);

  Ptr<HostDBRecord> record;
  {
    std::shared_lock<ts::shared_mutex> lock{bucket_lock};

    // get the record from cache
    record = hostDB.refcountcache->get(folded_hash);
    // If there was nothing in the cache-- this is a miss
    if (record.get() == nullptr) {
      record = probe_ip(hash);
      if (!record) {
        record = probe_hostfile(hash);
      }
      return record;
    }

    // If the dns response was failed, and we've hit the failed timeout, lets stop returning it
    if (record->is_failed() && record->is_ip_fail_timeout()) {
      return NO_RECORD;
      // if we aren't ignoring timeouts, and we are past it-- then remove the record
    } else if (!ignore_timeout && record->is_ip_timeout() && !record->serve_stale_but_revalidate()) {
      Metrics::Counter::increment(hostdb_rsb.ttl_expires);
      return NO_RECORD;
    }
  }

  // If the record is stale, but we want to revalidate-- lets start that up
  if ((!ignore_timeout && record->is_ip_configured_stale() && record->record_type != HostDBType::HOST) ||
      (record->is_ip_timeout() && record->serve_stale_but_revalidate())) {
    Metrics::Counter::increment(hostdb_rsb.total_serve_stale);
    if (hostDB.is_pending_dns_for_hash(hash.hash)) {
      Dbg(dbg_ctl_hostdb, "%s",
          swoc::bwprint(ts::bw_dbg, "stale {} {} {}, using with pending refresh", record->ip_age(),
                        record->ip_timestamp.time_since_epoch(), record->ip_timeout_interval)
            .c_str());
      return record;
    }
    Dbg(dbg_ctl_hostdb, "%s",
        swoc::bwprint(ts::bw_dbg, "stale {} {} {}, using while refresh", record->ip_age(), record->ip_timestamp.time_since_epoch(),
                      record->ip_timeout_interval)
          .c_str());
    HostDBContinuation         *c = hostDBContAllocator.alloc();
    HostDBContinuation::Options copt;
    copt.host_res_style = record->af_family == AF_INET6 ? HOST_RES_IPV6_ONLY : HOST_RES_IPV4_ONLY;
    c->init(hash, copt);
    SCOPED_MUTEX_LOCK(lock, c->mutex, this_ethread());
    c->do_dns();
  }
  return record;
}

//
// Get an entry by either name or IP
//
Action *
HostDBProcessor::getby(Continuation *cont, cb_process_result_pfn cb_process_result, HostDBHash &hash, Options const &opt)
{
  bool            force_dns        = false;
  bool            delay_reschedule = false;
  EThread        *thread           = this_ethread();
  Ptr<ProxyMutex> mutex            = thread->mutex;
  ip_text_buffer  ipb;

  if (opt.flags & HOSTDB_FORCE_DNS_ALWAYS) {
    force_dns = true;
  } else if (opt.flags & HOSTDB_FORCE_DNS_RELOAD) {
    force_dns = hostdb_re_dns_on_reload;
    if (force_dns) {
      Metrics::Counter::increment(hostdb_rsb.re_dns_on_reload);
    }
  }

  Metrics::Counter::increment(hostdb_rsb.total_lookups);

  if (!hostdb_enable ||                                       // if the HostDB is disabled,
      (hash.host_name && !*hash.host_name) ||                 // or host_name is empty string
      (hostdb_disable_reverse_lookup && hash.ip.isValid())) { // or try to lookup by ip address when the reverse lookup disabled
    if (cb_process_result) {
      (cont->*cb_process_result)(nullptr);
    } else {
      MUTEX_TRY_LOCK(lock, cont->mutex, thread);
      if (!lock.is_locked()) {
        delay_reschedule = true;
        goto Lretry;
      }
      cont->handleEvent(EVENT_HOST_DB_LOOKUP, nullptr);
    }
    return ACTION_RESULT_DONE;
  }

  // Attempt to find the result in-line, for level 1 hits
  if (!force_dns) {
    MUTEX_TRY_LOCK(lock, cont->mutex, thread);
    bool loop = lock.is_locked();
    while (loop) {
      loop = false; // Only loop on explicit set for retry.
      // find the partition lock
      ts::shared_mutex &bucket_lock = hostDB.refcountcache->lock_for_key(hash.hash.fold());

      // If we can get the lock and a level 1 probe succeeds, return
      HostDBRecord::Handle               r = probe(hash, false);
      std::shared_lock<ts::shared_mutex> lock{bucket_lock};
      if (r) {
        // fail, see if we should retry with alternate
        if (hash.db_mark != HOSTDB_MARK_SRV && r->is_failed() && hash.host_name) {
          loop = check_for_retry(hash.db_mark, opt.host_res_style);
        }
        if (!loop) {
          // No retry -> final result. Return it.
          if (hash.db_mark == HOSTDB_MARK_SRV) {
            Dbg(dbg_ctl_hostdb, "immediate SRV answer for %.*s from hostdb", int(hash.host_name.size()), hash.host_name.data());
            Dbg(dbg_ctl_dns_srv, "immediate SRV answer for %.*s from hostdb", int(hash.host_name.size()), hash.host_name.data());
          } else if (hash.host_name) {
            Dbg(dbg_ctl_hostdb, "immediate answer for %.*s", int(hash.host_name.size()), hash.host_name.data());
          } else {
            Dbg(dbg_ctl_hostdb, "immediate answer for %s", hash.ip.isValid() ? hash.ip.toString(ipb, sizeof ipb) : "<null>");
          }
          Metrics::Counter::increment(hostdb_rsb.total_hits);
          if (cb_process_result) {
            (cont->*cb_process_result)(r.get());
          } else {
            reply_to_cont(cont, r.get());
          }
          return ACTION_RESULT_DONE;
        }
        hash.refresh(); // only on reloop, because we've changed the family.
      }
    }
  }
  if (hash.db_mark == HOSTDB_MARK_SRV) {
    Dbg(dbg_ctl_hostdb, "delaying (force=%d) SRV answer for %.*s [timeout = %d]", force_dns, int(hash.host_name.size()),
        hash.host_name.data(), opt.timeout);
    Dbg(dbg_ctl_dns_srv, "delaying (force=%d) SRV answer for %.*s [timeout = %d]", force_dns, int(hash.host_name.size()),
        hash.host_name.data(), opt.timeout);
  } else if (hash.host_name) {
    Dbg(dbg_ctl_hostdb, "delaying (force=%d) answer for %.*s [timeout %d]", force_dns, int(hash.host_name.size()),
        hash.host_name.data(), opt.timeout);
  } else {
    Dbg(dbg_ctl_hostdb, "delaying (force=%d) answer for %s [timeout %d]", force_dns,
        hash.ip.isValid() ? hash.ip.toString(ipb, sizeof ipb) : "<null>", opt.timeout);
  }

Lretry:
  // Otherwise, create a continuation to do a deeper probe in the background
  //
  HostDBContinuation         *c = hostDBContAllocator.alloc();
  HostDBContinuation::Options copt;
  copt.timeout        = opt.timeout;
  copt.force_dns      = force_dns;
  copt.cont           = cont;
  copt.host_res_style = (hash.db_mark == HOSTDB_MARK_SRV) ? HOST_RES_NONE : opt.host_res_style;
  c->init(hash, copt);
  SET_CONTINUATION_HANDLER(c, &HostDBContinuation::probeEvent);

  if (delay_reschedule) {
    c->timeout = thread->schedule_in(c, MUTEX_RETRY_DELAY);
  } else {
    thread->schedule_imm(c);
  }

  return &c->action;
}

// Wrapper from getbyname to getby
//
Action *
HostDBProcessor::getbyname_re(Continuation *cont, const char *ahostname, int len, Options const &opt)
{
  HostDBHash hash;

  ink_assert(nullptr != ahostname);

  // Load the hash data.
  hash.set_host({ahostname, ahostname ? (len ? len : strlen(ahostname)) : 0});
  // Leave hash.ip invalid
  hash.port    = 0;
  hash.db_mark = db_mark_for(opt.host_res_style);
  hash.refresh();

  return getby(cont, nullptr, hash, opt);
}

Action *
HostDBProcessor::getbynameport_re(Continuation *cont, const char *ahostname, int len, Options const &opt)
{
  HostDBHash hash;

  ink_assert(nullptr != ahostname);

  // Load the hash data.
  hash.set_host({ahostname, ahostname ? (len ? len : strlen(ahostname)) : 0});
  // Leave hash.ip invalid
  hash.port    = opt.port;
  hash.db_mark = db_mark_for(opt.host_res_style);
  hash.refresh();

  return getby(cont, nullptr, hash, opt);
}

// Lookup Hostinfo by addr
Action *
HostDBProcessor::getbyaddr_re(Continuation *cont, sockaddr const *aip)
{
  HostDBHash hash;

  ink_assert(nullptr != aip);

  HostDBProcessor::Options opt;
  opt.host_res_style = HOST_RES_NONE;

  // Leave hash.host_name as nullptr
  hash.ip.assign(aip);
  hash.port    = ats_ip_port_host_order(aip);
  hash.db_mark = db_mark_for(opt.host_res_style);
  hash.refresh();

  return getby(cont, nullptr, hash, opt);
}

/* Support SRV records */
Action *
HostDBProcessor::getSRVbyname_imm(Continuation *cont, cb_process_result_pfn process_srv_info, const char *hostname, int len,
                                  Options const &opt)
{
  ink_assert(cont->mutex->thread_holding == this_ethread());
  HostDBHash hash;

  ink_assert(nullptr != hostname);

  hash.set_host({hostname, len ? len : strlen(hostname)});
  // Leave hash.ip invalid
  hash.port    = 0;
  hash.db_mark = HOSTDB_MARK_SRV;
  hash.refresh();

  return getby(cont, process_srv_info, hash, opt);
}

// Wrapper from getbyname to getby
//
Action *
HostDBProcessor::getbyname_imm(Continuation *cont, cb_process_result_pfn process_hostdb_info, const char *hostname, int len,
                               Options const &opt)
{
  ink_assert(cont->mutex->thread_holding == this_ethread());
  HostDBHash hash;

  ink_assert(nullptr != hostname);

  hash.set_host({hostname, len ? len : strlen(hostname)});
  // Leave hash.ip invalid
  // TODO: May I rename the wrapper name to getbynameport_imm ? - oknet
  //   By comparing getbyname_re and getbynameport_re, the hash.port should be 0 if only get hostinfo by name.
  hash.port    = opt.port;
  hash.db_mark = db_mark_for(opt.host_res_style);
  hash.refresh();

  return getby(cont, process_hostdb_info, hash, opt);
}

Action *
HostDBProcessor::iterate(Continuation *cont)
{
  ink_assert(cont->mutex->thread_holding == this_ethread());
  EThread *thread = cont->mutex->thread_holding;

  Metrics::Counter::increment(hostdb_rsb.total_lookups);

  HostDBContinuation         *c = hostDBContAllocator.alloc();
  HostDBContinuation::Options copt;
  copt.cont           = cont;
  copt.force_dns      = false;
  copt.timeout        = 0;
  copt.host_res_style = HOST_RES_NONE;
  c->init(HostDBHash(), copt);
  c->current_iterate_pos = 0;
  SET_CONTINUATION_HANDLER(c, &HostDBContinuation::iterateEvent);

  thread->schedule_in(c, HOST_DB_RETRY_PERIOD);

  return &c->action;
}

// Lookup done, insert into the local table, return data to the
// calling continuation.
// NOTE: if "i" exists it means we already allocated the space etc, just return
//
Ptr<HostDBRecord>
HostDBContinuation::lookup_done(TextView query_name, ts_seconds answer_ttl, SRVHosts *srv, Ptr<HostDBRecord> record)
{
  ink_assert(record);
  if (query_name.empty()) {
    if (hash.is_byname()) {
      Dbg(dbg_ctl_hostdb, "lookup_done() failed for '%.*s'", int(hash.host_name.size()), hash.host_name.data());
    } else if (hash.is_srv()) {
      Dbg(dbg_ctl_dns_srv, "SRV failed for '%.*s'", int(hash.host_name.size()), hash.host_name.data());
    } else {
      ip_text_buffer b;
      Dbg(dbg_ctl_hostdb, "failed for %s", hash.ip.toString(b, sizeof b));
    }
    record->ip_timestamp        = hostdb_current_timestamp;
    record->ip_timeout_interval = ts_seconds(std::clamp(hostdb_ip_fail_timeout_interval, 1u, HOST_DB_MAX_TTL));

    if (hash.is_srv()) {
      record->record_type = HostDBType::SRV;
    } else if (!hash.is_byname()) {
      record->record_type = HostDBType::HOST;
    }

    record->set_failed();

  } else {
    switch (hostdb_ttl_mode) {
    default:
      ink_assert(!"bad TTL mode");
    case TTL_OBEY:
      break;
    case TTL_IGNORE:
      answer_ttl = ts_seconds(hostdb_ip_timeout_interval);
      break;
    case TTL_MIN:
      if (ts_seconds(hostdb_ip_timeout_interval) < answer_ttl) {
        answer_ttl = ts_seconds(hostdb_ip_timeout_interval);
      }
      break;
    case TTL_MAX:
      if (ts_seconds(hostdb_ip_timeout_interval) > answer_ttl) {
        answer_ttl = ts_seconds(hostdb_ip_timeout_interval);
      }
      break;
    }
    Metrics::Counter::increment(hostdb_rsb.ttl, answer_ttl.count());

    // update the TTL
    record->ip_timestamp        = hostdb_current_timestamp;
    record->ip_timeout_interval = std::clamp(answer_ttl, ts_seconds(1), ts_seconds(HOST_DB_MAX_TTL));

    if (hash.is_byname()) {
      Dbg_bw(dbg_ctl_hostdb, "done {} TTL {}", hash.host_name, answer_ttl);
    } else if (hash.is_srv()) {
      ink_assert(srv && srv->hosts.size() && srv->hosts.size() <= hostdb_round_robin_max_count);

      record->record_type = HostDBType::SRV;
    } else {
      Dbg_bw(dbg_ctl_hostdb, "done {} TTL {}", hash.host_name, answer_ttl);
      record->record_type = HostDBType::HOST;
    }
  }

  return record;
}

int
HostDBContinuation::dnsPendingEvent(int event, Event *e)
{
  if (timeout) {
    timeout->cancel(this);
    timeout = nullptr;
  }
  if (event == EVENT_INTERVAL) {
    // we timed out, return a failure to the user
    MUTEX_TRY_LOCK(lock, action.mutex, ((Event *)e)->ethread);
    if (!lock.is_locked()) {
      timeout = eventProcessor.schedule_in(this, HOST_DB_RETRY_PERIOD);
      return EVENT_CONT;
    }
    if (!action.cancelled && action.continuation) {
      action.continuation->handleEvent(EVENT_HOST_DB_LOOKUP, nullptr);
    }
    if (hostDB.remove_from_pending_dns_for_hash(hash.hash, this)) {
      hostdb_cont_free(this);
    }
    return EVENT_DONE;
  } else {
    SET_HANDLER(&HostDBContinuation::probeEvent);
    return probeEvent(EVENT_INTERVAL, nullptr);
  }
}

// DNS lookup result state
int
HostDBContinuation::dnsEvent(int event, HostEnt *e)
{
  if (timeout) {
    timeout->cancel(this);
    timeout = nullptr;
  }
  EThread *thread = mutex->thread_holding;
  if (event != DNS_EVENT_LOOKUP) {
    // Event should be immediate or interval.
    if (!action.continuation) {
      // Nothing to do, give up.
      if (event == EVENT_INTERVAL) {
        // Timeout - clear all queries queued up for this FQDN because none of the other ones have sent an
        // actual DNS query. If the request rate is high enough this can cause a persistent queue where the
        // DNS query is never sent and all requests timeout, even if it was a transient error.
        // See issue #8417.
        remove_and_trigger_pending_dns();
      } else {
        // "local" signal to give up, usually due this being one of those "other" queries.
        // That generally means @a this has already been removed from the queue, but just in case...
        hostDB.pending_dns_for_hash(hash.hash).remove(this);
      }
      if (hostDB.remove_from_pending_dns_for_hash(hash.hash, this)) {
        hostdb_cont_free(this);
      }
      return EVENT_DONE;
    }
    MUTEX_TRY_LOCK(lock, action.mutex, thread);
    if (!lock.is_locked()) {
      timeout = thread->schedule_in(this, HOST_DB_RETRY_PERIOD);
      return EVENT_CONT;
    }
    // [amc] Callback to client to indicate a failure due to timeout.
    // We don't try a different family here because a timeout indicates
    // a server issue that won't be fixed by asking for a different
    // address family.
    if (!action.cancelled && action.continuation) {
      action.continuation->handleEvent(EVENT_HOST_DB_LOOKUP, nullptr);
    }
    action = nullptr;
    return EVENT_DONE;
  } else {
    bool failed = !e || !e->good;

    pending_action = nullptr;

    ttl = ts_seconds(failed ? 0 : e->ttl);

    Ptr<HostDBRecord> old_r = probe(hash, false);
    // If the DNS lookup failed with NXDOMAIN, remove the old record
    if (e && e->isNameError() && old_r) {
      hostDB.refcountcache->erase(old_r->key);
      old_r = nullptr;
      Dbg(dbg_ctl_hostdb, "Removing the old record when the DNS lookup failed with NXDOMAIN");
    }

    int         valid_records = 0;
    void       *first_record  = nullptr;
    sa_family_t af            = e ? e->ent.h_addrtype : AF_UNSPEC; // address family

    // Find the first record and total number of records.
    if (!failed) {
      if (hash.is_srv()) {
        valid_records = e->srv_hosts.hosts.size();
      } else {
        void *ptr; // tmp for current entry.
        for (int total_records = 0;
             total_records < static_cast<int>(hostdb_round_robin_max_count) && nullptr != (ptr = e->ent.h_addr_list[total_records]);
             ++total_records) {
          if (is_addr_valid(af, ptr)) {
            if (!first_record) {
              first_record = ptr;
            }
            // If we have found some records which are invalid, lets just shuffle around them.
            // This way we'll end up with e->ent.h_addr_list with all the valid responses at
            // the first `valid_records` slots
            if (valid_records != total_records) {
              e->ent.h_addr_list[valid_records] = e->ent.h_addr_list[total_records];
            }

            ++valid_records;
          } else {
            Warning("Invalid address removed for '%.*s'", int(hash.host_name.size()), hash.host_name.data());
          }
        }
        if (!first_record) {
          failed = true;
        }
      }
    } // else first is nullptr

    // In the event that the lookup failed (SOA response-- for example) we want to use hash.host_name, since it'll be ""
    TextView query_name = (failed || !hash.host_name.empty()) ? hash.host_name : TextView{e->ent.h_name, strlen(e->ent.h_name)};
    HostDBRecord::Handle r{HostDBRecord::alloc(query_name, valid_records, failed ? 0 : e->srv_hosts.srv_hosts_length)};
    r->key              = hash.hash.fold(); // always set the key
    r->af_family        = af;
    r->flags.f.failed_p = failed;

    // If the DNS lookup failed (errors such as SERVFAIL, etc.) but we have an old record
    // which is okay with being served stale-- lets continue to serve the stale record as long as
    // the record is willing to be served.
    bool serve_stale = false;
    if (failed && old_r && old_r->serve_stale_but_revalidate()) {
      r           = old_r;
      serve_stale = true;
    } else if (hash.is_byname()) {
      lookup_done(hash.host_name, ttl, failed ? nullptr : &e->srv_hosts, r);
    } else if (hash.is_srv()) {
      lookup_done(hash.host_name, /* hostname */
                  ttl,            /* ttl in seconds */
                  failed ? nullptr : &e->srv_hosts, r);
    } else if (failed) {
      lookup_done(hash.host_name, ttl, nullptr, r);
    } else {
      lookup_done(e->ent.h_name, ttl, &e->srv_hosts, r);
    }

    if (!failed) { // implies r != old_r
      auto rr_info = r->rr_info();
      // Fill in record type specific data.
      if (hash.is_srv()) {
        char *pos = rr_info.rebind<char>().end();
        SRV  *q[valid_records];
        ink_assert(valid_records <= (int)hostdb_round_robin_max_count);
        for (int i = 0; i < valid_records; ++i) {
          q[i] = &e->srv_hosts.hosts[i];
        }
        std::sort(q, q + valid_records, [](SRV *lhs, SRV *rhs) -> bool { return *lhs < *rhs; });

        SRV **cur_srv = q;
        for (auto &item : rr_info) {
          auto t = *cur_srv++;               // get next SRV record pointer.
          memcpy(pos, t->host, t->host_len); // Append the name to the overall record.
          item.assign(t, pos);
          pos += t->host_len;
          if (old_r) { // migrate as needed.
            for (auto &old_item : old_r->rr_info()) {
              if (item.data.srv.key == old_item.data.srv.key && 0 == strcmp(item.srvname(), old_item.srvname())) {
                item.migrate_from(old_item);
                break;
              }
            }
          }
          // Archetypical example - "%zd" doesn't work on FreeBSD, "%ld" doesn't work on Ubuntu, "%lld" doesn't work on Fedora.
          Dbg_bw(dbg_ctl_dns_srv, "inserted SRV RR record [{}] into HostDB with TTL: {} seconds", item.srvname(), ttl);
        }
      } else { // Otherwise this is a regular dns response
        unsigned idx = 0;
        for (auto &item : rr_info) {
          item.assign(af, e->ent.h_addr_list[idx++]);
          if (old_r) { // migrate as needed.
            for (auto &old_item : old_r->rr_info()) {
              if (item.data.ip == old_item.data.ip) {
                item.migrate_from(old_item);
                break;
              }
            }
          }
        }
      }
    }

    if (!serve_stale) { // implies r != old_r
      ts::shared_mutex                  &bucket_lock = hostDB.refcountcache->lock_for_key(hash.hash.fold());
      std::unique_lock<ts::shared_mutex> lock{bucket_lock};
      auto const                         duration_till_revalidate = r->expiry_time().time_since_epoch();
      auto const                         seconds_till_revalidate  = duration_cast<ts_seconds>(duration_till_revalidate).count();
      hostDB.refcountcache->put(r->key, r.get(), r->_record_size, seconds_till_revalidate);
    } else {
      Warning("Fallback to serving stale record, skip re-update of hostdb for %.*s", int(query_name.size()), query_name.data());
    }

    // try to callback the user
    //
    if (action.continuation) {
      // Check for IP family failover
      if (failed && check_for_retry(hash.db_mark, host_res_style)) {
        this->refresh_hash(); // family changed if we're doing a retry.
        SET_CONTINUATION_HANDLER(this, &HostDBContinuation::probeEvent);
        this->timeout = thread->schedule_in(this, MUTEX_RETRY_DELAY);
        return EVENT_CONT;
      }

      // We have seen cases were the action.mutex != action.continuation.mutex.  However, it seems that case
      // is likely a memory corruption... Thus the introduction of the assert.
      // Since reply_to_cont will call the handler on the action.continuation, it is important that we hold
      // that mutex.
      bool need_to_reschedule = true;
      MUTEX_TRY_LOCK(lock, action.mutex, thread);
      if (lock.is_locked()) {
        if (!action.cancelled) {
          if (action.continuation->mutex) {
            ink_release_assert(action.continuation->mutex == action.mutex);
          }
          reply_to_cont(action.continuation, r.get(), hash.is_srv());
        }
        need_to_reschedule = false;
      }

      if (need_to_reschedule) {
        SET_HANDLER(&HostDBContinuation::probeEvent);
        // Will reschedule on affinity thread or current thread
        timeout = eventProcessor.schedule_in(this, HOST_DB_RETRY_PERIOD);
        return EVENT_CONT;
      }
    }

    // Clean ourselves up
    bool cleanup_self = hostDB.remove_from_pending_dns_for_hash(hash.hash, this);

    // wake up everyone else who is waiting
    remove_and_trigger_pending_dns();

    if (cleanup_self) {
      hostdb_cont_free(this);
    }

    // all done, or at least scheduled to be all done
    //
    return EVENT_DONE;
  }
}

int
HostDBContinuation::iterateEvent(int event, Event *e)
{
  Dbg(dbg_ctl_hostdb, "iterateEvent event=%d eventp=%p", event, e);
  ink_assert(!link.prev && !link.next);
  EThread *t = e ? e->ethread : this_ethread();

  MUTEX_TRY_LOCK(lock, action.mutex, t);
  if (!lock.is_locked()) {
    Dbg(dbg_ctl_hostdb, "iterateEvent event=%d eventp=%p: reschedule due to not getting action mutex", event, e);
    mutex->thread_holding->schedule_in(this, HOST_DB_RETRY_PERIOD);
    return EVENT_CONT;
  }

  if (action.cancelled) {
    hostdb_cont_free(this);
    return EVENT_DONE;
  }

  // let's iterate through another record and then reschedule ourself.
  if (current_iterate_pos < hostDB.refcountcache->partition_count()) {
    // TODO: configurable number at a time?
    ts::shared_mutex                  &bucket_lock = hostDB.refcountcache->get_partition(current_iterate_pos).lock;
    std::shared_lock<ts::shared_mutex> lock{bucket_lock};

    auto &partMap = hostDB.refcountcache->get_partition(current_iterate_pos).get_map();
    for (const auto &it : partMap) {
      auto *r = static_cast<HostDBRecord *>(it.item.get());
      if (r && !r->is_failed()) {
        action.continuation->handleEvent(EVENT_INTERVAL, static_cast<void *>(r));
      }
    }
    current_iterate_pos++;
  }

  if (current_iterate_pos < hostDB.refcountcache->partition_count()) {
    // And reschedule ourselves to pickup the next bucket after HOST_DB_RETRY_PERIOD.
    Dbg(dbg_ctl_hostdb, "iterateEvent event=%d eventp=%p: completed current iteration %ld of %ld", event, e, current_iterate_pos,
        hostDB.refcountcache->partition_count());
    mutex->thread_holding->schedule_in(this, HOST_DB_ITERATE_PERIOD);
    return EVENT_CONT;
  } else {
    Dbg(dbg_ctl_hostdb, "iterateEvent event=%d eventp=%p: completed FINAL iteration %ld", event, e, current_iterate_pos);
    // if there are no more buckets, then we're done.
    action.continuation->handleEvent(EVENT_DONE, nullptr);
    hostdb_cont_free(this);
  }

  return EVENT_DONE;
}

//
// Probe state
//
int
HostDBContinuation::probeEvent(int /* event ATS_UNUSED */, Event *e)
{
  ink_assert(!link.prev && !link.next);
  EThread *t = e ? e->ethread : this_ethread();

  if (timeout) {
    timeout->cancel(this);
    timeout = nullptr;
  }

  MUTEX_TRY_LOCK(lock, action.mutex, t);

  // Separating lock checks here to make sure things don't break
  // when we check if the action is cancelled.
  if (!lock.is_locked()) {
    EThread *thread = this_ethread();
    timeout         = thread->schedule_in(this, HOST_DB_RETRY_PERIOD);
    return EVENT_CONT;
  }

  if (action.cancelled) {
    hostdb_cont_free(this);
    return EVENT_DONE;
  }

  //  If the action.continuation->mutex != action.mutex, we have a use after free/realloc
  ink_release_assert(!action.continuation || action.continuation->mutex == action.mutex);

  if (!hostdb_enable || (!*hash.host_name && !hash.ip.isValid())) {
    if (action.continuation) {
      action.continuation->handleEvent(EVENT_HOST_DB_LOOKUP, nullptr);
    }
    hostdb_cont_free(this);
    return EVENT_DONE;
  }

  if (!force_dns) {
    // Do the probe
    //
    HostDBRecord::Handle r = probe(hash, false);

    if (r) {
      Metrics::Counter::increment(hostdb_rsb.total_hits);
    }

    if (action.continuation && r) {
      reply_to_cont(action.continuation, r.get(), hash.is_srv());
    }

    // If it succeeds or it was a remote probe, we are done
    //
    if (r) {
      hostdb_cont_free(this);
      return EVENT_DONE;
    }
  }
  // If there are no remote nodes to probe, do a DNS lookup
  //
  do_dns();
  return EVENT_DONE;
}

int
HostDBContinuation::set_check_pending_dns()
{
  ts::shared_mutex                  &bucket_lock = hostDB.refcountcache->lock_for_key(hash.hash.fold());
  std::unique_lock<ts::shared_mutex> lock{bucket_lock};
  Queue<HostDBContinuation>         &q = hostDB.pending_dns_for_hash(hash.hash);
  this->setThreadAffinity(this_ethread());
  if (q.in(this)) {
    Metrics::Counter::increment(hostdb_rsb.insert_duplicate_to_pending_dns);
    Dbg(dbg_ctl_hostdb, "Skip the insertion of the same continuation to pending dns");
    return false;
  }
  HostDBContinuation *c = q.head;
  for (; c; c = static_cast<HostDBContinuation *>(c->link.next)) {
    if (hash.hash == c->hash.hash) {
      Dbg(dbg_ctl_hostdb, "enqueuing additional request");
      q.enqueue(this);
      return false;
    }
  }
  q.enqueue(this);
  return true;
}

void
HostDBContinuation::remove_and_trigger_pending_dns()
{
  Queue<HostDBContinuation> qq;
  HostDBContinuation       *c           = nullptr;
  ts::shared_mutex         &bucket_lock = hostDB.refcountcache->lock_for_key(hash.hash.fold());
  {
    std::unique_lock<ts::shared_mutex> lock{bucket_lock};
    Queue<HostDBContinuation>         &q = hostDB.pending_dns_for_hash(hash.hash);
    q.remove(this);
    c = q.head;
    while (c) {
      HostDBContinuation *n = static_cast<HostDBContinuation *>(c->link.next);
      if (hash.hash == c->hash.hash) {
        Dbg(dbg_ctl_hostdb, "dequeuing additional request");
        q.remove(c);
        if (!c->action.cancelled) {
          qq.enqueue(c);
        }
      }
      c = n;
    }
  }
  EThread *thread = this_ethread();
  while ((c = qq.dequeue())) {
    // resume all queued HostDBCont in the thread associated with the netvc to avoid nethandler locking issues.
    EThread *affinity_thread = c->getThreadAffinity();
    SCOPED_MUTEX_LOCK(lock, c->mutex, this_ethread());
    if (!affinity_thread || affinity_thread == thread) {
      c->handleEvent(EVENT_IMMEDIATE, nullptr);
    } else {
      if (c->timeout) {
        c->timeout->cancel();
      }
      c->timeout = eventProcessor.schedule_imm(c);
    }
  }
}

//
// Query the DNS processor
//
void
HostDBContinuation::do_dns()
{
  ink_assert(!action.cancelled);

  if (hostdb_lookup_timeout) {
    EThread *thread = this_ethread();
    timeout         = thread->schedule_in(this, HRTIME_SECONDS(hostdb_lookup_timeout));
  } else {
    timeout = nullptr;
  }
  if (set_check_pending_dns()) {
    DNSProcessor::Options opt;
    opt.timeout        = dns_lookup_timeout;
    opt.host_res_style = host_res_style_for(hash.db_mark);
    SET_HANDLER(&HostDBContinuation::dnsEvent);
    if (hash.is_byname()) {
      if (hash.dns_server) {
        opt.handler = hash.dns_server->x_dnsH;
      }
      pending_action = dnsProcessor.gethostbyname(this, hash.host_name, opt);
    } else if (hash.is_srv()) {
      Dbg(dbg_ctl_dns_srv, "SRV lookup of %.*s", int(hash.host_name.size()), hash.host_name.data());
      pending_action = dnsProcessor.getSRVbyname(this, hash.host_name, opt);
    } else {
      ip_text_buffer ipb;
      Dbg(dbg_ctl_hostdb, "DNS IP %s", hash.ip.toString(ipb, sizeof ipb));
      pending_action = dnsProcessor.gethostbyaddr(this, &hash.ip, opt);
    }
  } else {
    SET_HANDLER(&HostDBContinuation::dnsPendingEvent);
  }
}

//
// Background event
// Increment the hostdb_current_timestamp which functions as our cached version
// of ts_clock::now().  Might do other stuff here, like move records to the
// current position in the cluster.
int
HostDBContinuation::backgroundEvent(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  std::string dbg;

  hostdb_current_timestamp = ts_clock::now();

  // Do nothing if hosts file checking is not enabled.
  if (hostdb_hostfile_check_interval.count() == 0) {
    return EVENT_CONT;
  }

  if ((hostdb_current_timestamp.load() - hostdb_last_timestamp) > hostdb_hostfile_check_interval) {
    bool update_p = false; // do we need to reparse the file and update?
    char path[PATH_NAME_MAX];

    REC_ReadConfigString(path, "proxy.config.hostdb.host_file.path", sizeof(path));
    if (0 != strcasecmp(hostdb_hostfile_path.string(), path)) {
      Dbg(dbg_ctl_hostdb, "%s",
          swoc::bwprint(dbg, R"(Updating hosts file from "{}" to "{}")", hostdb_hostfile_path.view(), swoc::bwf::FirstOf(path, ""))
            .c_str());
      // path to hostfile changed
      hostdb_hostfile_update_timestamp = TS_TIME_ZERO; // never updated from this file
      hostdb_hostfile_path             = path;
      update_p                         = true;
    } else if (!hostdb_hostfile_path.empty()) {
      hostdb_last_timestamp = hostdb_current_timestamp;
      std::error_code ec;
      auto            stat{swoc::file::status(hostdb_hostfile_path, ec)};
      if (!ec) {
        if (swoc::file::last_write_time(stat) > hostdb_hostfile_update_timestamp) {
          update_p = true; // same file but it's changed.
        }
      } else {
        Dbg(dbg_ctl_hostdb, "%s",
            swoc::bwprint(dbg, R"(Failed to stat host file "{}" - {})", hostdb_hostfile_path.view(), ec).c_str());
      }
    }
    if (update_p) {
      Dbg(dbg_ctl_hostdb, "%s", swoc::bwprint(dbg, R"(Updating from host file "{}")", hostdb_hostfile_path.view()).c_str());
      UpdateHostsFile(hostdb_hostfile_path, hostdb_hostfile_check_interval);
    }
  }

  return EVENT_CONT;
}

HostDBInfo *
HostDBRecord::select_best_http(ts_time now, ts_seconds fail_window, sockaddr const *hash_addr)
{
  ink_assert(0 < rr_count && rr_count <= hostdb_round_robin_max_count);

  // @a best_any is set to a base candidate, which may be down.
  HostDBInfo *best_any = nullptr;
  // @a best_alive is set when a valid target has been selected and should be used.
  HostDBInfo *best_alive = nullptr;

  auto info{this->rr_info()};
  if (info.count() > 1) {
    if (HostDBProcessor::hostdb_strict_round_robin) {
      // Always select the next viable target - select failure means no valid targets at all.
      best_alive = best_any = this->select_next_rr(now, fail_window);
      Dbg(dbg_ctl_hostdb, "Using strict round robin - index %d", this->index_of(best_alive));
    } else if (HostDBProcessor::hostdb_timed_round_robin > 0) {
      auto ctime = rr_ctime.load(); // cache for atomic update.
      auto ntime = ctime + ts_seconds(HostDBProcessor::hostdb_timed_round_robin);
      // Check and update RR if it's time - this always yields a valid target if there is one.
      if (now > ntime && rr_ctime.compare_exchange_strong(ctime, ntime)) {
        best_alive = best_any = this->select_next_rr(now, fail_window);
        Dbg(dbg_ctl_hostdb, "Round robin timed interval expired - index %d", this->index_of(best_alive));
      } else { // pick the current index, which may be down.
        best_any = &info[this->rr_idx()];
      }
      Dbg(dbg_ctl_hostdb, "Using timed round robin - index %d", this->index_of(best_any));
    } else {
      // Walk the entries and find the best (largest) hash.
      unsigned int best_hash = 0; // any hash is better than this.
      for (auto &target : info) {
        unsigned int h = HOSTDB_CLIENT_IP_HASH(hash_addr, target.data.ip);
        if (best_hash <= h) {
          best_any  = &target;
          best_hash = h;
        }
      }
      Dbg(dbg_ctl_hostdb, "Using client affinity - index %d", this->index_of(best_any));
    }

    // If there is a base choice, search for valid target starting there.
    // Otherwise there is no valid target in the record.
    if (best_any && !best_alive) {
      // Starting at the current target, search for a valid one.
      for (unsigned short i = 0; i < rr_count; i++) {
        auto target = &info[this->rr_idx(i)];
        if (target->select(now, fail_window)) {
          best_alive = target;
          break;
        }
      }
    }
  } else {
    best_alive = &info[0];
  }

  return best_alive;
}

static constexpr int HOSTDB_TEST_MAX_OUTSTANDING = 20;
static constexpr int HOSTDB_TEST_LENGTH          = 200;

struct HostDBTestReverse;
using HostDBTestReverseHandler = int (HostDBTestReverse::*)(int, void *);
struct HostDBTestReverse : public Continuation {
  RegressionTest *test;
  int             type;
  int            *status;

  int           outstanding = 0;
  int           total       = 0;
  std::ranlux48 randu;

  int
  mainEvent(int event, Event *e)
  {
    if (event == EVENT_HOST_DB_LOOKUP) {
      auto *i = reinterpret_cast<HostDBRecord *>(e);
      if (i) {
        rprintf(test, "HostDBTestReverse: reversed %s\n", i->name());
      }
      outstanding--;
    }
    while (outstanding < HOSTDB_TEST_MAX_OUTSTANDING && total < HOSTDB_TEST_LENGTH) {
      IpEndpoint ip;
      ip.assign(IpAddr(static_cast<in_addr_t>(randu())));
      outstanding++;
      total++;
      if (!(outstanding % 100)) {
        rprintf(test, "HostDBTestReverse: %d\n", total);
      }
      hostDBProcessor.getbyaddr_re(this, &ip.sa);
    }
    if (!outstanding) {
      rprintf(test, "HostDBTestReverse: done\n");
      *status = REGRESSION_TEST_PASSED; //  TODO: actually verify it passed
      delete this;
    }
    return EVENT_CONT;
  }
  HostDBTestReverse(RegressionTest *t, int atype, int *astatus)
    : Continuation(new_ProxyMutex()), test(t), type(atype), status(astatus)
  {
    SET_HANDLER(&HostDBTestReverse::mainEvent);
    randu.seed(std::chrono::system_clock::now().time_since_epoch().count());
  }
};

#if TS_HAS_TESTS
REGRESSION_TEST(HostDBTests)(RegressionTest *t, int atype, int *pstatus)
{
  eventProcessor.schedule_imm(new HostDBTestReverse(t, atype, pstatus), ET_CALL);
}
#endif

HostDBStatsBlock hostdb_rsb;

void
ink_hostdb_init(ts::ModuleVersion v)
{
  static int init_called = 0;

  ink_release_assert(v.check(HOSTDB_MODULE_INTERNAL_VERSION));
  if (init_called) {
    return;
  }

  init_called = 1;

  //
  // Register stats
  //
  hostdb_rsb.total_lookups                   = Metrics::Counter::createPtr("proxy.process.hostdb.total_lookups");
  hostdb_rsb.total_hits                      = Metrics::Counter::createPtr("proxy.process.hostdb.total_hits");
  hostdb_rsb.total_serve_stale               = Metrics::Counter::createPtr("proxy.process.hostdb.total_serve_stale");
  hostdb_rsb.ttl                             = Metrics::Counter::createPtr("proxy.process.hostdb.ttl");
  hostdb_rsb.ttl_expires                     = Metrics::Counter::createPtr("proxy.process.hostdb.ttl_expires");
  hostdb_rsb.re_dns_on_reload                = Metrics::Counter::createPtr("proxy.process.hostdb.re_dns_on_reload");
  hostdb_rsb.insert_duplicate_to_pending_dns = Metrics::Counter::createPtr("proxy.process.hostdb.insert_duplicate_to_pending_dns");

  ts_host_res_global_init();
}

struct HostDBFileContinuation : public Continuation {
  using self = HostDBFileContinuation;
  using Keys = std::vector<CryptoHash>;

  int            idx  = 0;       ///< Working index.
  const char    *name = nullptr; ///< Host name (just for debugging)
  Keys          *keys = nullptr; ///< Entries from file.
  CryptoHash     hash;           ///< Key for entry.
  ats_scoped_str path;           ///< Used to keep the host file name around.

  HostDBFileContinuation() : Continuation(nullptr) {}
  /// Finish update
  static void finish(Keys *keys ///< Valid keys from update.
  );
  /// Clean up this instance.
  void destroy();
};

ClassAllocator<HostDBFileContinuation> hostDBFileContAllocator("hostDBFileContAllocator");

void
HostDBFileContinuation::destroy()
{
  this->~HostDBFileContinuation();
  hostDBFileContAllocator.free(this);
}

// Host file processing globals.

// We can't allow more than one update to be
// proceeding at a time in any case so we might as well make these
// globals.
std::atomic<bool> HostDBFileUpdateActive{false};

void
UpdateHostsFile(swoc::file::path const &path, ts_seconds interval)
{
  // Test and set for update in progress.
  bool flag = false;
  if (!HostDBFileUpdateActive.compare_exchange_strong(flag, true)) {
    Dbg(dbg_ctl_hostdb, "Skipped load of host file because update already in progress");
    return;
  }

  std::shared_ptr<HostFile> hf = ParseHostFile(path, interval);

  // Swap the pointer
  if (hf) {
    std::unique_lock lock(hostDB.host_file_mutex);
    hostDB.host_file = std::move(hf);
  }
  hostdb_hostfile_update_timestamp = hostdb_current_timestamp;
  // Mark this one as completed, so we can allow another update to happen
  HostDBFileUpdateActive = false;
}

//
// Regression tests
//
// Take a started hostDB and fill it up and make sure it doesn't explode
#if TS_HAS_TESTS
struct HostDBRegressionContinuation;

struct HostDBRegressionContinuation : public Continuation {
  int             hosts;
  const char    **hostnames;
  RegressionTest *test;
  int             type;
  int            *status;

  int success;
  int failure;
  int outstanding;
  int i;

  int
  mainEvent(int event, HostDBRecord *r)
  {
    (void)event;

    if (event == EVENT_INTERVAL) {
      rprintf(test, "hosts=%d success=%d failure=%d outstanding=%d i=%d\n", hosts, success, failure, outstanding, i);
    }
    if (event == EVENT_HOST_DB_LOOKUP) {
      --outstanding;
      if (r) {
        rprintf(test, "HostDBRecord r=%p\n", r);
        rprintf(test, "HostDBRecord hostname=%s\n", r->name());
        // If RR, print all of the enclosed records
        auto rr_info{r->rr_info()};
        for (int x = 0; x < r->rr_count; ++x) {
          ip_port_text_buffer ip_buf;
          rr_info[x].data.ip.toString(ip_buf, sizeof(ip_buf));
          rprintf(test, "hostdbinfo RR%d ip=%s\n", x, ip_buf);
        }
        ++success;
      } else {
        ++failure;
      }
    }

    if (i < hosts) {
      hostDBProcessor.getbyname_re(this, hostnames[i++], 0);
      return EVENT_CONT;
    } else {
      rprintf(test, "HostDBTestRR: %d outstanding %d success %d failure\n", outstanding, success, failure);
      if (success == hosts) {
        *status = REGRESSION_TEST_PASSED;
      } else {
        *status = REGRESSION_TEST_FAILED;
      }
      return EVENT_DONE;
    }
  }

  HostDBRegressionContinuation(int ahosts, const char **ahostnames, RegressionTest *t, int atype, int *astatus)
    : Continuation(new_ProxyMutex()),
      hosts(ahosts),
      hostnames(ahostnames),
      test(t),
      type(atype),
      status(astatus),
      success(0),
      failure(0),
      i(0)
  {
    outstanding = ahosts;
    SET_HANDLER(&HostDBRegressionContinuation::mainEvent);
  }
};

static const char *dns_test_hosts[] = {
  "www.apple.com", "www.ibm.com", "www.microsoft.com",
  "yahoo.com", // RR record
  "4.2.2.2",   // An IP-- since we don't expect resolution
  "127.0.0.1", // loopback since it has some special handling
};

REGRESSION_TEST(HostDBProcessor)(RegressionTest *t, int atype, int *pstatus)
{
  eventProcessor.schedule_in(new HostDBRegressionContinuation(6, dns_test_hosts, t, atype, pstatus), HRTIME_SECONDS(1));
}

#endif
// -----
void
HostDBRecord::free()
{
  if (_iobuffer_index >= 0) {
    Dbg(dbg_ctl_hostdb, "freeing %d bytes at [%p]", (1 << (7 + _iobuffer_index)), this);
    ioBufAllocator[_iobuffer_index].free_void(static_cast<void *>(this));
  }
}

HostDBRecord *
HostDBRecord::alloc(TextView query_name, unsigned int rr_count, size_t srv_name_size)
{
  const swoc::Scalar<8, ssize_t> qn_size = round_up(query_name.size() + 1);
  const swoc::Scalar<8, ssize_t> r_size  = round_up(sizeof(self_type) + qn_size + rr_count * sizeof(HostDBInfo) + srv_name_size);
  int                            iobuffer_index = iobuffer_size_to_index(r_size, hostdb_max_iobuf_index);
  ink_release_assert(iobuffer_index >= 0);
  auto ptr = ioBufAllocator[iobuffer_index].alloc_void();
  memset(ptr, 0, r_size);
  auto self = static_cast<self_type *>(ptr);
  new (self) self_type();
  self->_iobuffer_index = iobuffer_index;
  self->_record_size    = r_size;

  Dbg(dbg_ctl_hostdb, "allocating %ld bytes for %.*s with %d RR records at [%p]", r_size.value(), int(query_name.size()),
      query_name.data(), rr_count, self);

  // where in our block of memory we are
  int offset = sizeof(self_type);
  memcpy(self->apply_offset<void>(offset), query_name);
  offset          += qn_size;
  self->rr_offset  = offset;
  self->rr_count   = rr_count;
  // Construct the info instances to a valid state.
  for (auto &info : self->rr_info()) {
    new (&info) std::remove_reference_t<decltype(info)>;
  }

  return self;
}

HostDBRecord::self_type *
HostDBRecord::unmarshall(char *buff, unsigned size)
{
  if (size < sizeof(self_type)) {
    return nullptr;
  }
  auto src = reinterpret_cast<self_type *>(buff);
  ink_release_assert(size == src->_record_size);
  auto ptr  = ioBufAllocator[src->_iobuffer_index].alloc_void();
  auto self = static_cast<self_type *>(ptr);
  new (self) self_type();
  auto delta = sizeof(RefCountObj); // skip the VFTP and ref count.
  memcpy(static_cast<std::byte *>(ptr) + delta, buff + delta, size - delta);
  return self;
}

bool
HostDBRecord::serve_stale_but_revalidate() const
{
  // the option is disabled
  if (hostdb_serve_stale_but_revalidate <= 0) {
    return false;
  }

  // ip_timeout_interval == DNS TTL
  // hostdb_serve_stale_but_revalidate == number of seconds
  // ip_age() is the number of seconds between now() and when the entry was inserted
  if ((ip_timeout_interval + ts_seconds(hostdb_serve_stale_but_revalidate)) > ip_age()) {
    Dbg_bw(dbg_ctl_hostdb, "serving stale entry for {}, TTL: {}, serve_stale_for: {}, age: {} as requested by config", name(),
           ip_timeout_interval, hostdb_serve_stale_but_revalidate, ip_age());
    return true;
  }

  // otherwise, the entry is too old
  return false;
}

HostDBInfo *
HostDBRecord::select_best_srv(char *target, InkRand *rand, ts_time now, ts_seconds fail_window)
{
  ink_assert(rr_count <= 0 || static_cast<unsigned int>(rr_count) < hostdb_round_robin_max_count);

  int         i      = 0;
  int         live_n = 0;
  uint32_t    weight = 0, p = INT32_MAX;
  HostDBInfo *result = nullptr;
  auto        rr     = this->rr_info();
  // Array of live targets, sized by @a live_n
  HostDBInfo *live[rr.count()];
  for (auto &target : rr) {
    // skip down targets.
    if (rr[i].is_down(now, fail_window)) {
      continue;
    }

    if (target.data.srv.srv_priority <= p) {
      p               = target.data.srv.srv_priority;
      weight         += target.data.srv.srv_weight;
      live[live_n++]  = &target;
    } else {
      break;
    }
  };

  if (live_n == 0 || weight == 0) { // no valid or weighted choice, use strict RR
    result = this->select_next_rr(now, fail_window);
  } else {
    uint32_t xx = rand->random() % weight;
    for (i = 0; i < live_n - 1 && xx >= live[i]->data.srv.srv_weight; ++i)
      xx -= live[i]->data.srv.srv_weight;

    result = live[i];
  }

  if (result) {
    ink_strlcpy(target, result->srvname(), MAXDNAME);
    return result;
  }
  return nullptr;
}

HostDBInfo *
HostDBRecord::select_next_rr(ts_time now, ts_seconds fail_window)
{
  auto rr_info = this->rr_info();
  for (unsigned idx = 0, limit = rr_info.count(); idx < limit; ++idx) {
    auto &target = rr_info[this->next_rr()];
    if (target.select(now, fail_window)) {
      return &target;
    }
  }

  return nullptr;
}

unsigned
HostDBRecord::next_rr()
{
  auto raw_idx = ++_rr_idx;
  // Modulus on an atomic is a bit tricky - need to make sure the value is always decremented by the
  // modulus even if another thread incremented. Update to modulus value iff the value hasn't been
  // incremented elsewhere. Eventually the "last" incrementer will do the update.
  auto idx = raw_idx % rr_count;
  _rr_idx.compare_exchange_weak(raw_idx, idx);
  return idx;
}

HostDBInfo *
HostDBRecord::find(sockaddr const *addr)
{
  for (auto &item : this->rr_info()) {
    if (item.data.ip == addr) {
      return &item;
    }
  }
  return nullptr;
}

bool
ResolveInfo::resolve_immediate()
{
  if (resolved_p) {
    // nothing - already resolved.
  } else if (IpAddr tmp; TS_SUCCESS == tmp.load(lookup_name)) {
    swoc::bwprint(ts::bw_dbg, "[resolve_immediate] success - FQDN '{}' is a valid IP address.", lookup_name);
    Dbg(dbg_ctl_hostdb, "%s", ts::bw_dbg.c_str());
    addr.assign(tmp);
    resolved_p = true;
  }
  return resolved_p;
}

bool
ResolveInfo::set_active(HostDBInfo *info)
{
  active = info;
  if (info) {
    addr.assign(active->data.ip);
    resolved_p = true;
    return true;
  }
  resolved_p = false;
  return false;
}

bool
ResolveInfo::select_next_rr()
{
  if (active) {
    if (auto rr_info{this->record->rr_info()}; rr_info.count() > 1) {
      unsigned limit = active - rr_info.data(), idx = (limit + 1) % rr_info.count();
      while ((idx = (idx + 1) % rr_info.count()) != limit && !rr_info[idx].is_alive())
        ;
      active = &rr_info[idx];
      return idx != limit; // if the active record was actually changed.
    }
  }
  return false;
}

bool
ResolveInfo::set_upstream_address(IpAddr const &ip_addr)
{
  if (ip_addr.isValid()) {
    addr.assign(ip_addr);
    resolved_p = true;
    return true;
  }
  return false;
}
