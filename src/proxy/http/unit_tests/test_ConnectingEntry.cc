/** @file

  Catch based unit tests for ConnectingEntry.

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

#include "proxy/http/ConnectingEntry.h"
#include "proxy/http/HttpSM.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Lock.h"
#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/Net.h"
#include "tscore/ink_inet.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::string_view FQDN{"connecting.entry.test.origin"};

ConnectionTracker::TxnConfig &
test_config()
{
  // config_init keeps pointers to these for the records callbacks, so they must outlive the test.
  static ConnectionTracker::GlobalConfig global;
  static ConnectionTracker::TxnConfig    txn;
  static bool                            initialized = false;

  if (!initialized) {
    ink_net_init(NET_SYSTEM_MODULE_PUBLIC_VERSION);
    ConnectionTracker::config_init(&global, &txn, [](const char *, RecDataT, RecData, void *) -> int { return REC_ERR_OKAY; });
    initialized = true;
  }

  return txn;
}

// A reservation on the outbound group for @a addr_text, as HttpSM makes before it creates a ConnectingEntry.
std::shared_ptr<ConnectionTracker::Group>
reserved_group(char const *addr_text)
{
  IpEndpoint addr;

  REQUIRE(0 == ats_ip_pton(addr_text, &addr));

  auto state = ConnectionTracker::obtain_outbound(test_config(), FQDN, addr);

  REQUIRE(state.is_active());
  state.reserve();

  return state.drop();
}

bool
in_outbound_table(std::shared_ptr<ConnectionTracker::Group> const &group)
{
  std::vector<std::shared_ptr<ConnectionTracker::Group const>> groups;

  ConnectionTracker::get_outbound_groups(groups);
  for (auto const &g : groups) {
    if (g == group) {
      return true;
    }
  }

  return false;
}

/* Stands in for an HttpSM queued on a ConnectingEntry. With
 * proxy.config.http.connect_attempts_retry_backoff_base at its default of 0, a queued HttpSM that is
 * told the connect failed retries from inside that call: HandleResponse picks ORIGIN_SERVER_OPEN and
 * set_next_state() calls do_http_server_open(). That looks for a connect to queue on with
 * add_to_existing_request(), then obtains the outbound group and reserves a slot, and throttles the
 * transaction if the count passes server_max. The handler records what that retry would see. It does
 * not run HttpSM itself, which needs HttpConfig started, so that the retry happens inside the
 * notification is taken from HttpSM, not tested here.
 */
class RetryProbeSM : public HttpSM
{
public:
  RetryProbeSM()
  {
    mutex                  = new_ProxyMutex();
    t_state.current.server = &t_state.server_info;
    SET_HANDLER(&RetryProbeSM::state_connect_result);
  }

  void
  expect_retry(IpEndpoint const &addr, bool tracked)
  {
    _addr                 = addr;
    _tracked              = tracked;
    notified_event        = 0;
    count_at_retry        = 0;
    entry_pooled_at_retry = true;
  }

  int
  state_connect_result(int event, void * /* data ATS_UNUSED */)
  {
    notified_event        = event;
    entry_pooled_at_retry = this_ethread()->connecting_pool->m_ip_pool.count(_addr) != 0;
    if (_tracked) {
      auto retry     = ConnectionTracker::obtain_outbound(test_config(), FQDN, _addr);
      count_at_retry = retry.reserve();
      // Group::release(), unlike TxnState::clear(), takes the group out of the table at zero.
      retry.drop()->release();
    }

    return 0;
  }

  int  notified_event        = 0;
  int  count_at_retry        = 0;
  bool entry_pooled_at_retry = true;

private:
  IpEndpoint _addr;
  bool       _tracked = false;
};

// HttpSM::~HttpSM() releases t_state.http_config_param through configProcessor, which aborts while
// HttpConfig was never started. So one probe serves every test and is never destroyed; the static
// pointer keeps it reachable for leak checkers.
RetryProbeSM &
probe_sm()
{
  static RetryProbeSM *sm = new RetryProbeSM();

  return *sm;
}

// The outbound table is a static that is destroyed at exit, after the metrics that ~Group() decrements,
// so a test must take every group it adds out of the table again.
size_t
outbound_group_count()
{
  std::vector<std::shared_ptr<ConnectionTracker::Group const>> groups;

  ConnectionTracker::get_outbound_groups(groups);

  return groups.size();
}

// A reservation the test holds. It is given back through Group::release(), which takes the group out of
// the table at zero, also when a failed REQUIRE ends the test case early.
class HeldReservation
{
public:
  HeldReservation()                                   = default;
  HeldReservation(HeldReservation const &)            = delete;
  HeldReservation &operator=(HeldReservation const &) = delete;
  ~HeldReservation() { release(); }

  // Reserve a slot on the outbound group for @a addr, and return the count the reservation gave.
  int
  reserve(IpEndpoint const &addr)
  {
    auto      state = ConnectionTracker::obtain_outbound(test_config(), FQDN, addr);
    int const count = state.reserve();

    _group = state.drop();

    return count;
  }

  std::shared_ptr<ConnectionTracker::Group> const &
  group() const
  {
    return _group;
  }

  // Pass the reservation on to whoever gives it back next.
  std::shared_ptr<ConnectionTracker::Group>
  handover()
  {
    return std::move(_group);
  }

  void
  release()
  {
    if (_group) {
      _group->release();
      _group.reset();
    }
  }

private:
  std::shared_ptr<ConnectionTracker::Group> _group;
};

EThread *
connecting_thread()
{
  EThread *thread = this_ethread();

  REQUIRE(thread != nullptr);
  if (thread->connecting_pool == nullptr) {
    thread->connecting_pool = new ConnectingPool();
  }

  return thread;
}

// As HttpSM::do_http_server_open() does: hand the reservation, if any, to a new ConnectingEntry, queue
// the HttpSM on it and pool it.
ConnectingEntry *
queue_entry(EThread *thread, RetryProbeSM &sm, IpEndpoint const &addr, std::shared_ptr<ConnectionTracker::Group> group)
{
  auto *entry = new ConnectingEntry();

  entry->mutex = sm.mutex;
  SET_CONTINUATION_HANDLER(entry, &ConnectingEntry::state_http_server_open);
  entry->ipaddr.assign(&addr.sa);
  entry->enable_outbound_connection_tracking(std::move(group));
  entry->connect_sms.insert(&sm);
  thread->connecting_pool->m_ip_pool.insert(std::make_pair(entry->ipaddr, entry));

  return entry;
}

// The connect fails; the entry notifies the queued HttpSM and deletes itself.
void
fail_connect(EThread *thread, RetryProbeSM &sm, ConnectingEntry *entry, int event)
{
  SCOPED_MUTEX_LOCK(lock, sm.mutex, thread);
  entry->handleEvent(event, reinterpret_cast<void *>(static_cast<intptr_t>(-ECONNREFUSED)));
}

} // namespace

// A failed or cancelled connect deletes the entry while it still holds the reservation.
TEST_CASE("ConnectingEntry destructor returns the outbound reservation", "[ConnectingEntry]")
{
  auto group = reserved_group("192.0.2.10:80");

  REQUIRE(group->_count == 1);
  REQUIRE(in_outbound_table(group));

  auto *entry = new ConnectingEntry();

  entry->enable_outbound_connection_tracking(group);
  delete entry;

  CHECK(group->_count == 0);
  CHECK_FALSE(in_outbound_table(group));
}

TEST_CASE("ConnectingEntry destructor releases exactly one reservation", "[ConnectingEntry]")
{
  auto group = reserved_group("192.0.2.11:80");
  auto other = reserved_group("192.0.2.11:80");

  REQUIRE(group == other);
  REQUIRE(group->_count == 2);

  auto *entry = new ConnectingEntry();

  entry->enable_outbound_connection_tracking(group);
  delete entry;

  CHECK(other->_count == 1);
  CHECK(in_outbound_table(other));

  other->release();
}

// With connection tracking off, HttpSM still hands the entry its (empty) group; deleting it must not touch the tracker.
TEST_CASE("ConnectingEntry destructor without connection tracking", "[ConnectingEntry]")
{
  auto *entry = new ConnectingEntry();

  entry->enable_outbound_connection_tracking({});
  delete entry;
}

TEST_CASE("Failed connect returns its reservation before queued HttpSMs retry", "[ConnectingEntry]")
{
  // The four failure events share one branch in ConnectingEntry::state_http_server_open.
  int const event = GENERATE(NET_EVENT_OPEN_FAILED, VC_EVENT_ERROR, VC_EVENT_INACTIVITY_TIMEOUT, VC_EVENT_ACTIVE_TIMEOUT);
  // Connections to the origin that stay open across the failure.
  int const live_connections = GENERATE(0, 1);

  EThread *thread = connecting_thread();
  auto    &sm     = probe_sm();
  // Taken on the first generator run only, so a group left behind fails just the run that left it.
  static size_t const outbound_groups = outbound_group_count();

  IpEndpoint addr;

  REQUIRE(0 == ats_ip_pton("192.0.2.20:443", &addr));
  sm.expect_retry(addr, true);

  HeldReservation live;

  if (live_connections > 0) {
    REQUIRE(live.reserve(addr) == 1);
  }

  HeldReservation first;

  REQUIRE(first.reserve(addr) == live_connections + 1);

  auto group = first.group();

  fail_connect(thread, sm, queue_entry(thread, sm, addr, first.handover()), event);

  REQUIRE(sm.notified_event == event);
  // The retry must neither find the failed entry to queue on nor count its slot. With
  // per_server.connection.max at live_connections + 1, a higher count gets the retry throttled.
  CHECK_FALSE(sm.entry_pooled_at_retry);
  CHECK(sm.count_at_retry == live_connections + 1);
  CHECK(thread->connecting_pool->m_ip_pool.count(addr) == 0);
  // The failed connection's slot is given back exactly once, so the live connection keeps its own.
  CHECK(group->_count == live_connections);

  live.release();
  CHECK(outbound_group_count() == outbound_groups);
}

// With per_server.connection.max, min and the metrics off (the default), do_http_server_open() never
// obtains a group, and the entry gets the empty one that drop() returns. The entry must not touch it.
TEST_CASE("Failed connect with no tracking group notifies queued HttpSMs", "[ConnectingEntry]")
{
  int const event = GENERATE(NET_EVENT_OPEN_FAILED, VC_EVENT_ERROR, VC_EVENT_INACTIVITY_TIMEOUT, VC_EVENT_ACTIVE_TIMEOUT);

  EThread *thread = connecting_thread();
  auto    &sm     = probe_sm();

  IpEndpoint addr;

  REQUIRE(0 == ats_ip_pton("192.0.2.21:443", &addr));
  sm.expect_retry(addr, false);

  fail_connect(thread, sm, queue_entry(thread, sm, addr, ConnectionTracker::TxnState{}.drop()), event);

  CHECK(sm.notified_event == event);
  CHECK_FALSE(sm.entry_pooled_at_retry);
  CHECK(thread->connecting_pool->m_ip_pool.count(addr) == 0);
}
