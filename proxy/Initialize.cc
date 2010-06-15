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

   Initialize.cc --
   Created On      : Fri Feb  5 18:22:05 1999

   Description:


 ****************************************************************************/
#include "inktomi++.h"

#include "Diags.h"
#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_Layout.h"
#include "I_Version.h"

#include "Initialize.h" // TODO: move to I_Initialize.h ???


#define set_rlimit(name,max_it,ulim_it) max_out_limit(#name, name, max_it, ulim_it)
static rlim_t
max_out_limit(const char *name, int which, bool max_it = true, bool unlim_it = true)
{
  struct rlimit rl;

#if defined(linux)
#  define MAGIC_CAST(x) (enum __rlimit_resource)(x)
#else
#  define MAGIC_CAST(x) x
#endif

  if (max_it) {
    ink_release_assert(getrlimit(MAGIC_CAST(which), &rl) >= 0);
    if (rl.rlim_cur != rl.rlim_max) {
#if defined(darwin)
      if (which == RLIMIT_NOFILE)
        rl.rlim_cur = fmin(OPEN_MAX, rl.rlim_max);
      else
        rl.rlim_cur = rl.rlim_max;
#else
      rl.rlim_cur = rl.rlim_max;
#endif
      ink_release_assert(setrlimit(MAGIC_CAST(which), &rl) >= 0);
    }
  }

  if (unlim_it) {
    ink_release_assert(getrlimit(MAGIC_CAST(which), &rl) >= 0);
    if (rl.rlim_cur != RLIM_INFINITY) {
      rl.rlim_cur = (rl.rlim_max = RLIM_INFINITY);
      ink_release_assert(setrlimit(MAGIC_CAST(which), &rl) >= 0);
    }
  }
  ink_release_assert(getrlimit(MAGIC_CAST(which), &rl) >= 0);
  //syslog(LOG_NOTICE, "NOTE: %s(%d):cur(%d),max(%d)", name, which, (int)rl.rlim_cur, (int)rl.rlim_max);
  return rl.rlim_cur;
}

//
// Initialize operating system related information/services
//
void
init_system_settings(void)
{
  // Delimit file Descriptors
  fds_limit = set_rlimit(RLIMIT_NOFILE, true, false);

  set_rlimit(RLIMIT_STACK,true,true);
  set_rlimit(RLIMIT_DATA,true,true);
  set_rlimit(RLIMIT_FSIZE, true, false);
#ifdef RLIMIT_RSS
  set_rlimit(RLIMIT_RSS,true,true);
#endif
}

#if 0
// XXX: This is unused function
//      Either remove it or implement Layout functions
void
init_system_dirs(void)
{
  struct stat s;
  int err;

  if ((err = stat(system_config_directory, &s)) < 0) {
    // Try 'system_root_dir/etc/trafficserver' directory
    snprintf(system_config_directory, sizeof(system_config_directory),
             "%s%s%s%s%s",system_root_dir, DIR_SEP,"etc",DIR_SEP,"trafficserver");
    if ((err = stat(system_config_directory, &s)) < 0) {
      fprintf(stderr,"unable to stat() config dir '%s': %d %d, %s\n",
              system_config_directory, err, errno, strerror(errno));
      fprintf(stderr, "please set config path via 'proxy.config.config_dir' \n");
      _exit(1);
    }
  }

  if ((err = stat(system_runtime_dir, &s)) < 0) {
    // Try 'system_root_dir/var/trafficserver' directory
    snprintf(system_runtime_dir, sizeof(system_runtime_dir),
             "%s%s%s%s%s",system_root_dir, DIR_SEP,"var",DIR_SEP,"trafficserver");
    if ((err = stat(system_runtime_dir, &s)) < 0) {
      fprintf(stderr,"unable to stat() local state dir '%s': %d %d, %s\n",
              system_runtime_dir, err, errno, strerror(errno));
      fprintf(stderr,"please set 'proxy.config.local_state_dir'\n");
      _exit(1);
    }
  }

  if ((err = stat(system_log_dir, &s)) < 0) {
    // Try 'system_root_dir/var/log/trafficserver' directory
    snprintf(system_log_dir, sizeof(system_log_dir), "%s%s%s%s%s%s%s",
             system_root_dir, DIR_SEP,"var",DIR_SEP,"log",DIR_SEP,"trafficserver");
    if ((err = stat(system_log_dir, &s)) < 0) {
      fprintf(stderr,"unable to stat() log dir'%s': %d %d, %s\n",
              system_log_dir, err, errno, strerror(errno));
      fprintf(stderr,"please set 'proxy.config.log2.logfile_dir'\n");
      _exit(1);
    }
  }

}
#endif

#if defined(linux)
#include <sys/prctl.h>
#endif

static int
set_core_size(const char *name, RecDataT data_type, RecData data, void *opaque_token)
{

  RecInt size = data.rec_int;
  struct rlimit lim;
  bool failed = false;

  NOWARN_UNUSED(opaque_token);

  if (getrlimit(RLIMIT_CORE, &lim) < 0) {
    failed = true;
  } else {
    if (size < 0) {
      lim.rlim_cur = lim.rlim_max;
    } else {
      lim.rlim_cur = (rlim_t) size;
    }
    if (setrlimit(RLIMIT_CORE, &lim) < 0) {
      failed = true;
    }
#if defined(linux)
#ifndef PR_SET_DUMPABLE
#define PR_SET_DUMPABLE 4
#endif
    if (size != 0)
      prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif
  }

  if (failed == true) {
    Warning("Failed to set Core Limit : %s", strerror(errno));
  }
  return 0;
}

void
init_system_core_size(void)
{
  bool found;
  RecInt coreSize;
  found = (RecGetRecordInt("proxy.config.core_limit", &coreSize) == REC_ERR_OKAY);
  if (found == false) {
    Warning("Unable to determine core limit");
  } else {
    RecData rec_temp;
    rec_temp.rec_int = coreSize;
    set_core_size(NULL, RECD_INT, rec_temp, NULL);
    found = (REC_RegisterConfigUpdateFunc("proxy.config.core_limit", set_core_size, NULL) == REC_ERR_OKAY);
    ink_assert(found);
  }
}

void
init_system_memalign_heap(void)
{
  int64 ram_cache_max = -1;
  int enable_preallocation = 1;

  REC_ReadConfigInteger(enable_preallocation, "proxy.config.system.memalign_heap");
  if (enable_preallocation) {
    REC_ReadConfigInteger(ram_cache_max, "proxy.config.cache.ram_cache.size");
    if (ram_cache_max > 0) {
      if (!ink_memalign_heap_init(ram_cache_max))
        Warning("Unable to init memalign heap");
    } else {
      Warning("Unable to read proxy.config.cache.ram_cache.size var from config");
    }
  }
}

int system_syslog_facility = LOG_DAEMON;

//   Reads the syslog configuration variable
//     and sets the global integer for the
//     facility and calls open log with the
//     new facility
void
init_system_syslog_log_configure(void)
{
  char *facility_str = NULL;
  int facility;

  REC_ReadConfigStringAlloc(facility_str, "proxy.config.syslog_facility");

  if (facility_str == NULL || (facility = facility_string_to_int(facility_str)) < 0) {
    syslog(LOG_WARNING, "Bad or missing syslog facility.  " "Defaulting to LOG_DAEMON");
  } else {
    system_syslog_facility = facility;
    closelog();
    openlog("traffic_server", LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility);
  }
}


/*
void
init_system_logging()
{
  //  iObject::Init();
  //  iLogBufferBuffer::Init();
}
*/

void
init_system_adjust_num_of_net_threads(void)
{
  float autoconfig_scale = 1.0;
  int nth_auto_config = 1;
  int num_of_threads_tmp = 1;

  REC_ReadConfigInteger(nth_auto_config, "proxy.config.exec_thread.autoconfig");
  if (!nth_auto_config) {
    REC_ReadConfigInteger(num_of_threads_tmp, "proxy.config.exec_thread.limit");
    if (num_of_threads_tmp <= 0)
      num_of_threads_tmp = 1;
    else if (num_of_threads_tmp > MAX_NUMBER_OF_THREADS)
      num_of_threads_tmp = MAX_NUMBER_OF_THREADS;
    system_num_of_net_threads = num_of_threads_tmp;
    if (is_debug_tag_set("threads")) {
      fprintf(stderr, "# net threads Auto config - disabled - use config file settings\n");
    }
  } else {                      /* autoconfig is enabled */
    num_of_threads_tmp = system_num_of_net_threads;
    REC_ReadConfigFloat(autoconfig_scale, "proxy.config.exec_thread.autoconfig.scale");
    num_of_threads_tmp = (int) ((float) num_of_threads_tmp * autoconfig_scale);
    if (num_of_threads_tmp) {
      system_num_of_net_threads = num_of_threads_tmp;
    }
    if (unlikely(num_of_threads_tmp > MAX_NUMBER_OF_THREADS)) {
      num_of_threads_tmp = MAX_NUMBER_OF_THREADS;
    }
    if (is_debug_tag_set("threads")) {
      fprintf(stderr, "# net threads Auto config - enabled\n");
      fprintf(stderr, "# autoconfig scale: %f\n", autoconfig_scale);
      fprintf(stderr, "# scaled number of net threads: %d\n", num_of_threads_tmp);
    }
  }

  if (is_debug_tag_set("threads")) {
    fprintf(stderr, "# number of net threads: %d\n", system_num_of_net_threads);
  }
  if (unlikely(system_num_of_net_threads <= 0)) {      /* impossible case -just for protection */
    Warning("Number of Net Threads should be greater than 0");
    system_num_of_net_threads = 1;
  }
}


//////////////////////////////////////////////////////////////////////////////
//
//      void reconfigure_diags()
//
//      This function extracts the current diags configuration settings from
//      records.config, and rebuilds the Diags data structures.
//
//////////////////////////////////////////////////////////////////////////////
void
init_system_reconfigure_diags(void)
{
  int i;
  DiagsConfigState c;

  // initial value set to 0 or 1 based on command line tags
  c.enabled[DiagsTagType_Debug] = (diags->base_debug_tags != NULL);
  c.enabled[DiagsTagType_Action] = (diags->base_action_tags != NULL);

  c.enabled[DiagsTagType_Debug] = 1;
  c.enabled[DiagsTagType_Action] = 1;
  diags->show_location = 1;

  // read output routing values
  for (i = 0; i < DiagsLevel_Count; i++) {

    c.outputs[i].to_stdout = 0;
    c.outputs[i].to_stderr = 1;
    c.outputs[i].to_syslog = 1;
    c.outputs[i].to_diagslog = 1;
  }

  //////////////////////////////
  // clear out old tag tables //
  //////////////////////////////
  diags->deactivate_all(DiagsTagType_Debug);
  diags->deactivate_all(DiagsTagType_Action);

  //////////////////////////////////////////////////////////////////////
  //                     add new tag tables
  //////////////////////////////////////////////////////////////////////
  if (diags->base_debug_tags)
    diags->activate_taglist(diags->base_debug_tags, DiagsTagType_Debug);
  if (diags->base_action_tags)
    diags->activate_taglist(diags->base_action_tags, DiagsTagType_Action);

  ////////////////////////////////////
  // change the diags config values //
  ////////////////////////////////////
#if !defined (_WIN32) && !defined(__GNUC__) && !defined(hpux)
  diags->config = c;
#else
  memcpy(((void *) &diags->config), ((void *) &c), sizeof(DiagsConfigState));
#endif

}


void
init_system_diags(char *bdt, char *bat)
{
  FILE *diags_log_fp = NULL;
  char diags_logpath[PATH_NAME_MAX + 1];

  ink_filepath_make(diags_logpath, sizeof(diags_logpath),
                    system_log_dir, DIAGS_LOG_FILE);

  diags_log_fp = fopen(diags_logpath, "w");
  if (diags_log_fp) {
    int status;
    status = setvbuf(diags_log_fp, NULL, _IOLBF, 512);
    if (status != 0) {
      fclose(diags_log_fp);
      diags_log_fp = NULL;
    }
  }

  diags = NEW(new Diags(bdt, bat, diags_log_fp));
  if (diags_log_fp == NULL) {
    SrcLoc loc(__FILE__, __FUNCTION__, __LINE__);

    diags->print(NULL, DL_Warning, NULL, &loc,
                 "couldn't open diags log file '%s', " "will not log to this file", diags_logpath);
  } else {
    diags->print(NULL, DL_Status, "STATUS", NULL, "opened %s", diags_logpath);
  }

  init_system_reconfigure_diags();
}


#if 0 // TODO: remove after api/dir re-org

#define DEFAULT_REMOTE_MANAGEMENT_FLAG    0
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "etc/trafficserver"
#define DEFAULT_MANAGEMENT_DIRECTORY      "etc/trafficserver"

char error_tags[1024] = "";
char action_tags[1024] = "";
int  diags_init = 0;

//int command_flag = 0;
extern int use_accept_thread;
int num_of_net_threads = ink_number_of_processors();
//int num_of_cache_threads = 1;

int remote_management_flag = DEFAULT_REMOTE_MANAGEMENT_FLAG;
char management_directory[256] = DEFAULT_MANAGEMENT_DIRECTORY;
char system_config_directory[PATH_NAME_MAX + 1] = DEFAULT_SYSTEM_CONFIG_DIRECTORY;

Diags *diags;
DiagsConfig *diagsConfig;

AppVersionInfo appVersionInfo;

static void initialize_process_manager();

void
initialize_standalone()
{
  init_system();

  // Define the version info
  appVersionInfo.setup(PACKAGE_NAME,"traffic_server", PACKAGE_VERSION, __DATE__,
                        __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  // Local process manager
  initialize_process_manager();

  diagsConfig = NEW(new DiagsConfig(error_tags, action_tags));
  diags = diagsConfig->diags;
  diags_init = 1;
  eventProcessor.start(num_of_net_threads);

  // Set up IO Buffers
  int config_max_iobuffer_size = DEFAULT_MAX_BUFFER_SIZE;
  ReadConfigInteger(config_max_iobuffer_size,
                    "proxy.config.io.max_buffer_size");
  max_iobuffer_size = iobuffer_size_to_index(config_max_iobuffer_size,
                                           DEFAULT_BUFFER_SIZES - 1);
  if (default_small_iobuffer_size > max_iobuffer_size)
    default_small_iobuffer_size = max_iobuffer_size;
  if (default_large_iobuffer_size > max_iobuffer_size)
    default_large_iobuffer_size = max_iobuffer_size;

  init_buffer_allocators();

  netProcessor.start();

  return;
}

//
// Startup process manager
//
void initialize_process_manager()
{
  ProcessRecords *precs;
  struct stat s;
  int err;

  mgmt_use_syslog();

  // Temporary Hack to Enable Communuication with LocalManager
  if (getenv("PROXY_REMOTE_MGMT")) {
    remote_management_flag = true;
  }
  //
  // Remove excess '/'
  //
  if (management_directory[strlen(management_directory) - 1] == '/')
    management_directory[strlen(management_directory) - 1] = 0;

  if ((err = stat(management_directory, &s)) < 0) {
    // Try 'system_root_dir/etc/trafficserver' directory
    snprintf(management_directory, sizeof(management_directory),
             "%s%s%s%s%s",system_root_dir, DIR_SEP,"etc",DIR_SEP,"trafficserver");
    if ((err = stat(management_directory, &s)) < 0) {
      fprintf(stderr,"unable to stat() management path '%s': %d %d, %s\n",
                management_directory, err, errno, strerror(errno));
      fprintf(stderr,"please set management path via command line '-d <managment directory>'\n");
      _exit(1);
    }
  }

  RecProcessInit(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE, diags);

  if (!remote_management_flag) {
    LibRecordsConfigInit();
  }
  //
  // Start up manager
  //
  precs = NEW(new ProcessRecords(management_directory, "records.config", 0));

  pmgmt = NEW(new ProcessManager(remote_management_flag, management_directory, precs));

  pmgmt->start();

  RecProcessInitMessage(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE);

  pmgmt->reconfigure();

  init_system_dirs();// setup directories

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
  //             "proxy.process.version.server.build_compile_flags",
  //                 appVersionInfo.BldCompileFlagsStr,
  //             RECP_NULL);
}

#endif


