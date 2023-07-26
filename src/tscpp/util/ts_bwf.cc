/** @file

    Local extensions for @c swoc::BufferWriter

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

#include <unistd.h>
#include <sys/param.h>
#include <cctype>
#include <ctime>
#include <cmath>
#include <cmath>
#include <array>
#include <chrono>
#include <exception>

#include "tscpp/util/ts_bw_format.h"
#include "tscore/ink_thread.h"

using namespace std::literals;
using namespace swoc::literals;

namespace
{
swoc::BufferWriter &
BWF_Timestamp(swoc::BufferWriter &w, swoc::bwf::Spec const &spec)
{
  auto now   = std::chrono::system_clock::now();
  auto epoch = std::chrono::system_clock::to_time_t(now);
  swoc::LocalBufferWriter<48> lw;

  ctime_r(&epoch, lw.aux_data());
  lw.commit(19); // keep only leading text.
  lw.print(".{:03}", std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count() % 1000);
  w.write(lw.view().substr(4));
  return w;
}

swoc::BufferWriter &
BWF_Now(swoc::BufferWriter &w, swoc::bwf::Spec const &spec)
{
  return bwformat(w, spec, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

swoc::BufferWriter &
BWF_Tick(swoc::BufferWriter &w, swoc::bwf::Spec const &spec)
{
  return bwformat(w, spec, std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

swoc::BufferWriter &
BWF_ThreadID(swoc::BufferWriter &w, swoc::bwf::Spec const &spec)
{
  return bwformat(w, spec, pthread_self());
}

swoc::BufferWriter &
BWF_ThreadName(swoc::BufferWriter &w, swoc::bwf::Spec const &spec)
{
  char name[32]; // manual says at least 16, bump that up a bit.
  ink_get_thread_name(name, sizeof(name));
  return bwformat(w, spec, std::string_view{name});
}

static bool BW_INITIALIZED __attribute__((unused)) = []() -> bool {
  auto &global_table = swoc::bwf::Global_Names;
  global_table.assign("now", &BWF_Now);
  global_table.assign("tick", &BWF_Tick);
  global_table.assign("timestamp", &BWF_Timestamp);
  global_table.assign("thread-id", &BWF_ThreadID);
  global_table.assign("thread-name", &BWF_ThreadName);
  return true;
}();

} // namespace
