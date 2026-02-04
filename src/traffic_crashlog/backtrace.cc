/** @file

  backtrace.cc

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

  backtrace.cc

  Functions to generate backtrace from a TS process.

 ****************************************************************************/
#include "tscore/ink_config.h"

#include <sys/types.h>

#if TS_USE_REMOTE_UNWINDING
#include "tscore/Diags.h"

#include "tscore/TextBuffer.h"
#include <string.h>
#include <libunwind.h>
#include <libunwind-ptrace.h>
#if defined(__FreeBSD__)
#define __WALL        P_ALL
#define PTRACE_ATTACH PT_ATTACH
#define PTRACE_DETACH PT_DETACH
#define DATA_NULL     0
#else
#define DATA_NULL NULL
#endif
#include <sys/ptrace.h>
#include <cxxabi.h>
#include <vector>

namespace
{
using threadlist = std::vector<pid_t>;

DbgCtl dbg_ctl_backtrace{"backtrace"};

/** Enumerate all threads for a given process by reading /proc/<pid>/task. */
static threadlist
threads_for_process(pid_t proc)
{
  DIR           *dir   = nullptr;
  struct dirent *entry = nullptr;

  char       path[64];
  threadlist threads;

  if (snprintf(path, sizeof(path), "/proc/%ld/task", static_cast<long>(proc)) >= static_cast<int>(sizeof(path))) {
    goto done;
  }

  dir = opendir(path);
  if (dir == nullptr) {
    goto done;
  }

  while ((entry = readdir(dir))) {
    pid_t threadid;

    if (isdot(entry->d_name) || isdotdot(entry->d_name)) {
      continue;
    }

    threadid = strtol(entry->d_name, nullptr, 10);
    if (threadid > 0) {
      threads.push_back(threadid);
      Dbg(dbg_ctl_backtrace, "found thread %ld\n", (long)threadid);
    }
  }

done:
  if (dir) {
    closedir(dir);
  }

  return threads;
}

static void
backtrace_for_thread(pid_t threadid, TextBuffer &text)
{
  int              status;
  unw_addr_space_t addr_space = nullptr;
  unw_cursor_t     cursor;
  void            *ap     = nullptr;
  pid_t            target = -1;
  bool             attached{false};
  unsigned         level = 0;
  int              step_result;

  // First, attach to the child, causing it to stop.
  status = ptrace(PTRACE_ATTACH, threadid, 0, 0);
  if (status < 0) {
    text.format("  [ptrace ATTACH failed: %s (%d)]\n", strerror(errno), errno);
    Dbg(dbg_ctl_backtrace, "ptrace(ATTACH, %ld) -> %s (%d)\n", (long)threadid, strerror(errno), errno);
    return;
  }
  attached = true;

  // Wait for it to stop. The caller uses alarm() to enforce a timeout.
  target = waitpid(threadid, &status, __WALL | WUNTRACED);
  Dbg(dbg_ctl_backtrace, "waited for target %ld, found PID %ld, %s\n", (long)threadid, (long)target,
      WIFSTOPPED(status) ? "STOPPED" : "???");
  if (target < 0) {
    text.format("  [waitpid failed: %s (%d)]\n", strerror(errno), errno);
    goto done;
  }

  ap = _UPT_create(threadid);
  Dbg(dbg_ctl_backtrace, "created UPT %p", ap);
  if (ap == nullptr) {
    text.format("  [_UPT_create failed]\n");
    goto done;
  }

  addr_space = unw_create_addr_space(&_UPT_accessors, 0 /* byteorder */);
  Dbg(dbg_ctl_backtrace, "created address space %p\n", addr_space);
  if (addr_space == nullptr) {
    text.format("  [unw_create_addr_space failed]\n");
    goto done;
  }

  status = unw_init_remote(&cursor, addr_space, ap);
  Dbg(dbg_ctl_backtrace, "unw_init_remote(...) -> %d\n", status);
  if (status != 0) {
    text.format("  [unw_init_remote failed: %d]\n", status);
    goto done;
  }

  step_result = unw_step(&cursor);
  if (step_result <= 0) {
    text.format("  [unw_step returned %d on first call]\n", step_result);
  }

  while (step_result > 0) {
    unw_word_t ip;
    unw_word_t offset = 0;
    char       buf[256];

    unw_get_reg(&cursor, UNW_REG_IP, &ip);

    if (unw_get_proc_name(&cursor, buf, sizeof(buf), &offset) == 0) {
      int   demangle_status;
      char *name = abi::__cxa_demangle(buf, nullptr, nullptr, &demangle_status);
      text.format("%-4u 0x%016llx %s + 0x%lx\n", level, static_cast<unsigned long long>(ip), name ? name : buf,
                  static_cast<unsigned long>(offset));
      free(name);
    } else {
      text.format("%-4u 0x%016llx <unknown>\n", level, static_cast<unsigned long long>(ip));
    }

    ++level;
    step_result = unw_step(&cursor);
  }

done:
  if (addr_space) {
    unw_destroy_addr_space(addr_space);
  }

  if (ap) {
    _UPT_destroy(ap);
  }

  if (attached) {
    status = ptrace(PTRACE_DETACH, threadid, nullptr, DATA_NULL);
    Dbg(dbg_ctl_backtrace, "ptrace(DETACH, %ld) -> %d (errno %d)\n", (long)threadid, status, errno);
  }
}

/** Format a thread header with the thread name from /proc. */
static void
format_thread_header(pid_t threadid, const char *prefix, TextBuffer &text)
{
  ats_scoped_fd fd;
  char          path[128];

  snprintf(path, sizeof(path), "/proc/%ld/comm", static_cast<long>(threadid));
  fd = open(path, O_RDONLY);
  if (fd >= 0) {
    text.format("%s (TID %ld, ", prefix, static_cast<long>(threadid));
    text.readFromFD(fd);
    text.chomp();
    text.format("):\n");
  } else {
    text.format("%s (TID %ld):\n", prefix, static_cast<long>(threadid));
  }
}
} // namespace

int
ServerBacktrace(unsigned /* options */, pid_t pid, pid_t crashing_tid, char **trace)
{
  *trace = nullptr;

  threadlist threads(threads_for_process(pid));
  TextBuffer text(0);

  Dbg(dbg_ctl_backtrace, "tracing %zd threads for traffic_server PID %ld, crashing TID %ld\n", threads.size(),
      static_cast<long>(pid), static_cast<long>(crashing_tid));

  // First, trace the crashing thread.
  if (crashing_tid > 0) {
    Dbg(dbg_ctl_backtrace, "tracing crashing thread %ld\n", static_cast<long>(crashing_tid));
    format_thread_header(crashing_tid, "Crashing Thread", text);
    backtrace_for_thread(crashing_tid, text);
    text.format("\n");
  }

  // Then trace all other threads.
  bool printed_header = false;
  for (auto threadid : threads) {
    if (threadid == crashing_tid) {
      continue;
    }

    if (!printed_header) {
      text.format("Other Non-Crashing Threads:\n\n");
      printed_header = true;
    }

    Dbg(dbg_ctl_backtrace, "tracing thread %ld\n", static_cast<long>(threadid));
    format_thread_header(threadid, "Thread", text);
    backtrace_for_thread(threadid, text);
    text.format("\n");
  }

  *trace = text.release();
  return 0;
}

#else /* TS_USE_REMOTE_UNWINDING */

int
ServerBacktrace([[maybe_unused]] unsigned options, [[maybe_unused]] pid_t pid, [[maybe_unused]] pid_t crashing_tid, char **trace)
{
  *trace = nullptr;
  return -1;
}

#endif
