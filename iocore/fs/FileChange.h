/** @file FileChange.h

  Watch for file system changes.

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

#pragma once
#include "ts/apidefs.h"
#include "tscore/ink_config.h"

#include <thread>
#include <chrono>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>
#include <shared_mutex>
#include "tscore/ts_file.h"
#include "P_EventSystem.h"
#include <filesystem>
#include <algorithm>

#if TS_USE_INOTIFY
#include <sys/inotify.h>
#else
// implement this
#endif

using watch_handle_t = int;

// File watch info
class FileChangeInfo
{
public:
  FileChangeInfo(TSFileWatchKind kind, ts::file::path path, Continuation *contp) : kind{kind}, path{std::move(path)}, contp{contp}
  {
  }

  TSFileWatchKind kind;
  ts::file::path path;
  Continuation *contp;
};

class FileChangeManager
{
public:
  FileChangeManager() {}

  void init();

  /**
    Add a file watch

    @return a watch handle, or -1 on error
  */
  watch_handle_t add(const ts::file::path &path, TSFileWatchKind kind, Continuation *contp);

  /**
    Remove a file watch
  */
  void remove(watch_handle_t watch_handle);

private:
  std::thread poll_thread;
  std::unordered_map<watch_handle_t, FileChangeInfo> file_watches; // protected by file_watches_mutex
  std::shared_mutex file_watches_mutex;

#if TS_USE_INOTIFY
  void inotify_process_event(struct inotify_event *event);
  int inotify_fd;
#elif TS_USE_KQUEUE
  bool file_watches_dirty; // protected by file_watches_mutex

  std::vector<struct kevent> events_to_monitor;
  std::vector<struct kevent> events_from_kqueue;

  int kq;
  void kqueue_prepare_events();
  int kqueue_wait_for_events();
  void kqueue_process_event(const struct kevent &event);
#else
  // implement this
#endif
};

extern FileChangeManager fileChangeManager;
