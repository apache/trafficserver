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

/***************************************************************************
 LogStandalone.cc


 ***************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_lockfile.h"
#include "ts/ink_sys_control.h"
#include "ts/signals.h"
#include "DiagsConfig.h"

#include "P_EventSystem.h"
#include "P_RecProcess.h"

#include "ProcessManager.h"
#include "MgmtUtils.h"
// Needs LibRecordsConfigInit()
#include "RecordsConfig.h"

#define LOG_FILENAME_SIZE 255

class HttpBodyFactory;

// globals the rest of the system depends on
extern int fds_limit;
extern int cluster_port_number;

int command_flag                = 0;
int http_accept_port_number     = 0;
int http_accept_file_descriptor = 0;
int remote_management_flag      = 0;
int auto_clear_hostdb_flag      = 0;
char proxy_name[MAXDNAME + 1]   = "unknown";

char error_tags[1024]    = "";
char action_tags[1024]   = "";
char command_string[512] = "";

// Diags *diags = NULL;
DiagsConfig *diagsConfig      = nullptr;
HttpBodyFactory *body_factory = nullptr;
AppVersionInfo appVersionInfo;

/*-------------------------------------------------------------------------
  init_system
  -------------------------------------------------------------------------*/

// Handle fatal signals by logging and core dumping ...
static void
logging_crash_handler(int signo, siginfo_t *info, void *ptr)
{
  signal_format_siginfo(signo, info, appVersionInfo.AppStr);
  signal_crash_handler(signo, info, ptr);
}

static void
init_system(bool notify_syslog)
{
  fds_limit = ink_max_out_rlimit(RLIMIT_NOFILE);

  signal_register_crash_handler(logging_crash_handler);
  if (notify_syslog) {
    syslog(LOG_NOTICE, "NOTE: --- %s Starting ---", appVersionInfo.AppStr);
    syslog(LOG_NOTICE, "NOTE: %s Version: %s", appVersionInfo.AppStr, appVersionInfo.FullVersionInfoStr);
  }
}

/*-------------------------------------------------------------------------
  initialize_process_manager
  -------------------------------------------------------------------------*/

static void
initialize_process_manager()
{
  mgmt_use_syslog();

  // Temporary Hack to Enable Communuication with LocalManager
  if (getenv("PROXY_REMOTE_MGMT")) {
    remote_management_flag = true;
  }

  // diags should have been initialized by caller, e.g.: sac.cc
  ink_assert(diags);

  RecProcessInit(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE, diags);
  LibRecordsConfigInit();

  // Start up manager
  pmgmt = new ProcessManager(remote_management_flag);

  pmgmt->start();

  RecProcessInitMessage(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE);

  pmgmt->reconfigure();

  //
  // Define version info records
  //
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.short", appVersionInfo.VersionStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.long", appVersionInfo.FullVersionInfoStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_number", appVersionInfo.BldNumStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_time", appVersionInfo.BldTimeStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_date", appVersionInfo.BldDateStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_machine", appVersionInfo.BldMachineStr,
                        RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_person", appVersionInfo.BldPersonStr,
                        RECP_NON_PERSISTENT);
  //    RecRegisterStatString(RECT_PROCESS,
  //                         "proxy.process.version.server.build_compile_flags",
  //                         appVersionInfo.BldCompileFlagsStr,
  //                         RECP_NON_PERSISTENT);
}

/*-------------------------------------------------------------------------
  check_lockfile
  -------------------------------------------------------------------------*/

static void
check_lockfile()
{
  int err;
  pid_t holding_pid;
  char *lockfile = nullptr;

  if (access(Layout::get()->runtimedir.c_str(), R_OK | W_OK) == -1) {
    fprintf(stderr, "unable to access() dir'%s': %d, %s\n", Layout::get()->runtimedir.c_str(), errno, strerror(errno));
    fprintf(stderr, " please set correct path in env variable TS_ROOT \n");
    ::exit(1);
  }
  lockfile = ats_stringdup(Layout::relative_to(Layout::get()->runtimedir, SERVER_LOCK));

  Lockfile server_lockfile(lockfile);
  err = server_lockfile.Get(&holding_pid);

  if (err != 1) {
    char *reason = strerror(-err);
    fprintf(stderr, "FATAL: Can't acquire lockfile '%s'", lockfile);

    if ((err == 0) && (holding_pid != -1)) {
#if defined(solaris)
      fprintf(stderr, " (Lock file held by process ID %d)\n", (int)holding_pid);
#else
      fprintf(stderr, " (Lock file held by process ID %d)\n", holding_pid);
#endif
    } else if ((err == 0) && (holding_pid == -1)) {
      fprintf(stderr, " (Lock file exists, but can't read process ID)\n");
    } else if (reason) {
      fprintf(stderr, " (%s)\n", reason);
    } else {
      fprintf(stderr, "\n");
    }
    ::exit(1);
  }
  ats_free(lockfile);
}

/*-------------------------------------------------------------------------
  init_log_standalone

  This routine should be called from the main() function of the standalone
  program.
  -------------------------------------------------------------------------*/

void
init_log_standalone(const char *pgm_name, bool one_copy)
{
  char logfile[LOG_FILENAME_SIZE];

  // ensure that only one copy of the sac is running
  //
  if (one_copy) {
    check_lockfile();
  }

  snprintf(logfile, sizeof(logfile), "%s.log", pgm_name);

  // set stdin/stdout to be unbuffered
  //
  setbuf(stdin, nullptr);
  setbuf(stdout, nullptr);

  openlog(pgm_name, LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);

  init_system(true);
  initialize_process_manager();
  diagsConfig = new DiagsConfig(pgm_name, logfile, error_tags, action_tags);
  diags       = diagsConfig->diags;
}

/*-------------------------------------------------------------------------
  init_log_standalone_basic

  This routine is similar to init_log_standalone, but it is intended for
  simple standalone applications that do not read the records.config file
  and that do not need a process manager, thus it:

  1) does not call initialize_process_manager
  2) initializes the diags with use_records = false
  3) does not call Machine::init()
  4) assumes multiple copies of the application can run, so does not
     do lock checking
  -------------------------------------------------------------------------*/

void
init_log_standalone_basic(const char *pgm_name)
{
  char logfile[LOG_FILENAME_SIZE];

  snprintf(logfile, sizeof(logfile), "%s.log", pgm_name);
  openlog(pgm_name, LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);

  init_system(false);
  const bool use_records = false;
  diagsConfig            = new DiagsConfig(pgm_name, logfile, error_tags, action_tags, use_records);
  diags                  = diagsConfig->diags;
  // set stdin/stdout to be unbuffered
  //
  setbuf(stdin, nullptr);
  setbuf(stdout, nullptr);
}
