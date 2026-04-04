/** @file

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

#include "plugin.h"
#include "log.h"

#include "ts/ts.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
struct SharedLogEntry {
  TSTextLogObject handle   = nullptr;
  size_t          refcount = 0;
};

std::mutex                                      shared_log_mutex;
std::unordered_map<std::string, SharedLogEntry> shared_logs;
} // namespace

bool
create_log_file(const std::string &filename, TSTextLogObject &log_handle)
{
  std::lock_guard lock(shared_log_mutex);

  if (auto spot = shared_logs.find(filename); spot != shared_logs.end()) {
    ++spot->second.refcount;
    log_handle = spot->second.handle;
    return true;
  }

  TSTextLogObject handle = nullptr;
  if (TS_SUCCESS != TSTextLogObjectCreate(filename.c_str(), TS_LOG_MODE_ADD_TIMESTAMP, &handle)) {
    return false;
  }

  shared_logs.emplace(filename, SharedLogEntry{handle, 1});
  log_handle = handle;
  return true;
}

void
log_fingerprint(const JAxContext *ctx, TSTextLogObject &log_handle)
{
  if (log_handle == nullptr) {
    Dbg(dbg_ctl, "Log handle is not initialized.");
    return;
  }
  if (TS_ERROR == TSTextLogObjectWrite(log_handle, "Client: %s\t%s: %s", ctx->get_addr(), ctx->get_method_name(),
                                       ctx->get_fingerprint().c_str())) {
    Dbg(dbg_ctl, "Failed to write to log!");
  }
}

void
flush_log_file(const std::string &filename, TSTextLogObject &log_handle)
{
  TSTextLogObject handle_to_destroy = nullptr;
  {
    std::lock_guard lock(shared_log_mutex);
    auto            spot = shared_logs.find(filename);
    if (spot != shared_logs.end()) {
      if (--spot->second.refcount == 0) {
        handle_to_destroy = spot->second.handle;
        shared_logs.erase(spot);
      }
    } else {
      handle_to_destroy = log_handle;
    }
  }

  if (handle_to_destroy != nullptr) {
    TSTextLogObjectDestroy(handle_to_destroy);
  }
  log_handle = nullptr;
}
