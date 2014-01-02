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

#include "ink_config.h"
#include <stdlib.h>
#include "tcl.h"
#include <string.h>
#include "ink_args.h"
#include "ink_file.h"
#include "ink_error.h"
#include "I_Layout.h"
#include "I_Version.h"
#include "CliMgmtUtils.h"
#include "mgmtapi.h"

extern int Tcl_AppInit(Tcl_Interp * interp);
extern void Tcl_ReadlineMain(void);
extern int CliDisplayPrintf;
void eventCallbackFn(char *name, char *msg, int pri, void *data);

// default is that alarms appear in CLI as they occur
int AlarmCallbackPrint = 1;

// registers an event callback for all events in general
void register_event_callback(void);

AppVersionInfo appVersionInfo;
int version_flag = 0;

int
main(int argc, char *argv[])
{
  TSError status;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,"traffic_shell", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();

  // Argument description table used to describe how to parse command line args,
  // see 'ink_args.h' for meanings of the various fields
  ArgumentDescription argument_descriptions[] = {
    {"version", 'V', "Print Version Id", "T", &version_flag, NULL, NULL}
  };

  // Process command line arguments and dump into variables
  process_args(argument_descriptions, countof(argument_descriptions), argv);

  // check for the version number request
  if (version_flag) {
    ink_fputln(stderr, appVersionInfo.FullVersionInfoStr);
    exit(0);
  }

  Tcl_FindExecutable(argv[0]);

  // traffic_shell binary should use printf to display information onscreen
  CliDisplayPrintf = 1;

  // initialize MgmtAPI using TS runtime directory
  status = TSInit(Layout::get()->runtimedir, TS_MGMT_OPT_DEFAULTS);
  if (status) {
    printf("TSInit %d: Failed to initialize MgmtAPI in %s\n", status, Layout::get()->runtimedir);
  } else {
    printf("Successfully Initialized MgmtAPI in %s \n", Layout::get()->runtimedir);
  }

  register_event_callback();

#if HAVE_LIBREADLINE
  Tcl_SetMainLoop(Tcl_ReadlineMain);
#endif

  Tcl_Main(argc, argv, Tcl_AppInit);
  exit(0);
}

void
eventCallbackFn(char *name, char * /* msg ATS_UNUSED */, int /* pri ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (AlarmCallbackPrint == 1) {
    printf("\n**********\n" "ALARM SIGNALLED: %s\n" "**********\n", name);
  }

  return;
}

// registers an event callback for all events in general
void
register_event_callback(void)
{
  // TODO: Check return code?
  TSEventSignalCbRegister(NULL, eventCallbackFn, NULL);
}
