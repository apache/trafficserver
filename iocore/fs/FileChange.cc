/** @file FileChange.cc

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

#include "FileChange.h"
#include "tscore/Diags.h"
#include "P_EventSystem.h"

#include <cassert>
#include <functional>
#include <mutex>
#include <optional>

// Globals
FileChangeManager fileChangeManager;
static constexpr auto TAG ATS_UNUSED = "FileChange";

// Wrap a continuation
class FileChangeCallback : public Continuation
{
public:
  explicit FileChangeCallback(Continuation *contp, TSEvent event) : Continuation(contp->mutex.get()), m_cont(contp), m_event(event)
  {
    SET_HANDLER(&FileChangeCallback::event_handler);
  }

  int
  event_handler(int, void *eventp)
  {
    Event *e = reinterpret_cast<Event *>(eventp);
    if (m_cont->mutex) {
      MUTEX_TRY_LOCK(trylock, m_cont->mutex, this_ethread());
      if (!trylock.is_locked()) {
        eventProcessor.schedule_in(this, HRTIME_MSECONDS(10), ET_TASK);
      } else {
        m_cont->handleEvent(m_event, e->cookie);
        delete this;
      }
    } else {
      m_cont->handleEvent(m_event, e->cookie);
      delete this;
    }

    return 0;
  }

  std::string filename; // File name if the event is a file creation event.  This is used in the cookie for a create event.
  TSFileWatchData data;

private:
  Continuation *m_cont;
  TSEvent m_event;
};

#if TS_USE_INOTIFY
static constexpr size_t INOTIFY_BUF_SIZE = 4096;

static void
invoke(FileChangeCallback *cb)
{
  void *cookie = static_cast<void *>(&cb->data);
  eventProcessor.schedule_imm(cb, ET_TASK, 1, cookie);
}

void
FileChangeManager::process_file_event(struct inotify_event *event)
{
  std::shared_lock file_watches_read_lock(file_watches_mutex);
  auto finfo_it = file_watches.find(event->wd);
  if (finfo_it != file_watches.end()) {
    TSEvent event_type            = TS_EVENT_NONE;
    const struct file_info &finfo = finfo_it->second;
    Continuation *contp           = finfo.contp;

    if (event->mask & (IN_DELETE_SELF | IN_MOVED_FROM)) {
      Debug(TAG, "Delete file event (%d) on %s", event->mask, finfo.path.c_str());
      int rc2 = inotify_rm_watch(inotify_fd, event->wd);
      if (rc2 == -1) {
        Error("Failed to remove inotify watch on %s: %s (%d)", finfo.path.c_str(), strerror(errno), errno);
      }
      event_type             = TS_EVENT_FILE_DELETED;
      FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
      cb->data.wd            = event->wd;
      cb->data.name          = nullptr;
      invoke(cb);
    }

    if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
      // Name may be padded with nul characters.  Trim them.
      auto len = strnlen(event->name, event->len);
      std::string name{event->name, len};
      Debug(TAG, "Create file event (%d) on %s (wd = %d): %s", event->mask, finfo.path.c_str(), event->wd, name.c_str());
      event_type = TS_EVENT_FILE_CREATED;

      FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
      cb->filename           = name;
      cb->data.wd            = event->wd;
      cb->data.name          = cb->filename.c_str();
      invoke(cb);
    }

    if (event->mask & (IN_CLOSE_WRITE | IN_ATTRIB)) {
      Debug(TAG, "Modify file event (%d) on %s (wd = %d)", event->mask, finfo.path.c_str(), event->wd);
      event_type             = TS_EVENT_FILE_UPDATED;
      FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
      cb->data.wd            = event->wd;
      cb->data.name          = nullptr;
      invoke(cb);
    }

    if (event->mask & (IN_IGNORED)) {
      Debug(TAG, "Ignored file event (%d) on %s (wd = %d)", event->mask, finfo.path.c_str(), event->wd);
      event_type             = TS_EVENT_FILE_IGNORED;
      FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
      cb->data.wd            = event->wd;
      cb->data.name          = nullptr;
      invoke(cb);
    }
  }
}
#endif

void
FileChangeManager::init()
{
#if TS_USE_INOTIFY
  // TODO: auto configure based on whether inotify is available
  inotify_fd = inotify_init1(IN_CLOEXEC);
  if (inotify_fd == -1) {
    Error("Failed to init inotify: %s (%d)", strerror(errno), errno);
    return;
  }
  auto inotify_thread = [manager = this]() mutable {
    for (;;) {
      char inotify_buf[INOTIFY_BUF_SIZE];

      // blocking read
      ssize_t rc = read(manager->inotify_fd, inotify_buf, sizeof inotify_buf);

      if (rc == -1) {
        Error("Failed to read inotify: %s (%d)", strerror(errno), errno);
        if (errno == EINTR) {
          continue;
        } else {
          break;
        }
      }

      ssize_t offset = 0;
      while (offset < rc) {
        struct inotify_event *event = reinterpret_cast<struct inotify_event *>(inotify_buf + offset);

        // Process file events
        manager->process_file_event(event);
        offset += sizeof(struct inotify_event) + event->len;
      }
    }
  };
  poll_thread = std::thread(inotify_thread);
  poll_thread.detach();
#else
  // Implement this
#endif
}

watch_handle_t
FileChangeManager::add(const ts::file::path &path, TSFileWatchKind kind, Continuation *contp)
{
#if TS_USE_INOTIFY
  Debug(TAG, "Adding a watch on %s", path.c_str());
  watch_handle_t wd = 0;

  // Let the OS handle multiple watches on one file.
  uint32_t mask = 0;
  if (kind == TS_WATCH_CREATE) {
    mask = IN_CREATE | IN_MOVED_TO | IN_ONLYDIR;
  } else if (kind == TS_WATCH_DELETE) {
    mask = IN_DELETE_SELF | IN_MOVED_FROM;
  } else if (kind == TS_WATCH_MODIFY) {
    mask = IN_CLOSE_WRITE | IN_ATTRIB;
  }
  wd = inotify_add_watch(inotify_fd, path.c_str(), mask);
  if (wd == -1) {
    Error("Failed to add file watch on %s: %s (%d)", path.c_str(), strerror(errno), errno);
    return -1;
  } else {
    std::unique_lock file_watches_write_lock(file_watches_mutex);
    file_watches[wd] = {path, contp};
  }

  Debug(TAG, "Watch handle = %d", wd);
  return wd;
#else
  Warning("File change notification is not supported on this OS.");
  return 0;
#endif
}

void
FileChangeManager::remove(watch_handle_t watch_handle)
{
#if TS_USE_INOTIFY
  Debug(TAG, "Deleting watch %d", watch_handle);
  inotify_rm_watch(inotify_fd, watch_handle);
  std::unique_lock file_watches_write_lock(file_watches_mutex);
  file_watches.erase(watch_handle);
#endif
}
