/** @file

  Crash logging helper support

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

#include "Main.h"
#include "ts/I_Layout.h"
#include "I_Net.h"
#include "ts/signals.h"
#include "ts/ink_cap.h"

// ucontext.h is deprecated on Darwin, and we really only need it on Linux, so only
// include it if we are planning to use it.
#if defined(__linux__)
#include <ucontext.h>
#endif

static pid_t crash_logger_pid = -1;
static int crash_logger_fd    = NO_FD;

static char *
create_logger_path()
{
  RecString name;
  std::string bindir;
  ats_scoped_str fullpath;

  if (RecGetRecordString_Xmalloc("proxy.config.crash_log_helper", &name) != REC_ERR_OKAY) {
    return nullptr;
  }

  // Take an absolute path as it is ...
  if (name == nullptr || *name == '/') {
    return name;
  }

  // Otherwise locate it relative to $BINDIR.
  bindir   = RecConfigReadBinDir();
  fullpath = Layout::relative_to(bindir, name);

  ats_free(name);
  return fullpath.release();
}

static bool
check_logger_path(const char *path)
{
  struct stat sbuf;

  if (stat(path, &sbuf) != 0) {
    Error("failed to access crash log helper '%s': %s", path, strerror(errno));
    return false;
  }

  if (!S_ISREG(sbuf.st_mode) || access(path, X_OK) != 0) {
    Error("crash log helper '%s' is not executable", path);
    return false;
  }

  return true;
}

void
crash_logger_init(const char *user)
{
  ats_scoped_str logger(create_logger_path());
  const char *basename;

  pid_t child;
  int status;
  int pipe[2];

  // Do nothing the log helper was set to NULL, or we can't find it.
  if (!logger || !check_logger_path(logger)) {
    return;
  }

  // By this point, we have an absolute path, so we'd better be able to find the basename.
  basename = strrchr(logger, '/') + 1;

  socketpair(AF_UNIX, SOCK_STREAM, 0, pipe);

  child = fork();
  switch (child) {
  case -1:
    Error("failed to fork crash log helper: %s", strerror(errno));
    crash_logger_pid = -1;
    crash_logger_fd  = NO_FD;
    return;

  case 0:
    // Dupe the child socket to stdin.
    dup2(pipe[1], STDIN_FILENO);
    close(pipe[0]);
    close(pipe[1]);

    // Starting after stderr, keep closing file descriptors until we run out.
    for (int fd = STDERR_FILENO + 1; fd; ++fd) {
      if (close(fd) == -1) {
        break;
      }
    }

    ink_release_assert(execl(logger, basename, "--syslog", "--wait", "--host", TS_BUILD_CANONICAL_HOST, "--user", user, NULL) !=
                       -1);
    return; // not reached.
  }

  close(pipe[1]);
  crash_logger_pid = child;
  crash_logger_fd  = pipe[0];

  // Wait for the helper to stop
  if (waitpid(crash_logger_pid, &status, WUNTRACED) > 0) {
    Debug("server", "waited on PID %ld, %s", (long)crash_logger_pid, WIFSTOPPED(status) ? "STOPPED" : "???");

    if (WIFEXITED(status)) {
      Warning("crash logger '%s' unexpectedly exited with status %d", (const char *)logger, WEXITSTATUS(status));
      close(crash_logger_pid);
      crash_logger_pid = -1;
      crash_logger_fd  = NO_FD;
    }
  }
}

void
crash_logger_invoke(int signo, siginfo_t *info, void *ctx)
{
  if (crash_logger_pid != -1) {
    int status;

    // Let the crash logger free ...
    kill(crash_logger_pid, SIGCONT);

#if defined(__linux__)
    // Write the crashing thread information to the crash logger. While the siginfo_t is blesses by POSIX, the
    // ucontext_t can contain pointers, so it's highly platform dependent. On Linux with glibc, however, it is
    // a single memory block that we can just puke out.
    ATS_UNUSED_RETURN(write(crash_logger_fd, info, sizeof(siginfo_t)));
    ATS_UNUSED_RETURN(write(crash_logger_fd, (ucontext_t *)ctx, sizeof(ucontext_t)));
#endif

    close(crash_logger_fd);
    crash_logger_fd = NO_FD;

    // Wait for the logger to finish ...
    waitpid(crash_logger_pid, &status, 0);
  }

  // Log the signal, dump a stack trace and core.
  signal_format_siginfo(signo, info, appVersionInfo.AppStr); // XXX Add timestamp ...
  signal_crash_handler(signo, info, ctx);
}
