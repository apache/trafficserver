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

#include "tscore/Version.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_lockfile.h"
#include "tscore/ink_sys_control.h"
#include "tscore/signals.h"
#include "proxy/shared/DiagsConfig.h"

#include "../../iocore/eventsystem/P_EventSystem.h"
#include "../../records/P_RecProcess.h"

// Needs LibRecordsConfigInit()
#include "records/RecordsConfig.h"
#include "iocore/utils/Machine.h"
#include "iocore/eventsystem/RecProcess.h"

#define LOG_FILENAME_SIZE 255

static char error_tags[1024]  = "";
static char action_tags[1024] = "";

static DiagsConfig *diagsConfig = nullptr;

/*-------------------------------------------------------------------------
  init_system
  -------------------------------------------------------------------------*/

// Handle fatal signals by logging and core dumping ...
static void
logging_crash_handler(int signo, siginfo_t *info, void *ptr)
{
  signal_format_siginfo(signo, info, AppVersionInfo::get_version().application());
  signal_crash_handler(signo, info, ptr);
}

static void
init_system(bool notify_syslog)
{
  ink_set_fds_limit(ink_max_out_rlimit(RLIMIT_NOFILE));

  signal_register_crash_handler(logging_crash_handler);
  if (notify_syslog) {
    auto &version = AppVersionInfo::get_version();
    syslog(LOG_NOTICE, "NOTE: --- %s Starting ---", version.application());
    syslog(LOG_NOTICE, "NOTE: %s Version: %s", version.application(), version.full_version());
  }
}

/*-------------------------------------------------------------------------
  initialize_records
  -------------------------------------------------------------------------*/

static void
initialize_records()
{
  // diags should have been initialized by caller, e.g.: sac.cc
  ink_assert(diags());

  RecProcessInit(diags());
  LibRecordsConfigInit();

  //
  // Define version info records
  //
  auto &version = AppVersionInfo::get_version();
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.short", version.version(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.long", version.full_version(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_number", version.build_number(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_time", version.build_time(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_date", version.build_date(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_machine", version.build_machine(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_person", version.build_person(), RECP_NON_PERSISTENT);
}

/*-------------------------------------------------------------------------
  check_lockfile
  -------------------------------------------------------------------------*/

static void
check_lockfile()
{
  int   err;
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
      fprintf(stderr, " (Lock file held by process ID %d)\n", holding_pid);
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
  initialize_records();
  diagsConfig = new DiagsConfig(pgm_name, logfile, error_tags, action_tags);
}

/*-------------------------------------------------------------------------
  init_log_standalone_basic

  This routine is similar to init_log_standalone, but it is intended for
  simple standalone applications that do not read the records.yaml file
  and that do not need a process manager, thus it:

  1) does not call initialize_records
  2) initializes the diags with use_records = false
  3) assumes multiple copies of the application can run, so does not
     do lock checking
  -------------------------------------------------------------------------*/

void
init_log_standalone_basic(const char *pgm_name)
{
  char logfile[LOG_FILENAME_SIZE];

  snprintf(logfile, sizeof(logfile), "%s.log", pgm_name);
  openlog(pgm_name, LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);

  // Makes the logging library happy. ToDo: This should be eliminated when
  // the cross dependencies aren't ugly.
  Machine::init("localhost", nullptr);

  init_system(false);
  const bool use_records = false;
  diagsConfig            = new DiagsConfig(pgm_name, logfile, error_tags, action_tags, use_records);
  // set stdin/stdout to be unbuffered
  //
  setbuf(stdin, nullptr);
  setbuf(stdout, nullptr);
}
