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
#include "tscore/ink_assert.h"

#include <cassert>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <sys/fcntl.h>
#include <thread>
#include <ts/apidefs.h>
#include <chrono>
#if TS_USE_KQUEUE
#include <sys/event.h>
#endif

// Globals
FileChangeManager fileChangeManager;
static constexpr auto TAG ATS_UNUSED = "FileChange";

#if TS_USE_KQUEUE
using namespace std::chrono_literals;

// When using kqueue, new watches will take effect after at most this much time.
// This value should be greater than 0.  Smaller values cost more CPU.
static constexpr auto latency = 1s;

static constexpr timespec
chrono_to_timespec(std::chrono::nanoseconds duration)
{
  if (duration <= 0s) {
    duration = 1s;
  }

  auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
  duration -= seconds;
  return timespec{seconds.count(), duration.count()};
}

#endif

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

static void
invoke(FileChangeCallback *cb)
{
  void *cookie = static_cast<void *>(&cb->data);
  eventProcessor.schedule_imm(cb, ET_TASK, 1, cookie);
}

#if TS_USE_INOTIFY
static constexpr size_t INOTIFY_BUF_SIZE = 4096;

void
FileChangeManager::inotify_process_event(struct inotify_event *event)
{
  std::shared_lock file_watches_read_lock(file_watches_mutex);
  auto finfo_it = file_watches.find(event->wd);
  if (finfo_it != file_watches.end()) {
    TSEvent event_type          = TS_EVENT_NONE;
    const FileChangeInfo &finfo = finfo_it->second;
    Continuation *contp         = finfo.contp;

    if (event->mask & (IN_DELETE_SELF | IN_MOVED_FROM)) {
      Debug(TAG, "Delete file event (%d) on %s", event->mask, finfo.path.c_str());
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
#elif TS_USE_KQUEUE
static void
kqueue_make_event(int fd, const FileChangeInfo &info, struct kevent *event)
{
  unsigned int mask = 0;
  if (info.kind == TS_WATCH_CREATE) {
    mask = NOTE_WRITE | NOTE_DELETE | NOTE_RENAME;
  } else if (info.kind == TS_WATCH_DELETE) {
    mask = NOTE_DELETE | NOTE_RENAME;
  } else if (info.kind == TS_WATCH_MODIFY) {
    mask = NOTE_WRITE | NOTE_DELETE | NOTE_RENAME;
  } else {
    // Shouldn't get here
    ink_release_assert(false);
  }
  EV_SET(event, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, mask, 0, (void *)(uintptr_t)fd);
}

void
FileChangeManager::kqueue_prepare_events()
{
  if (file_watches_dirty) {
    // Make events for kqueue to monitor
    Debug(TAG, "Updating kqueue event list.");
    std::unique_lock file_watches_lock{file_watches_mutex}; // unique lock because file_watches_dirty will be written
    auto nwatches = file_watches.size();
    events_to_monitor.resize(nwatches);
    events_from_kqueue.resize(nwatches);
    int i = 0;
    for (const auto &[wd, info] : file_watches) {
      // Add a file event to the list of events to monitor
      kqueue_make_event(wd, info, &events_to_monitor[i]);
      i++;
    }
    file_watches_dirty = false;
  }
}

int
FileChangeManager::kqueue_wait_for_events()
{
  // Wait for kqueue events
  if (events_to_monitor.empty()) {
    std::this_thread::sleep_for(latency);
  } else {
    // I couldn't find a clear answer as to whether the timeout value can be modified by kevent (e.g. on an interrupt)
    constexpr timespec latency_timespec = chrono_to_timespec(latency);
    return kevent(kq, events_to_monitor.data(), events_to_monitor.size(), events_from_kqueue.data(), events_from_kqueue.size(),
                  &latency_timespec);
  }

  return 0;
}

void
FileChangeManager::kqueue_process_event(const struct kevent &event)
{
  std::shared_lock file_watches_read_lock(file_watches_mutex);
  auto fd64     = reinterpret_cast<uint64_t>(event.udata);
  auto fd       = static_cast<int>(fd64); // Intentionally truncating to an int.  Casting twice to suppress compiler warnings.
  auto finfo_it = file_watches.find(fd);
  if (finfo_it != file_watches.end()) {
    TSEvent event_type          = TS_EVENT_NONE;
    const FileChangeInfo &finfo = finfo_it->second;
    Continuation *contp         = finfo.contp;

    if (event.fflags & (NOTE_DELETE | NOTE_RENAME)) {
      Debug(TAG, "Delete file event (%d) on %s", event.fflags, finfo.path.c_str());
      if (finfo.kind == TS_WATCH_DELETE) {
        event_type             = TS_EVENT_FILE_DELETED;
        FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
        cb->data.wd            = fd;
        cb->data.name          = nullptr;
        invoke(cb);
      }

      // kqueue doesn't notify us if a file watch no longer applies, so we do it.
      event_type             = TS_EVENT_FILE_IGNORED;
      FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
      cb->data.wd            = fd;
      cb->data.name          = nullptr;
      invoke(cb);
    }

    if (event.fflags & (NOTE_WRITE) && finfo.kind == TS_WATCH_CREATE) {
      Debug(TAG, "Create file event (%d) on %s (wd = %d)", event.fflags, finfo.path.c_str(), fd);
      event_type = TS_EVENT_FILE_CREATED;

      FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
      cb->data.wd            = fd;
      cb->data.name          = nullptr; // a kqueue create has no name
      invoke(cb);
    }

    if (event.fflags & (NOTE_WRITE) && finfo.kind == TS_WATCH_MODIFY) {
      Debug(TAG, "Modify file event (%d) on %s (wd = %d)", event.fflags, finfo.path.c_str(), fd);
      event_type             = TS_EVENT_FILE_UPDATED;
      FileChangeCallback *cb = new FileChangeCallback(contp, event_type);
      cb->data.wd            = fd;
      cb->data.name          = nullptr;
      invoke(cb);
    }
  }
  if (event.flags & EV_ERROR) {
    Error("kqueue error: %s (%" PRIxPTR ")", strerror(event.data), event.data);
    return;
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
        manager->inotify_process_event(event);
        offset += sizeof(struct inotify_event) + event->len;
      }
    }
  };
  poll_thread = std::thread(inotify_thread);
  poll_thread.detach();
#elif TS_USE_KQUEUE
  auto kqueue_thread = [manager = this]() mutable {
    manager->kq = kqueue();
    if (manager->kq < 0) {
      Fatal("Failed to init kqueue: %s.", strerror(errno));
    }
    for (;;) {
      manager->kqueue_prepare_events();
      int event_count = manager->kqueue_wait_for_events();
      if (event_count == -1) {
        Error("kqueue error: %s", strerror(errno));
      }
      for (int i = 0; i < event_count; i++) {
        manager->kqueue_process_event(manager->events_from_kqueue[i]);
      }
    }
  };
  poll_thread = std::thread(kqueue_thread);
  poll_thread.detach();
#else
  // Implement this
#endif
}

watch_handle_t
FileChangeManager::add(const ts::file::path &path, TSFileWatchKind kind, Continuation *contp)
{
  watch_handle_t wd = 0;
  std::unique_lock file_watches_write_lock{file_watches_mutex};
  Debug(TAG, "Adding a watch on %s", path.c_str());

#if TS_USE_INOTIFY
  // Let the OS handle multiple watches on one file.
  uint32_t mask = 0;
  if (kind == TS_WATCH_CREATE) {
    mask = IN_CREATE | IN_MOVED_TO | IN_ONLYDIR;
  } else if (kind == TS_WATCH_DELETE) {
    mask = IN_DELETE_SELF | IN_MOVED_FROM;
  } else if (kind == TS_WATCH_MODIFY) {
    mask = IN_CLOSE_WRITE | IN_ATTRIB;
  } else {
    // Shouldn't get here
    ink_release_assert(false);
  }
  wd = inotify_add_watch(inotify_fd, path.c_str(), mask);
  if (wd == -1) {
    Error("Failed to add file watch on %s: %s (%d)", path.c_str(), strerror(errno), errno);
    return -1;
  }

  Debug(TAG, "Watch handle = %d", wd);
#elif TS_USE_KQUEUE
  int o_flags = 0;

#ifdef O_SYMLINK
  o_flags |= O_SYMLINK;
#endif

#ifdef O_EVTONLY
  // The descriptor is requested for event notifications only.
  o_flags |= O_EVTONLY;
#else
  o_flags |= O_RDONLY;
#endif

#ifdef O_DIRECTORY
  if (kind == TS_WATCH_CREATE) {
    // file creation can only be monitored from the directory above
    o_flags |= O_DIRECTORY;
  }
#endif

  wd = open(path.c_str(), o_flags);
  if (wd <= 0) {
    Error("Failed to open %s for monitoring: %s.", path.c_str(), strerror(errno));
    return -1;
  }
  file_watches_dirty = true;
#else
  Warning("File change notification is not supported on this OS.");
#endif
  file_watches.try_emplace(wd, kind, path, contp);
  return wd;
}

void
FileChangeManager::remove(watch_handle_t watch_handle)
{
  std::unique_lock file_watches_write_lock(file_watches_mutex);
  Debug(TAG, "Deleting watch %d", watch_handle);
#if TS_USE_INOTIFY
  inotify_rm_watch(inotify_fd, watch_handle);
#elif TS_USE_KQUEUE
  close(watch_handle);
  file_watches_dirty = true;
#endif
  file_watches.erase(watch_handle);
}
