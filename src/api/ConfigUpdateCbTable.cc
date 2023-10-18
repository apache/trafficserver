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
  std::filesystem::file_time_type timestamp;
  std::string fname = file_name;

  ink_assert(contp != nullptr);
  ink_assert(name != nullptr);

  if (fname.size() > 0) {
    timestamp = std::filesystem::last_write_time(fname);
  }
  cb_table.emplace(name, std::make_tuple(contp, fname, timestamp));
}

void
ConfigUpdateCbTable::invoke()
{
  for (auto &&it : cb_table) {
    auto &[contp, file_name, timestamp] = it.second;

    if (file_name.size() > 0) {
      auto newtime = std::filesystem::last_write_time(file_name);

      if (newtime > timestamp) {
        timestamp = newtime;
        invoke(contp);
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
