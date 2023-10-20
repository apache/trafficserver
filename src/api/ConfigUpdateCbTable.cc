/** @file

  Internal SDK stuff

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

#include "api/InkAPIInternal.h"

ConfigUpdateCbTable::ConfigUpdateCbTable() {}

ConfigUpdateCbTable::~ConfigUpdateCbTable() {}

void
ConfigUpdateCbTable::insert(INKContInternal *contp, const char *name, const char *file_name)
{
  ink_assert(contp != nullptr);
  ink_assert(name != nullptr);

  if (nullptr != file_name) {
    swoc::file::path file_path{file_name};
    std::error_code ec;
    auto timestamp = swoc::file::last_write_time(file_path, ec);

    if (!ec) {
      cb_table.emplace(name, std::make_tuple(contp, file_path, timestamp));
    } else {
      Error("Failed to stat %s: %s", file_path.c_str(), ec.message().c_str());
    }
  } else {
    cb_table.emplace(name, std::make_tuple(contp, swoc::file::path{}, swoc::file::file_time_type{}));
  }
}

void
ConfigUpdateCbTable::invoke()
{
  for (auto &&it : cb_table) {
    auto &[contp, file_path, timestamp] = it.second;

    if (!file_path.empty()) {
      std::error_code ec;
      auto newtime = swoc::file::last_write_time(file_path, ec);

      if (!ec) {
        if (newtime > timestamp) {
          timestamp = newtime;
          invoke(contp);
        }
      } else {
        Error("Failed to stat %s: %s", file_path.c_str(), ec.message().c_str());
      }
    } else {
      invoke(contp);
    }
  }
}

void
ConfigUpdateCbTable::invoke(INKContInternal *contp)
{
  eventProcessor.schedule_imm(new ConfigUpdateCallback(contp), ET_TASK);
}
