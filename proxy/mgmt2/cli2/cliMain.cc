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

#include <stdlib.h>
//#include "tclExtend.h"
#include "tcl.h"
#include <string.h>
#include <CliMgmtUtils.h>
#include "../api2/include/INKMgmtAPI.h"

extern int Tcl_AppInit(Tcl_Interp * interp);
extern int CliDisplayPrintf;
void eventCallbackFn(char *name, char *msg, int pri, void *data);

// default is that alarms appear in CLI as they occur
int AlarmCallbackPrint = 1;

// registers an event callback for all events in general
void register_event_callback(void);

int
main(int argc, char *argv[])
{
  char ts_path[512];
  char config_path[512];
  INKError status;

  // traffic_shell binary should use printf to display information onscreen
  CliDisplayPrintf = 1;

  // initialize MgmtAPI using TS directory specified in DEFAULT_TS_DIRECTORY_FILE
  // or DEFAULT_LOCAL_STATE_DIRECTORY if DEFAULT_TS_DIRECTORY_FILE does not exist

  if (GetTSDirectory(ts_path, sizeof(ts_path))) {
    status = INKInit(DEFAULT_LOCAL_STATE_DIRECTORY);
    if (status) {
      printf("INKInit %d: Failed to initialize MgmtAPI in %s\n", status, DEFAULT_LOCAL_STATE_DIRECTORY);
    } else {
      printf("Successfully Initialized MgmtAPI in %s \n", DEFAULT_LOCAL_STATE_DIRECTORY);
    }
  } else {
    snprintf(config_path, sizeof(config_path), "%s/var/trafficserver/", ts_path);
    // initialize MgmtAPI
    INKError status = INKInit(config_path);
    if (status) {
      printf("INKInit %d: Failed to initialize MgmtAPI in %s\n", status, config_path);
    }
  }

  register_event_callback();

  Tcl_Main(argc, argv, Tcl_AppInit);
  exit(0);
}

void
eventCallbackFn(char *name, char *msg, int pri, void *data)
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
  INKError err;

//  printf("\n[register_event_callback] \n");
  err = INKEventSignalCbRegister(NULL, eventCallbackFn, NULL);
}
