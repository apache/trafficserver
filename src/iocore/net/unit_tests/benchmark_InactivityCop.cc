/** @file

  Microbenchmark for InactivityCop, the once-per-second per-thread pass that
  checks NetEvents for an inactivity or active timeout.

  This measures InactivityCop as driven off NetHandler's timer wheel: each
  call visits only the deadlines the wheel hands it, not every open
  connection. No production code is exercised by a real net processor here:
  NetEvents are mocked, and the cop is driven synchronously on the Catch2
  main thread, which unit_test_main.cc already makes a valid EThread via
  EThread::set_specific().

  The cop is driven from a synthetic clock (Fixture::_now), advanced by
  exactly one TimerWheel<NetEvent>::TICK per call via InactivityCop::run(),
  not ink_get_hrtime(). The wheel's resolution is one second and production
  schedules the cop once per second, so one call == one tick is what
  production actually does; real time would either need a real sleep between
  every call or make most calls advance no tick at all (and do nothing),
  neither of which is a usable per-tick measurement. Every scenario below
  arms deadlines relative to fx._now for exactly this reason: a deadline
  armed from ink_get_hrtime() while the cop runs off the synthetic clock
  would drift out of sync with it.

  Not using Catch2's BENCHMARK macro: the cop is stateful across invocations
  (a fired deadline is dropped from the wheel until something re-arms it, and
  scenarios like keepalive, churn, and mass_expiry deliberately re-arm
  deadlines between calls), and BENCHMARK re-runs its body an adaptive,
  unlogged number of times to converge, which would silently multiply those
  state mutations in ways no scenario here is designed to tolerate. BENCHMARK
  also cannot report the get_mutex/get_thread touch counters, which are the
  primary, hardware-independent metrics this file is built around. A
  hand-rolled mean/min/max over a fixed, logged SAMPLE_RUNS is less polished
  but keeps both properties intact - do not "modernize" this to BENCHMARK
  without preserving them.

  Run only the benchmarks: ./test_net "[!benchmark]"

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
#include "../P_UnixNetVConnection.h"
#include "../P_Net.h"

#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/Lock.h"
#include "tscore/ink_hrtime.h"
#include "tsutil/Metrics.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#if defined(__has_feature)
#if __has_feature(address_sanitizer) && __has_include(<sanitizer/lsan_interface.h>)
#include <sanitizer/lsan_interface.h>
#define BENCH_HAVE_LSAN_INTERFACE 1
#endif
#endif

#include <sys/utsname.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
// --- Trap 1: net_rsb is a zero-initialized global of raw metric pointers.
// register_net_stats() (Net.cc) fills it in, but it is `static inline` and
// unreachable here, and it is never called anyway because we never start a
// real net processor. Populate by hand exactly the fields check_inactivity()
// touches, once, no matter how many fixtures get built.
void
ensure_net_metrics_registered()
{
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  net_rsb.inactivity_cop_lock_acquire_failure =
    Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_lock_acquire_failure");
  net_rsb.inactivity_cop_visited             = Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_visited");
  net_rsb.inactivity_cop_budget_exhausted    = Metrics::Counter::createPtr("proxy.process.net.inactivity_cop_budget_exhausted");
  net_rsb.default_inactivity_timeout_applied = Metrics::Counter::createPtr("proxy.process.net.default_inactivity_timeout_applied");
  net_rsb.default_inactivity_timeout_count   = Metrics::Counter::createPtr("proxy.process.net.default_inactivity_timeout_count");
  net_rsb.keep_alive_queue_timeout_count     = Metrics::Counter::createPtr("proxy.process.net.dynamic_keep_alive_timeout_in_count");
  net_rsb.keep_alive_queue_timeout_total     = Metrics::Counter::createPtr("proxy.process.net.dynamic_keep_alive_timeout_in_total");
}

// Frozen at commit 300f40c9 to sizeof(UnixNetVConnection) as it stood then.
// Deliberately NOT tied to a live sizeof(UnixNetVConnection): the timer
// wheel this benchmark exists to baseline will change that type's layout
// (new hook/deadline fields, cop_link removed), and if the mock's stride
// moved with it, a later checkpoint would stop measuring the same
// cache-miss profile - a shrinking UnixNetVConnection would make the new
// algorithm look faster for a reason that has nothing to do with the
// algorithm. Bump this only as a deliberate, called-out rebaseline.
constexpr size_t MOCK_FOOTPRINT_BYTES = 1584; // sizeof(UnixNetVConnection) at 300f40c9

constexpr uint32_t SHUFFLE_SEED = 0xC0FFEE;
constexpr int      WARMUP_RUNS  = 2;
constexpr int      SAMPLE_RUNS  = 25;

std::vector<size_t> const N_VALUES = {1000, 10000, 100000};

// Written only by the thread driving the cop (the Catch2 main thread that
// owns the Fixture). The ContentionHolder background thread only ever
// touches Ptr<ProxyMutex> objects directly; it never calls into
// MockNetEvent, so there is no second writer here. Keeping these as plain
// counters (not std::atomic) matters: get_thread()/get_mutex() are exactly
// the per-connection calls the timer wheel removes, and an atomic RMW on
// every one of ~100000 calls per timed run would inflate the *current*
// number with instrumentation overhead that has nothing to do with the
// algorithm - overhead a later "faster" measurement would not be paying,
// making the reported speedup look bigger than the real one.
struct Counters {
  uint64_t get_mutex_touches  = 0;
  uint64_t get_thread_touches = 0;
  uint64_t callbacks          = 0;
};

// Trap 4: get_thread() must return the same EThread the cop is driven from,
// or the refill loop's `ne->get_thread() == this_ethread()` skips every
// mock and the benchmark measures nothing.
class MockNetEvent : public NetEvent
{
public:
  MockNetEvent(EThread *thread, Counters *counters) : _thread(thread), _counters(counters) { mutex = new_ProxyMutex(); }

  void
  net_read_io(NetHandler *) override
  {
  }
  void
  net_write_io(NetHandler *) override
  {
  }
  void
  free_thread(EThread *) override
  {
  }

  int
  callback(int event = CONTINUATION_EVENT_NONE, void * /* data */ = nullptr) override
  {
    ++_counters->callbacks;
    last_event = event;
    return EVENT_DONE;
  }

  /// Which timeout this mock was last told about. The queue-management path
  /// picks between VC_EVENT_INACTIVITY_TIMEOUT and VC_EVENT_ACTIVE_TIMEOUT, and
  /// that choice is the thing worth asserting.
  int last_event = CONTINUATION_EVENT_NONE;

  // Mirrors UnixNetVConnection::set_inactivity_timeout / set_default_inactivity_timeout /
  // is_default_inactivity_timeout (UnixNetVConnection.cc:1290-1310).
  // Mirrors UnixNetVConnection, including its use of ink_get_hrtime(). No
  // scenario calls this - they set the deadline fields directly from the
  // fixture's synthetic clock - so the real-time stamp here is currently
  // inert. If you do start exercising it, pass the fixture's _now in instead:
  // a deadline on the real clock while the cop runs off the synthetic one
  // drifts out of sync with it and the scenario silently measures nothing.
  void
  set_inactivity_timeout(ink_hrtime timeout_in) override
  {
    inactivity_timeout_in      = timeout_in;
    next_inactivity_timeout_at = (timeout_in > 0) ? ink_get_hrtime() + timeout_in : 0;
  }

  void
  set_default_inactivity_timeout(ink_hrtime timeout_in) override
  {
    default_inactivity_timeout_in = timeout_in;
  }

  bool
  is_default_inactivity_timeout() override
  {
    return use_default_inactivity_timeout && inactivity_timeout_in == 0;
  }

  EThread *
  get_thread() override
  {
    ++_counters->get_thread_touches;
    return _thread;
  }

  int
  close() override
  {
    return 0;
  }

  int
  get_fd() override
  {
    return -1;
  }

  Ptr<ProxyMutex> &
  get_mutex() override
  {
    ++_counters->get_mutex_touches;
    return mutex;
  }

  ContFlags &
  get_control_flags() override
  {
    return _flags;
  }

  Ptr<ProxyMutex> mutex;

private:
  EThread  *_thread;
  Counters *_counters;
  ContFlags _flags;
};

// Trap 3: pad every mock out to a fixed, frozen footprint so the sweep's
// cost profile (cache misses over scattered, connection-sized objects)
// matches production instead of a flattering dense std::vector<Mock>.
struct PaddedMock : public MockNetEvent {
  using MockNetEvent::MockNetEvent;
  char _pad[MOCK_FOOTPRINT_BYTES > sizeof(MockNetEvent) ? MOCK_FOOTPRINT_BYTES - sizeof(MockNetEvent) : 1];
};

// If either of these fire, MockNetEvent (or the live UnixNetVConnection, see
// the [sizes] line printed at test-run start) has drifted since the
// footprint was frozen above; that is exactly the situation the frozen
// constant exists to make loud instead of silently absorbing into a
// _pad[1].
static_assert(sizeof(MockNetEvent) <= MOCK_FOOTPRINT_BYTES, "MockNetEvent grew past the frozen mock footprint; shrink it or bump "
                                                            "MOCK_FOOTPRINT_BYTES as a deliberate, called-out rebaseline");
static_assert(sizeof(PaddedMock) >= MOCK_FOOTPRINT_BYTES, "PaddedMock must be at least the frozen mock footprint");

std::string
git_short_sha()
{
  std::array<char, 64> buf{};
  FILE                *pipe = popen("git rev-parse --short HEAD 2>/dev/null", "r");
  if (pipe == nullptr) {
    return "unknown";
  }
  std::string result;
  if (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
    result = buf.data();
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
      result.pop_back();
    }
  }
  pclose(pipe);
  return result.empty() ? "unknown" : result;
}

std::string
host_name()
{
  utsname u{};
  if (uname(&u) == 0) {
    return std::string(u.nodename);
  }
  return "unknown";
}

// Printed exactly once, from a Catch2 listener's testRunStarting, so it
// cannot migrate between test cases or vanish under a filtered / `--order
// rand` run. This is the provenance a saved table needs to stay
// interpretable later: what was measured (sizes), what varied (seed, N,
// sample counts), and where it came from (commit, build, host).
void
print_provenance_once()
{
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  std::printf("\n[provenance] commit=%s build=%s host=%s\n", git_short_sha().c_str(),
#ifdef DEBUG
              "debug",
#else
              "release",
#endif
              host_name().c_str());
  std::printf("[provenance] N_VALUES=1000,10000,100000 MOCK_FOOTPRINT_BYTES=%zu SAMPLE_RUNS=%d WARMUP_RUNS=%d shuffle_seed=0x%X\n",
              MOCK_FOOTPRINT_BYTES, SAMPLE_RUNS, WARMUP_RUNS, SHUFFLE_SEED);
  std::printf("[sizes] sizeof(MockNetEvent)=%zu sizeof(PaddedMock)=%zu sizeof(UnixNetVConnection)=%zu (frozen footprint=%zu)\n",
              sizeof(MockNetEvent), sizeof(PaddedMock), sizeof(UnixNetVConnection), MOCK_FOOTPRINT_BYTES);
  if (sizeof(UnixNetVConnection) != MOCK_FOOTPRINT_BYTES) {
    std::printf("[sizes] NOTE: live sizeof(UnixNetVConnection) differs from the frozen footprint; numbers are not directly "
                "comparable to the 300f40c9 baseline until MOCK_FOOTPRINT_BYTES is deliberately updated\n");
  }
}

class ProvenanceListener final : public Catch::EventListenerBase
{
public:
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const &) override
  {
    print_provenance_once();
  }
};

struct Fixture {
  NetHandler                               nh;
  Counters                                 counters;
  std::vector<std::unique_ptr<PaddedMock>> mocks;
  InactivityCop                            cop;
  // The clock InactivityCop::run() is driven from. Seeded from
  // ink_get_hrtime() so it starts in the same neighborhood as the real clock
  // (relevant only in that nothing here compares it against ink_get_hrtime()
  // directly), then advanced by exactly one TimerWheel<NetEvent>::TICK per
  // run()/warmup() call and nothing else. Every scenario must arm deadlines
  // relative to this, not ink_get_hrtime(), or its notion of "now" drifts
  // from the cop's.
  ink_hrtime _now;

  explicit Fixture(size_t n) : cop(Ptr<ProxyMutex>(new_ProxyMutex()), nh), _now(ink_get_hrtime())
  {
    ensure_net_metrics_registered();

    // NetHandler::configure_per_thread_values() computes config.max_connections_in /
    // eventProcessor.thread_group[ET_NET]._count. ET_NET is #defined to
    // ET_CALL (include/iocore/net/Net.h), and unit_test_main.cc's
    // eventProcessor.start(test_threads) spawns test_threads == 1 ET_CALL
    // threads, so that count is 1 here, not 0 - calling
    // configure_per_thread_values() would not actually crash in this
    // harness. We still don't call it: setting max_connections_per_thread_in
    // / max_requests_per_thread_in directly to 0 keeps this fixture
    // decoupled from that production wiring and guarantees
    // manage_active_queue()/manage_keep_alive_queue() early-return
    // regardless of whatever eventProcessor state this binary happens to be
    // in.
    nh.mutex  = new_ProxyMutex();
    nh.thread = this_ethread();
    // Production initializes the wheel's cursor to real time in
    // initialize_thread_for_net() (UnixNet.cc) before anything is scheduled;
    // mirror that with the fixture's own clock instead of ink_get_hrtime(),
    // so the wheel's cursor and every scenario's armed deadlines share one
    // clock.
    nh.timer_wheel.init(_now);
    nh.config.max_connections_in         = 0;
    nh.config.max_requests_in            = 0;
    nh.config.default_inactivity_timeout = 30;
    nh.max_connections_per_thread_in     = 0;
    nh.max_requests_per_thread_in        = 0;

    build(n);
  }

  // Mocks are destroyed by ~Fixture() without ever passing through
  // stopCop(), which is startCop()'s only cancel path in production. Cancel
  // by hand here so no mock is destroyed while still linked into
  // nh.timer_wheel -- a dangling wheel entry into freed memory is a real
  // use-after-free under ASan even if nothing in this binary ever walks the
  // bucket again.
  ~Fixture()
  {
    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    for (auto &m : mocks) {
      nh.stopCop(m.get());
    }
  }

  void
  build(size_t n)
  {
    mocks.clear();
    mocks.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      // Trap 3: individually `new`-allocated, not a dense vector<Mock>.
      mocks.push_back(std::make_unique<PaddedMock>(this_ethread(), &counters));
    }

    std::vector<PaddedMock *> order;
    order.reserve(n);
    for (auto &m : mocks) {
      order.push_back(m.get());
    }
    // Fixed seed: traversal order (open_list) differs from allocation order
    // reproducibly, so the sweep pays for real scattered access every run.
    std::mt19937 rng(SHUFFLE_SEED);
    std::shuffle(order.begin(), order.end(), rng);
    // Goes through the real NetHandler::startCop() (not a direct
    // open_list.enqueue()) so the eager global-default-timeout application it
    // does is exercised here too; startCop() asserts the NetHandler mutex is
    // held by this thread. This is setup, not the timed region: every
    // scenario's setup() overwrites default_inactivity_timeout_in before any
    // warmup/sample call, so this has no effect on measured numbers.
    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    for (auto *m : order) {
      m->nh = &nh;
      nh.startCop(m);
    }
  }

  // Advances _now by exactly one tick and runs the cop against it. Shared by
  // run()/warmup() so "a call is a tick" cannot drift between the two.
  void
  step()
  {
    Event event;
    event.ethread = this_ethread();
    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    _now += TimerWheel<NetEvent>::TICK;
    cop.run(_now, &event);
  }

  // An untimed cop pass, one tick, for priming state before sampling begins.
  void
  warmup()
  {
    step();
  }

  // One cop pass, one tick, timed by the caller.
  void
  run()
  {
    step();
  }
};

double
to_ms(std::chrono::steady_clock::duration d)
{
  return std::chrono::duration<double, std::milli>(d).count();
}

struct Sample {
  double   wall_ms;
  uint64_t get_mutex_touches;
  uint64_t get_thread_touches;
  uint64_t callbacks;
  uint64_t lock_failures;
  /// Read from the production proxy.process.net.inactivity_cop_visited metric,
  /// so scenario assertions can cross-check it against the mock's own counts.
  uint64_t visited;
};

struct Summary {
  double   mean_ms            = 0.0;
  double   min_ms             = 0.0;
  double   max_ms             = 0.0;
  uint64_t get_mutex_touches  = 0;
  uint64_t get_thread_touches = 0;
  uint64_t callbacks          = 0;
  uint64_t lock_failures      = 0;
};

Summary
summarize(std::vector<Sample> &samples)
{
  Summary s;
  double  sum = 0.0;
  for (auto const &sample : samples) {
    sum += sample.wall_ms;
  }
  s.mean_ms = sum / static_cast<double>(samples.size());

  // Not a percentile: with SAMPLE_RUNS this small, "p99" would just be the
  // second-largest sample (0.99*(25-1) truncates to index 23 of 25) and
  // would silently mean something else if SAMPLE_RUNS changes. min/max over
  // the sample is the honest summary at this sample size.
  std::vector<double> times;
  times.reserve(samples.size());
  for (auto const &sample : samples) {
    times.push_back(sample.wall_ms);
  }
  std::sort(times.begin(), times.end());
  s.min_ms = times.front();
  s.max_ms = times.back();

  // Touch/callback/lock-failure counts are identical across runs for every
  // scenario here except churn (which rotates a small fixed-size window)
  // and lock_contention (asserted exactly elsewhere), so report the last
  // sample; that is noted in the printed table header.
  s.get_mutex_touches  = samples.back().get_mutex_touches;
  s.get_thread_touches = samples.back().get_thread_touches;
  s.callbacks          = samples.back().callbacks;
  s.lock_failures      = samples.back().lock_failures;
  return s;
}

Sample
timed_run(Fixture &fx)
{
  uint64_t mutex_before    = fx.counters.get_mutex_touches;
  uint64_t thread_before   = fx.counters.get_thread_touches;
  uint64_t cb_before       = fx.counters.callbacks;
  int64_t  lockfail_before = Metrics::Counter::load(net_rsb.inactivity_cop_lock_acquire_failure);
  int64_t  visited_before  = Metrics::Counter::load(net_rsb.inactivity_cop_visited);

  // steady_clock, not fx._now: fx._now is the cop's synthetic clock (see
  // Fixture), not wall time of the benchmark process itself.
  auto t0 = std::chrono::steady_clock::now();
  fx.run();
  auto t1 = std::chrono::steady_clock::now();

  Sample s;
  s.wall_ms            = to_ms(t1 - t0);
  s.get_mutex_touches  = fx.counters.get_mutex_touches - mutex_before;
  s.get_thread_touches = fx.counters.get_thread_touches - thread_before;
  s.callbacks          = fx.counters.callbacks - cb_before;
  s.lock_failures = static_cast<uint64_t>(Metrics::Counter::load(net_rsb.inactivity_cop_lock_acquire_failure) - lockfail_before);
  s.visited       = static_cast<uint64_t>(Metrics::Counter::load(net_rsb.inactivity_cop_visited) - visited_before);
  return s;
}

void
set_idle(Fixture &fx)
{
  ink_hrtime now = fx._now;
  for (auto &m : fx.mocks) {
    m->default_inactivity_timeout_in = 0;
    m->next_inactivity_timeout_at    = now + HRTIME_HOUR;
    m->next_activity_timeout_at      = 0;
    // The wheel only revisits a scheduled deadline; writing the fields
    // directly (as production code never does) needs an explicit rearm.
    m->rearm_timer();
  }
}

void
refresh_keepalive(Fixture &fx)
{
  ink_hrtime now = fx._now;
  for (auto &m : fx.mocks) {
    m->default_inactivity_timeout_in = HRTIME_SECONDS(30);
    m->next_inactivity_timeout_at    = now + HRTIME_SECONDS(30);
    m->next_activity_timeout_at      = 0;
    m->rearm_timer();
  }
}

void
set_mass_expiry(Fixture &fx)
{
  ink_hrtime now = fx._now;
  for (auto &m : fx.mocks) {
    m->default_inactivity_timeout_in = 0;
    m->next_inactivity_timeout_at    = now - HRTIME_SECOND;
    m->next_activity_timeout_at      = 0;
    m->rearm_timer();
  }
}

// Rotates a ~1% window of connections between "just expired" and "freshly
// reset" each call, so on any given tick about 1% of the population is
// hitting a timeout while the rest sit in steady state, as a mixed
// realistic load would.
struct ChurnState {
  size_t window        = 0;
  size_t cursor        = 0;
  bool   have_previous = false;

  explicit ChurnState(size_t n) : window(std::max<size_t>(1, n / 100)) {}
};

void
churn_tick(Fixture &fx, ChurnState &state)
{
  ink_hrtime now = fx._now;
  size_t     n   = fx.mocks.size();

  // The window expired on the previous tick has, by now, either fired its
  // callback or would fire again immediately; simulate the connection being
  // replaced / the timeout renewed before rotating the window forward, so
  // the population that is "currently expired" stays at ~1% instead of
  // accumulating every tick.
  if (state.have_previous) {
    for (size_t j = 0; j < state.window; ++j) {
      size_t idx                                   = (state.cursor + j) % n;
      fx.mocks[idx]->default_inactivity_timeout_in = 0;
      fx.mocks[idx]->next_inactivity_timeout_at    = now + HRTIME_HOUR;
      fx.mocks[idx]->next_activity_timeout_at      = 0;
      fx.mocks[idx]->rearm_timer();
    }
    state.cursor = (state.cursor + state.window) % n;
  }

  for (size_t j = 0; j < state.window; ++j) {
    size_t idx                                   = (state.cursor + j) % n;
    fx.mocks[idx]->default_inactivity_timeout_in = 0;
    fx.mocks[idx]->next_inactivity_timeout_at    = now - HRTIME_SECOND;
    fx.mocks[idx]->next_activity_timeout_at      = 0;
    fx.mocks[idx]->rearm_timer();
  }
  state.have_previous = true;
}

void
init_churn_baseline(Fixture &fx)
{
  set_idle(fx);
}

// Holds ~10% of a fixture's mutexes from a second, genuine EThread for the
// duration of each timed cop run, so MUTEX_TRY_LOCK in check_inactivity()
// really does fail for that subset. Uses the normal blocking lock API
// (MUTEX_TAKE_LOCK/MUTEX_UNTAKE_LOCK) from that thread's own identity;
// nothing is faked by hand-setting thread_holding.
class ContentionHolder
{
public:
  // RAII pairing for acquire_for_next_run()/release_after_run(): if
  // anything throws between the two calls (push_back can throw at
  // N=100000), an unpaired acquire would leave the worker parked holding
  // _held forever. Prefer this over calling the two methods directly.
  class ScopedRun
  {
  public:
    explicit ScopedRun(ContentionHolder &holder) : _holder(holder) { _holder.acquire_for_next_run(); }
    ~ScopedRun() { _holder.release_after_run(); }
    ScopedRun(ScopedRun const &)            = delete;
    ScopedRun &operator=(ScopedRun const &) = delete;

  private:
    ContentionHolder &_holder;
  };

  ContentionHolder(std::vector<std::unique_ptr<PaddedMock>> &mocks, size_t fraction_pct)
  {
    size_t count = std::max<size_t>(1, mocks.size() * fraction_pct / 100);
    // Hold the *last* `count` mocks, not the first: set_mass_expiry() walks
    // fx.mocks in index order into a single wheel bucket that is a LIFO
    // stack (DLL::push()/pop()), so the last-pushed - highest-index - mocks
    // are the first ones expire() pops. Holding the first `count` mocks
    // would put every contended entry behind the whole rest of the
    // population, past TIMEOUT_BUDGET, and this check would never see them.
    for (size_t i = mocks.size() - count; i < mocks.size(); ++i) {
      _held.push_back(mocks[i]->mutex);
    }
    _worker = std::thread([this] { run(); });
  }

  ~ContentionHolder()
  {
    // Unwind-safe regardless of ScopedRun: if a run is still parked waiting
    // for release_after_run() (e.g. an exception unwound out of a
    // ScopedRun's scope in some future caller that does not use it), free
    // it before asking the worker to stop, so this destructor - and the
    // 10% of mutexes it is holding - can never block forever.
    _release_gen.store(_acquire_gen.load(std::memory_order_relaxed), std::memory_order_release);
    _stop.store(true, std::memory_order_relaxed);
    _worker.join();
  }

  size_t
  held_count() const
  {
    return _held.size();
  }

  // Blocks until the held mutexes are actually held by _ethread.
  void
  acquire_for_next_run()
  {
    uint64_t g = _acquire_gen.fetch_add(1, std::memory_order_relaxed) + 1;
    while (_held_gen.load(std::memory_order_acquire) != g) {
      std::this_thread::yield();
    }
  }

  // Releases the mutexes held for the run just measured.
  void
  release_after_run()
  {
    uint64_t g = _acquire_gen.load(std::memory_order_relaxed);
    _release_gen.store(g, std::memory_order_release);
    while (_released_gen.load(std::memory_order_acquire) != g) {
      std::this_thread::yield();
    }
  }

private:
  void
  run()
  {
    // EThread::~EThread() release-asserts that the thread's own mutex is
    // still self-held, an invariant only the normal thread startup path
    // establishes. A stack-local EThread destructed at the end of this
    // function trips that assert, so this mirrors unit_test_main.cc's
    // `new EThread; ...->set_specific();` and deliberately leaks it for the
    // life of the process instead.
    EThread *ethread = new EThread;
    ethread->set_specific();
    _ethread = ethread;
#ifdef BENCH_HAVE_LSAN_INTERFACE
    // One leaked EThread per ContentionHolder (one per N here); tell LSan
    // this one is deliberate rather than have it reported every ASan run.
    // See the detect_odr_violation=0 ASAN_OPTIONS override for this same
    // binary at src/iocore/net/CMakeLists.txt for the sibling case of a
    // known, accepted sanitizer finding in this test target.
    __lsan_ignore_object(ethread);
#endif

    uint64_t last_seen = 0;
    for (;;) {
      while (!_stop.load(std::memory_order_relaxed) && _acquire_gen.load(std::memory_order_acquire) == last_seen) {
        std::this_thread::yield();
      }
      uint64_t g = _acquire_gen.load(std::memory_order_acquire);
      if (g == last_seen) {
        return; // stop requested, no new work queued
      }
      last_seen = g;

      for (auto &m : _held) {
        MUTEX_TAKE_LOCK(m, _ethread);
      }
      _held_gen.store(g, std::memory_order_release);

      while (_release_gen.load(std::memory_order_acquire) != g) {
        std::this_thread::yield();
      }
      for (auto it = _held.rbegin(); it != _held.rend(); ++it) {
        MUTEX_UNTAKE_LOCK(it->get(), _ethread);
      }
      _released_gen.store(g, std::memory_order_release);
    }
  }

  std::vector<Ptr<ProxyMutex>> _held;
  std::thread                  _worker;
  EThread                     *_ethread = nullptr;
  std::atomic<bool>            _stop{false};
  std::atomic<uint64_t>        _acquire_gen{0};
  std::atomic<uint64_t>        _held_gen{0};
  std::atomic<uint64_t>        _release_gen{0};
  std::atomic<uint64_t>        _released_gen{0};
};

void
print_header()
{
  std::printf("\n%-15s %10s %10s %10s %10s %12s %14s %14s %10s %12s\n", "scenario", "N", "mean_ms", "min_ms", "max_ms", "ns/conn",
              "get_mutex/run", "get_thread/run", "cb/run", "lockfail/run");
  std::printf("%-15s %10s %10s %10s %10s %12s %14s %14s %10s %12s\n", "--------", "-", "-------", "------", "------", "-------",
              "-------------", "--------------", "------", "------------");
}

void
print_row(char const *scenario, size_t n, Summary const &s)
{
  double ns_per_conn = (s.mean_ms * 1.0e6) / static_cast<double>(n);
  std::printf("%-15s %10zu %10.4f %10.4f %10.4f %12.2f %14llu %14llu %10llu %12llu\n", scenario, n, s.mean_ms, s.min_ms, s.max_ms,
              ns_per_conn, static_cast<unsigned long long>(s.get_mutex_touches),
              static_cast<unsigned long long>(s.get_thread_touches), static_cast<unsigned long long>(s.callbacks),
              static_cast<unsigned long long>(s.lock_failures));
}

Summary
report(char const *name, size_t n, std::vector<Sample> &samples)
{
  Summary s = summarize(samples);
  print_row(name, n, s);
  return s;
}

struct Scenario {
  char const *name;
  void (*setup)(Fixture &);
  void (*per_tick)(Fixture &, void *state);
};

void
arm(Fixture &fx, Scenario const &scenario)
{
  if (scenario.setup != nullptr) {
    scenario.setup(fx);
  }
}

void
arm_and_warmup(Fixture &fx, Scenario const &scenario, void *state)
{
  arm(fx, scenario);
  for (int i = 0; i < WARMUP_RUNS; ++i) {
    if (scenario.per_tick != nullptr) {
      scenario.per_tick(fx, state);
    }
    fx.warmup();
  }
}

std::vector<Sample>
sample_scenario(Fixture &fx, Scenario const &scenario, void *state)
{
  std::vector<Sample> samples;
  samples.reserve(SAMPLE_RUNS);
  for (int i = 0; i < SAMPLE_RUNS; ++i) {
    if (scenario.per_tick != nullptr) {
      scenario.per_tick(fx, state);
    }
    samples.push_back(timed_run(fx));
  }
  return samples;
}

std::vector<Sample>
run_scenario(size_t n, Scenario const &scenario, void *state = nullptr)
{
  Fixture fx(n);
  arm_and_warmup(fx, scenario, state);
  return sample_scenario(fx, scenario, state);
}

// For scenarios whose first sample must catch a population's very first due
// tick (mass_expiry, lock_contention): the wheel visits a scheduled deadline
// exactly once, at the first expire() call whose cursor reaches it, and that
// is guaranteed to be the very next call after setup() arms it. Any warmup
// call in between would drain that population under TIMEOUT_BUDGET before a
// sample ever sees it - true of any clock driving the cop, not an artifact
// of real-time settling - so these skip the warmup loop entirely.
std::vector<Sample>
run_scenario_no_warmup(size_t n, Scenario const &scenario, void *state = nullptr)
{
  Fixture fx(n);
  arm(fx, scenario);
  return sample_scenario(fx, scenario, state);
}

void
refresh_keepalive_tick(Fixture &fx, void * /* state */)
{
  refresh_keepalive(fx);
}

void
churn_tick_thunk(Fixture &fx, void *state)
{
  churn_tick(fx, *static_cast<ChurnState *>(state));
}

Scenario const IDLE_SCENARIO      = {"idle", set_idle, nullptr};
Scenario const KEEPALIVE_SCENARIO = {"keepalive", nullptr, refresh_keepalive_tick};
// Driven through run_scenario_no_warmup(): setup() puts the whole population
// due on the very next tick, and any warmup call would drain it before a
// sample sees it.
Scenario const MASS_EXPIRY_SCENARIO = {"mass_expiry", set_mass_expiry, nullptr};
Scenario const CHURN_SCENARIO       = {"churn", init_churn_baseline, churn_tick_thunk};
// Uses expired deadlines, not set_idle: since check_inactivity() now skips the try-lock
// for connections that are provably not due, idle connections never attempt the lock and
// ContentionHolder's held fraction would never be exercised. Expired deadlines force the
// cop to lock every connection (as in mass_expiry), so the held 10% still fails the way
// production contention would. Also driven through run_scenario_no_warmup() for the same
// reason as mass_expiry.
Scenario const LOCK_CONTENTION_SCENARIO = {"lock_contention", set_mass_expiry, nullptr};

} // namespace

TEST_CASE("InactivityCop: idle connections", "[!benchmark][net][inactivity_cop]")
{
  print_header();
  for (size_t n : N_VALUES) {
    std::vector<Sample> samples = run_scenario(n, IDLE_SCENARIO);
    Summary             s       = report("idle", n, samples);

    // The refill walk is gone: the cop now visits only what the wheel hands
    // it, and none of these mocks are ever due, so get_thread() is never
    // called at all. Anything above 0 here would mean a leftover sweep.
    INFO("idle sanity check: get_thread touches per run must be 0 (no sweep), and callbacks must be 0");
    CHECK(s.get_thread_touches == 0);
    CHECK(s.callbacks == 0);

    // Cross-check the production metric against the scenario: nothing is due,
    // so the cop examined nothing. A value tracking N instead would mean
    // inactivity_cop_visited is counting open connections, not due deadlines -
    // exactly the regression the metric exists to make visible.
    INFO("idle sanity check: the inactivity_cop_visited metric must be 0 when nothing is due");
    CHECK(samples.back().visited == 0);
  }
}

TEST_CASE("InactivityCop: keepalive steady state", "[!benchmark][net][inactivity_cop]")
{
  print_header();
  for (size_t n : N_VALUES) {
    std::vector<Sample> samples = run_scenario(n, KEEPALIVE_SCENARIO);
    report("keepalive", n, samples);
  }
}

TEST_CASE("InactivityCop: mass expiry", "[!benchmark][net][inactivity_cop]")
{
  print_header();
  for (size_t n : N_VALUES) {
    std::vector<Sample> samples = run_scenario_no_warmup(n, MASS_EXPIRY_SCENARIO);
    report("mass_expiry", n, samples);

    // run_scenario_no_warmup() (see MASS_EXPIRY_SCENARIO) puts the whole
    // population due in a single wheel bucket that the first sample begins
    // draining. InactivityCop::TIMEOUT_BUDGET caps callbacks per call, so the
    // first sample fires exactly min(N, TIMEOUT_BUDGET); for N above the
    // budget the drain spills into as many following samples as it takes, so
    // the total summed across every sample is what must equal N exactly.
    size_t const expected_first = std::min(n, static_cast<size_t>(InactivityCop::TIMEOUT_BUDGET));
    INFO("mass_expiry sanity check: the first sample must fire exactly min(N, TIMEOUT_BUDGET) callbacks");
    CHECK(samples.front().callbacks == expected_first);

    uint64_t total = 0;
    for (auto const &sample : samples) {
      total += sample.callbacks;
    }
    // Every element in this scenario is due, so each one the cop examined also
    // fired. The metric is produced by production code (Fire::deadline_of) and
    // the callback count by the mock, so equality here cross-checks the two
    // against each other rather than just asserting the metric is non-zero.
    uint64_t visited_total = 0;
    for (auto const &sample : samples) {
      visited_total += sample.visited;
    }
    INFO("mass_expiry sanity check: inactivity_cop_visited must equal the callbacks the mocks recorded");
    CHECK(visited_total == n);

    INFO("mass_expiry sanity check: total callbacks summed across all samples must equal N exactly");
    CHECK(total == n);
  }
}

TEST_CASE("InactivityCop: churn", "[!benchmark][net][inactivity_cop]")
{
  print_header();
  for (size_t n : N_VALUES) {
    ChurnState          state(n);
    std::vector<Sample> samples = run_scenario(n, CHURN_SCENARIO, &state);
    report("churn", n, samples);

    // Each per_tick call rotates exactly state.window (~1% of N) mocks into
    // "due" and, since a run is exactly one tick, that window lands in the
    // one bucket the following run() call drains: every sample must fire
    // exactly that many callbacks, not zero (the switchover-era regression
    // this scenario exists to catch) and not some fraction of it.
    INFO("churn sanity check: every sample must fire exactly the ~1% churn window, not zero");
    for (auto const &sample : samples) {
      CHECK(sample.callbacks == state.window);
    }
  }
}

TEST_CASE("InactivityCop: lock contention", "[!benchmark][net][inactivity_cop]")
{
  print_header();
  for (size_t n : N_VALUES) {
    // The sampling loop cannot be the generic per_tick() shape regardless:
    // contention has to bracket each individual timed call (acquire before,
    // release after), not just mutate state before it. arm(), not
    // arm_and_warmup(): see LOCK_CONTENTION_SCENARIO.
    Fixture fx(n);
    arm(fx, LOCK_CONTENTION_SCENARIO);

    ContentionHolder holder(fx.mocks, /* fraction_pct = */ 10);
    size_t const     expected_failures = holder.held_count();

    std::vector<Sample> samples;
    samples.reserve(SAMPLE_RUNS);
    for (int i = 0; i < SAMPLE_RUNS; ++i) {
      ContentionHolder::ScopedRun guard(holder);
      samples.push_back(timed_run(fx));
    }
    report("lock_contention", n, samples);

    uint64_t total_failures = 0;
    for (auto const &sample : samples) {
      total_failures += sample.lock_failures;
    }

    // A lock failure re-schedules the element (see check_inactivity's Fire
    // functor) rather than firing it, floored to the wheel's next tick past
    // wherever the drain currently is. With the synthetic clock this is now
    // fully deterministic (no OS scheduling jitter to hide behind), so below
    // TIMEOUT_BUDGET the exact count is provable: the first sample drains the
    // whole population in a single tick with no cursor lag, after which the
    // bucket this scenario uses holds nothing but the held mocks - which are
    // still locked on every subsequent sample - so every one of the
    // SAMPLE_RUNS samples fails exactly once per held mock.
    if (n <= static_cast<size_t>(InactivityCop::TIMEOUT_BUDGET)) {
      INFO("lock_contention sanity check: below TIMEOUT_BUDGET, every held mock fails exactly once per sample");
      CHECK(total_failures == static_cast<uint64_t>(expected_failures) * static_cast<uint64_t>(SAMPLE_RUNS));
    } else {
      // Above TIMEOUT_BUDGET, the first several samples leave the cursor
      // lagging (TimerWheel::expire's early return doesn't advance it), so a
      // later sample can catch up several ticks at once and revisit a
      // rescheduled held mock more than once in that same call, interleaved
      // with however many non-held mocks share its original bucket. That is
      // still fully deterministic, but not expressible as a closed form
      // without re-simulating the wheel's bucket walk as a second oracle
      // here, so only the floor is asserted: every held mock fails at least
      // once.
      INFO("lock_contention sanity check: total lock failures summed across all samples must be at least the held count");
      CHECK(total_failures >= expected_failures);
    }
  }
}

// Direct check that startCop()/stopCop() populate and drain nh.timer_wheel.
//
// build() alone is not enough to land a mock in the wheel: startCop() only
// applies default_inactivity_timeout_in (mirrors UnixNetVConnection), and
// _earliest_deadline() looks at next_inactivity_timeout_at /
// next_activity_timeout_at, which stay 0 until something enabled I/O or
// called set_inactivity_timeout() with a positive value - exactly like a
// freshly accepted, idle real connection. So arm a deadline the way a real
// accept path does (e.g. UnixNetVConnection::acceptEvent -> set_inactivity_timeout())
// and rearm explicitly before checking.
TEST_CASE("InactivityCop: timer wheel population", "[net][inactivity_cop]")
{
  constexpr size_t n = 1000;
  Fixture          fx(n);

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    ink_hrtime const now = ink_get_hrtime();
    for (auto &m : fx.mocks) {
      m->next_inactivity_timeout_at = now + HRTIME_SECONDS(30);
      fx.nh.rearm_timer(m.get());
    }
  }

  INFO("every mock with a deadline must be scheduled in the wheel after rearm_timer()");
  for (auto &m : fx.mocks) {
    CHECK(fx.nh.timer_wheel.is_scheduled(m.get()));
  }

  constexpr size_t stopped_count = 10;
  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    for (size_t i = 0; i < stopped_count; ++i) {
      fx.nh.stopCop(fx.mocks[i].get());
    }
  }

  INFO("stopCop() must cancel exactly the mocks it was called on, leaving the rest scheduled");
  for (size_t i = 0; i < n; ++i) {
    bool const expect_scheduled = i >= stopped_count;
    CHECK(fx.nh.timer_wheel.is_scheduled(fx.mocks[i].get()) == expect_scheduled);
  }

  // ~Fixture() calls stopCop() on every mock; that is exercised as idempotent
  // here too for the ones already stopped above.
}

// A connection the cop examines but does not fire must stay in the wheel.
//
// The wheel hands an element to the delegate when its deadline is <= now, but
// the cop only fires on a deadline strictly < now. A deadline exactly equal to
// now therefore lands in the gap: the element has already been popped, and if
// the cop returns without firing or re-arming it, nothing will ever visit it
// again and that connection never times out.
TEST_CASE("InactivityCop: an examined but unfired connection stays scheduled", "[net][inactivity_cop]")
{
  constexpr size_t n = 4;
  Fixture          fx(n);

  // Far enough ahead of the wheel's cursor (seeded from ink_get_hrtime() when
  // the fixture was built) that the pass below actually drains the bucket the
  // mocks land in, rather than finding no tick to advance through.
  ink_hrtime const now = ink_get_hrtime() + HRTIME_SECONDS(5);

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    for (auto &m : fx.mocks) {
      // Deadline exactly equal to the `now` the cop will be driven with, so
      // the wheel pops it (deadline <= now) but `deadline < now` is false.
      m->next_inactivity_timeout_at = now;
      fx.nh.rearm_timer(m.get());
    }
  }

  INFO("precondition: every mock is scheduled before the pass");
  for (auto &m : fx.mocks) {
    REQUIRE(fx.nh.timer_wheel.is_scheduled(m.get()));
  }

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    Event event;
    event.ethread = this_ethread();
    fx.cop.run(now, &event);
  }

  INFO("the cop examined these mocks without firing them; each must still be scheduled, or it can never time out");
  for (auto &m : fx.mocks) {
    CHECK(fx.nh.timer_wheel.is_scheduled(m.get()));
  }
  CHECK(fx.counters.callbacks == 0);
}

// proxy.config.net.inactivity_check_frequency may be raised above 1, which
// leaves the wheel's one-second buckets alone and just means a pass advances
// several ticks. Every deadline in the skipped-over ticks must still fire on
// that pass, not only the ones in the newest bucket.
TEST_CASE("InactivityCop: one pass covers every tick it advances through", "[net][inactivity_cop]")
{
  constexpr int    ticks = 3;
  constexpr size_t n     = ticks;
  Fixture          fx(n);

  ink_hrtime const base = ink_get_hrtime() + HRTIME_SECONDS(10);

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    // One mock due in each of three consecutive ticks.
    for (size_t i = 0; i < n; ++i) {
      fx.mocks[i]->next_inactivity_timeout_at = base + HRTIME_SECONDS(static_cast<int>(i));
      fx.nh.rearm_timer(fx.mocks[i].get());
    }
  }

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    Event event;
    event.ethread = this_ethread();
    // A single pass, as a cop_freq of 3 would do: well past all three deadlines.
    fx.cop.run(base + HRTIME_SECONDS(ticks + 1), &event);
  }

  INFO("a pass spanning several ticks must fire every deadline in them, not just the last bucket's");
  CHECK(fx.counters.callbacks == n);
  for (auto &m : fx.mocks) {
    CHECK_FALSE(fx.nh.timer_wheel.is_scheduled(m.get()));
  }
}

// --- Step 0: NetHandler::_close_ne event selection -------------------------
//
// _close_ne is the queue-management close path (capacity eviction from
// manage_keep_alive_queue, expired-deadline close from manage_active_queue). It
// is reached only when the per-thread limits are non-zero, which is why the
// scenarios above -- which set them to 0 so the queue managers early-return --
// never touch it.
//
// Drive it through manage_keep_alive_queue() by putting mocks in the keep-alive
// queue and setting the limit below the queue size.
namespace
{
struct EvictionFixture {
  NetHandler                               nh;
  Counters                                 counters;
  std::vector<std::unique_ptr<PaddedMock>> mocks;
  InactivityCop                            cop;

  explicit EvictionFixture(size_t n) : cop(Ptr<ProxyMutex>(new_ProxyMutex()), nh)
  {
    ensure_net_metrics_registered();

    nh.mutex  = new_ProxyMutex();
    nh.thread = this_ethread();
    nh.timer_wheel.init(ink_get_hrtime());
    nh.config.default_inactivity_timeout = 30;
    // High while populating, so add_to_keep_alive_queue()'s own call to
    // manage_keep_alive_queue() does not evict during setup. evict() lowers it.
    nh.max_connections_per_thread_in = 1000000;
    nh.max_requests_per_thread_in    = 0;

    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    for (size_t i = 0; i < n; ++i) {
      mocks.emplace_back(std::make_unique<PaddedMock>(this_ethread(), &counters));
      mocks.back()->nh = &nh;
      nh.startCop(mocks.back().get());
    }
  }

  ~EvictionFixture()
  {
    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    for (auto &m : mocks) {
      nh.stopCop(m.get());
    }
  }

  /// Drop the limit below the queue size and run exactly one eviction pass.
  void
  evict()
  {
    SCOPED_MUTEX_LOCK(lock, nh.mutex, this_ethread());
    nh.max_connections_per_thread_in = 1;
    nh.manage_keep_alive_queue();
  }
};
} // namespace

// The bug: a connection relying on default_inactivity_timeout has
// inactivity_timeout_in == 0, and _close_ne's predicate required it to be
// non-zero, so eviction fired nothing at all -- the queue stayed over capacity
// and the walk closed nothing.
TEST_CASE("NetHandler::_close_ne evicts a default-timeout connection", "[net][nethandler][close_ne]")
{
  EvictionFixture fx(4);

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    for (auto &m : fx.mocks) {
      // On the default timeout: no explicit inactivity duration, deadline not
      // yet reached. Eviction is for capacity, not expiry.
      m->inactivity_timeout_in      = 0;
      m->next_inactivity_timeout_at = ink_get_hrtime() + HRTIME_SECONDS(300);
      m->active_timeout_in          = 0;
      m->next_activity_timeout_at   = 0;
      fx.nh.add_to_keep_alive_queue(m.get());
    }
  }

  fx.evict();

  INFO("capacity eviction must close default-timeout connections, not silently skip them");
  CHECK(fx.counters.callbacks > 0);
}

// An expired active timeout must be reported as such. This is what rules out
// "just drop inactivity_timeout_in from the condition": because the clobbered
// next_inactivity_timeout_at <= now is always true, that would make the
// inactivity branch unconditional and the active branch dead.
TEST_CASE("NetHandler::_close_ne reports an expired active timeout as ACTIVE", "[net][nethandler][close_ne]")
{
  EvictionFixture  fx(2);
  ink_hrtime const now = ink_get_hrtime();

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    for (auto &m : fx.mocks) {
      m->inactivity_timeout_in      = 0;
      m->next_inactivity_timeout_at = 0;
      m->active_timeout_in          = HRTIME_SECONDS(10);
      m->next_activity_timeout_at   = now - HRTIME_SECONDS(1); // expired
      fx.nh.add_to_keep_alive_queue(m.get());
    }
  }

  fx.evict();

  INFO("an expired active timeout must not be reported as an inactivity timeout");
  REQUIRE(fx.counters.callbacks > 0);
  CHECK(fx.mocks.front()->last_event == VC_EVENT_ACTIVE_TIMEOUT);
}

TEST_CASE("NetHandler::_close_ne reports an expired inactivity timeout as INACTIVITY", "[net][nethandler][close_ne]")
{
  EvictionFixture  fx(2);
  ink_hrtime const now = ink_get_hrtime();

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    for (auto &m : fx.mocks) {
      m->inactivity_timeout_in      = HRTIME_SECONDS(10);
      m->next_inactivity_timeout_at = now - HRTIME_SECONDS(1); // expired
      m->active_timeout_in          = 0;
      m->next_activity_timeout_at   = 0;
      fx.nh.add_to_keep_alive_queue(m.get());
    }
  }

  fx.evict();

  REQUIRE(fx.counters.callbacks > 0);
  CHECK(fx.mocks.front()->last_event == VC_EVENT_INACTIVITY_TIMEOUT);
}

// Both expired: inactivity wins, matching InactivityCop::Fire's precedence.
TEST_CASE("NetHandler::_close_ne prefers INACTIVITY when both deadlines expired", "[net][nethandler][close_ne]")
{
  EvictionFixture  fx(2);
  ink_hrtime const now = ink_get_hrtime();

  {
    SCOPED_MUTEX_LOCK(lock, fx.nh.mutex, this_ethread());
    for (auto &m : fx.mocks) {
      m->inactivity_timeout_in      = HRTIME_SECONDS(10);
      m->next_inactivity_timeout_at = now - HRTIME_SECONDS(2);
      m->active_timeout_in          = HRTIME_SECONDS(10);
      m->next_activity_timeout_at   = now - HRTIME_SECONDS(1);
      fx.nh.add_to_keep_alive_queue(m.get());
    }
  }

  fx.evict();

  REQUIRE(fx.counters.callbacks > 0);
  CHECK(fx.mocks.front()->last_event == VC_EVENT_INACTIVITY_TIMEOUT);
}

CATCH_REGISTER_LISTENER(ProvenanceListener);
