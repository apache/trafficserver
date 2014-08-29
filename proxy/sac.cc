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

#define DIAGS_LOG_FILENAME "collector.log"

// sac-specific command-line flags
//
static int version_flag = 0;

// command-line argument descriptions
//

ArgumentDescription argument_descriptions[] = {

  {"version", 'V', "Print Version Id", "T", &version_flag, NULL, NULL},
#ifdef DEBUG
  {"error_tags", 'T', "Colon-Separated Debug Tags", "S1023", &error_tags,
   NULL, NULL},
  {"action_tags", 'A', "Colon-Separated Debug Tags", "S1023", &action_tags,
   NULL, NULL},
#endif
  {"help", 'h', "HELP!", NULL, NULL, NULL, usage},
};

/*-------------------------------------------------------------------------
  main
  -------------------------------------------------------------------------*/

int
main(int /* argc ATS_UNUSED */, char *argv[])
{
  // build the application information structure
  //
  appVersionInfo.setup(PACKAGE_NAME,PROGRAM_NAME, PACKAGE_VERSION, __DATE__,
                       __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();
  // take care of command-line arguments
  //
  process_args(argument_descriptions, countof(argument_descriptions), argv);

  // check for the version number request
  //
  if (version_flag) {
    fprintf(stderr, "%s\n", appVersionInfo.FullVersionInfoStr);
    _exit(0);
  }

  diagsConfig = new DiagsConfig(DIAGS_LOG_FILENAME, error_tags, action_tags, false);
  diags = diagsConfig->diags;
  diags->prefix_str = "Collector ";

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
  size_t stacksize;

  REC_ReadConfigInteger(stacksize, "proxy.config.thread.default.stacksize");
  eventProcessor.start(ink_number_of_processors(), stacksize);
  ink_net_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  netProcessor.start(0, stacksize);
  Machine::init();

  Log::init(Log::NO_REMOTE_MANAGEMENT | Log::STANDALONE_COLLATOR);

  // we're off to the races ...
  //
  Note("-- SAC running --");
  this_thread()->execute();
}
