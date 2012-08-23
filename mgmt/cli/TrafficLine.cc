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

#include "libts.h"

#include "ink_args.h"
#include "I_Version.h"
#include "Tokenizer.h"
#include "TextBuffer.h"
#include "mgmtapi.h"

static const char *programName;

static char ReadVar[1024];
static char SetVar[1024];
static char VarValue[1024];
static int ReRead;
static int Shutdown;
static int BounceCluster;
static int BounceLocal;
static int QueryDeadhosts;
static int Startup;
static int ShutdownMgmtCluster;
static int ShutdownMgmtLocal;
static int ClearCluster;
static int ClearNode;
static int VersionFlag;

static TSError
handleArgInvocation()
{
  if (ReRead == 1) {
    return TSReconfigure();
  } else if (ShutdownMgmtCluster == 1) {
    return TSRestart(true);
  } else if (ShutdownMgmtLocal == 1) {
    return TSRestart(false);
  } else if (Shutdown == 1) {
    return TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_OFF);
  } else if (BounceCluster == 1) {
    return TSBounce(true);
  } else if (BounceLocal == 1) {
    return TSBounce(false);
  } else if (Startup == 1) {
    return TSProxyStateSet(TS_PROXY_ON, TS_CACHE_CLEAR_OFF);
  } else if (ClearCluster == 1) {
    return TSStatsReset(true);
  } else if (ClearNode == 1) {
    return TSStatsReset(false);
  } else if (QueryDeadhosts == 1) {
    fprintf(stderr, "Query Deadhosts is not implemented, it requires support for congestion control.\n");
    fprintf(stderr, "For more details, examine the old code in cli/CLI.cc: QueryDeadhosts()\n");
    return TS_ERR_FAIL;
  } else if (*ReadVar != '\0') {        // Handle a value read
    if (*SetVar != '\0' || *VarValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read and set values at the same time\n", programName);
      return TS_ERR_FAIL;
    } else {
      TSError err;
      TSRecordEle *rec_ele = TSRecordEleCreate();

      if ((err = TSRecordGet(ReadVar, rec_ele)) != TS_ERR_OKAY) {
        fprintf(stderr, "%s: %s\n", programName, TSGetErrorMessage(err));
      } else {
        switch (rec_ele->rec_type) {
        case TS_REC_INT:
          printf("%" PRId64 "\n", rec_ele->int_val);
          break;
        case TS_REC_COUNTER:
          printf("%" PRId64 "\n", rec_ele->counter_val);
          break;
        case TS_REC_FLOAT:
          printf("%f\n", rec_ele->float_val);
          break;
        case TS_REC_STRING:
          printf("%s\n", rec_ele->string_val);
          break;
        default:
          fprintf(stderr, "%s: unknown record type (%d)\n", programName, rec_ele->rec_type);
          err = TS_ERR_FAIL;
          break;
        }
      }
      TSRecordEleDestroy(rec_ele);
      return err;
    }
  } else if (*SetVar != '\0') { // Setting a variable
    if (*VarValue == '\0') {
      fprintf(stderr, "%s: Set requires a -v argument\n", programName);
      return TS_ERR_FAIL;
    } else {
      TSError err;
      TSActionNeedT action;

      if ((err = TSRecordSet(SetVar, VarValue, &action)) != TS_ERR_OKAY)
        fprintf(stderr, "%s: Please correct your variable name and|or value\n", programName);
      return err;
    }
  } else if (*VarValue != '\0') {       // We have a value but no variable to set
    fprintf(stderr, "%s: Must specify variable to set with -s when using -v\n", programName);
    return TS_ERR_FAIL;
  }

  fprintf(stderr, "%s: No arguments specified\n", programName);
  return TS_ERR_FAIL;
}

int
main(int argc, char **argv)
{
  NOWARN_UNUSED(argc);
  AppVersionInfo appVersionInfo;
  TSError status;

  programName = argv[0];

  ReadVar[0] = '\0';
  SetVar[0] = '\0';
  VarValue[0] = '\0';
  ReRead = 0;
  Shutdown = 0;
  BounceCluster = 0;
  BounceLocal = 0;
  QueryDeadhosts = 0;
  Startup = 0;
  ShutdownMgmtCluster = 0;
  ShutdownMgmtLocal = 0;
  ClearCluster = 0;
  ClearNode = 0;
  VersionFlag = 0;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,"traffic_line", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

/* Argument description table used to describe how to parse command line args, */
/* see 'ink_args.h' for meanings of the various fields */
  ArgumentDescription argument_descriptions[] = {
    {"query_deadhosts", 'q', "Query congested sites", "F", &QueryDeadhosts, NULL, NULL},
    {"read_var", 'r', "Read Variable", "S1024", &ReadVar, NULL, NULL},
    {"set_var", 's', "Set Variable (requires -v option)", "S1024", &SetVar, NULL, NULL},
    {"value", 'v', "Set Value (used with -s option)", "S1024", &VarValue, NULL, NULL},
    {"help", 'h', "Help", NULL, NULL, NULL, usage},
    {"reread_config", 'x', "Reread Config Files", "F", &ReRead, NULL, NULL},
    {"restart_cluster", 'M', "Restart traffic_manager (cluster wide)", "F", &ShutdownMgmtCluster, NULL, NULL},
    {"restart_local", 'L', "Restart traffic_manager (local node)", "F", &ShutdownMgmtLocal, NULL, NULL},
    {"shutdown", 'S', "Shutdown traffic_server (local node)", "F", &Shutdown, NULL, NULL},
    {"startup", 'U', "Start traffic_server (local node)", "F", &Startup, NULL, NULL},
    {"bounce_cluster", 'B', "Bounce traffic_server (cluster wide)", "F", &BounceCluster, NULL, NULL},
    {"bounce_local", 'b', "Bounce local traffic_server", "F", &BounceLocal, NULL, NULL},
    {"clear_cluster", 'C', "Clear Statistics (cluster wide)", "F", &ClearCluster, NULL, NULL},
    {"clear_node", 'c', "Clear Statistics (local node)", "F", &ClearNode, NULL, NULL},
    {"version", 'V', "Print Version Id", "T", &VersionFlag, NULL, NULL},
  };

  // Process command line arguments and dump into variables
  process_args(argument_descriptions, SIZE(argument_descriptions), argv);

  // check for the version number request
  if (VersionFlag) {
    ink_fputln(stderr, appVersionInfo.FullVersionInfoStr);
    exit(0);
  }

  // Connect to Local Manager and do it.
  if (TS_ERR_OKAY != TSInit(NULL, static_cast<TSInitOptionT>(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS))) {
    fprintf(stderr, "error: could not connect to management port, make sure traffic_manager is running\n");
    exit(1);
  }
    
  status = handleArgInvocation();

  // Done with the mgmt API.
  TSTerminate();

  if (TS_ERR_OKAY != status) {
    if (ReadVar[0] == '\0' && SetVar[0] == '\0')
      fprintf(stderr, "error: the requested command failed\n");
    exit(1);
  }

  exit(0);
}
