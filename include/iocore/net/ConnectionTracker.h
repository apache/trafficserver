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

#include "swoc/IntrusiveHashMap.h"

#include <string_view>
#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <tuple>
#include "records/RecCore.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_config.h"
#include "tscore/ink_mutex.h"
#include "tscore/ink_inet.h"
#include "tscore/Diags.h"
#include "tscore/CryptoHash.h"
#include "swoc/bwf_fwd.h"
#include "swoc/TextView.h"
#include <tscore/MgmtDefs.h>
#include "iocore/net/SessionSharingAPIEnums.h"

/**
 * Singleton class to keep track of the number of inbound and outbound connections.
 *
 * Outbound connections are divided into equivalence classes called "groups"
 * here. For outbound connections, groups will vary based on the session
 * matching configuration.  For inbound connections, a group is always based on
 * the remote IP address.  Tracking data is stored for each group.
 */
class ConnectionTracker
{
  using self_type = ConnectionTracker; ///< Self reference type.

public:
  // Non-copyable.
  ConnectionTracker(const self_type &)    = delete;
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
    int server_max{0};                ///< Maximum concurrent server connections.
    int server_min{0};                ///< Minimum keepalive server connections.
    MatchType server_match{MATCH_IP}; ///< Server match type.
  };

  /** Static configuration values. */
  struct GlobalConfig {
    std::chrono::seconds client_alert_delay{60}; ///< Alert delay in seconds.
    std::chrono::seconds server_alert_delay{60}; ///< Alert delay in seconds.
  };

  // The names of the configuration values.
  // Unfortunately these are not used in RecordsConfig.cc so that must be made consistent by hand.
  // Note: These need to be @c constexpr or there are static initialization ordering risks.
  static constexpr std::string_view CONFIG_CLIENT_VAR_ALERT_DELAY{"proxy.config.http.per_client.connection.alert_delay"};
  static constexpr std::string_view CONFIG_SERVER_VAR_MAX{"proxy.config.http.per_server.connection.max"};
  static constexpr std::string_view CONFIG_SERVER_VAR_MIN{"proxy.config.http.per_server.connection.min"};
  static constexpr std::string_view CONFIG_SERVER_VAR_MATCH{"proxy.config.http.per_server.connection.match"};
  static constexpr std::string_view CONFIG_SERVER_VAR_ALERT_DELAY{"proxy.config.http.per_server.connection.alert_delay"};

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

    enum class DirectionType { INBOUND, OUTBOUND };

    DirectionType _direction;                 ///< Whether the group is for inbound or outbound connections.
    IpEndpoint _addr;                         ///< Remote IP address.
    CryptoHash _hash;                         ///< Hash of the FQDN.
    MatchType _match_type{MATCH_IP};          ///< Type of matching.
    std::string _fqdn;                        ///< Expanded FQDN, set if matching on FQDN.
    int _min_keep_alive_conns{0};             /// < Min keep alive conns on this server group
    Key _key;                                 ///< Pre-assembled key which references the following members.
    std::chrono::seconds const &_alert_delay; ///< A reference to client or server alert_delay depending upon connection direction.

    // Counting data.
    std::atomic<int> _count{0};         ///< Number of inbound or outbound connections.
    std::atomic<int> _count_max{0};     ///< largest observed @a count value.
    std::atomic<int> _blocked{0};       ///< Number of connections blocked since last alert.
    std::atomic<int> _in_queue{0};      ///< # of connections queued, waiting for a connection.
    std::atomic<Ticker> _last_alert{0}; ///< Absolute time of the last alert.

    /** Constructor.
     * Construct from @c Key because the use cases do a table lookup first so the @c Key is already constructed.
     * @param key A populated @c Key structure - values are copied to the @c Group.
     * @param fqdn The full FQDN.
     * @param min_keep_alive The minimum number of origin keep alive connections to maintain.
     */
    Group(DirectionType direction, Key const &key, std::string_view fqdn, int min_keep_alive);
    ~Group();
    /// Key equality checker.
    static bool equal(Key const &lhs, Key const &rhs);
    /// Hashing function.
    static size_t hash(Key const &);
    /// Check and clear alert enable.
    /// This is a modifying call - internal state will be updated to prevent too frequent alerts.
    /// @param lat The last alert time, in epoch seconds, if the method returns @c true.
    /// @return @c true if an alert should be generated, @c false otherwise.
    bool should_alert(std::time_t *lat = nullptr);
    /// Time of the last alert in epoch seconds.
    std::time_t get_last_alert_epoch_time() const;

    /// Release the reference count to this group and remove it from the
    /// group table if it is no longer referenced.
    void release();
  };

  /// Container for per transaction state and operations.
  struct TxnState {
    std::shared_ptr<Group> _g; ///< Active group for this transaction.
    bool _reserved_p{false};   ///< Set if a connection slot has been reserved.
    bool _queued_p{false};     ///< Set if the connection is delayed / queued.

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
    /// Clear all reservations.
    void clear();
    /// Transfer ownership of the group outside of this state.
    /// @return The group for this reservation.
    std::shared_ptr<Group> drop();
    /// Update the maximum observed count if needed against @a count.
    void update_max_count(int count);

    /** Generate a Notice that the group has become unblocked.
     *
     * @param config Transaction local configuration.
     * @param count Current connection count for display in message.
     * @param addr IP address of the upstream.
     */
    void Note_Unblocked(const TxnConfig *config, int count, const sockaddr *addr);

    /** Generate a Warning that a connection was blocked.
     *
     * @param max_connections The maximum configured number of connections for the group.
     * @param sm_id ID to display in Warning.
     * @param count Count value to display in Warning.
     * @param addr IP address of the upstream.
     * @param debug_tag Tag to use for the debug message. If no debug message should be generated set this to @c nullptr.
     */
    void Warn_Blocked(int max_connections, int64_t id, int count, const sockaddr *addr, const char *debug_tag = nullptr);
  };

  /** Get or create the @c Group for the specified inbound session properties.
   * @param addr The IP address of the client.
   * @return A @c Group for the arguments, existing if possible and created if not.
   */
  static TxnState obtain_inbound(IpEndpoint const &addr);

  /** Get or create the @c Group for the specified outbound session properties.
   * @param txn_cnf The transaction local configuration.
   * @param fqdn The fully qualified domain name of the upstream.
   * @param addr The IP address of the upstream.
   * @return A @c Group for the arguments, existing if possible and created if not.
   */
  static TxnState obtain_outbound(TxnConfig const &txn_cnf, std::string_view fqdn, IpEndpoint const &addr);

  /** Get the currently existing outbound groups.
   * @param [out] groups parameter - pointers to the groups are pushed in to this container.
   *
   * The groups are loaded in to @a groups, which is cleared before loading. Note the groups returned will remain valid
   * although data inside the groups is volatile.
   */
  static void get_outbound_groups(std::vector<std::shared_ptr<Group const>> &groups);
  /** Write the outbound connection tracking data to JSON.
   * @return string containing a JSON encoding of the table.
   */
  static std::string outbound_to_json_string();
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
   * @param config_cb The callback to invoke when a configuration is updated.
   */
  static void config_init(GlobalConfig *global, TxnConfig *txn, RecConfigUpdateCb const &config_cb);

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
  static const MgmtConverter MIN_SERVER_CONV;
  static const MgmtConverter MAX_SERVER_CONV;
  static const MgmtConverter SERVER_MATCH_CONV;

protected:
  static GlobalConfig *_global_config; ///< Global configuration data.

  /// Provide std::unordered_map compatible hash and equality functions for @c Group.
  struct GroupMapHelper {
    using key_type   = Group::Key const &;
    using value_type = Group;

    /// Return the hash of @a key.
    size_t operator()(key_type &key) const;

    /// Compare @a lhs and @a rhs for equality.
    bool operator()(key_type &lhs, key_type &rhs) const;
  };

  /// Internal implementation class instance.
  struct TableSingleton {
    friend ConnectionTracker::Group;
    std::unordered_map<Group::Key, std::shared_ptr<Group>, GroupMapHelper, GroupMapHelper>
      _table;          ///< Hash table of connection groups.
    std::mutex _mutex; ///< Lock for insert, delete, and find.
  };
  static TableSingleton _inbound_table;
  static TableSingleton _outbound_table;

  /// Get the implementation instance.
  /// @note This is done purely to allow subclasses to reuse methods in this class.
  TableSingleton &inbound_instance();
  TableSingleton &outbound_instance();
};

inline ConnectionTracker::TableSingleton &
ConnectionTracker::inbound_instance()
{
  return _inbound_table;
}

inline ConnectionTracker::TableSingleton &
ConnectionTracker::outbound_instance()
{
  return _outbound_table;
}

inline size_t
ConnectionTracker::Group::hash(const Key &key)
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
ConnectionTracker::TxnState::is_active()
{
  return nullptr != _g;
}

inline int
ConnectionTracker::TxnState::reserve()
{
  _reserved_p = true;
  return ++_g->_count;
}

inline void
ConnectionTracker::TxnState::release()
{
  if (_reserved_p) {
    _reserved_p = false;
    --_g->_count;
  }
}

inline std::shared_ptr<ConnectionTracker::Group>
ConnectionTracker::TxnState::drop()
{
  _reserved_p = false;
  return std::move(_g);
}

inline int
ConnectionTracker::TxnState::enqueue()
{
  _queued_p = true;
  return ++_g->_in_queue;
}

inline void
ConnectionTracker::TxnState::dequeue()
{
  if (_queued_p) {
    _queued_p = false;
    --_g->_in_queue;
  }
}

inline void
ConnectionTracker::TxnState::clear()
{
  if (_g) {
    this->dequeue();
    this->release();
    _g = nullptr;
  }
}

inline void
ConnectionTracker::TxnState::update_max_count(int count)
{
  auto cmax = _g->_count_max.load();
  if (count > cmax) {
    _g->_count_max.compare_exchange_weak(cmax, count);
  }
}

inline void
ConnectionTracker::TxnState::blocked()
{
  ++_g->_blocked;
}

/* === GroupMapHelper === */
inline size_t
ConnectionTracker::GroupMapHelper::operator()(key_type &key) const
{
  return Group::hash(key);
}

inline bool
ConnectionTracker::GroupMapHelper::operator()(key_type &lhs, key_type &rhs) const
{
  return Group::equal(lhs, rhs);
}
/* === */

namespace swoc
{
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ConnectionTracker::MatchType type);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ConnectionTracker::Group::Key const &key);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ConnectionTracker::Group const &g);
} // namespace swoc
