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

#include "traffic_crashlog.h"
#include "tscore/ink_args.h"
#include "tscore/ink_cap.h"
#include "tscore/Version.h"
#include "tscore/Layout.h"
#include "tscore/ink_syslog.h"
#include "records/RecProcess.h"
#include "records/RecordsConfig.h"
#include "tscore/BaseLogFile.h"
#include "tscore/runroot.h"
#include "iocore/eventsystem/RecProcess.h"
#include <unistd.h>

static int   syslog_mode  = false;
static int   debug_mode   = false;
static int   wait_mode    = false;
static char *host_triplet = nullptr;
static int   target_pid   = getppid();
static char *user         = nullptr;
static char *exec_pgm     = nullptr;

// If pid_t is not sizeof(int), we will have to jiggle argument parsing.
extern char __pid_size_static_assert[sizeof(pid_t) == sizeof(int) ? 0 : -1];

static const ArgumentDescription argument_descriptions[] = {
  {"target", '-', "Target process ID",                                        "I",  &target_pid,   nullptr, nullptr},
  {"host",   '-', "Host triplet for the process being logged",                "S*", &host_triplet, nullptr, nullptr},
  {"wait",   '-', "Stop until signalled at startup",                          "F",  &wait_mode,    nullptr, nullptr},
  {"syslog", '-', "Syslog after writing a crash log",                         "F",  &syslog_mode,  nullptr, nullptr},
  {"debug",  '-', "Enable debugging mode",                                    "F",  &debug_mode,   nullptr, nullptr},
  {"user",   '-', "Username used to set privileges",                          "S*", &user,         nullptr, nullptr},
  {"exec",   '-', "Program to execute at crash time (takes 1 pid parameter)", "S*", &exec_pgm,     nullptr, nullptr},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION(),
  RUNROOT_ARGUMENT_DESCRIPTION()
};

static struct tm
timestamp()
{
  time_t    now = time(nullptr);
  struct tm tm;

  localtime_r(&now, &tm);
  return tm;
}

static char *
crashlog_name()
{
  char           filename[64];
  struct tm      now = timestamp();
  std::string    logdir(RecConfigReadLogDir());
  ats_scoped_str pathname;

  strftime(filename, sizeof(filename), "crash-%Y-%m-%d-%H%M%S.log", &now);
  pathname = Layout::relative_to(logdir, filename);

  return pathname.release();
}

static FILE *
crashlog_open(const char *path)
{
  int fd;

  fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0400);
  return (fd == -1) ? nullptr : fdopen(fd, "w");
}

extern int ServerBacktrace(unsigned /* options */, int pid, char **trace);

bool
crashlog_write_backtrace(FILE *fp, pid_t pid, const crashlog_target &)
{
  char *trace = nullptr;
  int   mgmterr;

  // NOTE: sometimes we can't get a backtrace because the ptrace attach will fail with
  // EPERM. I've seen this happen when a debugger is attached, which makes sense, but it
  // can also happen without a debugger. Possibly in that case, there is a race with the
  // kernel locking the process information?

  if ((mgmterr = ServerBacktrace(0, static_cast<int>(pid), &trace)) != 0) {
    fprintf(fp, "Unable to retrieve backtrace: %d\n", mgmterr);
    return false;
  }

  fprintf(fp, "%s", trace);
  free(trace);
  return true;
}

void
crashlog_exec_pgm(FILE *fp, pid_t pid)
{
  if (exec_pgm) {
    fprintf(fp, "Executing Program `%s %d`:\n", exec_pgm, pid);
    fflush(fp);
    if (int chpid = fork(); chpid == 0) {
      char tmp[32];
      snprintf(tmp, sizeof(tmp), "%d", pid);
      char *args[3] = {exec_pgm, tmp, nullptr};

      dup2(fileno(fp), STDOUT_FILENO);
      dup2(fileno(fp), STDERR_FILENO);

      if (execv(exec_pgm, args) == -1) {
        Error("Failed to exec pgm: %s\n", strerror(errno));
      }
    } else {
      int stat = 0;
      waitpid(chpid, &stat, 0);
      fflush(fp);
      Note("Exec program returned status %d (pid %d)\n", stat, chpid);
    }
  }
}

int
main(int /* argc ATS_UNUSED */, const char **argv)
{
  FILE           *fp;
  char           *logname;
  crashlog_target target;
  pid_t           parent = getppid();

  DiagsPtr::set(new Diags("traffic_crashlog", "" /* tags */, "" /* actions */, new BaseLogFile("stderr")));

  auto &version = AppVersionInfo::setup_version("traffic_crashlog");

  // Process command line arguments and dump into variables
  process_args(&version, argument_descriptions, countof(argument_descriptions), argv);

  if (wait_mode) {
    EnableDeathSignal(SIGKILL);
    kill(getpid(), SIGSTOP);
  }

  // If our parent changed, then we were woken after traffic_server exited. There's no point trying to
  // emit a crashlog because traffic_server is gone.
  if (getppid() != parent) {
    return 0;
  }

  runroot_handler(argv);
  Layout::create();
  RecProcessInit(nullptr /* diags */);
  LibRecordsConfigInit();

  if (syslog_mode) {
    RecString name;
    int       facility = -1;

    if (REC_ReadConfigStringAlloc(name, "proxy.config.syslog_facility") == REC_ERR_OKAY) {
      facility = facility_string_to_int(name);
      ats_free(name);
    }

    if (facility < 0) {
      facility = LOG_DAEMON;
    }

    openlog(version.application(), LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility);
    diags()->config.outputs[DL_Debug].to_syslog     = true;
    diags()->config.outputs[DL_Status].to_syslog    = true;
    diags()->config.outputs[DL_Note].to_syslog      = true;
    diags()->config.outputs[DL_Warning].to_syslog   = true;
    diags()->config.outputs[DL_Error].to_syslog     = true;
    diags()->config.outputs[DL_Fatal].to_syslog     = true;
    diags()->config.outputs[DL_Alert].to_syslog     = true;
    diags()->config.outputs[DL_Emergency].to_syslog = true;
  }

  Note("crashlog started, target=%ld, debug=%s syslog=%s, uid=%ld euid=%ld", static_cast<long>(target_pid),
       debug_mode ? "true" : "false", syslog_mode ? "true" : "false", (long)getuid(), (long)geteuid());

  ink_zero(target);
  target.pid       = static_cast<pid_t>(target_pid);
  target.timestamp = timestamp();

  if (host_triplet && strncmp(host_triplet, "x86_64-unknown-linux", sizeof("x86_64-unknown-linux") - 1) == 0) {
    ssize_t nbytes;
    target.flags |= CRASHLOG_HAVE_THREADINFO;

    nbytes = read(STDIN_FILENO, &target.siginfo, sizeof(target.siginfo));
    if (nbytes < static_cast<ssize_t>(sizeof(target.siginfo))) {
      Warning("received %zd of %zu expected signal info bytes", nbytes, sizeof(target.siginfo));
      target.flags &= ~CRASHLOG_HAVE_THREADINFO;
    }

    nbytes = read(STDIN_FILENO, &target.ucontext, sizeof(target.ucontext));
    if (nbytes < static_cast<ssize_t>(sizeof(target.ucontext))) {
      Warning("received %zd of %zu expected thread context bytes", nbytes, sizeof(target.ucontext));
      target.flags &= ~CRASHLOG_HAVE_THREADINFO;
    }
  }

  logname = crashlog_name();

  fp = debug_mode ? stdout : crashlog_open(logname);
  if (fp == nullptr) {
    Error("failed to create '%s': %s", logname, strerror(errno));
    ats_free(logname);
    return 1;
  }

  Note("logging to %p", fp);

  crashlog_write_procname(fp, target);
  crashlog_write_exename(fp, target);
  fprintf(fp, LABELFMT "Traffic Server %s\n", "Version:", PACKAGE_VERSION);
  crashlog_write_uname(fp, target);
  crashlog_write_datime(fp, target);

  fprintf(fp, "\n");
  crashlog_write_siginfo(fp, target);

  fprintf(fp, "\n");
  crashlog_write_registers(fp, target);

  fprintf(fp, "\n");
  crashlog_write_backtrace(fp, parent, target);

  fprintf(fp, "\n");
  crashlog_write_procstatus(fp, target);
  fprintf(fp, "\n");
  crashlog_write_proclimits(fp, target);

  fprintf(fp, "\n");
  crashlog_write_regions(fp, target);

  fprintf(fp, "\n");
  crashlog_exec_pgm(fp, target.pid);

  Error("wrote crash log to %s", logname);

  ats_free(logname);

  fflush(fp);
  fclose(fp);
  return 0;
}
