/** @file

  Create metrics for tests.

  Usage: test_metrics.so [--count=N] [--counter=NAME=VALUE ...] [NAME=VALUE ...]

  --count=N creates the gauges plugin.test_metrics.gauge_<i> with the value i, for i from 0 to N - 1.  NAME=VALUE creates
  the gauge NAME with the signed value VALUE.  --counter=NAME=VALUE creates the counter NAME with the unsigned 64-bit value
  VALUE.

  Plugin messages (traffic_ctl plugin msg TAG ARG):

    test_metrics NAME=VALUE    creates the gauge NAME, or sets it if it exists.
    test_metrics.stall MS      holds the lock of the string metrics for MS milliseconds.  A TSRecordDump that reaches the
                               string metrics in that time waits, so a test can slow down a render of the stats.
    test_metrics.hold_task MS  queues a task that keeps a task thread busy for MS milliseconds.

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

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#include <swoc/TextView.h>
#include <ts/ts.h>
#include <tsutil/Metrics.h>

namespace
{
constexpr char             PLUGIN_NAME[]  = "test_metrics";
constexpr std::string_view COUNT_OPTION   = "--count=";
constexpr std::string_view COUNTER_OPTION = "--counter=";
constexpr char             USAGE[]        = "Usage: test_metrics.so [--count=N] [--counter=NAME=VALUE ...] [NAME=VALUE ...]";
constexpr std::string_view STALL_TAG      = "test_metrics.stall";
constexpr std::string_view HOLD_TASK_TAG  = "test_metrics.hold_task";

DbgCtl dbg_ctl{PLUGIN_NAME};

bool
create_gauge(const std::string &name, int64_t value)
{
  int const id = TSStatCreate(name.c_str(), TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);

  if (id == TS_ERROR) {
    TSError("[%s] Cannot create %s", PLUGIN_NAME, name.c_str());
    return false;
  }
  TSStatIntSet(id, value);
  return true;
}

// TSStatCreate creates only gauges, so a counter comes from ts::Metrics.
bool
create_counter(std::string_view spec)
{
  swoc::TextView       value{spec};
  swoc::TextView const name = value.split_prefix_at('=');

  if (name.empty()) {
    TSError("[%s] %s", PLUGIN_NAME, USAGE);
    return false;
  }
  ts::Metrics::Counter::increment(ts::Metrics::Counter::createPtr(name), swoc::svtou(value, nullptr, 10));
  return true;
}

bool
assign_gauge(std::string_view arg)
{
  swoc::TextView       value{arg};
  swoc::TextView const name = value.split_prefix_at('=');

  if (name.empty()) {
    TSError("[%s] %s", PLUGIN_NAME, USAGE);
    return false;
  }
  return create_gauge(std::string{name}, swoc::svtoi(value, nullptr, 10));
}

// TSRecordDump holds the lock of the string metrics while it passes them to its callback, so sleeping in the
// callback stalls every other TSRecordDump when it reaches the string metrics.
void
stall_on_strings(TSRecordType /* rec_type ATS_UNUSED */, void *edata, int /* registered ATS_UNUSED */, const char * /* name */,
                 TSRecordDataType data_type, TSRecordData * /* datum ATS_UNUSED */)
{
  auto &stall = *static_cast<std::chrono::milliseconds *>(edata);

  if (data_type == TS_RECORDDATATYPE_STRING && stall.count() > 0) {
    Dbg(dbg_ctl, "Stalling record dumps for %lld ms", static_cast<long long>(stall.count()));
    std::this_thread::sleep_for(stall);
    stall = std::chrono::milliseconds{0};
    Dbg(dbg_ctl, "Stopped stalling record dumps");
  }
}

int
hold_task(TSCont contp, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  auto *hold = static_cast<std::chrono::milliseconds *>(TSContDataGet(contp));

  Dbg(dbg_ctl, "Holding a task thread for %lld ms", static_cast<long long>(hold->count()));
  std::this_thread::sleep_for(*hold);
  Dbg(dbg_ctl, "Released the task thread");
  delete hold;
  TSContDestroy(contp);
  return 0;
}

int
handle_message(TSCont /* contp ATS_UNUSED */, TSEvent /* event ATS_UNUSED */, void *edata)
{
  auto const            *msg = static_cast<const TSPluginMsg *>(edata);
  std::string_view const tag{msg->tag};
  std::string_view const arg{static_cast<const char *>(msg->data), msg->data_size};

  if (tag == PLUGIN_NAME) {
    if (assign_gauge(arg)) {
      Dbg(dbg_ctl, "Assigned %.*s", static_cast<int>(arg.size()), arg.data());
    }
  } else if (tag == STALL_TAG) {
    std::chrono::milliseconds stall{swoc::svtoi(arg, nullptr, 10)};

    std::thread([stall]() mutable { TSRecordDump(TS_RECORDTYPE_PLUGIN, stall_on_strings, &stall); }).detach();
  } else if (tag == HOLD_TASK_TAG) {
    long long const ms    = swoc::svtoi(arg, nullptr, 10);
    TSCont          contp = TSContCreate(hold_task, TSMutexCreate());

    TSContDataSet(contp, new std::chrono::milliseconds{ms});
    TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_TASK);
    Dbg(dbg_ctl, "Queued a task that holds a task thread for %lld ms", ms);
  }
  return 0;
}
} // namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  int created = 0;

  for (int i = 1; i < argc; ++i) {
    std::string_view const arg{argv[i]};

    if (arg.starts_with(COUNT_OPTION)) {
      long const count = swoc::svtoi(arg.substr(COUNT_OPTION.size()), nullptr, 10);

      for (long n = 0; n < count; ++n) {
        created += create_gauge("plugin.test_metrics.gauge_" + std::to_string(n), n);
      }
    } else if (arg.starts_with(COUNTER_OPTION)) {
      created += create_counter(arg.substr(COUNTER_OPTION.size()));
    } else {
      created += assign_gauge(arg);
    }
  }

  Dbg(dbg_ctl, "Created %d metrics", created);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(handle_message, TSMutexCreate()));
}
