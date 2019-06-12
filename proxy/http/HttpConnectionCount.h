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

#include <string_view>
#include <chrono>
#include <atomic>
#include <sstream>
#include <tuple>
#include <mutex>
#include "tscore/ink_platform.h"
#include "tscore/ink_config.h"
#include "tscore/ink_mutex.h"
#include "tscore/ink_inet.h"
#include "tscore/IntrusiveHashMap.h"
#include "tscore/Diags.h"
#include "tscore/CryptoHash.h"
#include "tscore/BufferWriterForward.h"
#include "tscpp/util/TextView.h"
#include <MgmtDefs.h>
#include "HttpProxyAPIEnums.h"
#include "Show.h"

/**
 * Singleton class to keep track of the number of outbound connections.
 *
 * Outbound connections are divided in to equivalence classes (called "groups" here) based on the
 * session matching setting. Tracking data is stored for each group.
 */
class OutboundConnTrack
{
  using self_type = OutboundConnTrack; ///< Self reference type.

public:
  // Non-copyable.
  OutboundConnTrack(const self_type &) = delete;
  self_type &operator=(const self_type &) = delete;

  /// Definition of an upstream server group equivalence class.
  enum MatchType {
    MATCH_IP   = TS_SERVER_OUTBOUND_MATCH_IP,   ///< Match by IP address.
    MATCH_PORT = TS_SERVER_OUTBOUND_MATCH_PORT, ///< Match by IP address and port.
    MATCH_HOST = TS_SERVER_OUTBOUND_MATCH_HOST, ///< Match by hostname (FQDN).
    MATCH_BOTH = TS_SERVER_OUTBOUND_MATCH_BOTH, ///< Hostname, IP Address and port.
  };

  /// String equivalents for @c MatchType.
  static const std::array<std::string_view, static_cast<int>(MATCH_BOTH) + 1> MATCH_TYPE_NAME;

  /// Per transaction configuration values.
  struct TxnConfig {
    int max{0};                ///< Maximum concurrent connections.
    MatchType match{MATCH_IP}; ///< Match type.
  };

  /** Static configuration values. */
  struct GlobalConfig {
    int queue_size{0};                          ///< Maximum delayed transactions.
    std::chrono::milliseconds queue_delay{100}; ///< Reschedule / queue delay in ms.
    std::chrono::seconds alert_delay{60};       ///< Alert delay in seconds.
  };

  // The names of the configuration values.
  // Unfortunately these are not used in RecordsConfig.cc so that must be made consistent by hand.
  // Note: These need to be @c constexpr or there are static initialization ordering risks.
  static constexpr std::string_view CONFIG_VAR_MAX{"proxy.config.http.per_server.connection.max"_sv};
  static constexpr std::string_view CONFIG_VAR_MATCH{"proxy.config.http.per_server.connection.match"_sv};
  static constexpr std::string_view CONFIG_VAR_QUEUE_SIZE{"proxy.config.http.per_server.connection.queue_size"_sv};
  static constexpr std::string_view CONFIG_VAR_QUEUE_DELAY{"proxy.config.http.per_server.connection.queue_delay"_sv};
  static constexpr std::string_view CONFIG_VAR_ALERT_DELAY{"proxy.config.http.per_server.connection.alert_delay"_sv};

  /// A record for the outbound connection count.
  /// These are stored per outbound session equivalence class, as determined by the session matching.
  struct Group {
    /// Base clock.
    using Clock = std::chrono::system_clock;
    /// Time point type, based on the clock to be used.
    using TimePoint = Clock::time_point;
    /// Raw type for clock / time point counts.
    using Ticker = TimePoint::rep;
    /// Length of time to suppress alerts for a group.
    static const std::chrono::seconds ALERT_DELAY;

    /// Equivalence key - two groups are equivalent if their keys are equal.
    struct Key {
      IpEndpoint const &_addr;      ///< Remote IP address.
      CryptoHash const &_hash;      ///< Hash of the FQDN.
      MatchType const &_match_type; ///< Type of matching.
    };

    IpEndpoint _addr;      ///< Remote IP address.
    CryptoHash _hash;      ///< Hash of the FQDN.
    MatchType _match_type; ///< Type of matching.
    std::string _fqdn;     ///< Expanded FQDN, set if matching on FQDN.
    Key _key;              ///< Pre-assembled key which references the following members.

    // Counting data.
    std::atomic<int> _count{0};         ///< Number of outbound connections.
    std::atomic<int> _count_max{0};     ///< largest observed @a count value.
    std::atomic<int> _blocked{0};       ///< Number of outbound connections blocked since last alert.
    std::atomic<int> _rescheduled{0};   ///< # of connection reschedules.
    std::atomic<int> _in_queue{0};      ///< # of connections queued, waiting for a connection.
    std::atomic<Ticker> _last_alert{0}; ///< Absolute time of the last alert.

    // Links for intrusive container.
    Group *_next{nullptr};
    Group *_prev{nullptr};

    /** Constructor.
     * Construct from @c Key because the use cases do a table lookup first so the @c Key is already constructed.
     * @param key A populated @c Key structure - values are copied to the @c Group.
     * @param fqdn The full FQDN.
     */
    Group(Key const &key, std::string_view fqdn);
    /// Key equality checker.
    static bool equal(Key const &lhs, Key const &rhs);
    /// Hashing function.
    static uint64_t hash(Key const &);
    /// Check and clear alert enable.
    /// This is a modifying call - internal state will be updated to prevent too frequent alerts.
    /// @param lat The last alert time, in epoch seconds, if the method returns @c true.
    /// @return @c true if an alert should be generated, @c false otherwise.
    bool should_alert(std::time_t *lat = nullptr);
    /// Time of the last alert in epoch seconds.
    std::time_t get_last_alert_epoch_time() const;
  };

  /// Container for per transaction state and operations.
  struct TxnState {
    Group *_g{nullptr};      ///< Active group for this transaction.
    bool _reserved_p{false}; ///< Set if a connection slot has been reserved.
    bool _queued_p{false};   ///< Set if the connection is delayed / queued.

    /// Check if tracking is active.
    bool is_active();

    /// Reserve a connection.
    int reserve();
    /// Release a connection reservation.
    void release();
    /// Reserve a queue / retry slot.
    int enqueue();
    /// Release a block
    void dequeue();
    /// Note blocking a transaction.
    void blocked();
    /// Note a rescheduling
    void rescheduled();
    /// Clear all reservations.
    void clear();
    /// Drop the reservation - assume it will be cleaned up elsewhere.
    /// @return The group for this reservation.
    Group *drop();
    /// Update the maximum observed count if needed against @a count.
    void update_max_count(int count);

    /** Generate a Notice that the group has become unblocked.
     *
     * @param config Transaction local configuration.
     * @param count Current connection count for display in message.
     * @param addr IP address of the upstream.
     */
    void Note_Unblocked(TxnConfig *config, int count, const sockaddr *addr);

    /** Generate a Warning that a connection was blocked.
     *
     * @param config Transaction local configuration.
     * @param sm_id State machine ID to display in Warning.
     * @param count Count value to display in Warning.
     * @param addr IP address of the upstream.
     * @param debug_tag Tag to use for the debug message. If no debug message should be generated set this to @c nullptr.
     */
    void Warn_Blocked(TxnConfig *config, int64_t sm_id, int count, const sockaddr *addr, const char *debug_tag = nullptr);
  };

  /** Get or create the @c Group for the specified session properties.
   * @param txn_cnf The transaction local configuration.
   * @param fqdn The fully qualified domain name of the upstream.
   * @param addr The IP address of the upstream.
   * @return A @c Group for the arguments, existing if possible and created if not.
   */
  static TxnState obtain(TxnConfig const &txn_cnf, std::string_view fqdn, const IpEndpoint &addr);

  /** Get the currently existing groups.
   * @param [out] groups parameter - pointers to the groups are pushed in to this container.
   *
   * The groups are loaded in to @a groups, which is cleared before loading. Note the groups returned will remain valid
   * although data inside the groups is volatile.
   */
  static void get(std::vector<Group const *> &groups);
  /** Write the connection tracking data to JSON.
   * @return string containing a JSON encoding of the table.
   */
  static std::string to_json_string();
  /** Write the groups to @a f.
   * @param f Output file.
   */
  static void dump(FILE *f);
  /** Do global initialization.
   *
   * This sets up the global configuration and any configuration update callbacks needed. It is presumed
   * the caller has set up the actual storage where the global configuration data is stored.
   *
   * @param config The storage for the global configuration data.
   * @param txn The storage for the default per transaction data.
   */
  static void config_init(GlobalConfig *global, TxnConfig *txn);

  /// Tag used for debugging output.
  static constexpr char const *const DEBUG_TAG{"conn_track"};

  /** Convert a string to a match type.
   *
   * @a type is updated only if this method returns @c true.
   *
   * @param [in] tag Tag to look up.
   * @param [out] type Resulting type.
   * @return @c true if @a tag was valid and @a type was updated, otherwise @c false.
   */
  static bool lookup_match_type(std::string_view tag, MatchType &type);

  /** Generate a warning message for a bad @c MatchType tag.
   *
   * @param tag The invalid tag.
   */
  static void Warning_Bad_Match_Type(std::string_view tag);

  // Converters for overridable values for use in the TS API.
  static const MgmtConverter MAX_CONV;
  static const MgmtConverter MATCH_CONV;

protected:
  static GlobalConfig *_global_config; ///< Global configuration data.

  /// Types and methods for the hash table.
  struct Linkage {
    using key_type   = Group::Key const &;
    using value_type = Group;

    static value_type *&next_ptr(value_type *value);
    static value_type *&prev_ptr(value_type *value);

    static uint64_t hash_of(key_type key);

    static key_type key_of(value_type *v);

    static bool equal(key_type lhs, key_type rhs);
  };

  /// Internal implementation class instance.
  struct Imp {
    IntrusiveHashMap<Linkage> _table; ///< Hash table of upstream groups.
    std::mutex _mutex;                ///< Lock for insert & find.
  };
  static Imp _imp;

  /// Get the implementation instance.
  /// @note This is done purely to allow subclasses to reuse methods in this class.
  Imp &instance();
};

inline OutboundConnTrack::Imp &
OutboundConnTrack::instance()
{
  return _imp;
}

inline OutboundConnTrack::Group::Group(Key const &key, std::string_view fqdn)
  : _hash(key._hash), _match_type(key._match_type), _key{_addr, _hash, _match_type}
{
  // store the host name if relevant.
  if (MATCH_HOST == _match_type || MATCH_BOTH == _match_type) {
    _fqdn.assign(fqdn);
  }
  // store the IP address if relevant.
  if (MATCH_HOST == _match_type) {
    _addr.setToAnyAddr(AF_INET);
  } else {
    ats_ip_copy(_addr, key._addr);
  }
}

inline uint64_t
OutboundConnTrack::Group::hash(const Key &key)
{
  switch (key._match_type) {
  case MATCH_IP:
    return ats_ip_hash(&key._addr.sa);
  case MATCH_PORT:
    return ats_ip_port_hash(&key._addr.sa);
  case MATCH_HOST:
    return key._hash.fold();
  case MATCH_BOTH:
    return ats_ip_port_hash(&key._addr.sa) ^ key._hash.fold();
  default:
    return 0;
  }
}

inline bool
OutboundConnTrack::TxnState::is_active()
{
  return nullptr != _g;
}

inline int
OutboundConnTrack::TxnState::reserve()
{
  _reserved_p = true;
  return ++_g->_count;
}

inline void
OutboundConnTrack::TxnState::release()
{
  if (_reserved_p) {
    _reserved_p = false;
    --_g->_count;
  }
}

inline OutboundConnTrack::Group *
OutboundConnTrack::TxnState::drop()
{
  _reserved_p = false;
  return _g;
}

inline int
OutboundConnTrack::TxnState::enqueue()
{
  _queued_p = true;
  return ++_g->_in_queue;
}

inline void
OutboundConnTrack::TxnState::dequeue()
{
  if (_queued_p) {
    _queued_p = false;
    --_g->_in_queue;
  }
}

inline void
OutboundConnTrack::TxnState::clear()
{
  if (_g) {
    this->dequeue();
    this->release();
    _g = nullptr;
  }
}

inline void
OutboundConnTrack::TxnState::update_max_count(int count)
{
  auto cmax = _g->_count_max.load();
  if (count > cmax) {
    _g->_count_max.compare_exchange_weak(cmax, count);
  }
}

inline void
OutboundConnTrack::TxnState::blocked()
{
  ++_g->_blocked;
}

inline void
OutboundConnTrack::TxnState::rescheduled()
{
  ++_g->_rescheduled;
}

/* === Linkage === */
inline auto
OutboundConnTrack::Linkage::next_ptr(value_type *value) -> value_type *&
{
  return value->_next;
}

inline auto
OutboundConnTrack::Linkage::prev_ptr(value_type *value) -> value_type *&
{
  return value->_prev;
}

inline uint64_t
OutboundConnTrack::Linkage::hash_of(key_type key)
{
  return Group::hash(key);
}

inline auto
OutboundConnTrack::Linkage::key_of(value_type *value) -> key_type
{
  return value->_key;
}

inline bool
OutboundConnTrack::Linkage::equal(key_type lhs, key_type rhs)
{
  return Group::equal(lhs, rhs);
}
/* === */

Action *register_ShowConnectionCount(Continuation *, HTTPHdr *);

namespace ts
{
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, OutboundConnTrack::MatchType type);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, OutboundConnTrack::Group::Key const &key);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, OutboundConnTrack::Group const &g);
} // namespace ts
