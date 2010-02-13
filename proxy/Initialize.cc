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
#include "ink_port.h"
#include "Diags.h"
#include "DiagsConfig.h"
#include "Main.h"
#include "EThread.h"
#include "EventProcessor.h"
#include "Net.h"
#include "Config.h"
#include "ProcessManager.h"
#include "I_Version.h"
#include "ink_syslog.h"
#include "MgmtUtils.h"

#if (HOST_OS == freebsd)
#define DEFAULT_NUMBER_OF_THREADS         1
#else
#define DEFAULT_NUMBER_OF_THREADS         sysconf(_SC_NPROCESSORS_ONLN) // number of Net threads
#endif
#define DEFAULT_REMOTE_MANAGEMENT_FLAG    0
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "etc/trafficserver"
#define DEFAULT_MANAGEMENT_DIRECTORY      "etc/trafficserver"


char error_tags[1024] = "";
char action_tags[1024] = "";
int diags_init = 0;


int command_flag = 0;
int fds_limit = 0;
int http_accept_port_number = 0;
int remote_management_flag = DEFAULT_REMOTE_MANAGEMENT_FLAG;
char management_directory[256] = DEFAULT_MANAGEMENT_DIRECTORY;
char system_config_directory[PATH_NAME_MAX + 1] = DEFAULT_SYSTEM_CONFIG_DIRECTORY;
int use_accept_thread = 1;
int num_of_net_threads = DEFAULT_NUMBER_OF_THREADS;
int num_of_cache_threads = 1;
Diags *diags;
DiagsConfig *diagsConfig;

AppVersionInfo appVersionInfo;

static void initialize_process_manager();

static rlim_t
max_out_limit(int which, bool max_it)
{
  struct rlimit rl;

  ink_release_assert(getrlimit(which, &rl) >= 0);
  if (max_it && rl.rlim_cur != rl.rlim_max) {
    rl.rlim_cur = rl.rlim_max;
    ink_release_assert(setrlimit(which, &rl) >= 0);
  }

  ink_release_assert(getrlimit(which, &rl) >= 0);
  rlim_t ret = rl.rlim_cur;

  return ret;

}

//
// Initialize operating system related information/services
//
void
init_system()
{
  //
  // Delimit file Descriptors
  //

  fds_limit = max_out_limit(RLIMIT_NOFILE, true);

  max_out_limit(RLIMIT_DATA, true);
  max_out_limit(RLIMIT_FSIZE, true);
  max_out_limit(RLIMIT_RSS, true);
}


void
initialize_standalone()
{
  init_system();
  // Define the version info
  appVersionInfo.setup(PACKAGE_NAME,"traffic_server", PACKAGE_VERSION, __DATE__,
                        __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  // Local process manager
  initialize_process_manager();

  diagsConfig = NEW(new DiagsConfig(error_tags, action_tags); diags = diagsConfig->diags; diags_init = 1; eventProcessor.start(1,       //n_spawn_threads
                                                                                                                               1,       // n_call_threads
                                                                                                                               num_of_net_threads,      //n_net_threads
                                                                                                                               1,       // n_disk_threads
                                                                                                                               0,       //n_cluster_threads (unused)
                                                                                                                               0,       // No FTP threads
                                                                                                                               num_of_cache_threads     // n_cache_threads
                    );
                    // Set up IO Buffers
                    int config_max_iobuffer_size = DEFAULT_MAX_BUFFER_SIZE;
                    ReadConfigInteger(config_max_iobuffer_size,
                                      "proxy.config.io.max_buffer_size");
                    max_iobuffer_size = buffer_size_to_index(config_max_iobuffer_size,
                                                             DEFAULT_BUFFER_SIZES - 1);
                    if (default_small_iobuffer_size > max_iobuffer_size)
                    default_small_iobuffer_size = max_iobuffer_size;
                    if (default_large_iobuffer_size > max_iobuffer_size)
                    default_large_iobuffer_size = max_iobuffer_size;
                    init_buffer_allocators(); netProcessor.start(); return;}

//
// Startup process manager
//
                    void initialize_process_manager()
                    {
                    ProcessRecords * precs; mgmt_use_syslog();
                    // Temporary Hack to Enable Communuication with LocalManager
                    if (getenv("PROXY_REMOTE_MGMT")) {
                    remote_management_flag = true;}

                    //
                    // Remove excess '/'
                    //
                    if (management_directory[strlen(management_directory) - 1] == '/')
                    management_directory[strlen(management_directory) - 1] = 0;
                    //
                    // Start up manager
                    //
                    precs = NEW(new ProcessRecords(management_directory, "records.config", "lm.config"));
                    pmgmt = NEW(new ProcessManager(remote_management_flag, management_directory,
                                                   precs));
                    ReadConfigString(system_config_directory, "proxy.config.config_dir", PATH_NAME_MAX);
                    //
                    // Define version info records
                    //
                    precs->setString("proxy.process.version.server.short",
                                     appVersionInfo.VersionStr);
                    precs->setString("proxy.process.version.server.long",
                                     appVersionInfo.FullVersionInfoStr);
                    precs->setString("proxy.process.version.server.build_number",
                                     appVersionInfo.BldNumStr);
                    precs->setString("proxy.process.version.server.build_time",
                                     appVersionInfo.BldTimeStr);
                    precs->setString("proxy.process.version.server.build_date",
                                     appVersionInfo.BldDateStr);
                    precs->setString("proxy.process.version.server.build_machine",
                                     appVersionInfo.BldMachineStr);
                    precs->setString("proxy.process.version.server.build_person", appVersionInfo.BldPersonStr);
//  precs->setString("proxy.process.version.server.build_compile_flags",
//                   appVersionInfo.BldCompileFlagsStr);
                    }


                    void clear_http_handler_times()
                    {
                    return;}

                    void initialize_thread_for_icp(EThread * thread)
                    {
                    (void) thread; return;}

                    void initialize_thread_for_ftp(EThread * thread)
                    {
                    (void) thread; return;}

                    void initialize_thread_for_cluster(EThread * thread)
                    {
                    (void) thread;}

// void syslog_thr_init()
//
//   On the alpha, each thread must set its own syslog
//     parameters.  This function is to be called by
//     each thread at start up.  It inits syslog
//     with stored facility information from system
//     startup
//
                    void syslog_thr_init()
                    {
                    }
