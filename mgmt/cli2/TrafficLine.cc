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
#include "I_Layout.h"
#include "I_Version.h"
#include "Tokenizer.h"
#include "TextBuffer.h"
#include "INKMgmtAPI.h"

static const char *programName;

static char readVar[1024];
static char setVar[1024];
static char varValue[1024];
static int reRead;
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

static INKError
handleArgInvocation()
{
  if (reRead == 1) {
    return INKReconfigure();
  } else if (ShutdownMgmtCluster == 1) {
    return INKRestart(true);
  } else if (ShutdownMgmtLocal == 1) {
    return INKRestart(false);
  } else if (Shutdown == 1) {
    return INKProxyStateSet(INK_PROXY_OFF, INK_CACHE_CLEAR_OFF);
  } else if (BounceCluster == 1) {
  } else if (BounceLocal == 1) {
  } else if (Startup == 1) {
    return INKProxyStateSet(INK_PROXY_ON, INK_CACHE_CLEAR_OFF);
  } else if (ClearCluster == 1) {
  } else if (ClearNode == 1) {
  } else if (QueryDeadhosts == 1) {
  } else if (*readVar != '\0') {        // Handle a value read
    if (*setVar != '\0' || *varValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read and set values at the same time\n", programName);
      return INK_ERR_FAIL;
    } else {
    }
  } else if (*setVar != '\0') { // Setting a variable
    if (*varValue == '\0') {
      fprintf(stderr, "%s: Set requires a -v argument\n", programName);
      return INK_ERR_FAIL;
    } else {
    }
  } else if (*varValue != '\0') {       // We have a value but no variable to set
    fprintf(stderr, "%s: Must specify variable to set with -s when using -v\n", programName);
    return INK_ERR_FAIL;
  }

  fprintf(stderr, "%s: No arguments specified\n", programName);
  return INK_ERR_FAIL;
}

int
main(int argc, char **argv)
{
  NOWARN_UNUSED(argc);
  AppVersionInfo appVersionInfo;
  INKError status;

  programName = argv[0];

  readVar[0] = '\0';
  setVar[0] = '\0';
  varValue[0] = '\0';
  reRead = 0;
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
    {"read_var", 'r', "Read Variable", "S1024", &readVar, NULL, NULL},
    {"set_var", 's', "Set Variable (requires -v option)", "S1024", &setVar, NULL, NULL},
    {"value", 'v', "Set Value (used with -s option)", "S1024", &varValue, NULL, NULL},
    {"help", 'h', "Help", NULL, NULL, NULL, usage},
    {"reread_config", 'x', "Reread Config Files", "F", &reRead, NULL, NULL},
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

  // Connect to Local Manager
  Layout::create();
  INKInit(Layout::get()->runtimedir, static_cast<TSInitOptionT>(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS));

  // Do it
  status = handleArgInvocation();

  // Done with the mgmt API.
  INKTerminate();
  if (INK_ERR_OKAY != status) {
    fprintf(stderr, "error: the requested command failed\n");
    exit(1);
  }
  exit(0);
}
