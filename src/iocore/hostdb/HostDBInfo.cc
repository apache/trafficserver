/** @file

  HostDBInfo implementation

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

#include <sys/socket.h>

#include "iocore/hostdb/HostDBProcessor.h"

namespace
{
DbgCtl dbg_ctl_hostdb_info{"hostdb_info"};

/** Assign raw storage to an @c IpAddr
 *
 * @param ip Destination.
 * @param af IP family.
 * @param ptr Raw data for an address of family @a af.
 */
void
ip_addr_set(IpAddr     &ip, ///< Target storage.
            uint8_t     af, ///< Address format.
            void const *ptr ///< Raw address data
)
{
  if (AF_INET6 == af) {
    ip = *static_cast<in6_addr const *>(ptr);
  } else if (AF_INET == af) {
    ip = *static_cast<in_addr_t const *>(ptr);
  } else {
    ip.invalidate();
  }
}
} // namespace

auto
HostDBInfo::assign(sa_family_t af, void const *addr) -> self_type &
{
  type = HostDBType::ADDR;
  ip_addr_set(data.ip, af, addr);
  return *this;
}

auto
HostDBInfo::assign(IpAddr const &addr) -> self_type &
{
  type    = HostDBType::ADDR;
  data.ip = addr;
  return *this;
}

/** Assign SRV record and name
 *
 * @param[in] srv Pointer to SRV from which to assign.
 * @param[in] name Pointer to hostname for the record
 *
 * @note This function assumes that name is stored near to the this pointer of
 * the assigned instance within a uint16_t worth of bytes.  This invariant must
 * be adhered to by the caller.
 *
 * @todo Refactor handling of SRV records
 */
auto
HostDBInfo::assign(SRV const *srv, char const *name) -> self_type &
{
  type                  = HostDBType::SRV;
  data.srv.srv_weight   = srv->weight;
  data.srv.srv_priority = srv->priority;
  data.srv.srv_port     = srv->port;
  data.srv.key          = srv->key;
  // Danger! This offset calculation assumes that name and this are with 16-bits of each
  // other.  This invariant must be upheld for every caller of this function.
  data.srv.srv_offset = name - reinterpret_cast<char const *>(this);
  return *this;
}

char const *
HostDBInfo::srvname() const
{
  return data.srv.srv_offset ? reinterpret_cast<char const *>(this) + data.srv.srv_offset : nullptr;
}

HostDBInfo &
HostDBInfo::operator=(HostDBInfo const &that)
{
  if (this != &that) {
    memcpy(static_cast<void *>(this), static_cast<const void *>(&that), sizeof(*this));
  }
  return *this;
}

ts_time
HostDBInfo::last_fail_time() const
{
  return _last_failure;
}

uint8_t
HostDBInfo::fail_count() const
{
  return _fail_count;
}

HostDBInfo::State
HostDBInfo::state(ts_time now, ts_seconds fail_window) const
{
  auto last_fail = this->last_fail_time();
  if (last_fail == TS_TIME_ZERO) {
    return State::UP;
  }

  if (now <= last_fail + fail_window) {
    return State::DOWN;
  } else {
    return State::SUSPECT;
  }
}

bool
HostDBInfo::is_up() const
{
  return this->last_fail_time() == TS_TIME_ZERO;
}

bool
HostDBInfo::is_down(ts_time now, ts_seconds fail_window) const
{
  return this->state(now, fail_window) == State::DOWN;
}

bool
HostDBInfo::is_suspect(ts_time now, ts_seconds fail_window) const
{
  return this->state(now, fail_window) == State::SUSPECT;
}

/** Mark the target as UP
 *
 * @return @c true if the target was previously DOWN or SUSPECT (i.e., a state change occurred).
 */
bool
HostDBInfo::mark_up()
{
  auto t = _last_failure.exchange(TS_TIME_ZERO);
  _fail_count.store(0);

  return t != TS_TIME_ZERO;
}

/** Mark the entry as DOWN.
 *
 * @param[in] now         Time of the failure.
 * @param[in] fail_window The fail window duration (proxy.config.http.down_server.cache_time).
 * @return @c true if @a this was marked down, @c false if not.
 *
 * Handles two transitions:
 * - UP → DOWN: @c _last_failure is @c TS_TIME_ZERO; set via CAS.
 * - SUSPECT → DOWN: @c fail_window has elapsed since the last failure; @c _last_failure is
 *   refreshed via CAS to restart the fail window.
 *
 * On a successful transition @c _fail_count is reset to zero so that the next SUSPECT window
 * accumulates failures from a clean baseline.
 *
 * Returns @c false if the server is already DOWN (within the active fail window), so the
 * fail window is not refreshed by concurrent failures.
 */
bool
HostDBInfo::mark_down(ts_time now, ts_seconds fail_window)
{
  // UP -> DOWN
  auto t0{TS_TIME_ZERO};
  if (_last_failure.compare_exchange_strong(t0, now)) {
    // Reset so the next SUSPECT window starts with a fresh failure count.
    _fail_count.store(0);
    return true;
  }

  // After the failed CAS, t0 holds the current _last_failure value.
  // SUSPECT -> DOWN: the fail window has elapsed; refresh _last_failure to restart it.
  if (t0 + fail_window < now) {
    if (_last_failure.compare_exchange_strong(t0, now)) {
      // Reset so the next SUSPECT window starts with a fresh failure count.
      _fail_count.store(0);
      return true;
    }
  }

  // Already DOWN; don't refresh the fail window.
  return false;
}

/** Increment the connection failure counter and conditionally mark the target DOWN.
 *
 * @param[in] now         Current time, used as the failure timestamp if the target is marked DOWN.
 * @param[in] max_retries Number of failures that triggers a transition to DOWN.
 * @param[in] fail_window The fail window duration (proxy.config.http.down_server.cache_time).
 * @return A pair { @c marked_down, @c fail_count } where @c marked_down is @c true if this call
 *         caused the target to transition to DOWN (i.e. @c fail_count just reached @a max_retries
 *         and the @c mark_down CAS succeeded), and @c fail_count is the updated counter value.
 *
 * @note @c marked_down can be @c false even when @c fail_count >= @a max_retries if another
 *       thread concurrently marked the target DOWN first (the CAS on @c _last_failure will fail).
 */
std::pair<bool, uint8_t>
HostDBInfo::increment_fail_count(ts_time now, uint8_t max_retries, ts_seconds fail_window)
{
  auto fcount      = ++_fail_count;
  bool marked_down = false;

  Dbg(dbg_ctl_hostdb_info, "fail_count=%d max_retries=%d", fcount, max_retries);

  if (fcount >= max_retries) {
    marked_down = mark_down(now, fail_window);
  }
  return std::make_pair(marked_down, fcount);
}

/** Migrate data after a DNS update.
 *
 * @param[in] that Source item.
 *
 * This moves only specific state information, it is not a generic copy.
 */
void
HostDBInfo::migrate_from(HostDBInfo::self_type const &that)
{
  this->_last_failure = that._last_failure.load();
  this->_fail_count   = that._fail_count.load();
  this->http_version  = that.http_version;
}
