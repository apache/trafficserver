/** @file

  A brief file description

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

/****************************************************************************

  Basic Locks for Threads



**************************************************************************/
#include "P_EventSystem.h"
#include "tscore/Diags.h"
#include "tscore/ink_hrtime.h"

#include <map>

ClassAllocator<ProxyMutex> mutexAllocator("mutexAllocator");

struct FrameHeader {
  char     magic[4];
  uint32_t label_count;
  uint32_t label_offset;
  uint32_t label_size;
  uint32_t entry_count;
  uint32_t entry_offset;
  uint32_t entry_size;
  int64_t  time_start;
  int64_t  time_last;
};

struct LabelEntry {
  uint16_t size;
  char     data[0];
};

enum MutexOp { TryLockOp = 0, LockOp = 1, UnlockOp = 2 };

struct MutexInfo {
  int     thread    = 0;
  int     file      = 0;
  int     func      = 0;
  int     line      = 0;
  int     handler   = 0;
  int64_t hold_time = 0;
};

struct LedgerEntry {
  char      id[4];
  MutexOp   op;
  MutexInfo holding;
  MutexInfo waiting;
  uintptr_t mutex_addr;
#ifdef ENABLE_EVENT_CORRELATION
  Event::CorrelationType correlation;
#else
  char padcorr[8]; // padding to align if correlation is not enabled
#endif
  int64_t timestamp;
  char    pad[4];

  LedgerEntry()
  {
    strncpy(id, "LENT", 4);
    strncpy(pad, "EEND", 4);
  }
};

class InternedLabels
{
public:
  InternedLabels() { ldata.reserve(1024 * 16); }
  int
  intern(const std::string_view v)
  {
    if (!v.empty()) {
      auto [it, inserted] = offsets.insert({v, 0});
      if (inserted) {
        it->second = push_string(v);
      }

      return it->second;
    }
    using namespace std::literals;
    return intern("empty"sv);
  }

  int
  push_string(std::string_view v)
  {
    int result = ldata.size();
    push_entry(v);
    return result;
  }

  char *
  data()
  {
    return ldata.data();
  }

  size_t
  size()
  {
    return ldata.size();
  }

  size_t
  label_count()
  {
    return offsets.size();
  }

private:
  std::map<std::string_view, int> offsets;
  std::vector<char>               ldata;

  LabelEntry *
  push_entry(std::string_view v)
  {
    size_t size = v.size() + sizeof(LabelEntry);
    if ((ldata.capacity() - ldata.size()) < size) {
      ldata.reserve(ldata.capacity() * 2);
    }
    int offset = ldata.size();
    ldata.resize(ldata.size() + size);

    LabelEntry *result = new (ldata.data() + offset) LabelEntry;
    result->size       = v.size();
    memcpy(result->data, v.data(), v.size());

    return result;
  }
};

class LockLedger
{
public:
  LockLedger() { entries.reserve(100000); }
  ~LockLedger() {}

  MutexInfo
  info_for(ProxyMutex *m)
  {
    MutexInfo e;
    if (m->thread_holding) {
      e.thread = labels.intern(m->thread_holding->name());
    } else {
      using namespace std::literals;
      e.thread = labels.intern("unnamed"sv);
    }
    e.file = labels.intern(m->srcloc.basefile());
    e.func = labels.intern(m->srcloc.func);
    e.line = m->srcloc.line;
    if (m->handler) {
      e.handler = labels.intern(std::string_view(m->handler));
    }
    e.hold_time = m->hold_time;
    return e;
  }

  MutexInfo
  info_for(const SourceLocation &srcloc, const char *handler, EThread *wt)
  {
    MutexInfo e;
    e.thread = labels.intern(wt->name());
    e.file   = labels.intern(srcloc.basefile());
    e.func   = labels.intern(srcloc.func);
    e.line   = srcloc.line;
    if (handler) {
      e.handler = labels.intern(std::string_view(handler));
    }
    return e;
  }

  LedgerEntry &
  add_entry(MutexOp op, ProxyMutex *m)
  {
    auto &e = entries.emplace_back();
    ;
    e.timestamp  = ink_get_hrtime();
    e.op         = op;
    e.mutex_addr = reinterpret_cast<uintptr_t>(m);
    e.holding    = info_for(m);
    e.waiting    = {};
#ifdef ENABLE_EVENT_CORRELATION
    e.correlation = EThread::correlation();
#endif

    return e;
  }

  LedgerEntry &
  add_entry(MutexOp op, ProxyMutex *m, const SourceLocation &waitloc, const char *waithandler, EThread *wt)
  {
    auto &e      = entries.emplace_back();
    e.timestamp  = ink_get_hrtime();
    e.op         = op;
    e.mutex_addr = reinterpret_cast<uintptr_t>(m);
    if (op != TryLockOp) {
      e.holding = info_for(m);
    } else {
      e.holding = {};
    }
    e.waiting = info_for(waitloc, waithandler, wt);
#ifdef ENABLE_EVENT_CORRELATION
    e.correlation = EThread::correlation();
#endif

    return e;
  }

  size_t
  buffer_size()
  {
    return sizeof(FrameHeader) + labels.size() + entries.size() * sizeof(LedgerEntry);
  }

  void
  reset()
  {
    entries.resize(0);
  }

  bool
  empty()
  {
    return entries.empty();
  }

  size_t
  size()
  {
    return entries.size();
  }

  std::array<std::pair<char *, size_t>, 3>
  buffers()
  {
    std::array<std::pair<char *, size_t>, 3> result;
    result[0].first  = reinterpret_cast<char *>(&header);
    result[0].second = sizeof(header);
    result[1].first  = labels.data();
    result[1].second = labels.size();
    result[2].first  = reinterpret_cast<char *>(entries.data());
    result[2].second = sizeof(LedgerEntry) * entries.size();

    strncpy(header.magic, "LOCK", 4);
    header.entry_count  = entries.size();
    header.entry_offset = result[0].second + result[1].second;
    header.entry_size   = result[2].second;
    header.label_count  = labels.label_count();
    header.label_offset = result[0].second;
    header.label_size   = result[1].second;
    if (entries.empty()) {
      header.time_start = 0;
      header.time_last  = 0;
    } else {
      header.time_start = entries.front().timestamp;
      header.time_last  = entries.back().timestamp;
    }

    return result;
  }

private:
  FrameHeader              header;
  InternedLabels           labels;
  std::vector<LedgerEntry> entries;
};

class ThreadMutexLedger
{
public:
  ThreadMutexLedger() : fd(-1), last_flush(ink_get_hrtime()) {}

  ~ThreadMutexLedger()
  {
    flush();
    if (fd != -1) {
      close(fd);
      fd = -1;
    }
  }

  void
  flush()
  {
    if (fd == -1 && this_ethread()) {
      char             path[256];
      std::string_view name = this_ethread()->name();
      snprintf(path, sizeof(path), "/tmp/lockledger.%.*s.bin", static_cast<int>(name.size()), name.data());
      fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
      ink_assert(fd != -1);
    }
    if (fd != -1 && !ledger.empty()) {
      iovec v[3];
      int   i = 0;
      for (auto [p, s] : ledger.buffers()) {
        v[i].iov_base = p;
        v[i].iov_len  = s;
        i++;
      }
      ssize_t bc = writev(fd, v, 3);
      ink_assert(bc == static_cast<ssize_t>(ledger.buffer_size()));
      ledger.reset();
    }
  }

  void
  add_entry(MutexOp op, ProxyMutex *m)
  {
    auto &e = ledger.add_entry(op, m);
    if (ledger.size() > 250000 || e.timestamp > (last_flush + (5 * HRTIME_SECOND))) {
      flush();
      last_flush = e.timestamp;
    }
  }

  void
  add_entry(MutexOp op, ProxyMutex *m, const SourceLocation &w, const char *ah, EThread *wt)
  {
    auto &e = ledger.add_entry(op, m, w, ah, wt);
    if (e.timestamp > (last_flush + (5 * HRTIME_SECOND))) {
      flush();
      last_flush = e.timestamp;
    }
  }

  int        fd;
  ink_hrtime last_flush;
  LockLedger ledger;
};

thread_local ThreadMutexLedger the_ledger;

void
lock_waiting(ProxyMutex *m, const SourceLocation &waiterloc, const char *ahandler, EThread *wt)
{
  the_ledger.add_entry(TryLockOp, m, waiterloc, ahandler, wt);
}

void
lock_holding(ProxyMutex *m)
{
}

void
lock_taken(ProxyMutex *m)
{
}

void
lock_locked(ProxyMutex *m)
{
  the_ledger.add_entry(LockOp, m);
}

void
lock_unlock(ProxyMutex *m)
{
  the_ledger.add_entry(UnlockOp, m);
}

#ifdef LOCK_CONTENTION_PROFILING
void
ProxyMutex::print_lock_stats(int flag)
{
  if (flag) {
    if (total_acquires < 10)
      return;
    printf("Lock Stats (Dying):successful %d (%.2f%%), unsuccessful %d (%.2f%%) blocking %d \n", successful_nonblocking_acquires,
           (nonblocking_acquires > 0 ? successful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0),
           unsuccessful_nonblocking_acquires,
           (nonblocking_acquires > 0 ? unsuccessful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0), blocking_acquires);
    fflush(stdout);
  } else {
    if (!(total_acquires % 100)) {
      printf("Lock Stats (Alive):successful %d (%.2f%%), unsuccessful %d (%.2f%%) blocking %d \n", successful_nonblocking_acquires,
             (nonblocking_acquires > 0 ? successful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0),
             unsuccessful_nonblocking_acquires,
             (nonblocking_acquires > 0 ? unsuccessful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0),
             blocking_acquires);
      fflush(stdout);
    }
  }
}
#endif // LOCK_CONTENTION_PROFILING
