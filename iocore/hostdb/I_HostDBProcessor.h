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

#include <chrono>
#include <atomic>

#include "tscore/HashFNV.h"
#include "tscore/ink_time.h"
#include "tscore/CryptoHash.h"
#include "tscore/ink_align.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_resolver.h"
#include "tscore/HTTPVersion.h"

#include "swoc/MemSpan.h"

#include "I_EventSystem.h"
#include "SRV.h"

// Event returned on a lookup
#define EVENT_HOST_DB_LOOKUP       (HOSTDB_EVENT_EVENTS_START + 0)
#define EVENT_HOST_DB_IP_REMOVED   (HOSTDB_EVENT_EVENTS_START + 1)
#define EVENT_HOST_DB_GET_RESPONSE (HOSTDB_EVENT_EVENTS_START + 2)

#define EVENT_SRV_LOOKUP       (SRV_EVENT_EVENTS_START + 0)
#define EVENT_SRV_IP_REMOVED   (SRV_EVENT_EVENTS_START + 1)
#define EVENT_SRV_GET_RESPONSE (SRV_EVENT_EVENTS_START + 2)

//
// Data
//
struct HostDBContinuation;
struct ResolveInfo;

//
// The host database stores host information, most notably the
// IP address.
//
// Since host information is relatively small, we can afford to have
// a reasonable size memory cache, and use a (relatively) sparse
// disk representation to decrease # of seeks.
//
extern int hostdb_enable;
/** Epoch timestamp of the current hosts file check.
 *
 * This also functions as a cached version of ts_clock::now(). Since it is
 * updated in backgroundEvent which runs every second, it should be
 * approximately accurate to a second.
 */
extern std::atomic<ts_time> hostdb_current_timestamp;

/** How long before any DNS response is consided stale, regardless of DNS TTL.
 * This corresponds to proxy.config.hostdb.verify_after.
 */
extern unsigned int hostdb_ip_stale_interval;
extern unsigned int hostdb_ip_timeout_interval;
extern unsigned int hostdb_ip_fail_timeout_interval;
extern unsigned int hostdb_serve_stale_but_revalidate;
extern unsigned int hostdb_round_robin_max_count;

extern int hostdb_max_iobuf_index;

static inline unsigned int
makeHostHash(const char *string)
{
  ink_assert(string && *string);

  if (string && *string) {
    ATSHash32FNV1a fnv;
    fnv.update(string, strlen(string), ATSHash::nocase());
    fnv.final();
    return fnv.get();
  }

  return 0;
}

//
// Types
//

class HostDBRecord;

/// Information for an SRV record.
struct SRVInfo {
  unsigned int srv_offset   : 16; ///< Memory offset from @c HostDBInfo to name.
  unsigned int srv_weight   : 16;
  unsigned int srv_priority : 16;
  unsigned int srv_port     : 16;
  unsigned int key;
};

/// Type of data stored.
enum class HostDBType : uint8_t {
  UNSPEC, ///< No valid data.
  ADDR,   ///< IP address.
  SRV,    ///< SRV record.
  HOST    ///< Hostname (reverse DNS)
};
char const *name_of(HostDBType t);

/** Information about a single target.
 */
struct HostDBInfo {
  using self_type = HostDBInfo; ///< Self reference type.

  /// Default constructor.
  HostDBInfo() = default;

  HostDBInfo &operator=(HostDBInfo const &that);

  /// Absolute time of when this target failed.
  /// A value of zero (@c TS_TIME_ZERO ) indicates no failure.
  ts_time last_fail_time() const;

  /// Target is alive - no known failure.
  bool is_alive();

  /// Target has failed and is still in the blocked time window.
  bool is_dead(ts_time now, ts_seconds fail_window);

  /** Select this target.
   *
   * @param now Current time.
   * @param fail_window Failure window.
   * @return Status of the selection.
   *
   * If a zombie is selected the failure time is updated to make it look dead to other threads in a thread safe
   * manner. The caller should check @c last_fail_time to see if a zombie was selected.
   */
  bool select(ts_time now, ts_seconds fail_window);

  /// Check if this info is valid.
  bool is_valid() const;

  /// Mark this info as invalid.
  void invalidate();

  /** Mark the entry as down.
   *
   * @param now Time of the failure.
   * @return @c true if @a this was marked down, @c false if not.
   *
   * This can return @c false if the entry is already marked down, in which case the failure time is not updated.
   */
  bool mark_down(ts_time now);

  /** Mark the target as up / alive.
   *
   * @return Previous alive state of the target.
   */
  bool mark_up();

  char const *srvname() const;

  /** Migrate data after a DNS update.
   *
   * @param that Source item.
   *
   * This moves only specific state information, it is not a generic copy.
   */
  void migrate_from(self_type const &that);

  /// A target is either an IP address or an SRV record.
  /// The type should be indicated by @c flags.f.is_srv;
  union {
    IpAddr ip;   ///< IP address / port data.
    SRVInfo srv; ///< SRV record.
  } data{IpAddr{}};

  /// Data that migrates after updated DNS records are processed.
  /// @see migrate_from
  /// @{
  /// Last time a failure was recorded.
  std::atomic<ts_time> last_failure{TS_TIME_ZERO};
  /// Count of connection failures
  std::atomic<uint8_t> fail_count{0};
  /// Expected HTTP version of the target based on earlier transactions.
  HTTPVersion http_version = HTTP_INVALID;
  /// @}

  self_type &assign(IpAddr const &addr);

protected:
  self_type &assign(sa_family_t af, void const *addr);
  self_type &assign(SRV const *srv, char const *name);

  HostDBType type = HostDBType::UNSPEC; ///< Invalid data.

  friend HostDBContinuation;
};

inline HostDBInfo &
HostDBInfo::operator=(HostDBInfo const &that)
{
  if (this != &that) {
    memcpy(static_cast<void *>(this), static_cast<const void *>(&that), sizeof(*this));
  }
  return *this;
}

inline ts_time
HostDBInfo::last_fail_time() const
{
  return last_failure;
}

inline bool
HostDBInfo::is_alive()
{
  return this->last_fail_time() == TS_TIME_ZERO;
}

inline bool
HostDBInfo::is_dead(ts_time now, ts_seconds fail_window)
{
  auto last_fail = this->last_fail_time();
  return (last_fail != TS_TIME_ZERO) && (last_fail + fail_window < now);
}

inline bool
HostDBInfo::mark_up()
{
  auto t = last_failure.exchange(TS_TIME_ZERO);
  return t != TS_TIME_ZERO;
}

inline bool
HostDBInfo::mark_down(ts_time now)
{
  auto t0{TS_TIME_ZERO};
  return last_failure.compare_exchange_strong(t0, now);
}

inline bool
HostDBInfo::select(ts_time now, ts_seconds fail_window)
{
  auto t0 = this->last_fail_time();
  if (t0 == TS_TIME_ZERO) {
    return true; // it's alive and so is valid for selection.
  }
  // Success means this is a zombie and this thread updated the failure time.
  return (t0 + fail_window < now) && last_failure.compare_exchange_strong(t0, now);
}

inline void
HostDBInfo::migrate_from(HostDBInfo::self_type const &that)
{
  this->last_failure = that.last_failure.load();
  this->http_version = that.http_version;
}

inline bool
HostDBInfo::is_valid() const
{
  return type != HostDBType::UNSPEC;
}

inline void
HostDBInfo::invalidate()
{
  type = HostDBType::UNSPEC;
}

// ----
/** Root item for HostDB.
 * This is the container for HostDB data. It is always an array of @c HostDBInfo instances plus metadata.
 * All strings are C-strings and therefore don't need a distinct size.
 *
 */
class HostDBRecord : public RefCountObj
{
  friend struct HostDBContinuation;
  friend struct ShowHostDB;
  using self_type = HostDBRecord;

  /// Size of the IO buffer block owned by @a this.
  /// If negative @a this is in not allocated memory.
  int _iobuffer_index{-1};
  /// Actual size of the data.
  unsigned _record_size = sizeof(self_type);

public:
  HostDBRecord()                      = default;
  HostDBRecord(self_type const &that) = delete;

  using Handle = Ptr<HostDBRecord>; ///< Shared pointer type to hold an instance.

  /** Allocate an instance from the IOBuffers.
   *
   * @param query_name Name of the query for the record.
   * @param rr_count Number of info instances.
   * @param srv_name_size Storage for SRV names, if any.
   * @return An instance sufficient to hold the specified data.
   *
   * The query name will stored and initialized, and the info instances initialized.
   */
  static self_type *alloc(ts::TextView query_name, unsigned rr_count, size_t srv_name_size = 0);

  /// Type of data stored in this record.
  HostDBType record_type = HostDBType::UNSPEC;

  /// IP family of this record.
  sa_family_t af_family = AF_UNSPEC;

  /// Offset from @a this to the VLA.
  unsigned short rr_offset = 0;

  /// Number of @c HostDBInfo instances.
  unsigned short rr_count = 0;

  /// Timing data for switch records in the RR.
  std::atomic<ts_time> rr_ctime{TS_TIME_ZERO};

  /// Hash key.
  uint64_t key{0};

  /// When the DNS response was received.
  ts_time ip_timestamp;

  /// Valid duration of the DNS response data.
  /// In the code this functions as the TTL in HostDB calculations, but may not
  /// be the response's TTL based upon configuration such as
  /// proxy.config.hostdb.ttl_mode.
  ts_seconds ip_timeout_interval;

  /** Atomically advance the round robin index.
   *
   * If multiple threads call this simultaneously each thread will get a distinct return value.
   *
   * @return The new round robin index.
   */
  unsigned next_rr();

  /** Pick the next round robin and update the record atomically.
   *
   * @note This may select a zombie server and reserve it for the caller, therefore the caller must
   * attempt to connect to the selected target if possible.
   *
   * @param now Current time to use for aliveness calculations.
   * @param fail_window Blackout time for dead servers.
   * @return Status of the updated target.
   *
   * If the return value is @c HostDBInfo::Status::DEAD this means all targets are dead and there is
   * no valid upstream.
   *
   * @note Concurrency - this is not done under lock and depends on the caller for correct use.
   * For strict round robin, it is a feature that every call will get a distinct index. For
   * timed round robin, the caller must arrange to have only one thread call this per time interval.
   */
  HostDBInfo *select_next_rr(ts_time now, ts_seconds fail_window);

  /// Check if this record is of SRV targets.
  bool is_srv() const;

  /** Query name for the record.
   * @return A C-string.
   * If this is a @c HOST record, this is the resolved named and the query was based on the IP address.
   * Otherwise this is the name used in the DNS query.
   */
  char const *name() const;

  /** Query name for the record.
   * @return A view.
   * If this is a @c HOST record, this is the resolved named and the query was based on the IP address.
   * Otherwise this is the name used in the DNS query.
   * @note Although not included in the view, the name is always nul terminated and the string can
   * be used as a C-string.
   */
  ts::TextView name_view() const;

  /// Get the array of info instances.
  swoc::MemSpan<HostDBInfo> rr_info();

  /** Find a host record by IP address.
   *
   * @param addr Address key.
   * @return A pointer to the info instance if a match is found, @c nullptr if not.
   */
  HostDBInfo *find(sockaddr const *addr);

  /** Select an upstream target.
   *
   * @param now Current time.
   * @param fail_window Dead server blackout time.
   * @param hash_addr Inbound remote IP address.
   * @return A selected target, or @c nullptr if there are no valid targets.
   *
   * This accounts for the round robin setting. The default is to use "client affinity" in
   * which case @a hash_addr is as a hash seed to select the target.
   *
   * This may select a zombie target, which can be detected by checking the target's last
   * failure time. If it is not @c TS_TIME_ZERO the target is a zombie. Other transactions will
   * be blocked from selecting that target until @a fail_window time has passed.
   *
   * In cases other than strict round robin, a base target is selected. If valid, that is returned,
   * but if not then the targets in this record are searched until a valid one is found. The result
   * is this can be called to select a target for failover when a previous target fails.
   */
  HostDBInfo *select_best_http(ts_time now, ts_seconds fail_window, sockaddr const *hash_addr);
  HostDBInfo *select_best_srv(char *target, InkRand *rand, ts_time now, ts_seconds fail_window);

  bool is_failed() const;

  void set_failed();

  /// @return The time point when the item expires.
  ts_time expiry_time() const;

  /// @return The age of the DNS response.
  ts_seconds ip_age() const;

  /// @return How long before the DNS response becomes stale (i.e., exceeds @a
  /// ip_timeout_interval from the time of the response).
  ts_seconds ip_time_remaining() const;

  /// @return Whether the age of the DNS response exceeds @a the user's
  /// configured proxy.config.hostdb.verify_after value.
  bool is_ip_configured_stale() const;

  /** Whether we have exceeded the DNS response's TTL (i.e., whether the DNS
   * response is stale). */
  bool is_ip_timeout() const;

  bool is_ip_fail_timeout() const;

  void refresh_ip();

  /** Whether the DNS response can still be used per
   * proxy.config.hostdb.serve_stale_for configuration.
   *
   * @return False if serve_stale_for is not configured or if the DNS
   * response's age is less than TTL + the serve_stale_for value. Note that
   * this function will return true for DNS responses whose age is less than
   * TTL (i.e., for responses that are not yet stale).
   */
  bool serve_stale_but_revalidate() const;

  /// Deallocate @a this.
  void free() override;

  /** The current round robin index.
   *
   * @return The current index.
   *
   * @note The internal index may be out of range due to concurrency constraints - this insures the
   * returned value is in range.
   */
  unsigned short rr_idx() const;

  /** Offset from the current round robin index.
   *
   * @param delta Distance from the current index.
   * @return The effective index.
   */
  unsigned short rr_idx(unsigned short delta) const;

  /// The index of @a target in this record.
  int index_of(HostDBInfo const *target) const;

  /** Allocation and initialize an instance from a serialized buffer.
   *
   * @param buff Serialization data.
   * @param size Size of @a buff.
   * @return An instance initialized from @a buff.
   */
  static self_type *unmarshall(char *buff, unsigned size);

  /// Database version.
  static constexpr ts::VersionNumber Version{3, 0};

protected:
  /// Current active info.
  /// @note This value may be out of range due to the difficulty of synchronization, therefore
  /// must always be taken modulus @c rr_count when used. Use the @c rr_idx() method unless
  /// raw access is required.
  std::atomic<unsigned short> _rr_idx = 0;

  /** Access an internal object at @a offset.
   *
   * @tparam T Type of object.
   * @param offset Offset of object.
   * @return A pointer to the object of type @a T.
   *
   * @a offset is applied to @a this record and the result cast to a pointer to @a T.
   *
   * @note @a offset based at @a this.
   */
  template <typename T>
  T *
  apply_offset(unsigned offset)
  {
    return reinterpret_cast<T *>(reinterpret_cast<char *>(this) + offset);
  }

  template <typename T>
  T const *
  apply_offset(unsigned offset) const
  {
    return reinterpret_cast<T const *>(reinterpret_cast<char const *>(this) + offset);
  }

  union {
    uint16_t all;
    struct {
      unsigned failed_p : 1; ///< DNS error.
    } f;
  } flags{0};
};

struct HostDBCache;
struct HostDBHash;

// Prototype for inline completion function or
//  getbyname_imm()
using cb_process_result_pfn = void (Continuation::*)(HostDBRecord *r);

Action *iterate(Continuation *cont);

/** Information for doing host resolution for a request.
 *
 * This is effectively a state object for a request attempting to connect upstream. Information about its attempt
 * that are local to the request are kept here, while shared data is accessed via the @c HostDBInfo pointers.
 *
 * A primitive version of the IP address generator concept.
 */
struct ResolveInfo {
  using self_type = ResolveInfo; ///< Self reference type.

  /// Not quite sure what this is for.
  enum UpstreamResolveStyle { UNDEFINED_LOOKUP, ORIGIN_SERVER, PARENT_PROXY, HOST_NONE };

  /** Origin server address source selection.

      If config says to use CTA (client target addr) state is TRY_CLIENT, otherwise it
      remains the default. If the connect fails then we switch to a USE. We go to USE_HOSTDB if (1)
      the HostDB lookup is successful and (2) some address other than the CTA is available to try.
      Otherwise we keep retrying on the CTA (USE_CLIENT) up to the max retry value.  In essence we
      try to treat the CTA as if it were another RR value in the HostDB record.
   */
  enum class OS_Addr {
    TRY_DEFAULT, ///< Initial state, use what config says.
    TRY_HOSTDB,  ///< Try HostDB data.
    TRY_CLIENT,  ///< Try client target addr.
    USE_HOSTDB,  ///< Force use of HostDB target address.
    USE_CLIENT,  ///< Force client target addr.
    USE_API      ///< Use the API provided address.
  };

  ResolveInfo()  = default;
  ~ResolveInfo() = default;

  /// Keep a reference to the base HostDB object, so it doesn't get GC'd.
  Ptr<HostDBRecord> record;
  HostDBInfo *active = nullptr; ///< Active host record.

  /// Working address. The meaning / source of the value depends on other elements.
  /// This is the "resolved" address if @a resolved_p is @c true.
  IpEndpoint addr;

  int attempts = 0; ///< Number of connection attempts.

  char const *lookup_name             = nullptr;
  char srv_hostname[MAXDNAME]         = {0};
  const sockaddr *inbound_remote_addr = nullptr; ///< Remote address of inbound client - used for hashing.
  in_port_t srv_port                  = 0;       ///< Port from SRV lookup or API call.

  OS_Addr os_addr_style           = OS_Addr::TRY_DEFAULT;
  HostResStyle host_res_style     = HOST_RES_IPV4;
  UpstreamResolveStyle looking_up = UNDEFINED_LOOKUP;

  HTTPVersion http_version = HTTP_INVALID;

  bool resolved_p = false; ///< If there is a valid, resolved address in @a addr.

  /// Flag for @a addr being set externally.
  //  bool api_addr_set_p = false;

  /*** Set to true by default.  If use_client_target_address is set
   * to 1, this value will be set to false if the client address is
   * not in the DNS pool */
  bool cta_validated_p = true;

  bool set_active(HostDBInfo *info);

  bool set_active(sockaddr const *s);

  bool set_active(std::nullptr_t);

  /** Force a resolved address.
   *
   * @param sa Address to use for the upstream.
   * @return @c true if successful, @c false if error.
   *
   * This fails if @a sa isn't a valid IP address.
   */
  bool set_upstream_address(const sockaddr *sa);

  bool set_upstream_address(IpAddr const &ip_addr);

  void set_upstream_port(in_port_t port);

  /** Check and (if possible) immediately resolve the upstream address without consulting the HostDB.
   * The cases where this is successful are
   * - The address is already resolved (@a resolved_p is @c true).
   * - The upstream was set explicitly.
   * - The hostname is a valid IP address.
   *
   * @return @c true if the upstream address was resolved, @c false if not.
   */
  bool resolve_immediate();

  /** Mark the active target as dead.
   *
   * @param now Time of failure.
   * @return @c true if the server was marked as dead, @c false if not.
   *
   */
  bool mark_active_server_dead(ts_time now);

  /** Mark the active target as alive.
   *
   * @return @c true if the target changed state.
   */
  bool mark_active_server_alive();

  /// Select / resolve to the next RR entry for the record.
  bool select_next_rr();

  bool is_srv() const;
};

/** The Host Database access interface. */
struct HostDBProcessor : public Processor {
  friend struct HostDBSync;
  // Public Interface

  // Lookup Hostinfo by name
  //    cont->handleEvent( EVENT_HOST_DB_LOOKUP, HostDBInfo * ); on success
  //    cont->handleEVent( EVENT_HOST_DB_LOOKUP, 0); on failure
  // Failure occurs when the host cannot be DNS-ed
  // NOTE: Will call the continuation back before returning if data is in the
  //       cache.  The HostDBInfo * becomes invalid when the callback returns.
  //       The HostDBInfo may be changed during the callback.

  enum {
    HOSTDB_DO_NOT_FORCE_DNS   = 0,
    HOSTDB_ROUND_ROBIN        = 0,
    HOSTDB_FORCE_DNS_RELOAD   = 1,
    HOSTDB_FORCE_DNS_ALWAYS   = 2,
    HOSTDB_DO_NOT_ROUND_ROBIN = 4
  };

  /// Optional parameters for getby...
  struct Options {
    typedef Options self;                                  ///< Self reference type.
    int port                    = 0;                       ///< Target service port (default 0 -> don't care)
    int flags                   = HOSTDB_DO_NOT_FORCE_DNS; ///< Processing flags (default HOSTDB_DO_NOT_FORCE_DNS)
    int timeout                 = 0;                       ///< Timeout value (default 0 -> default timeout)
    HostResStyle host_res_style = HOST_RES_IPV4;           ///< How to query host (default HOST_RES_IPV4)

    Options() {}
    /// Set the flags.
    self &
    setFlags(int f)
    {
      flags = f;
      return *this;
    }
  };

  /// Default options.
  static Options const DEFAULT_OPTIONS;

  HostDBProcessor() {}
  Action *getbyname_re(Continuation *cont, const char *hostname, int len, Options const &opt = DEFAULT_OPTIONS);

  Action *getbynameport_re(Continuation *cont, const char *hostname, int len, Options const &opt = DEFAULT_OPTIONS);

  Action *getSRVbyname_imm(Continuation *cont, cb_process_result_pfn process_srv_info, const char *hostname, int len,
                           Options const &opt = DEFAULT_OPTIONS);

  Action *getbyname_imm(Continuation *cont, cb_process_result_pfn process_hostdb_info, const char *hostname, int len,
                        Options const &opt = DEFAULT_OPTIONS);

  Action *iterate(Continuation *cont);

  /** Lookup Hostinfo by addr */
  Action *getbyaddr_re(Continuation *cont, sockaddr const *aip);

  /** Configuration. */
  static int hostdb_strict_round_robin;
  static int hostdb_timed_round_robin;

  // Processor Interface
  /* hostdb does not use any dedicated event threads
   * currently. Dont pass any value to start
   */
  int start(int no_of_additional_event_threads = 0, size_t stacksize = DEFAULT_STACKSIZE) override;

  // Private
  HostDBCache *cache();

private:
  Action *getby(Continuation *cont, cb_process_result_pfn cb_process_result, HostDBHash &hash, Options const &opt);
};

inline bool
HostDBRecord::is_srv() const
{
  return HostDBType::SRV == record_type;
}

inline char const *
HostDBRecord::name() const
{
  return this->apply_offset<char const>(sizeof(self_type));
}

inline ts::TextView
HostDBRecord::name_view() const
{
  return {this->name(), ts::TextView::npos};
}

inline ts_time
HostDBRecord::expiry_time() const
{
  return ip_timestamp + ip_timeout_interval + ts_seconds(hostdb_serve_stale_but_revalidate);
}

inline ts_seconds
HostDBRecord::ip_age() const
{
  static constexpr ts_seconds ZERO{0};
  static constexpr ts_seconds MAX{0x7FFFFFFF};

  return std::clamp(std::chrono::duration_cast<ts_seconds>(hostdb_current_timestamp.load() - ip_timestamp), ZERO, MAX);
}

inline ts_seconds
HostDBRecord::ip_time_remaining() const
{
  static constexpr ts_seconds ZERO{0};
  static constexpr ts_seconds MAX{0x7FFFFFFF};
  return std::clamp(std::chrono::duration_cast<ts_seconds>(ip_timeout_interval - this->ip_age()), ZERO, MAX);
}

inline bool
HostDBRecord::is_ip_configured_stale() const
{
  return (
    ((ip_timeout_interval >= ts_seconds(2 * hostdb_ip_stale_interval)) && (ip_age() >= ts_seconds(hostdb_ip_stale_interval))));
}

inline bool
HostDBRecord::is_ip_timeout() const
{
  return ip_age() >= ip_timeout_interval;
}

inline bool
HostDBRecord::is_ip_fail_timeout() const
{
  return ip_age() >= ts_seconds(hostdb_ip_fail_timeout_interval);
}

inline void
HostDBRecord::refresh_ip()
{
  ip_timestamp = hostdb_current_timestamp;
}

inline swoc::MemSpan<HostDBInfo>
HostDBRecord::rr_info()
{
  return {this->apply_offset<HostDBInfo>(rr_offset), rr_count};
}

inline bool
HostDBRecord::is_failed() const
{
  return flags.f.failed_p;
}

inline void
HostDBRecord::set_failed()
{
  flags.f.failed_p = true;
}

inline unsigned short
HostDBRecord::rr_idx() const
{
  return _rr_idx % rr_count;
}

inline unsigned short
HostDBRecord::rr_idx(unsigned short delta) const
{
  return (_rr_idx + delta) % rr_count;
}

inline int
HostDBRecord::index_of(HostDBInfo const *target) const
{
  return target ? target - this->apply_offset<HostDBInfo>(rr_offset) : -1;
}

// --

inline bool
ResolveInfo::set_active(sockaddr const *s)
{
  return this->set_active(record->find(s));
}

inline bool
ResolveInfo::mark_active_server_alive()
{
  return active->mark_up();
}

inline bool
ResolveInfo::mark_active_server_dead(ts_time now)
{
  return active != nullptr && active->mark_down(now);
}

inline bool
ResolveInfo::set_active(std::nullptr_t)
{
  active     = nullptr;
  resolved_p = false;
  return false;
}

inline bool
ResolveInfo::set_upstream_address(sockaddr const *sa)
{
  return resolved_p = addr.assign(sa).isValid();
}

inline void
ResolveInfo::set_upstream_port(in_port_t port)
{
  srv_port = port;
}

inline bool
ResolveInfo::is_srv() const
{
  return record && record->is_srv();
}
// ---

void run_HostDBTest();

extern HostDBProcessor hostDBProcessor;

void ink_hostdb_init(ts::ModuleVersion version);
