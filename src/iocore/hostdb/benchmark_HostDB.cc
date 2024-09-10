/** @file

  Test HostFile

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

#include "iocore/dns/DNSProcessor.h"
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EventProcessor.h"
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/eventsystem/Lock.h"
#include "iocore/net/NetProcessor.h"
#include "records/RecordsConfig.h"
#include "swoc/swoc_file.h"
#include "tscore/DiagsTypes.h"
#include "tscore/Layout.h"
#include "iocore/eventsystem/RecProcess.h"
#include "iocore/hostdb/HostDBProcessor.h"
#include "tscore/TSSystemState.h"
#include "tscore/Version.h"
#include "tscore/ink_hrtime.h"
#include "tscore/ink_hw.h"
#include "tsutil/DbgCtl.h"
#include <algorithm>
#include <functional>

#include "iocore/hostdb/HostDB.h"

#include <random>
#include <fstream>

#if __has_include(<latch>)
#include <latch>
using latch = std::latch;
#else
struct latch {
  int                     count;
  std::mutex              m;
  std::condition_variable cv;

  latch(int count) : count(count) {}

  void
  wait()
  {
    std::unique_lock lock{m};
    cv.wait(lock, [this] { return count == 0; });
  }

  void
  count_down()
  {
    std::unique_lock lock{m};
    if (0 == --count) {
      cv.notify_all();
    }
  }
};
#endif

namespace
{

DbgCtl          dbg_ctl_hostdb_test{"hostdb_test"};
HostDBProcessor hdb;

struct FContinuation : Continuation {
  FContinuation(std::function<int(int, Event *)> &&f) : Continuation(new_ProxyMutex()), f(f)
  {
    SET_HANDLER(&FContinuation::handle);
  }

  int
  handle(int event, Event *e)
  {
    return f(event, e);
  }

  std::function<int(int, Event *)> f;
};

int
stop_events(int, Event *)
{
  std::printf("Killered\n");
  TSSystemState::shut_down_event_system();
  return 0;
}

} // end anonymous namespace

std::string
temp_prefix()
{
  char            buffer[PATH_MAX];
  std::error_code err;
  const char     *tmpdir = getenv("TMPDIR");
  if (tmpdir == nullptr) {
    tmpdir = "/tmp";
  }
  snprintf(buffer, sizeof(buffer), "%s/cachetest.XXXXXX", tmpdir);
  auto prefix = swoc::file::path(mkdtemp(buffer));
  bool result = swoc::file::create_directories(prefix / "var" / "trafficserver", err, 0755);
  if (!result) {
    Dbg(dbg_ctl_hostdb_test, "Failed to create directories for test: %s(%s)", prefix.c_str(), err.message().c_str());
  }
  ink_assert(result);

  return prefix.string();
}

void
init_ts(std::string_view name, int debug_on = 0)
{
  DiagsPtr::set(new Diags(name, "", "", new BaseLogFile("stderr")));
  swoc::file::path prefix = temp_prefix();

  diags()->activate_taglist("dns|hostdb", DiagsTagType_Debug);
  diags()->config.enabled(DiagsTagType_Debug, debug_on);
  diags()->show_location = SHOW_LOCATION_DEBUG;

  Layout::create(prefix.view());
  RecProcessInit(diags());
  LibRecordsConfigInit();
  ink_event_system_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_net_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_hostdb_init(HOSTDB_MODULE_PUBLIC_VERSION);
  ink_dns_init(HOSTDB_MODULE_PUBLIC_VERSION);

  netProcessor.init();

  int nproc = ink_number_of_processors();
  eventProcessor.start(nproc);
  dnsProcessor.start(0, 1024 * 1024);

  hdb.start();

  EThread *thread = new EThread();
  thread->set_specific();
  init_buffer_allocators(0);
}

struct StartDNS : Continuation {
  struct Result {
    std::chrono::duration<float> d;
    std::string                  hostname;
    std::string                  ip;
    bool                         immediate;
  };
  using HostList   = std::vector<std::string>;
  using ResultList = std::vector<Result>;
  using Clock      = std::chrono::high_resolution_clock;
  using ms         = std::chrono::milliseconds;

  HostList           hostlist;
  int                id;
  latch             &done_latch;
  HostList::iterator it;
  ResultList         results;
  Clock::time_point  start_time;
  bool               is_callback;

  StartDNS(const std::vector<std::string> &hlist, int id, latch &l)
    : Continuation(new_ProxyMutex()), hostlist(hlist), id(id), done_latch(l)
  {
    std::random_device rd;
    std::mt19937       gen{rd()};

    std::shuffle(hostlist.begin(), hostlist.end(), gen);
    it = hostlist.begin();

    SET_HANDLER(&StartDNS::start_dns);
  }

  std::string
  ip(HostDBRecord *r)
  {
    char buff[256];
    r->rr_info()[0].data.ip.toString(buff, sizeof(buff));
    return buff;
  }

  void
  handle_hostdb(HostDBRecord *r)
  {
    auto now = Clock::now();
    results.push_back(Result{
      now - start_time,
      r->name(),
      ip(r),
      !is_callback,
    });
  }

  void
  print_results()
  {
    for (auto &res : results) {
      std::printf("[%02d] %32s: %-20s %f (%s)\n", id, res.hostname.c_str(), res.ip.c_str(), res.d.count(),
                  res.immediate ? "true" : "false");
    }
  }

  int
  start_dns(int e, void *ep)
  {
    switch (e) {
    case EVENT_HOST_DB_LOOKUP:
      is_callback = true;
      handle_hostdb(reinterpret_cast<HostDBRecord *>(ep));
      [[fallthrough]];
    default:
      Dbg(dbg_ctl_hostdb_test, "start_dns event %d", e);
      do {
        if (it == hostlist.end()) {
          done_latch.count_down();
          return 0;
        }
        start_time  = Clock::now();
        is_callback = false;
      } while (hdb.getbyname_imm(this, static_cast<cb_process_result_pfn>(&StartDNS::handle_hostdb), (it++)->c_str(), 0) ==
               ACTION_RESULT_DONE);
      break;
    }
    return 0;
  }
};

StartDNS::HostList
lines(const std::string &fname)
{
  std::ifstream      in(fname);
  std::string        line;
  StartDNS::HostList result;

  if (in.is_open()) {
    while (std::getline(in, line)) {
      result.push_back(line);
    }
    in.close();
  } else {
    printf("Failed to open file %s\n", fname.c_str());
  }

  return result;
}

int
main(int argc, char **argv)
{
  StartDNS::HostList hosts;
  if (argc > 1) {
    hosts = lines(argv[1]);
  } else {
    hosts = {
      "www.yahoo.com",    "developer.apple.com", "www.google.com", "www.apple.com",
      "sports.yahoo.com", "finance.yahoo.com",   "www.github.com",
    };
  }
  int dbg = 0;
  if (argc > 2) {
    dbg = 1;
  }

  init_ts("hostdb_test", dbg);

  auto threads = eventProcessor.active_group_threads(ET_CALL);
  int  count   = threads.end() - threads.begin();

  latch                   l{count};
  std::vector<StartDNS *> tests;

  for (auto &t : eventProcessor.active_group_threads(ET_CALL)) {
    StartDNS *startDns = new StartDNS{hosts, t->id, l};
    t->schedule_imm(startDns);
    tests.push_back(startDns);
  }

  FContinuation killer(stop_events);
  eventProcessor.schedule_in(&killer, HRTIME_SECONDS(300));

  l.wait();

  int                          results_count{};
  int                          dns_count{};
  std::chrono::duration<float> total_duration{};
  std::chrono::duration<float> min_d = std::chrono::duration<float>::max();
  std::chrono::duration<float> max_d{};
  std::chrono::duration<float> min_i = std::chrono::duration<float>::max();
  std::chrono::duration<float> max_i{};

  for (auto *test : tests) {
    test->print_results();
    results_count += test->results.size();
    for (auto &r : test->results) {
      total_duration += r.d;
      if (!r.immediate) {
        dns_count++;
        min_d = std::min(min_d, r.d);
        max_d = std::max(max_d, r.d);
      } else {
        min_i = std::min(min_i, r.d);
        max_i = std::max(max_i, r.d);
      }
    }
    delete test;
  }

  printf("Hosts: %zu lookup count: %d thread count: %d\n", hosts.size(), dns_count, count);
  printf("dns min/max: %2.6f/%2.6f\n", min_d.count(), max_d.count());
  printf("imm min/max: %2.6f/%2.6f\n", min_i.count(), max_i.count());
  printf("Total results: %d average lookup %f\n", results_count, total_duration.count() / results_count);
  hdb.shutdown();
}

class HttpSessionAccept;
HttpSessionAccept *plugin_http_accept = nullptr;
