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

#include "libts.h"
#include "ink_sys_control.h"
#include "signals.h"
#include "DiagsConfig.h"
#include "Main.h"

#include "Error.h"
#include "P_EventSystem.h"
#include "P_RecProcess.h"

#include "ProcessManager.h"
#include "MgmtUtils.h"
// Needs LibRecordsConfigInit()
#include "RecordsConfig.h"

#define LOG_ReadConfigString REC_ReadConfigString

#define HttpBodyFactory		int

// globals the rest of the system depends on
extern int fds_limit;
extern int cluster_port_number;

int diags_init = 0;
int command_flag = 0;
int http_accept_port_number = 0;
int http_accept_file_descriptor = 0;
int remote_management_flag = 0;
int auto_clear_hostdb_flag = 0;
char proxy_name[MAXDNAME + 1] = "unknown";

char system_root_dir[PATH_NAME_MAX + 1] = "";
char system_config_directory[PATH_NAME_MAX + 1] = "";
char system_runtime_dir[PATH_NAME_MAX + 1] = "";
char system_log_dir[PATH_NAME_MAX + 1] = "";
char management_directory[PATH_NAME_MAX + 1] = "";

char error_tags[1024] = "";
char action_tags[1024] = "";
char command_string[512] = "";


Diags *diags = NULL;
DiagsConfig *diagsConfig = NULL;
HttpBodyFactory *body_factory = NULL;
AppVersionInfo appVersionInfo;

/*-------------------------------------------------------------------------
  init_system
  -------------------------------------------------------------------------*/

void
init_system()
{
  fds_limit = ink_max_out_rlimit(RLIMIT_NOFILE, true, false);

  init_signals();
  syslog(LOG_NOTICE, "NOTE: --- SAC Starting ---");
  syslog(LOG_NOTICE, "NOTE: SAC Version: %s", appVersionInfo.FullVersionInfoStr);
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

  if (management_directory[0] == '\0') {
    ink_strlcpy(management_directory, Layout::get()->sysconfdir, sizeof(management_directory));
    if (access(management_directory, R_OK) == -1) {
      fprintf(stderr,"unable to access() management path '%s': %d, %s\n",
              management_directory, errno, strerror(errno));
      fprintf(stderr,"please set management path via command line '-d <managment directory>'\n");
      _exit(1);
    }
  }

  // diags should have been initialized by caller, e.g.: sac.cc
  ink_assert(diags);

  RecProcessInit(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE, diags);

  if (!remote_management_flag) {
    LibRecordsConfigInit();
  }
  //
  // Start up manager
  pmgmt = NEW(new ProcessManager(remote_management_flag, management_directory));

  pmgmt->start();

  RecProcessInitMessage(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE);

  pmgmt->reconfigure();

  LOG_ReadConfigString(system_config_directory, "proxy.config.config_dir", PATH_NAME_MAX);

  //
  // Define version info records
  //
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.short", appVersionInfo.VersionStr, RECP_NULL);
  RecRegisterStatString(RECT_PROCESS,
                        "proxy.process.version.server.long", appVersionInfo.FullVersionInfoStr, RECP_NULL);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_number", appVersionInfo.BldNumStr, RECP_NULL);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_time", appVersionInfo.BldTimeStr, RECP_NULL);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_date", appVersionInfo.BldDateStr, RECP_NULL);
  RecRegisterStatString(RECT_PROCESS,
                        "proxy.process.version.server.build_machine", appVersionInfo.BldMachineStr, RECP_NULL);
  RecRegisterStatString(RECT_PROCESS,
                        "proxy.process.version.server.build_person", appVersionInfo.BldPersonStr, RECP_NULL);
//    RecRegisterStatString(RECT_PROCESS,
//                         "proxy.process.version.server.build_compile_flags",
//                         appVersionInfo.BldCompileFlagsStr,
//                         RECP_NULL);
}

/*-------------------------------------------------------------------------
  shutdown_system
  -------------------------------------------------------------------------*/

void
shutdown_system()
{
}


/*-------------------------------------------------------------------------
  check_lockfile
  -------------------------------------------------------------------------*/

static void
check_lockfile(const char *config_dir, const char *pgm_name)
{
  NOWARN_UNUSED(config_dir);
  NOWARN_UNUSED(pgm_name);
  int err;
  pid_t holding_pid;
  char *lockfile = NULL;

  if (access(Layout::get()->runtimedir, R_OK | W_OK) == -1) {
    fprintf(stderr,"unable to access() dir'%s': %d, %s\n",
            Layout::get()->runtimedir, errno, strerror(errno));
    fprintf(stderr," please set correct path in env variable TS_ROOT \n");
    _exit(1);
  }
  lockfile = Layout::relative_to(Layout::get()->runtimedir, SERVER_LOCK);

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
    _exit(1);
  }
  ats_free(lockfile);
}

/*-------------------------------------------------------------------------
  syslog_thr_init

  For the DEC alpha, the syslog call must be made for each thread.
  -------------------------------------------------------------------------*/

void
syslog_thr_init()
{
}

/*-------------------------------------------------------------------------
  init_log_standalone

  This routine should be called from the main() function of the standalone
  program.
  -------------------------------------------------------------------------*/

void
init_log_standalone(const char *pgm_name, bool one_copy)
{
  // ensure that only one copy of the sac is running
  //
  if (one_copy) {
    check_lockfile(system_config_directory, pgm_name);
  }
  // set stdin/stdout to be unbuffered
  //
  setbuf(stdin, NULL);
  setbuf(stdout, NULL);

  openlog(pgm_name, LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);

  init_system();
  initialize_process_manager();
  diagsConfig = NEW(new DiagsConfig(error_tags, action_tags));
  diags = diagsConfig->diags;
  diags_init = 1;
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
  openlog(pgm_name, LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);

  init_system();
  const bool use_records = false;
  diagsConfig = NEW(new DiagsConfig(error_tags, action_tags, use_records));
  diags = diagsConfig->diags;
  // set stdin/stdout to be unbuffered
  //
  setbuf(stdin, NULL);
  setbuf(stdout, NULL);

  diags_init = 1;
}
