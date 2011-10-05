/** @file

  Standalone Collator

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

#include "ink_config.h"
#include "ink_file.h"
#include "ink_unused.h"
#include "I_Layout.h"
#include "I_Version.h"
#include "P_Net.h"

#define PROGRAM_NAME  "traffic_sac"

#include "LogStandalone.cc"

#include "LogAccess.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogFile.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogBuffer.h"
#include "LogUtils.h"
#include "LogSock.h"
#include "Log.h"
#include "DiagsConfig.h"
#include "I_Machine.h"

extern int CacheClusteringEnabled;
int auto_clear_cache_flag = 0;

// sac-specific command-line flags
//
int version_flag = 0;

// command-line argument descriptions
//
static char configDirectoryType[8] = "S1024";

ArgumentDescription argument_descriptions[] = {

  {"version", 'V', "Print Version Id", "T", &version_flag, NULL, NULL},
  {"config_dir", 'c', "Config Directory", configDirectoryType,
   &system_config_directory, NULL, NULL},
#ifdef DEBUG
  {"error_tags", 'T', "Colon-Separated Debug Tags", "S1023", &error_tags,
   NULL, NULL},
  {"action_tags", 'A', "Colon-Separated Debug Tags", "S1023", &action_tags,
   NULL, NULL},
#endif
  {"help", 'h', "HELP!", NULL, NULL, NULL, usage},
};
int n_argument_descriptions = SIZE(argument_descriptions);


/*-------------------------------------------------------------------------
  main
  -------------------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
  // build the application information structure
  //
  appVersionInfo.setup(PACKAGE_NAME,PROGRAM_NAME, PACKAGE_VERSION, __DATE__,
                       __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();
  // take care of command-line arguments
  //
  snprintf(configDirectoryType, sizeof(configDirectoryType), "S%d", PATH_NAME_MAX - 1);
  process_args(argument_descriptions, n_argument_descriptions, argv);

  // Get log directory
  ink_strlcpy(system_log_dir, Layout::get()->logdir, sizeof(system_log_dir));
  if (access(system_log_dir, R_OK) == -1) {
    fprintf(stderr, "unable to change to log directory \"%s\" [%d '%s']\n", system_log_dir, errno, strerror(errno));
    fprintf(stderr, " please set correct path in env variable TS_ROOT \n");
    exit(1);
  }

  management_directory[0] = 0;
  ink_strlcat(management_directory, system_config_directory, sizeof(management_directory));

  // check for the version number request
  //
  if (version_flag) {
    fprintf(stderr, "%s\n", appVersionInfo.FullVersionInfoStr);
    _exit(0);
  }

  diagsConfig = NEW(new DiagsConfig(error_tags, action_tags, false));
  diags = diagsConfig->diags;

  // initialize this application for standalone logging operation
  //
  bool one_copy = true;
  init_log_standalone(PROGRAM_NAME, one_copy);

  // set up IO Buffers
  //
  int config_max_iobuffer_size = DEFAULT_MAX_BUFFER_SIZE;
  max_iobuffer_size = buffer_size_to_index(config_max_iobuffer_size, DEFAULT_BUFFER_SIZES - 1);
  if (default_small_iobuffer_size > max_iobuffer_size)
    default_small_iobuffer_size = max_iobuffer_size;
  if (default_large_iobuffer_size > max_iobuffer_size)
    default_large_iobuffer_size = max_iobuffer_size;
  init_buffer_allocators();

  // initialize the event and net processor
  //
  eventProcessor.start(ink_number_of_processors());
  ink_net_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  netProcessor.start();
  Machine::init();

  Log::init(Log::NO_REMOTE_MANAGEMENT | Log::STANDALONE_COLLATOR);

  // we're off to the races ...
  //
  Note("-- SAC running --");
  this_thread()->execute();
}
