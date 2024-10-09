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

#if TS_USE_REMOTE_UNWINDING
#include "tscore/Diags.h"

#include "tscore/TextBuffer.h"
#include <string.h>
#include <libunwind.h>
#include <libunwind-ptrace.h>
#include <sys/ptrace.h>
#include <cxxabi.h>
#include <vector>

namespace
{
using threadlist = std::vector<pid_t>;

DbgCtl dbg_ctl_backtrace{"backtrace"};

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
  unsigned         level  = 0;

  // First, attach to the child, causing it to stop.
  status = ptrace(PTRACE_ATTACH, threadid, 0, 0);
  if (status < 0) {
    Dbg(dbg_ctl_backtrace, "ptrace(ATTACH, %ld) -> %s (%d)\n", (long)threadid, strerror(errno), errno);
    return;
  }

  // Wait for it to stop (XXX should be a timed wait ...)
  target = waitpid(threadid, &status, __WALL | WUNTRACED);
  Dbg(dbg_ctl_backtrace, "waited for target %ld, found PID %ld, %s\n", (long)threadid, (long)target,
      WIFSTOPPED(status) ? "STOPPED" : "???");
  if (target < 0) {
    goto done;
  }

  ap = _UPT_create(threadid);
  Dbg(dbg_ctl_backtrace, "created UPT %p", ap);
  if (ap == nullptr) {
    goto done;
  }

  addr_space = unw_create_addr_space(&_UPT_accessors, 0 /* byteorder */);
  Dbg(dbg_ctl_backtrace, "created address space %p\n", addr_space);
  if (addr_space == nullptr) {
    goto done;
  }

  status = unw_init_remote(&cursor, addr_space, ap);
  Dbg(dbg_ctl_backtrace, "unw_init_remote(...) -> %d\n", status);
  if (status != 0) {
    goto done;
  }

  while (unw_step(&cursor) > 0) {
    unw_word_t ip;
    unw_word_t offset;
    char       buf[256];

    unw_get_reg(&cursor, UNW_REG_IP, &ip);

    if (unw_get_proc_name(&cursor, buf, sizeof(buf), &offset) == 0) {
      int   status;
      char *name = abi::__cxa_demangle(buf, nullptr, nullptr, &status);
      text.format("%-4u 0x%016llx %s + %p\n", level, static_cast<unsigned long long>(ip), name ? name : buf, (void *)offset);
      free(name);
    } else {
      text.format("%-4u 0x%016llx 0x0 + %p\n", level, static_cast<unsigned long long>(ip), (void *)offset);
    }

    ++level;
  }

done:
  if (addr_space) {
    unw_destroy_addr_space(addr_space);
  }

  if (ap) {
    _UPT_destroy(ap);
  }

  status = ptrace(PTRACE_DETACH, target, NULL, NULL);
  Dbg(dbg_ctl_backtrace, "ptrace(DETACH, %ld) -> %d (errno %d)\n", (long)target, status, errno);
}
} // namespace
int
ServerBacktrace(unsigned /* options */, int pid, char **trace)
{
  *trace = nullptr;

  threadlist threads(threads_for_process(pid));
  TextBuffer text(0);

  Dbg(dbg_ctl_backtrace, "tracing %zd threads for traffic_server PID %ld\n", threads.size(), (long)pid);

  for (auto threadid : threads) {
    Dbg(dbg_ctl_backtrace, "tracing thread %ld\n", (long)threadid);
    // Get the thread name using /proc/PID/comm
    ats_scoped_fd fd;
    char          threadname[128];

    snprintf(threadname, sizeof(threadname), "/proc/%ld/comm", static_cast<long>(threadid));
    fd = open(threadname, O_RDONLY);
    if (fd >= 0) {
      text.format("Thread %ld, ", static_cast<long>(threadid));
      text.readFromFD(fd);
      text.chomp();
    } else {
      text.format("Thread %ld", static_cast<long>(threadid));
    }

    text.format(":\n");

    backtrace_for_thread(threadid, text);
    text.format("\n");
  }

  *trace = text.release();
  return 0;
}

#else /* TS_USE_REMOTE_UNWINDING */

int
ServerBacktrace([[maybe_unused]] unsigned options, [[maybe_unused]] int pid, char **trace)
{
  *trace = nullptr;
  return -1;
}

#endif
