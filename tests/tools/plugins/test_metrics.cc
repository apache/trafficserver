/** @file

  Create metrics for tests.

  Usage: test_metrics.so [--count=N] [NAME=VALUE ...]

  --count=N creates the gauges plugin.test_metrics.gauge_<i> with the value i, for i from 0 to N - 1.  NAME=VALUE creates
  the gauge NAME with the signed value VALUE.

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

#include <cstdint>
#include <string>
#include <string_view>

#include <swoc/TextView.h>
#include <ts/ts.h>

namespace
{
constexpr char             PLUGIN_NAME[] = "test_metrics";
constexpr std::string_view COUNT_OPTION  = "--count=";

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
    } else if (auto const eq = arg.find('='); eq != std::string_view::npos && eq > 0) {
      created += create_gauge(std::string{arg.substr(0, eq)}, swoc::svtoi(arg.substr(eq + 1), nullptr, 10));
    } else {
      TSError("[%s] Usage: %s.so [--count=N] [NAME=VALUE ...]", PLUGIN_NAME, PLUGIN_NAME);
    }
  }

  Dbg(dbg_ctl, "Created %d metrics", created);
}
