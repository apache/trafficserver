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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_args.h"
#include "ts/I_Version.h"
#include "ts/Tokenizer.h"
#include "ts/TextBuffer.h"
#include "mgmtapi.h"
#include <stdio.h>
#include <string.h>

static char ReadVar[1024];
static char MatchVar[1024];
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
static char ZeroCluster[1024];
static char ZeroNode[1024];
static char StorageCmdOffline[1024];
static int ShowAlarms;
static int ShowStatus;
static int ShowBacktrace;
static int DrainTraffic;
static char ClearAlarms[1024];

static TSMgmtError
handleArgInvocation()
{
  unsigned restart = DrainTraffic ? TS_RESTART_OPT_DRAIN : TS_RESTART_OPT_NONE;

  if (ReRead == 1) {
    return TSReconfigure();
  } else if (ShutdownMgmtCluster == 1) {
    return TSRestart(restart | TS_RESTART_OPT_CLUSTER);
  } else if (ShutdownMgmtLocal == 1) {
    return TSRestart(restart);
  } else if (Shutdown == 1) {
    return TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_OFF);
  } else if (BounceCluster == 1) {
    return TSBounce(restart | TS_RESTART_OPT_CLUSTER);
  } else if (BounceLocal == 1) {
    return TSBounce(restart);
  } else if (Startup == 1) {
    return TSProxyStateSet(TS_PROXY_ON, TS_CACHE_CLEAR_OFF);
  } else if (ClearCluster == 1) {
    return TSStatsReset(true, NULL);
  } else if (ClearNode == 1) {
    return TSStatsReset(false, NULL);
  } else if (*ZeroNode != '\0' || *ZeroCluster != '\0') {
    TSMgmtError err;
    TSRecordEle *rec_ele = TSRecordEleCreate();
    char *name           = *ZeroNode ? ZeroNode : ZeroCluster;

    if ((err = TSRecordGet(name, rec_ele)) != TS_ERR_OKAY) {
      fprintf(stderr, "%s: %s\n", program_name, TSGetErrorMessage(err));
      TSRecordEleDestroy(rec_ele);
      return err;
    }
    TSRecordEleDestroy(rec_ele);
    return TSStatsReset(*ZeroCluster ? true : false, name);
  } else if (QueryDeadhosts == 1) {
    fprintf(stderr, "Query Deadhosts is not implemented, it requires support for congestion control.\n");
    fprintf(stderr, "For more details, examine the old code in cli/CLI.cc: QueryDeadhosts()\n");
    return TS_ERR_FAIL;
  } else if (*StorageCmdOffline) {
    return TSStorageDeviceCmdOffline(StorageCmdOffline);
  } else if (ShowAlarms == 1) {
    // Show all active alarms, this was moved from the old traffic_shell implementation (show:alarms).
    TSList events = TSListCreate();

    if (TS_ERR_OKAY != TSActiveEventGetMlt(events)) {
      TSListDestroy(events);
      fprintf(stderr, "Error Retrieving Alarm List\n");
      return TS_ERR_FAIL;
    }

    int count = TSListLen(events);

    if (count > 0) {
      printf("Active Alarms\n");
      for (int i = 0; i < count; i++) {
        char *name = static_cast<char *>(TSListDequeue(events));
        printf("  %d. %s\n", i + 1, name);
      }
    } else {
      printf("\nNo active alarms.\n");
    }
    TSListDestroy(events);
    return TS_ERR_OKAY;
  } else if (*ClearAlarms != '\0') {
    // Clear (some) active alarms, this was moved from the old traffic_shell implementation (config:alarm)
    TSList events = TSListCreate();
    size_t len    = strlen(ClearAlarms);

    if (TS_ERR_OKAY != TSActiveEventGetMlt(events)) {
      TSListDestroy(events);
      fprintf(stderr, "Error Retrieving Alarm List\n");
      return TS_ERR_FAIL;
    }

    int count = TSListLen(events);

    if (count == 0) {
      printf("No Alarms to resolve\n");
      TSListDestroy(events);
      return TS_ERR_OKAY;
    }

    int errors = 0;
    bool all   = false;
    int num    = -1;

    if ((3 == len) && (0 == strncasecmp(ClearAlarms, "all", len))) {
      all = true;
    } else {
      num = strtol(ClearAlarms, NULL, 10) - 1;
      if (num <= 0)
        num = -1;
    }

    for (int i = 0; i < count; i++) {
      char *name = static_cast<char *>(TSListDequeue(events));

      if (all || ((num > -1) && (num == i)) || ((strlen(name) == len) && (0 == strncasecmp(ClearAlarms, name, len)))) {
        if (TS_ERR_OKAY != TSEventResolve(name)) {
          fprintf(stderr, "Errur: Unable to resolve alarm %s\n", name);
          ++errors;
        }
        if (num > 0) // If a specific event number was specified, we can stop now
          break;
      }
    }
    TSListDestroy(events);
    return (errors > 0 ? TS_ERR_FAIL : TS_ERR_OKAY);
  } else if (ShowStatus == 1) {
    switch (TSProxyStateGet()) {
    case TS_PROXY_ON:
      printf("Proxy -- on\n");
      break;
    case TS_PROXY_OFF:
      printf("Proxy -- off\n");
      break;
    case TS_PROXY_UNDEFINED:
      printf("Proxy status undefined\n");
      break;
    }
    return TS_ERR_OKAY;
  } else if (ShowBacktrace == 1) {
    TSString trace = NULL;
    TSMgmtError err;

    err = TSProxyBacktraceGet(0, &trace);
    if (err == TS_ERR_OKAY) {
      printf("%s\n", trace);
      TSfree(trace);
    }

    return err;
  } else if (*ReadVar != '\0') { // Handle a value read
    if (*SetVar != '\0' || *VarValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read and set values at the same time\n", program_name);
      return TS_ERR_FAIL;
    } else {
      TSMgmtError err;
      TSRecordEle *rec_ele = TSRecordEleCreate();

      if ((err = TSRecordGet(ReadVar, rec_ele)) != TS_ERR_OKAY) {
        fprintf(stderr, "%s: %s\n", program_name, TSGetErrorMessage(err));
      } else {
        switch (rec_ele->rec_type) {
        case TS_REC_INT:
          printf("%" PRId64 "\n", rec_ele->valueT.int_val);
          break;
        case TS_REC_COUNTER:
          printf("%" PRId64 "\n", rec_ele->valueT.counter_val);
          break;
        case TS_REC_FLOAT:
          printf("%f\n", rec_ele->valueT.float_val);
          break;
        case TS_REC_STRING:
          printf("%s\n", rec_ele->valueT.string_val);
          break;
        default:
          fprintf(stderr, "%s: unknown record type (%d)\n", program_name, rec_ele->rec_type);
          err = TS_ERR_FAIL;
          break;
        }
      }
      TSRecordEleDestroy(rec_ele);
      return err;
    }
  } else if (*MatchVar != '\0') { // Handle a value read
    if (*SetVar != '\0' || *VarValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read and set values at the same time\n", program_name);
      return TS_ERR_FAIL;
    } else {
      TSMgmtError err;
      TSList list = TSListCreate();

      if ((err = TSRecordGetMatchMlt(MatchVar, list)) != TS_ERR_OKAY) {
        char *msg = TSGetErrorMessage(err);
        fprintf(stderr, "%s: %s\n", program_name, msg);
        ats_free(msg);
      }

      // If the RPC call failed, the list will be empty, so we won't print anything. Otherwise,
      // print all the results, freeing them as we go.
      for (TSRecordEle *rec_ele = (TSRecordEle *)TSListDequeue(list); rec_ele; rec_ele = (TSRecordEle *)TSListDequeue(list)) {
        switch (rec_ele->rec_type) {
        case TS_REC_INT:
          printf("%s %" PRId64 "\n", rec_ele->rec_name, rec_ele->valueT.int_val);
          break;
        case TS_REC_COUNTER:
          printf("%s %" PRId64 "\n", rec_ele->rec_name, rec_ele->valueT.counter_val);
          break;
        case TS_REC_FLOAT:
          printf("%s %f\n", rec_ele->rec_name, rec_ele->valueT.float_val);
          break;
        case TS_REC_STRING:
          printf("%s %s\n", rec_ele->rec_name, rec_ele->valueT.string_val);
          break;
        default:
          // just skip it ...
          break;
        }

        TSRecordEleDestroy(rec_ele);
      }

      TSListDestroy(list);
      return err;
    }
  } else if (*SetVar != '\0') { // Setting a variable
    if (*VarValue == '\0') {
      fprintf(stderr, "%s: Set requires a -v argument\n", program_name);
      return TS_ERR_FAIL;
    } else {
      TSMgmtError err;
      TSActionNeedT action;

      if ((err = TSRecordSet(SetVar, VarValue, &action)) != TS_ERR_OKAY) {
        fprintf(stderr, "%s: Please correct your variable name and|or value\n", program_name);
        return err;
      }

      switch (action) {
      case TS_ACTION_SHUTDOWN:
        printf("Set %s, full shutdown required\n", SetVar);
        break;
      case TS_ACTION_RESTART:
        printf("Set %s, restart required\n", SetVar);
        break;
      case TS_ACTION_RECONFIGURE:
        printf("Set %s, please wait 10 seconds for traffic server to sync configuration, restart is not required\n", SetVar);
        break;
      case TS_ACTION_DYNAMIC:
      default:
        printf("Set %s\n", SetVar);
        break;
      }

      return err;
    }
  } else if (*VarValue != '\0') { // We have a value but no variable to set
    fprintf(stderr, "%s: Must specify variable to set with -s when using -v\n", program_name);
    return TS_ERR_FAIL;
  }

  fprintf(stderr, "%s: No arguments specified\n", program_name);
  return TS_ERR_FAIL;
}

int
main(int /* argc ATS_UNUSED */, const char **argv)
{
  AppVersionInfo appVersionInfo;
  TSMgmtError status;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME, "traffic_line [DEPRECATED]", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON,
                       "");

  program_name = appVersionInfo.AppStr;

  ReadVar[0]          = '\0';
  MatchVar[0]         = '\0';
  SetVar[0]           = '\0';
  VarValue[0]         = '\0';
  ReRead              = 0;
  Shutdown            = 0;
  BounceCluster       = 0;
  BounceLocal         = 0;
  QueryDeadhosts      = 0;
  Startup             = 0;
  ShutdownMgmtCluster = 0;
  ShutdownMgmtLocal   = 0;
  ClearCluster        = 0;
  ClearNode           = 0;
  ZeroCluster[0]      = '\0';
  ZeroNode[0]         = '\0';
  *StorageCmdOffline  = 0;
  ShowAlarms          = 0;
  ShowStatus          = 0;
  ClearAlarms[0]      = '\0';

  /* Argument description table used to describe how to parse command line args, */
  /* see 'ink_args.h' for meanings of the various fields */
  ArgumentDescription argument_descriptions[] = {
    {"query_deadhosts", 'q', "Query congested sites", "F", &QueryDeadhosts, NULL, NULL},
    {"read_var", 'r', "Read Variable", "S1024", &ReadVar, NULL, NULL},
    {"match_var", 'm', "Match Variable", "S1024", &MatchVar, NULL, NULL},
    {"set_var", 's', "Set Variable (requires -v option)", "S1024", &SetVar, NULL, NULL},
    {"value", 'v', "Set Value (used with -s option)", "S1024", &VarValue, NULL, NULL},
    {"reread_config", 'x', "Reread Config Files", "F", &ReRead, NULL, NULL},
    {"restart_cluster", 'M', "Restart traffic_manager (cluster wide)", "F", &ShutdownMgmtCluster, NULL, NULL},
    {"restart_local", 'L', "Restart traffic_manager (local node)", "F", &ShutdownMgmtLocal, NULL, NULL},
    {"shutdown", 'S', "Shutdown traffic_server (local node)", "F", &Shutdown, NULL, NULL},
    {"startup", 'U', "Start traffic_server (local node)", "F", &Startup, NULL, NULL},
    {"bounce_cluster", 'B', "Bounce traffic_server (cluster wide)", "F", &BounceCluster, NULL, NULL},
    {"bounce_local", 'b', "Bounce local traffic_server", "F", &BounceLocal, NULL, NULL},
    {"clear_cluster", 'C', "Clear Statistics (cluster wide)", "F", &ClearCluster, NULL, NULL},
    {"clear_node", 'c', "Clear Statistics (local node)", "F", &ClearNode, NULL, NULL},
    {"zero_cluster", 'Z', "Zero Specific Statistic (cluster wide)", "S1024", &ZeroCluster, NULL, NULL},
    {"zero_node", 'z', "Zero Specific Statistic (local node)", "S1024", &ZeroNode, NULL, NULL},
    {"offline", '-', "Mark cache storage offline", "S1024", &StorageCmdOffline, NULL, NULL},
    {"alarms", '-', "Show all alarms", "F", &ShowAlarms, NULL, NULL},
    {"clear_alarms", '-', "Clear specified, or all,  alarms", "S1024", &ClearAlarms, NULL, NULL},
    {"status", '-', "Show proxy server status", "F", &ShowStatus, NULL, NULL},
    {"backtrace", '-', "Show proxy stack backtrace", "F", &ShowBacktrace, NULL, NULL},
    {"drain", '-', "Wait for client connections to drain before restarting", "F", &DrainTraffic, NULL, NULL},
    HELP_ARGUMENT_DESCRIPTION(),
    VERSION_ARGUMENT_DESCRIPTION()};

  // Process command line arguments and dump into variables
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  // Connect to Local Manager and do it.
  if (TS_ERR_OKAY != TSInit(NULL, static_cast<TSInitOptionT>(TS_MGMT_OPT_NO_EVENTS | TS_MGMT_OPT_NO_SOCK_TESTS))) {
    fprintf(stderr, "error: could not connect to management port, make sure traffic_manager is running\n");
    exit(1);
  }

  status = handleArgInvocation();

  // Done with the mgmt API.
  TSTerminate();

  if (TS_ERR_OKAY != status) {
    char *msg = TSGetErrorMessage(status);
    if (ReadVar[0] == '\0' && SetVar[0] == '\0')
      fprintf(stderr, "error: the requested command failed: %s\n", msg);

    TSfree(msg);
    exit(1);
  }

  exit(0);
}
