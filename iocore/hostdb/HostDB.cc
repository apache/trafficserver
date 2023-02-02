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

#include "Main.h"
#include "P_HostDB.h"
#include "P_RefCountCacheSerializer.h"
#include "tscore/I_Layout.h"
#include "Show.h"
#include "tscore/ts_file.h"
#include "tscore/ink_apidefs.h"
#include "tscore/bwf_std_format.h"
#include "MgmtDefs.h" // MgmtInt, MgmtFloat, etc
#include "HostFile.h"

#include <utility>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <shared_mutex>
#include <http/HttpConfig.h>

using ts::TextView;
using std::chrono::duration_cast;
using swoc::round_down;
using swoc::round_up;

HostDBProcessor hostDBProcessor;
int HostDBProcessor::hostdb_strict_round_robin = 0;
int HostDBProcessor::hostdb_timed_round_robin  = 0;
HostDBProcessor::Options const HostDBProcessor::DEFAULT_OPTIONS;
HostDBContinuation::Options const HostDBContinuation::DEFAULT_OPTIONS;
int hostdb_enable                              = true;
int hostdb_migrate_on_demand                   = true;
int hostdb_lookup_timeout                      = 30;
int hostdb_re_dns_on_reload                    = false;
int hostdb_ttl_mode                            = TTL_OBEY;
unsigned int hostdb_round_robin_max_count      = 16;
unsigned int hostdb_ip_stale_interval          = HOST_DB_IP_STALE;
unsigned int hostdb_ip_timeout_interval        = HOST_DB_IP_TIMEOUT;
unsigned int hostdb_ip_fail_timeout_interval   = HOST_DB_IP_FAIL_TIMEOUT;
unsigned int hostdb_serve_stale_but_revalidate = 0;
static ts_seconds hostdb_hostfile_check_interval{std::chrono::hours(24)};
// Epoch timestamp of the current hosts file check. This also functions as a
// cached version of ts_clock::now().
std::atomic<ts_time> hostdb_current_timestamp{TS_TIME_ZERO};
// Epoch timestamp of the last time we actually checked for a hosts file update.
static ts_time hostdb_last_timestamp{TS_TIME_ZERO};
// Epoch timestamp when we updated the hosts file last.
static ts_time hostdb_hostfile_update_timestamp{TS_TIME_ZERO};
static char hostdb_filename[PATH_NAME_MAX] = DEFAULT_HOST_DB_FILENAME;
int hostdb_max_count                       = DEFAULT_HOST_DB_SIZE;
static ts::file::path hostdb_hostfile_path;
ts_seconds hostdb_sync_frequency{0};
int hostdb_disable_reverse_lookup = 0;
int hostdb_max_iobuf_index        = BUFFER_SIZE_INDEX_32K;

ClassAllocator<HostDBContinuation> hostDBContAllocator("hostDBContAllocator");

namespace
{
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

/** Template for creating conversions and initialization for @c std::chrono based configuration variables.
 *
 * @tparam V The exact type of the configuration variable.
 *
 * The tricky template code is to enable having a class instance for each configuration variable, instead of for each _type_ of
 * configuration variable. This is required because the callback interface requires functions and so the actual storage must be
 * accessible from that function. *
 */
template <typename V> struct ConfigDuration {
  using self_type = ConfigDuration;
  V *_var; ///< Pointer to the variable to control.

  /** Constructor.
   *
   * @param v The variable to update.
   */
  ConfigDuration(V &v) : _var(&v) {}

  /// Convert to the mgmt (configuration) type.
  static MgmtInt
  to_mgmt(void const *data)
  {
    return static_cast<MgmtInt>(static_cast<V const *>(data)->count());
  }

  /// Convert from the mgmt (configuration) type.
  static void
  from_mgmt(void *data, MgmtInt i)
  {
    *static_cast<V *>(data) = V{i};
  }

  /// The conversion structure, which handles @c MgmtInt.
  static inline const MgmtConverter Conversions{&to_mgmt, &from_mgmt};

  /** Process start up conversion from configuration.
   *
   * @param type The data type in the configuration.
   * @param data The data in the configuration.
   * @param var Pointer to the variable to update.
   * @return @c true if @a data was successfully converted and stored, @c false if not.
   *
   * @note @a var is the target variable because it was explicitly set to be the value of @a _var in @c Enable.
   */
  static bool
  callback(char const *, RecDataT type, RecData data, void *var)
  {
    if (RECD_INT == type) {
      (*self_type::Conversions.store_int)(var, data.rec_int);
      return true;
    }
    return false;
  }

  /** Enable.
   *
   * @param name Name of the configuration variable.
   *
   * This enables both reading from the configuration and handling the callback for dynamic
   * updates of the variable.
   */
  void
  Enable(std::string_view name)
  {
    Enable_Config_Var(name, &self_type::callback, _var);
  }
};

ConfigDuration HostDBDownServerCacheTimeVar{HttpConfig::m_master.oride.down_server_timeout};
// Make the conversions visible to the plugin API. This allows exporting just the conversions
// without having to export the class definition. Again, the compiler doesn't allow doing this
// in one line.
extern MgmtConverter const &HostDBDownServerCacheTimeConv;
MgmtConverter const &HostDBDownServerCacheTimeConv = HostDBDownServerCacheTimeVar.Conversions;

// Not run time configurable, therefore no support beyond this class needed.
ConfigDuration HostDBSyncFrequencyVar{hostdb_sync_frequency};

void
HostDB_Config_Init()
{
  HostDBDownServerCacheTimeVar.Enable("proxy.config.http.down_server.cache_time");
  HostDBSyncFrequencyVar.Enable("proxy.config.cache.hostdb.sync_frequency");
}

// Static configuration information

HostDBCache hostDB;

void UpdateHostsFile(ts::file::path const &path, ts_seconds interval);

static inline bool
is_addr_valid(uint8_t af, ///< Address family (format of data)
              void *ptr   ///< Raw address data (not a sockaddr variant!)
)
{
  return (AF_INET == af && INADDR_ANY != *(reinterpret_cast<in_addr_t *>(ptr))) ||
         (AF_INET6 == af && !IN6_IS_ADDR_UNSPECIFIED(reinterpret_cast<in6_addr *>(ptr)));
}

inline void
hostdb_cont_free(HostDBContinuation *cont)
{
  if (cont->pending_action) {
    cont->pending_action->cancel();
  }
  if (cont->timeout) {
    cont->timeout->cancel();
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
static Action *register_ShowHostDB(Continuation *c, HTTPHdr *h);

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
    uint8_t m               = static_cast<uint8_t>(db_mark); // be sure of the type.

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
    int n = ip.isIp6() ? sizeof(in6_addr) : sizeof(in_addr_t);
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
  Queue<HostDBContinuation> &q = pending_dns_for_hash(hash);
  for (HostDBContinuation *c = q.head; c; c = static_cast<HostDBContinuation *>(c->link.next)) {
    if (hash == c->hash.hash) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<HostFile>
HostDBCache::acquire_host_file()
{
  std::shared_lock lock(host_file_mutex);
  auto zret = host_file;
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
  int wait_event(int event, void *edata);

  HostDBBackgroundTask(ts_seconds frequency);
};

HostDBBackgroundTask::HostDBBackgroundTask(ts_seconds frequency) : Continuation(new_ProxyMutex()), frequency(frequency)
{
  SET_HANDLER(&HostDBBackgroundTask::sync_event);
}

int
HostDBBackgroundTask::wait_event(int, void *)
{
  auto next_sync = this->frequency - (ts_hr_clock::now() - start_time);

  SET_HANDLER(&HostDBBackgroundTask::sync_event);
  if (next_sync > ts_milliseconds{100}) {
    eventProcessor.schedule_in(this, duration_cast<ts_nanoseconds>(next_sync).count(), ET_TASK);
  } else {
    eventProcessor.schedule_imm(this, ET_TASK);
  }
  return EVENT_DONE;
}

struct HostDBSync : public HostDBBackgroundTask {
  std::string storage_path;
  std::string full_path;
  HostDBSync(ts_seconds frequency, const std::string &storage_path, const std::string &full_path)
    : HostDBBackgroundTask(frequency), storage_path(std::move(storage_path)), full_path(std::move(full_path)){};
  int
  sync_event(int, void *) override
  {
    SET_HANDLER(&HostDBSync::wait_event);
    start_time = ts_hr_clock::now();

    new RefCountCacheSerializer<HostDBRecord>(this, hostDBProcessor.cache()->refcountcache, this->frequency.count(),
                                              this->storage_path, this->full_path);
    return EVENT_DONE;
  }
};

int
HostDBCache::start(int flags)
{
  (void)flags; // unused
  char storage_path[PATH_NAME_MAX];
  MgmtInt hostdb_max_size = 0;
  int hostdb_partitions   = 64;

  storage_path[0] = '\0';

  // Read configuration
  // Command line overrides manager configuration.
  //
  REC_ReadConfigInt32(hostdb_enable, "proxy.config.hostdb.enabled");
  REC_ReadConfigString(storage_path, "proxy.config.hostdb.storage_path", sizeof(storage_path));
  REC_ReadConfigString(hostdb_filename, "proxy.config.hostdb.filename", sizeof(hostdb_filename));

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

  //
  // Load and sync HostDB, if we've asked for it.
  //
  if (hostdb_sync_frequency.count() > 0) {
    // If proxy.config.hostdb.storage_path is not set, use the local state dir. If it is set to
    // a relative path, make it relative to the prefix.
    if (storage_path[0] == '\0') {
      ats_scoped_str rundir(RecConfigReadRuntimeDir());
      ink_strlcpy(storage_path, rundir, sizeof(storage_path));
    } else if (storage_path[0] != '/') {
      Layout::relative_to(storage_path, sizeof(storage_path), Layout::get()->prefix, storage_path);
    }

    Debug("hostdb", "Storage path is %s", storage_path);

    if (access(storage_path, W_OK | R_OK) == -1) {
      Warning("Unable to access() directory '%s': %d, %s", storage_path, errno, strerror(errno));
      Warning("Please set 'proxy.config.hostdb.storage_path' or 'proxy.config.local_state_dir'");
    }

    // Combine the path and name
    char full_path[2 * PATH_NAME_MAX];
    ink_filepath_make(full_path, 2 * PATH_NAME_MAX, storage_path, hostdb_filename);

    Debug("hostdb", "Opening %s, partitions=%d storage_size=%" PRIu64 " items=%d", full_path, hostdb_partitions, hostdb_max_size,
          hostdb_max_count);
    int load_ret = LoadRefCountCacheFromPath<HostDBRecord>(*this->refcountcache, full_path, HostDBRecord::unmarshall);
    if (load_ret != 0) {
      Warning("Error loading cache from %s: %d", full_path, load_ret);
    }

    eventProcessor.schedule_imm(new HostDBSync(hostdb_sync_frequency, storage_path, full_path), ET_TASK);
  }

  this->pending_dns       = new Queue<HostDBContinuation, Continuation::Link_link>[hostdb_partitions];
  this->remoteHostDBQueue = new Queue<HostDBContinuation, Continuation::Link_link>[hostdb_partitions];
  return 0;
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

  if (auto_clear_hostdb_flag) {
    hostDB.refcountcache->clear();
  }

  statPagesManager.register_http("hostdb", register_ShowHostDB);

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

  //
  // Initialize hostdb_current_timestamp which is our cached version of
  // ts_clock::now().
  //
  hostdb_current_timestamp = ts_clock::now();

  HostDBContinuation *b = hostDBContAllocator.alloc();
  SET_CONTINUATION_HANDLER(b, &HostDBContinuation::backgroundEvent);
  b->mutex = new_ProxyMutex();
  b->updateHostFileConfig();
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
  mutex              = hostDB.refcountcache->lock_for_key(hash.hash.fold());
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
  Ptr<ProxyMutex> old_bucket_mutex = hostDB.refcountcache->lock_for_key(hash.hash.fold());
  // We're not pending DNS anymore.
  remove_and_trigger_pending_dns();
  hash.refresh();
  // Update the mutex if it's from the bucket.
  // Some call sites modify this after calling @c init so need to check.
  if (mutex == old_bucket_mutex) {
    mutex = hostDB.refcountcache->lock_for_key(hash.hash.fold());
  }
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
    Debug("hostdb", "hostname = %s", r->name());
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
probe(const Ptr<ProxyMutex> &mutex, HostDBHash const &hash, bool ignore_timeout)
{
  static const Ptr<HostDBRecord> NO_RECORD;

  // If hostdb is disabled, don't return anything
  if (!hostdb_enable) {
    return NO_RECORD;
  }

  // Otherwise HostDB is enabled, so we'll do our thing
  ink_assert(this_ethread() == hostDB.refcountcache->lock_for_key(hash.hash.fold())->thread_holding);
  uint64_t folded_hash = hash.hash.fold();

  // get the record from cache
  Ptr<HostDBRecord> record = hostDB.refcountcache->get(folded_hash);
  // If there was nothing in the cache-- this is a miss
  if (record.get() == nullptr) {
    return record;
  }

  // If the dns response was failed, and we've hit the failed timeout, lets stop returning it
  if (record->is_failed() && record->is_ip_fail_timeout()) {
    return NO_RECORD;
    // if we aren't ignoring timeouts, and we are past it-- then remove the record
  } else if (!ignore_timeout && record->is_ip_timeout() && !record->serve_stale_but_revalidate()) {
    HOSTDB_INCREMENT_DYN_STAT(hostdb_ttl_expires_stat);
    return NO_RECORD;
  }

  // If the record is stale, but we want to revalidate-- lets start that up
  if ((!ignore_timeout && record->is_ip_configured_stale() && record->record_type != HostDBType::HOST) ||
      (record->is_ip_timeout() && record->serve_stale_but_revalidate())) {
    HOSTDB_INCREMENT_DYN_STAT(hostdb_total_serve_stale_stat);
    if (hostDB.is_pending_dns_for_hash(hash.hash)) {
      Debug("hostdb", "%s",
            ts::bwprint(ts::bw_dbg, "stale {} {} {}, using with pending refresh", record->ip_age(),
                        record->ip_timestamp.time_since_epoch(), record->ip_timeout_interval)
              .c_str());
      return record;
    }
    Debug("hostdb", "%s",
          ts::bwprint(ts::bw_dbg, "stale {} {} {}, using while refresh", record->ip_age(), record->ip_timestamp.time_since_epoch(),
                      record->ip_timeout_interval)
            .c_str());
    HostDBContinuation *c = hostDBContAllocator.alloc();
    HostDBContinuation::Options copt;
    copt.host_res_style = record->af_family == AF_INET6 ? HOST_RES_IPV6_ONLY : HOST_RES_IPV4_ONLY;
    c->init(hash, copt);
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
  bool force_dns        = false;
  EThread *thread       = this_ethread();
  Ptr<ProxyMutex> mutex = thread->mutex;
  ip_text_buffer ipb;

  if (opt.flags & HOSTDB_FORCE_DNS_ALWAYS) {
    force_dns = true;
  } else if (opt.flags & HOSTDB_FORCE_DNS_RELOAD) {
    force_dns = hostdb_re_dns_on_reload;
    if (force_dns) {
      HOSTDB_INCREMENT_DYN_STAT(hostdb_re_dns_on_reload_stat);
    }
  }

  HOSTDB_INCREMENT_DYN_STAT(hostdb_total_lookups_stat);

  if (!hostdb_enable ||                                       // if the HostDB is disabled,
      (hash.host_name && !*hash.host_name) ||                 // or host_name is empty string
      (hostdb_disable_reverse_lookup && hash.ip.isValid())) { // or try to lookup by ip address when the reverse lookup disabled
    if (cb_process_result) {
      (cont->*cb_process_result)(nullptr);
    } else {
      MUTEX_TRY_LOCK(lock, cont->mutex, thread);
      if (!lock.is_locked()) {
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
      Ptr<ProxyMutex> bucket_mutex = hostDB.refcountcache->lock_for_key(hash.hash.fold());
      MUTEX_TRY_LOCK(lock2, bucket_mutex, thread);
      if (lock2.is_locked()) {
        // If we can get the lock and a level 1 probe succeeds, return
        HostDBRecord::Handle r = probe(bucket_mutex, hash, false);
        if (r) {
          // fail, see if we should retry with alternate
          if (hash.db_mark != HOSTDB_MARK_SRV && r->is_failed() && hash.host_name) {
            loop = check_for_retry(hash.db_mark, opt.host_res_style);
          }
          if (!loop) {
            // No retry -> final result. Return it.
            if (hash.db_mark == HOSTDB_MARK_SRV) {
              Debug("hostdb", "immediate SRV answer for %.*s from hostdb", int(hash.host_name.size()), hash.host_name.data());
              Debug("dns_srv", "immediate SRV answer for %.*s from hostdb", int(hash.host_name.size()), hash.host_name.data());
            } else if (hash.host_name) {
              Debug("hostdb", "immediate answer for %.*s", int(hash.host_name.size()), hash.host_name.data());
            } else {
              Debug("hostdb", "immediate answer for %s", hash.ip.isValid() ? hash.ip.toString(ipb, sizeof ipb) : "<null>");
            }
            HOSTDB_INCREMENT_DYN_STAT(hostdb_total_hits_stat);
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
  }
  if (hash.db_mark == HOSTDB_MARK_SRV) {
    Debug("hostdb", "delaying (force=%d) SRV answer for %.*s [timeout = %d]", force_dns, int(hash.host_name.size()),
          hash.host_name.data(), opt.timeout);
    Debug("dns_srv", "delaying (force=%d) SRV answer for %.*s [timeout = %d]", force_dns, int(hash.host_name.size()),
          hash.host_name.data(), opt.timeout);
  } else if (hash.host_name) {
    Debug("hostdb", "delaying (force=%d) answer for %.*s [timeout %d]", force_dns, int(hash.host_name.size()),
          hash.host_name.data(), opt.timeout);
  } else {
    Debug("hostdb", "delaying (force=%d) answer for %s [timeout %d]", force_dns,
          hash.ip.isValid() ? hash.ip.toString(ipb, sizeof ipb) : "<null>", opt.timeout);
  }

Lretry:
  // Otherwise, create a continuation to do a deeper probe in the background
  //
  HostDBContinuation *c = hostDBContAllocator.alloc();
  HostDBContinuation::Options copt;
  copt.timeout        = opt.timeout;
  copt.force_dns      = force_dns;
  copt.cont           = cont;
  copt.host_res_style = (hash.db_mark == HOSTDB_MARK_SRV) ? HOST_RES_NONE : opt.host_res_style;
  c->init(hash, copt);
  SET_CONTINUATION_HANDLER(c, &HostDBContinuation::probeEvent);

  thread->schedule_in(c, MUTEX_RETRY_DELAY);

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
  EThread *thread   = cont->mutex->thread_holding;
  ProxyMutex *mutex = thread->mutex.get();

  HOSTDB_INCREMENT_DYN_STAT(hostdb_total_lookups_stat);

  HostDBContinuation *c = hostDBContAllocator.alloc();
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
  ink_assert(this_ethread() == hostDB.refcountcache->lock_for_key(hash.hash.fold())->thread_holding);
  ink_assert(record);
  if (query_name.empty()) {
    if (is_byname()) {
      Debug("hostdb", "lookup_done() failed for '%.*s'", int(hash.host_name.size()), hash.host_name.data());
    } else if (is_srv()) {
      Debug("dns_srv", "SRV failed for '%.*s'", int(hash.host_name.size()), hash.host_name.data());
    } else {
      ip_text_buffer b;
      Debug("hostdb", "failed for %s", hash.ip.toString(b, sizeof b));
    }
    record->ip_timestamp        = hostdb_current_timestamp;
    record->ip_timeout_interval = ts_seconds(std::clamp(hostdb_ip_fail_timeout_interval, 1u, HOST_DB_MAX_TTL));

    if (is_srv()) {
      record->record_type = HostDBType::SRV;
    } else if (!is_byname()) {
      record->record_type = HostDBType::HOST;
    }

    record->set_failed();
    return record;

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
    HOSTDB_SUM_DYN_STAT(hostdb_ttl_stat, answer_ttl.count());

    // update the TTL
    record->ip_timestamp        = hostdb_current_timestamp;
    record->ip_timeout_interval = std::clamp(answer_ttl, ts_seconds(1), ts_seconds(HOST_DB_MAX_TTL));

    if (is_byname()) {
      Debug_bw("hostdb", "done {} TTL {}", hash.host_name, answer_ttl);
    } else if (is_srv()) {
      ink_assert(srv && srv->hosts.size() && srv->hosts.size() <= hostdb_round_robin_max_count);

      record->record_type = HostDBType::SRV;
    } else {
      Debug_bw("hostdb", "done {} TTL {}", hash.host_name, answer_ttl);
      record->record_type = HostDBType::HOST;
    }
  }

  return record;
}

int
HostDBContinuation::dnsPendingEvent(int event, Event *e)
{
  ink_assert(this_ethread() == hostDB.refcountcache->lock_for_key(hash.hash.fold())->thread_holding);
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
    hostDB.pending_dns_for_hash(hash.hash).remove(this);
    hostdb_cont_free(this);
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
  ink_assert(this_ethread() == hostDB.refcountcache->lock_for_key(hash.hash.fold())->thread_holding);
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
      hostdb_cont_free(this);
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

    Ptr<HostDBRecord> old_r = probe(mutex, hash, false);
    // If the DNS lookup failed with NXDOMAIN, remove the old record
    if (e && e->isNameError() && old_r) {
      hostDB.refcountcache->erase(old_r->key);
      old_r = nullptr;
      Debug("hostdb", "Removing the old record when the DNS lookup failed with NXDOMAIN");
    }

    int valid_records  = 0;
    void *first_record = nullptr;
    sa_family_t af     = e ? e->ent.h_addrtype : AF_UNSPEC; // address family

    // Find the first record and total number of records.
    if (!failed) {
      if (is_srv()) {
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
    } else if (is_byname()) {
      lookup_done(hash.host_name, ttl, failed ? nullptr : &e->srv_hosts, r);
    } else if (is_srv()) {
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
      if (is_srv()) {
        char *pos = rr_info.rebind<char>().end();
        SRV *q[valid_records];
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
          Debug_bw("dns_srv", "inserted SRV RR record [{}] into HostDB with TTL: {} seconds", t->host, ttl);
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
      auto const duration_till_revalidate = r->expiry_time().time_since_epoch();
      auto const seconds_till_revalidate  = duration_cast<ts_seconds>(duration_till_revalidate).count();
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
        thread->schedule_in(this, MUTEX_RETRY_DELAY);
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
          reply_to_cont(action.continuation, r.get(), is_srv());
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
    hostDB.pending_dns_for_hash(hash.hash).remove(this);

    // wake up everyone else who is waiting
    remove_and_trigger_pending_dns();

    hostdb_cont_free(this);

    // all done, or at least scheduled to be all done
    //
    return EVENT_DONE;
  }
}

int
HostDBContinuation::iterateEvent(int event, Event *e)
{
  Debug("hostdb", "iterateEvent event=%d eventp=%p", event, e);
  ink_assert(!link.prev && !link.next);
  EThread *t = e ? e->ethread : this_ethread();

  MUTEX_TRY_LOCK(lock, action.mutex, t);
  if (!lock.is_locked()) {
    Debug("hostdb", "iterateEvent event=%d eventp=%p: reschedule due to not getting action mutex", event, e);
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
    Ptr<ProxyMutex> bucket_mutex = hostDB.refcountcache->get_partition(current_iterate_pos).lock;
    MUTEX_TRY_LOCK(lock_bucket, bucket_mutex, t);
    if (!lock_bucket.is_locked()) {
      // we couldn't get the bucket lock, let's just reschedule and try later.
      Debug("hostdb", "iterateEvent event=%d eventp=%p: reschedule due to not getting bucket mutex", event, e);
      mutex->thread_holding->schedule_in(this, HOST_DB_RETRY_PERIOD);
      return EVENT_CONT;
    }

    IntrusiveHashMap<RefCountCacheLinkage> &partMap = hostDB.refcountcache->get_partition(current_iterate_pos).get_map();
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
    Debug("hostdb", "iterateEvent event=%d eventp=%p: completed current iteration %ld of %ld", event, e, current_iterate_pos,
          hostDB.refcountcache->partition_count());
    mutex->thread_holding->schedule_in(this, HOST_DB_ITERATE_PERIOD);
    return EVENT_CONT;
  } else {
    Debug("hostdb", "iterateEvent event=%d eventp=%p: completed FINAL iteration %ld", event, e, current_iterate_pos);
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
    timeout = mutex->thread_holding->schedule_in(this, HOST_DB_RETRY_PERIOD);
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
    Ptr<HostDBRecord> r = probe(mutex, hash, false);

    if (r) {
      HOSTDB_INCREMENT_DYN_STAT(hostdb_total_hits_stat);
    }

    if (action.continuation && r) {
      reply_to_cont(action.continuation, r.get(), is_srv());
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
  Queue<HostDBContinuation> &q = hostDB.pending_dns_for_hash(hash.hash);
  this->setThreadAffinity(this_ethread());
  if (q.in(this)) {
    HOSTDB_INCREMENT_DYN_STAT(hostdb_insert_duplicate_to_pending_dns_stat);
    Debug("hostdb", "Skip the insertion of the same continuation to pending dns");
    return false;
  }
  HostDBContinuation *c = q.head;
  for (; c; c = static_cast<HostDBContinuation *>(c->link.next)) {
    if (hash.hash == c->hash.hash) {
      Debug("hostdb", "enqueuing additional request");
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
  Queue<HostDBContinuation> &q = hostDB.pending_dns_for_hash(hash.hash);
  q.remove(this);
  HostDBContinuation *c = q.head;
  Queue<HostDBContinuation> qq;
  while (c) {
    HostDBContinuation *n = static_cast<HostDBContinuation *>(c->link.next);
    if (hash.hash == c->hash.hash) {
      Debug("hostdb", "dequeuing additional request");
      q.remove(c);
      qq.enqueue(c);
    }
    c = n;
  }
  EThread *thread = this_ethread();
  while ((c = qq.dequeue())) {
    // resume all queued HostDBCont in the thread associated with the netvc to avoid nethandler locking issues.
    EThread *affinity_thread = c->getThreadAffinity();
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
  if (is_byname()) {
    Debug("hostdb", "DNS %.*s", int(hash.host_name.size()), hash.host_name.data());
    IpAddr tip;
    if (0 == tip.load(hash.host_name)) {
      // Need to consider if this is necessary - could the record in ResolveInfo be left null and
      // just the resolved address set?
      if (action.continuation) {
        HostDBRecord::Handle r{HostDBRecord::alloc(hash.host_name, 1)};
        r->af_family = tip.family();
        auto &info   = r->rr_info()[0];
        info.assign(tip);
        // tricksy - @a reply_to_cont must use an intrusive pointer to @a r if it needs to persist
        // @a r doesn't go out of scope until after this returns. This continuation shares the mutex
        // of the target continuation therefore this is always dispatched synchronously.
        reply_to_cont(action.continuation, r.get());
      }
      hostdb_cont_free(this);
      return;
    }
  }

  // Check if this can be fulfilled by the host file
  if (auto static_hosts = hostDB.acquire_host_file(); static_hosts) {
    if (auto r = static_hosts->lookup(hash); r && action.continuation) {
      // Set the TTL based on how often we stat() the host file
      r = lookup_done(hash.host_name, static_hosts->ttl, nullptr, r);
      reply_to_cont(action.continuation, r.get());
      hostdb_cont_free(this);
      return;
    }
  }

  if (hostdb_lookup_timeout) {
    timeout = mutex->thread_holding->schedule_in(this, HRTIME_SECONDS(hostdb_lookup_timeout));
  } else {
    timeout = nullptr;
  }
  if (set_check_pending_dns()) {
    DNSProcessor::Options opt;
    opt.timeout        = dns_lookup_timeout;
    opt.host_res_style = host_res_style_for(hash.db_mark);
    SET_HANDLER(&HostDBContinuation::dnsEvent);
    if (is_byname()) {
      if (hash.dns_server) {
        opt.handler = hash.dns_server->x_dnsH;
      }
      pending_action = dnsProcessor.gethostbyname(this, hash.host_name, opt);
    } else if (is_srv()) {
      Debug("dns_srv", "SRV lookup of %.*s", int(hash.host_name.size()), hash.host_name.data());
      pending_action = dnsProcessor.getSRVbyname(this, hash.host_name, opt);
    } else {
      ip_text_buffer ipb;
      Debug("hostdb", "DNS IP %s", hash.ip.toString(ipb, sizeof ipb));
      pending_action = dnsProcessor.gethostbyaddr(this, &hash.ip, opt);
    }
  } else {
    SET_HANDLER(&HostDBContinuation::dnsPendingEvent);
  }
}

void
HostDBContinuation::updateHostFileConfig()
{
  RecInt tmp_interval{};

  REC_ReadConfigInteger(tmp_interval, "proxy.config.hostdb.host_file.interval");
  hostdb_hostfile_check_interval = std::chrono::seconds(tmp_interval);
}

//
// Background event
// Increment the hostdb_current_timestamp which funcions as our cached version
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

    updateHostFileConfig();
    REC_ReadConfigString(path, "proxy.config.hostdb.host_file.path", sizeof(path));
    if (0 != strcasecmp(hostdb_hostfile_path.string(), path)) {
      Debug("hostdb", "%s",
            ts::bwprint(dbg, R"(Updating hosts file from "{}" to "{}")", hostdb_hostfile_path, ts::bwf::FirstOf(path, "")).c_str());
      // path to hostfile changed
      hostdb_hostfile_update_timestamp = TS_TIME_ZERO; // never updated from this file
      hostdb_hostfile_path             = path;
      update_p                         = true;
    } else if (!hostdb_hostfile_path.empty()) {
      hostdb_last_timestamp = hostdb_current_timestamp;
      std::error_code ec;
      auto stat{ts::file::status(hostdb_hostfile_path, ec)};
      if (!ec) {
        if (ts_clock::from_time_t(modification_time(stat)) > hostdb_hostfile_update_timestamp) {
          update_p = true; // same file but it's changed.
        }
      } else {
        Debug("hostdb", "%s", ts::bwprint(dbg, R"(Failed to stat host file "{}" - {})", hostdb_hostfile_path, ec).c_str());
      }
    }
    if (update_p) {
      Debug("hostdb", "%s", ts::bwprint(dbg, R"(Updating from host file "{}")", hostdb_hostfile_path).c_str());
      UpdateHostsFile(hostdb_hostfile_path, hostdb_hostfile_check_interval);
    }
  }

  return EVENT_CONT;
}

HostDBInfo *
HostDBRecord::select_best_http(ts_time now, ts_seconds fail_window, sockaddr const *hash_addr)
{
  ink_assert(0 < rr_count && rr_count <= hostdb_round_robin_max_count);

  // @a best_any is set to a base candidate, which may be dead.
  HostDBInfo *best_any = nullptr;
  // @a best_alive is set when a valid target has been selected and should be used.
  HostDBInfo *best_alive = nullptr;

  auto info{this->rr_info()};

  if (HostDBProcessor::hostdb_strict_round_robin) {
    // Always select the next viable target - select failure means no valid targets at all.
    best_alive = best_any = this->select_next_rr(now, fail_window);
    Debug("hostdb", "Using strict round robin - index %d", this->index_of(best_alive));
  } else if (HostDBProcessor::hostdb_timed_round_robin > 0) {
    auto ctime = rr_ctime.load(); // cache for atomic update.
    auto ntime = ctime + ts_seconds(HostDBProcessor::hostdb_timed_round_robin);
    // Check and update RR if it's time - this always yields a valid target if there is one.
    if (now > ntime && rr_ctime.compare_exchange_strong(ctime, ntime)) {
      best_alive = best_any = this->select_next_rr(now, fail_window);
      Debug("hostdb", "Round robin timed interval expired - index %d", this->index_of(best_alive));
    } else { // pick the current index, which may be dead.
      best_any = &info[this->rr_idx()];
    }
    Debug("hostdb", "Using timed round robin - index %d", this->index_of(best_any));
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
    Debug("hostdb", "Using client affinity - index %d", this->index_of(best_any));
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

  return best_alive;
}

struct ShowHostDB;
using ShowHostDBEventHandler = int (ShowHostDB::*)(int, Event *);
struct ShowHostDB : public ShowCont {
  char *name;
  uint16_t port;
  IpEndpoint ip;
  bool force;
  bool output_json;
  int records_seen;

  int
  showMain(int event, Event *e)
  {
    CHECK_SHOW(begin("HostDB"));
    CHECK_SHOW(show("<a href=\"./showall\">Show all HostDB records<a/><hr>"));
    CHECK_SHOW(show("<form method = GET action = \"./name\">\n"
                    "Lookup by name (e.g. trafficserver.apache.org):<br>\n"
                    "<input type=text name=name size=64 maxlength=256>\n"
                    "</form>\n"
                    "<form method = GET action = \"./ip\">\n"
                    "Lookup by IP (e.g. 127.0.0.1):<br>\n"
                    "<input type=text name=ip size=64 maxlength=256>\n"
                    "</form>\n"
                    "<form method = GET action = \"./nameforce\">\n"
                    "Force DNS by name (e.g. trafficserver.apache.org):<br>\n"
                    "<input type=text name=name size=64 maxlength=256>\n"
                    "</form>\n"));
    return complete(event, e);
  }

  int
  showLookup(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    SET_HANDLER(&ShowHostDB::showLookupDone);
    if (name) {
      HostDBProcessor::Options opts;
      opts.port  = port;
      opts.flags = HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS;
      hostDBProcessor.getbynameport_re(this, name, strlen(name), opts);
    } else {
      hostDBProcessor.getbyaddr_re(this, &ip.sa);
    }
    return EVENT_CONT;
  }

  int
  showAll(int event, Event *e)
  {
    if (!output_json) {
      CHECK_SHOW(begin("HostDB All Records"));
      CHECK_SHOW(show("<hr>"));
    } else {
      CHECK_SHOW(show("["));
    }
    SET_HANDLER(&ShowHostDB::showAllEvent);
    hostDBProcessor.iterate(this);
    return EVENT_CONT;
  }

  int
  showAllEvent(int event, Event *e)
  {
    if (event == EVENT_INTERVAL) {
      auto *r = reinterpret_cast<HostDBRecord *>(e);
      if (output_json && records_seen++ > 0) {
        CHECK_SHOW(show(",")); // we need to separate records
      }
      auto rr_info{r->rr_info()};
      if (rr_info.count()) {
        if (!output_json) {
          CHECK_SHOW(show("<table border=1>\n"));
          CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Total", r->rr_count));
          CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Current", r->_rr_idx.load()));
          CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "Stale", r->is_ip_configured_stale() ? "Yes" : "No"));
          CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "Timed-Out", r->is_ip_timeout() ? "Yes" : "No"));
          CHECK_SHOW(show("</table>\n"));
        } else {
          CHECK_SHOW(show(",\"%s\":\"%d\",", "rr_total", r->rr_count));
          CHECK_SHOW(show("\"%s\":\"%d\",", "rr_current", r->_rr_idx.load()));
          CHECK_SHOW(show("\"rr_records\":["));
        }
        CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "TTL", r->ip_time_remaining()));

        bool need_separator = false;
        for (auto &item : rr_info) {
          showOne(&item, r->record_type, event, e);
          if (output_json) {
            CHECK_SHOW(show("}")); // we need to separate records
            if (need_separator) {
              CHECK_SHOW(show(","));
            }
            need_separator = true;
          }
        }

        if (!output_json) {
          CHECK_SHOW(show("<br />\n<br />\n"));
        } else {
          CHECK_SHOW(show("]"));
        }
      }

      if (output_json) {
        CHECK_SHOW(show("}"));
      }

    } else if (event == EVENT_DONE) {
      if (output_json) {
        CHECK_SHOW(show("]"));
        return completeJson(event, e);
      } else {
        return complete(event, e);
      }
    } else {
      ink_assert(!"unexpected event");
    }
    return EVENT_CONT;
  }

  int
  showOne(HostDBInfo *info, HostDBType record_type, int event, Event *e)
  {
    ip_text_buffer b;
    if (!output_json) {
      CHECK_SHOW(show("<table border=1>\n"));
      CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "Type", name_of(record_type)));

      if (HostDBType::SRV == record_type) {
        CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "Hostname", info->srvname()));
      }

      // Let's display the hash.
      CHECK_SHOW(show("<tr><td>%s</td><td>%u</td></tr>\n", "LastFailure", info->last_failure.load().time_since_epoch().count()));

      if (HostDBType::SRV == record_type) {
        CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Weight", info->data.srv.srv_weight));
        CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Priority", info->data.srv.srv_priority));
        CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Port", info->data.srv.srv_port));
        CHECK_SHOW(show("<tr><td>%s</td><td>%x</td></tr>\n", "Key", info->data.srv.key));
      } else {
        CHECK_SHOW(show("<tr><td>%s</td><td>%s</td></tr>\n", "IP", info->data.ip.toString(b, sizeof b)));
      }

      CHECK_SHOW(show("</table>\n"));
    } else {
      CHECK_SHOW(show("{"));
      CHECK_SHOW(show("\"%s\":\"%s\",", "type", name_of(record_type)));

      if (HostDBType::SRV == record_type) {
        CHECK_SHOW(show("\"%s\":\"%s\",", "hostname", info->srvname()));
      }

      CHECK_SHOW(show("\"%s\":\"%u\",", "lastfailure", info->last_failure.load().time_since_epoch().count()));

      if (HostDBType::SRV == record_type) {
        CHECK_SHOW(show("\"%s\":\"%d\",", "weight", info->data.srv.srv_weight));
        CHECK_SHOW(show("\"%s\":\"%d\",", "priority", info->data.srv.srv_priority));
        CHECK_SHOW(show("\"%s\":\"%d\",", "port", info->data.srv.srv_port));
        CHECK_SHOW(show("\"%s\":\"%x\",", "key", info->data.srv.key));
      } else {
        CHECK_SHOW(show("\"%s\":\"%s\"", "ip", info->data.ip.toString(b, sizeof b)));
      }
    }
    return EVENT_CONT;
  }

  int
  showLookupDone(int event, Event *e)
  {
    auto *r = reinterpret_cast<HostDBRecord *>(e);

    CHECK_SHOW(begin("HostDB Lookup"));
    if (name) {
      CHECK_SHOW(show("<H2>%s</H2>\n", name));
    } else {
      CHECK_SHOW(show("<H2>%u.%u.%u.%u</H2>\n", PRINT_IP(ip)));
    }
    if (r) {
      auto rr_data = r->rr_info();
      if (rr_data.count()) {
        CHECK_SHOW(show("<table border=1>\n"));
        CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Total", r->rr_count));
        CHECK_SHOW(show("<tr><td>%s</td><td>%d</td></tr>\n", "Current", r->_rr_idx.load()));
        CHECK_SHOW(show("</table>\n"));

        for (auto &item : rr_data) {
          showOne(&item, r->record_type, event, e);
        }
      }
    } else {
      if (!name) {
        ip_text_buffer b;
        CHECK_SHOW(show("<H2>%s Not Found</H2>\n", ats_ip_ntop(&ip.sa, b, sizeof b)));
      } else {
        CHECK_SHOW(show("<H2>%s Not Found</H2>\n", name));
      }
    }
    return complete(event, e);
  }

  ShowHostDB(Continuation *c, HTTPHdr *h)
    : ShowCont(c, h), name(nullptr), port(0), force(false), output_json(false), records_seen(0)
  {
    ats_ip_invalidate(&ip);
    SET_HANDLER(&ShowHostDB::showMain);
  }
};

#define STR_LEN_EQ_PREFIX(_x, _l, _s) (!ptr_len_ncasecmp(_x, _l, _s, sizeof(_s) - 1))

static Action *
register_ShowHostDB(Continuation *c, HTTPHdr *h)
{
  ShowHostDB *s = new ShowHostDB(c, h);
  int path_len;
  const char *path = h->url_get()->path_get(&path_len);

  SET_CONTINUATION_HANDLER(s, &ShowHostDB::showMain);
  if (STR_LEN_EQ_PREFIX(path, path_len, "ip")) {
    s->force = !ptr_len_ncasecmp(path + 3, path_len - 3, "force", 5);
    int query_len;
    const char *query = h->url_get()->query_get(&query_len);
    s->sarg           = ats_strndup(query, query_len);
    char *gn          = nullptr;
    if (s->sarg) {
      gn = static_cast<char *>(memchr(s->sarg, '=', strlen(s->sarg)));
    }
    if (gn) {
      ats_ip_pton(gn + 1, &s->ip); // hope that's null terminated.
    }
    SET_CONTINUATION_HANDLER(s, &ShowHostDB::showLookup);
  } else if (STR_LEN_EQ_PREFIX(path, path_len, "name")) {
    s->force = !ptr_len_ncasecmp(path + 5, path_len - 5, "force", 5);
    int query_len;
    const char *query = h->url_get()->query_get(&query_len);
    s->sarg           = ats_strndup(query, query_len);
    char *gn          = nullptr;
    if (s->sarg) {
      gn = static_cast<char *>(memchr(s->sarg, '=', strlen(s->sarg)));
    }
    if (gn) {
      s->name   = gn + 1;
      char *pos = strstr(s->name, "%3A");
      if (pos != nullptr) {
        s->port = atoi(pos + 3);
        *pos    = '\0'; // Null terminate name
      } else {
        s->port = 0;
      }
    }
    SET_CONTINUATION_HANDLER(s, &ShowHostDB::showLookup);
  } else if (STR_LEN_EQ_PREFIX(path, path_len, "showall")) {
    int query_len     = 0;
    const char *query = h->url_get()->query_get(&query_len);
    if (query && query_len && strstr(query, "json")) {
      s->output_json = true;
    }
    Debug("hostdb", "dumping all hostdb records");
    SET_CONTINUATION_HANDLER(s, &ShowHostDB::showAll);
  }
  this_ethread()->schedule_imm(s);
  return &s->action;
}

static constexpr int HOSTDB_TEST_MAX_OUTSTANDING = 20;
static constexpr int HOSTDB_TEST_LENGTH          = 200;

struct HostDBTestReverse;
using HostDBTestReverseHandler = int (HostDBTestReverse::*)(int, void *);
struct HostDBTestReverse : public Continuation {
  RegressionTest *test;
  int type;
  int *status;

  int outstanding = 0;
  int total       = 0;
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
  eventProcessor.schedule_imm(new HostDBTestReverse(t, atype, pstatus), ET_CACHE);
}
#endif

RecRawStatBlock *hostdb_rsb;

void
ink_hostdb_init(ts::ModuleVersion v)
{
  static int init_called = 0;

  ink_release_assert(v.check(HOSTDB_MODULE_INTERNAL_VERSION));
  if (init_called) {
    return;
  }

  init_called = 1;
  // do one time stuff
  // create a stat block for HostDBStats
  hostdb_rsb = RecAllocateRawStatBlock(static_cast<int>(HostDB_Stat_Count));

  //
  // Register stats
  //

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS, "proxy.process.hostdb.total_lookups", RECD_INT, RECP_PERSISTENT,
                     (int)hostdb_total_lookups_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS, "proxy.process.hostdb.total_hits", RECD_INT, RECP_PERSISTENT,
                     (int)hostdb_total_hits_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS, "proxy.process.hostdb.total_serve_stale", RECD_INT, RECP_PERSISTENT,
                     (int)hostdb_total_serve_stale_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS, "proxy.process.hostdb.ttl", RECD_FLOAT, RECP_PERSISTENT, (int)hostdb_ttl_stat,
                     RecRawStatSyncAvg);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS, "proxy.process.hostdb.ttl_expires", RECD_INT, RECP_PERSISTENT,
                     (int)hostdb_ttl_expires_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS, "proxy.process.hostdb.re_dns_on_reload", RECD_INT, RECP_PERSISTENT,
                     (int)hostdb_re_dns_on_reload_stat, RecRawStatSyncSum);

  RecRegisterRawStat(hostdb_rsb, RECT_PROCESS, "proxy.process.hostdb.insert_duplicate_to_pending_dns", RECD_INT, RECP_PERSISTENT,
                     (int)hostdb_insert_duplicate_to_pending_dns_stat, RecRawStatSyncSum);

  ts_host_res_global_init();
}

struct HostDBFileContinuation : public Continuation {
  using self = HostDBFileContinuation;
  using Keys = std::vector<CryptoHash>;

  int idx          = 0;       ///< Working index.
  const char *name = nullptr; ///< Host name (just for debugging)
  Keys *keys       = nullptr; ///< Entries from file.
  CryptoHash hash;            ///< Key for entry.
  ats_scoped_str path;        ///< Used to keep the host file name around.

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
UpdateHostsFile(ts::file::path const &path, ts_seconds interval)
{
  // Test and set for update in progress.
  bool flag = false;
  if (!HostDBFileUpdateActive.compare_exchange_strong(flag, true)) {
    Debug("hostdb", "Skipped load of host file because update already in progress");
    return;
  }

  std::shared_ptr<HostFile> hf = ParseHostFile(path, interval);

  // Swap the pointer
  if (hf) {
    std::unique_lock lock(hostDB.host_file_mutex);
    hostDB.host_file = hf;
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
  int hosts;
  const char **hostnames;
  RegressionTest *test;
  int type;
  int *status;

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
        rprintf(test, "HostDBRecord r=%x\n", r);
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
    Debug("hostdb", "freeing %d bytes at [%p]", (1 << (7 + _iobuffer_index)), this);
    ioBufAllocator[_iobuffer_index].free_void(static_cast<void *>(this));
  }
}

HostDBRecord *
HostDBRecord::alloc(TextView query_name, unsigned int rr_count, size_t srv_name_size)
{
  const swoc::Scalar<8, ssize_t> qn_size = round_up(query_name.size() + 1);
  const swoc::Scalar<8, ssize_t> r_size  = round_up(sizeof(self_type) + qn_size + rr_count * sizeof(HostDBInfo) + srv_name_size);
  int iobuffer_index                     = iobuffer_size_to_index(r_size, hostdb_max_iobuf_index);
  ink_release_assert(iobuffer_index >= 0);
  auto ptr = ioBufAllocator[iobuffer_index].alloc_void();
  memset(ptr, 0, r_size);
  auto self = static_cast<self_type *>(ptr);
  new (self) self_type();
  self->_iobuffer_index = iobuffer_index;
  self->_record_size    = r_size;

  Debug("hostdb", "allocating %ld bytes for %.*s with %d RR records at [%p]", r_size.value(), int(query_name.size()),
        query_name.data(), rr_count, self);

  // where in our block of memory we are
  int offset = sizeof(self_type);
  memcpy(self->apply_offset<void>(offset), query_name);
  offset          += qn_size;
  self->rr_offset = offset;
  self->rr_count  = rr_count;
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
    Debug_bw("hostdb", "serving stale entry for {}, TTL: {}, serve_stale_for: {}, age: {} as requested by config", name(),
             ip_timeout_interval, hostdb_serve_stale_but_revalidate, ip_age());
    return true;
  }

  // otherwise, the entry is too old
  return false;
}

HostDBInfo *
HostDBRecord::select_best_srv(char *target, InkRand *rand, ts_time now, ts_seconds fail_window)
{
  ink_assert(rr_count <= 0 || static_cast<unsigned int>(rr_count) > hostdb_round_robin_max_count);

  int i           = 0;
  int live_n      = 0;
  uint32_t weight = 0, p = INT32_MAX;
  HostDBInfo *result = nullptr;
  auto rr            = this->rr_info();
  // Array of live targets, sized by @a live_n
  HostDBInfo *live[rr.count()];
  for (auto &target : rr) {
    // skip dead upstreams.
    if (rr[i].is_dead(now, fail_window)) {
      continue;
    }

    if (target.data.srv.srv_priority <= p) {
      p              = target.data.srv.srv_priority;
      weight         += target.data.srv.srv_weight;
      live[live_n++] = &target;
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
    ink_strlcpy(target, this->name(), MAXDNAME);
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
    ts::bwprint(ts::bw_dbg, "[resolve_immediate] success - FQDN '{}' is a valid IP address.", lookup_name);
    Debug("hostdb", "%s", ts::bw_dbg.c_str());
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
