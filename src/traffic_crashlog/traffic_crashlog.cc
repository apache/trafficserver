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
#include "tscore/I_Version.h"
#include "tscore/I_Layout.h"
#include "tscore/ink_syslog.h"
#include "records/I_RecProcess.h"
#include "RecordsConfig.h"
#include "tscore/BaseLogFile.h"
#include "tscore/runroot.h"

static int syslog_mode    = false;
static int debug_mode     = false;
static int wait_mode      = false;
static char *host_triplet = nullptr;
static int target_pid     = getppid();
static char *user         = nullptr;

// If pid_t is not sizeof(int), we will have to jiggle argument parsing.
extern char __pid_size_static_assert[sizeof(pid_t) == sizeof(int) ? 0 : -1];

static AppVersionInfo appVersionInfo;
static const ArgumentDescription argument_descriptions[] = {
  {"target", '-', "Target process ID", "I", &target_pid, nullptr, nullptr},
  {"host", '-', "Host triplet for the process being logged", "S*", &host_triplet, nullptr, nullptr},
  {"wait", '-', "Stop until signalled at startup", "F", &wait_mode, nullptr, nullptr},
  {"syslog", '-', "Syslog after writing a crash log", "F", &syslog_mode, nullptr, nullptr},
  {"debug", '-', "Enable debugging mode", "F", &debug_mode, nullptr, nullptr},
  {"user", '-', "Username used to set privileges", "S*", &user, nullptr, nullptr},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION(),
  RUNROOT_ARGUMENT_DESCRIPTION()};

static struct tm
timestamp()
{
  time_t now = time(nullptr);
  struct tm tm;

  localtime_r(&now, &tm);
  return tm;
}

static char *
crashlog_name()
{
  char filename[64];
  struct tm now = timestamp();
  std::string logdir(RecConfigReadLogDir());
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

static unsigned
max_passwd_size()
{
#if defined(_SC_GETPW_R_SIZE_MAX)
  long val = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (val > 0) {
    return (unsigned)val;
  }
#endif

  return 4096;
}

static void
change_privileges()
{
  struct passwd *pwd;
  struct passwd pbuf;
  char buf[max_passwd_size()];

  if (getpwnam_r(user, &pbuf, buf, sizeof(buf), &pwd) != 0) {
    Error("missing password database entry for username '%s': %s", user, strerror(errno));
    return;
  }

  if (pwd == nullptr) {
    // Password entry not found ...
    Error("missing password database entry for '%s'", user);
    return;
  }

  if (setegid(pwd->pw_gid) != 0) {
    Error("setegid(%d) failed: %s", pwd->pw_gid, strerror(errno));
    return;
  }

  if (setreuid(pwd->pw_uid, 0) != 0) {
    Error("setreuid(%d, %d) failed: %s", pwd->pw_uid, 0, strerror(errno));
    return;
  }
}

int
main(int /* argc ATS_UNUSED */, const char **argv)
{
  FILE *fp;
  char *logname;
  TSMgmtError mgmterr;
  crashlog_target target;
  pid_t parent = getppid();

  diags = new Diags("traffic_crashlog", "" /* tags */, "" /* actions */, new BaseLogFile("stderr"));

  appVersionInfo.setup(PACKAGE_NAME, "traffic_crashlog", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Process command line arguments and dump into variables
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  // XXX This is a hack. traffic_manager starts traffic_server with the euid of the admin user. We are still
  // privileged, but won't be able to open files in /proc or ptrace the target. This really should be fixed
  // in traffic_manager.
  if (getuid() == 0) {
    change_privileges();
  }

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
  RecProcessInit(RECM_STAND_ALONE, nullptr /* diags */);
  LibRecordsConfigInit();

  if (syslog_mode) {
    RecString name;
    int facility = -1;

    if (REC_ReadConfigStringAlloc(name, "proxy.config.syslog_facility") == REC_ERR_OKAY) {
      facility = facility_string_to_int(name);
      ats_free(name);
    }

    if (facility < 0) {
      facility = LOG_DAEMON;
    }

    openlog(appVersionInfo.AppStr, LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility);
    diags->config.outputs[DL_Debug].to_syslog     = true;
    diags->config.outputs[DL_Status].to_syslog    = true;
    diags->config.outputs[DL_Note].to_syslog      = true;
    diags->config.outputs[DL_Warning].to_syslog   = true;
    diags->config.outputs[DL_Error].to_syslog     = true;
    diags->config.outputs[DL_Fatal].to_syslog     = true;
    diags->config.outputs[DL_Alert].to_syslog     = true;
    diags->config.outputs[DL_Emergency].to_syslog = true;
  }

  Note("crashlog started, target=%ld, debug=%s syslog=%s, uid=%ld euid=%ld", (long)target_pid, debug_mode ? "true" : "false",
       syslog_mode ? "true" : "false", (long)getuid(), (long)geteuid());

  mgmterr = TSInit(nullptr, (TSInitOptionT)(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS));
  if (mgmterr != TS_ERR_OKAY) {
    char *msg = TSGetErrorMessage(mgmterr);
    Warning("failed to intialize management API: %s", msg);
    TSfree(msg);
  }

  ink_zero(target);
  target.pid       = (pid_t)target_pid;
  target.timestamp = timestamp();

  if (host_triplet && strncmp(host_triplet, "x86_64-unknown-linux", sizeof("x86_64-unknown-linux") - 1) == 0) {
    ssize_t nbytes;
    target.flags |= CRASHLOG_HAVE_THREADINFO;

    nbytes = read(STDIN_FILENO, &target.siginfo, sizeof(target.siginfo));
    if (nbytes < (ssize_t)sizeof(target.siginfo)) {
      Warning("received %zd of %zu expected signal info bytes", nbytes, sizeof(target.siginfo));
      target.flags &= ~CRASHLOG_HAVE_THREADINFO;
    }

    nbytes = read(STDIN_FILENO, &target.ucontext, sizeof(target.ucontext));
    if (nbytes < (ssize_t)sizeof(target.ucontext)) {
      Warning("received %zd of %zu expected thread context bytes", nbytes, sizeof(target.ucontext));
      target.flags &= ~CRASHLOG_HAVE_THREADINFO;
    }
  }

  logname = crashlog_name();

  fp = debug_mode ? stdout : crashlog_open(logname);
  if (fp == nullptr) {
    Error("failed to create '%s': %s", logname, strerror(errno));
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
  crashlog_write_backtrace(fp, target);

  fprintf(fp, "\n");
  crashlog_write_procstatus(fp, target);

  fprintf(fp, "\n");
  crashlog_write_proclimits(fp, target);

  fprintf(fp, "\n");
  crashlog_write_regions(fp, target);

  fprintf(fp, "\n");
  crashlog_write_records(fp, target);

  Error("wrote crash log to %s", logname);

  fflush(fp);
  fclose(fp);
  return 0;
}
