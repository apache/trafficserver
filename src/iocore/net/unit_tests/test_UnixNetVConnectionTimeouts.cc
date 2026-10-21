/** @file

  Catch based unit tests for UnixNetVConnection timeouts driven by the
  InactivityCop's timer wheel.

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

#include "../InactivityCop.h"
#include "../P_UnixNet.h"
#include "../P_UnixNetProcessor.h"
#include "../P_UnixNetVConnection.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <new>
#include <thread>

#if defined(__SANITIZE_ADDRESS__)
#define TEST_HAVE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_HAVE_ASAN 1
#endif
#endif
#if defined(TEST_HAVE_ASAN) && __has_include(<sanitizer/lsan_interface.h>)
#include <sanitizer/lsan_interface.h>
#define TEST_HAVE_LSAN_INTERFACE 1
#endif

// These drive a real UnixNetVConnection, rather than a mock NetEvent, so that
// mainEvent()'s VIO locking and its re-arming are what is under test.

namespace
{

void
register_net_metrics()
{
  net_rsb.default_inactivity_timeout_applied = Metrics::Counter::createPtr("proxy.process.net.default_inactivity_timeout_applied");
  net_rsb.default_inactivity_timeout_count   = Metrics::Counter::createPtr("proxy.process.net.default_inactivity_timeout_count");
  net_rsb.inactivity_cop_lock_acquire_failure =
    Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_lock_acquire_failure");
  net_rsb.inactivity_cop_budget_exhausted = Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_budget_exhausted");
  net_rsb.inactivity_cop_visited          = Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_visited");
  net_rsb.inactivity_cop_passes           = Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_passes");
  net_rsb.inactivity_cop_fired            = Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_fired");
  net_rsb.inactivity_cop_pass_time_us     = Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_pass_time_us");
  net_rsb.inactivity_cop_pass_max_us      = Metrics::Gauge::createPtr("proxy.process.net.inactivity_cop_pass_max_us");
  net_rsb.keep_alive_queue_timeout_count  = Metrics::Counter::createPtr("proxy.process.net.dynamic_keep_alive_timeout_in_count");
  net_rsb.keep_alive_queue_timeout_total  = Metrics::Counter::createPtr("proxy.process.net.dynamic_keep_alive_timeout_in_total");
}

// mainEvent() finds its NetHandler through the thread's private storage, so a
// NetHandler has to live there. The net processor is never started in this
// binary, so reserve the slot and construct one on the test thread.
NetHandler &
thread_net_handler()
{
  static NetHandler *nh = [] {
    if (unix_netProcessor.netHandler_offset == 0) {
      unix_netProcessor.netHandler_offset = eventProcessor.allocate(sizeof(NetHandler));
    }
    ink_release_assert(unix_netProcessor.netHandler_offset > 0);

    auto *h                          = new (get_NetHandler(this_ethread())) NetHandler();
    h->mutex                         = new_ProxyMutex();
    h->thread                        = this_ethread();
    h->max_connections_per_thread_in = 0;
    h->max_requests_per_thread_in    = 0;
    return h;
  }();
  return *nh;
}

// Stands in for the HttpSM: records timeouts and keeps the connection. If
// rearm_with is set it also sets that timeout, as a handler would. The cop is
// driven at future times here while re-arming stamps deadlines from the real
// clock, so a re-armed deadline is already due on the cop's clock; a long
// rearm_with keeps a pass from delivering it again.
struct TimeoutSink : public Continuation {
  UnixNetVConnection *vc         = nullptr;
  ink_hrtime          rearm_with = 0;
  int                 events     = 0;
  int                 last_event = 0;

  TimeoutSink() : Continuation(new_ProxyMutex()) { SET_HANDLER(&TimeoutSink::handle); }

  int
  handle(int event, void * /* data ATS_UNUSED */)
  {
    ++events;
    last_event = event;
    if (rearm_with > 0) {
      vc->set_inactivity_timeout(rearm_with);
    }
    return EVENT_CONT;
  }
};

// Holds a mutex from another EThread, the way a busy HttpSM holds its VIO mutex.
class ForeignHolder
{
public:
  explicit ForeignHolder(Ptr<ProxyMutex> m) : _mutex(std::move(m))
  {
    _worker = std::thread([this] {
      // EThread::~EThread() asserts on a thread that never started normally,
      // so this one is deliberately leaked, as unit_test_main.cc does.
      EThread *t = new EThread;
      t->set_specific();
#ifdef TEST_HAVE_LSAN_INTERFACE
      __lsan_ignore_object(t);
#endif
      MUTEX_TAKE_LOCK(_mutex, t);
      _held.store(true, std::memory_order_release);
      while (!_release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      MUTEX_UNTAKE_LOCK(_mutex.get(), t);
    });
    while (!_held.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  }

  ~ForeignHolder() { release(); }

  void
  release()
  {
    _release.store(true, std::memory_order_release);
    if (_worker.joinable()) {
      _worker.join();
    }
  }

private:
  Ptr<ProxyMutex>   _mutex;
  std::thread       _worker;
  std::atomic<bool> _held{false};
  std::atomic<bool> _release{false};
};

struct Harness {
  NetHandler         &nh;
  InactivityCop       cop;
  TimeoutSink         sink;
  UnixNetVConnection *vc;
  ink_hrtime const    t0;

  explicit Harness(ink_hrtime default_inactivity_in)
    : nh(thread_net_handler()), cop(Ptr<ProxyMutex>(new_ProxyMutex()), nh), vc(new UnixNetVConnection()), t0(ink_get_hrtime())
  {
    register_net_metrics();

    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    nh.timer_wheel.init(t0);

    vc->thread = this_ethread();
    vc->nh     = &nh;
    vc->mutex  = new_ProxyMutex();
    SET_CONTINUATION_HANDLER(vc, &UnixNetVConnection::mainEvent);
    vc->set_default_inactivity_timeout(default_inactivity_in);
    vc->read.vio.op = VIO::READ;
    vc->read.vio.set_continuation(&sink);
    vc->read.enabled = 1;
    sink.vc          = vc;
    nh.startCop(vc);
  }

  ~Harness()
  {
    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    nh.stopCop(vc);
    vc->nh = nullptr;
    delete vc;
  }

  bool
  scheduled() const
  {
    return nh.timer_wheel.is_scheduled(vc);
  }

  void
  run_at(ink_hrtime offset)
  {
    Event event;
    event.ethread = this_ethread();
    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    cop.run(t0 + offset, &event);
  }
};

} // namespace

// The cop pops the vc from the wheel before calling mainEvent(). A VIO lock
// miss there must put it back, or the timeout is never retried.
TEST_CASE("A VIO lock miss in mainEvent retries the timeout on a later tick", "[net][inactivity_cop][unixnetvc]")
{
  Harness h(0);
  h.vc->set_inactivity_timeout(HRTIME_SECONDS(5));
  REQUIRE(h.scheduled());

  {
    ForeignHolder busy(h.sink.mutex);
    h.run_at(HRTIME_SECONDS(7));
    CHECK(h.sink.events == 0);
    INFO("the vc must still be in the wheel after the lock miss");
    CHECK(h.scheduled());
  }

  h.run_at(HRTIME_SECONDS(8));
  CHECK(h.sink.events == 1);
  CHECK(h.sink.last_event == VC_EVENT_INACTIVITY_TIMEOUT);
}

// HttpSM calls set_inactivity_timeout(0) when the transaction's no-activity
// timeout is 0. The default must still apply, as it does for
// cancel_inactivity_timeout().
TEST_CASE("Clearing the inactivity timeout falls back to the default", "[net][inactivity_cop][unixnetvc]")
{
  Harness h(HRTIME_SECONDS(5));
  h.vc->set_inactivity_timeout(HRTIME_SECONDS(60));
  h.vc->set_inactivity_timeout(0);

  CHECK(h.vc->is_default_inactivity_timeout());
  CHECK(h.vc->next_inactivity_timeout_at != 0);
  REQUIRE(h.scheduled());

  h.sink.rearm_with = HRTIME_HOURS(1);
  h.run_at(HRTIME_SECONDS(7));
  CHECK(h.sink.events == 1);
  CHECK(h.sink.last_event == VC_EVENT_INACTIVITY_TIMEOUT);
}

// mainEvent() clears the deadline it delivered. A handler that keeps the
// connection without setting a new timeout must fall back to the default.
TEST_CASE("A connection kept after a delivered timeout falls back to the default", "[net][inactivity_cop][unixnetvc]")
{
  Harness h(HRTIME_SECONDS(30));
  h.vc->set_inactivity_timeout(HRTIME_SECONDS(5));

  h.run_at(HRTIME_SECONDS(7));
  REQUIRE(h.sink.events == 1);

  CHECK(h.vc->is_default_inactivity_timeout());
  CHECK(h.vc->next_inactivity_timeout_at != 0);
  REQUIRE(h.scheduled());

  h.sink.rearm_with = HRTIME_HOURS(1);
  h.run_at(HRTIME_SECONDS(40));
  CHECK(h.sink.events == 2);
}

// With no default, a delivered timeout leaves the vc out of the wheel. The next
// netActivity() re-arms the explicit timeout and must put it back.
TEST_CASE("netActivity re-arms an explicit timeout after a delivered one", "[net][inactivity_cop][unixnetvc]")
{
  Harness h(0);
  h.vc->set_inactivity_timeout(HRTIME_SECONDS(5));

  h.run_at(HRTIME_SECONDS(7));
  REQUIRE(h.sink.events == 1);
  CHECK(h.vc->next_inactivity_timeout_at == 0);
  CHECK_FALSE(h.scheduled());

  h.vc->netActivity();
  CHECK(h.vc->next_inactivity_timeout_at != 0);
  REQUIRE(h.scheduled());

  h.run_at(HRTIME_SECONDS(20));
  CHECK(h.sink.events == 2);
}
