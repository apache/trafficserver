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
#include <stdio.h>
#include <string.h>

static const char *programName;

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
static char ClearAlarms[1024];
static int VersionFlag;
static char viaHeader[1024];

struct VIA
{
  VIA() : title(NULL), next(NULL) { }
  VIA(const char * t) : title(t), next(NULL) { }

  ~VIA() {
    delete next;
  }

  const char * title;
  const char * viaData[128];
  VIA * next;
};

//Function to get via header table for every field/category in the via header
static VIA *
detailViaLookup(char flag)
{
  VIA * viaTable = NULL;

  //Detailed via codes after ":"
  switch (flag) {
  case 't':
    viaTable = new VIA("Tunnel info");
    viaTable->viaData[(unsigned char) ' '] = "no tunneling";
    viaTable->viaData[(unsigned char) 'U'] = "tunneling because of url (url suggests dynamic content)";
    viaTable->viaData[(unsigned char) 'M'] = "tunneling due to a method (e.g. CONNECT)";
    viaTable->viaData[(unsigned char) 'O'] = "tunneling because cache is turned off";
    viaTable->viaData[(unsigned char) 'F'] = "tunneling due to a header field (such as presence of If-Range header)";
    break;
  case 'c':
    //Cache type
    viaTable = new VIA( "Cache Type");
    viaTable->viaData[(unsigned char) 'C'] = "cache";
    viaTable->viaData[(unsigned char) 'L'] = "cluster, (not used)";
    viaTable->viaData[(unsigned char) 'I'] = "icp";
    viaTable->viaData[(unsigned char) ' '] = "unknown";

    //Cache Lookup Result
    viaTable->next = new VIA("Cache Lookup Result");
    viaTable->next->viaData[(unsigned char) 'C'] = "cache hit but config forces revalidate";
    viaTable->next->viaData[(unsigned char) 'I'] = "conditional miss (client sent conditional, fresh in cache, returned 412)";
    viaTable->next->viaData[(unsigned char) ' '] = "cache miss or no cache lookup";
    viaTable->next->viaData[(unsigned char) 'U'] = "cache hit, but client forces revalidate (e.g. Pragma: no-cache)";
    viaTable->next->viaData[(unsigned char) 'D'] = "cache hit, but method forces revalidated (e.g. ftp, not anonymous)";
    viaTable->next->viaData[(unsigned char) 'M'] = "cache miss (url not in cache)";
    viaTable->next->viaData[(unsigned char) 'N'] = "conditional hit (client sent conditional, doc fresh in cache, returned 304)";
    viaTable->next->viaData[(unsigned char) 'H'] = "cache hit";
    viaTable->next->viaData[(unsigned char) 'S'] = "cache hit, but expired";
    break;
  case 'i':
    viaTable = new VIA("ICP status");
    viaTable->viaData[(unsigned char) ' '] = "no icp";
    viaTable->viaData[(unsigned char) 'S'] = "connection opened successfully";
    viaTable->viaData[(unsigned char) 'F'] = "connection open failed";
    break;
  case 'p':
    viaTable = new VIA("Parent proxy connection status");
    viaTable->viaData[(unsigned char) ' '] = "no parent proxy or unknown";
    viaTable->viaData[(unsigned char) 'S'] = "connection opened successfully";
    viaTable->viaData[(unsigned char) 'F'] = "connection open failed";
    break;
  case 's':
    viaTable = new VIA("Origin server connection status");
    viaTable->viaData[(unsigned char) ' '] = "no server connection needed";
    viaTable->viaData[(unsigned char) 'S'] = "connection opened successfully";
    viaTable->viaData[(unsigned char) 'F'] = "connection open failed";
    break;
  default:
    fprintf(stderr, "%s: %s: %c\n", programName, "Invalid VIA header character",flag);
    break;
  }
  return viaTable;
}

//Function to get via header table for every field/category in the via header
static VIA *
standardViaLookup(char flag)
{
  VIA * viaTable;

  //Via codes before ":"
  switch (flag) {
    case 'u':
      viaTable = new VIA("Request headers received from client");
      viaTable->viaData[(unsigned char) 'C'] = "cookie";
      viaTable->viaData[(unsigned char) 'E'] = "error in request";
      viaTable->viaData[(unsigned char) 'S'] = "simple request (not conditional)";
      viaTable->viaData[(unsigned char) 'N'] = "no-cache";
      viaTable->viaData[(unsigned char) 'I'] = "IMS";
      viaTable->viaData[(unsigned char) ' '] = "unknown";
      break;
    case 'c':
      viaTable = new VIA( "Result of Traffic Server cache lookup for URL");
      viaTable->viaData[(unsigned char) 'A'] = "in cache, not acceptable (a cache \"MISS\")";
      viaTable->viaData[(unsigned char) 'H'] = "in cache, fresh (a cache \"HIT\")";
      viaTable->viaData[(unsigned char) 'S'] = "in cache, stale (a cache \"MISS\")";
      viaTable->viaData[(unsigned char) 'R'] = "in cache, fresh Ram hit (a cache \"HIT\")";
      viaTable->viaData[(unsigned char) 'M'] = "miss (a cache \"MISS\")";
      viaTable->viaData[(unsigned char) ' '] = "no cache lookup";
      break;
    case 's':
      viaTable = new VIA("Response information received from origin server");
      viaTable->viaData[(unsigned char) 'E'] = "error in response";
      viaTable->viaData[(unsigned char) 'S'] = "connection opened successfully";
      viaTable->viaData[(unsigned char) 'N'] = "not-modified";
      viaTable->viaData[(unsigned char) ' '] = "no server connection needed";
      break;
    case 'f':
      viaTable = new VIA("Result of document write-to-cache:");
      viaTable->viaData[(unsigned char) 'U'] = "updated old cache copy";
      viaTable->viaData[(unsigned char) 'D'] = "cached copy deleted";
      viaTable->viaData[(unsigned char) 'W'] = "written into cache (new copy)";
      viaTable->viaData[(unsigned char) ' '] = "no cache write performed";
      break;
    case 'p':
      viaTable = new VIA("Proxy operation result");
      viaTable->viaData[(unsigned char) 'R'] = "origin server revalidated";
      viaTable->viaData[(unsigned char) ' '] = "unknown";
      viaTable->viaData[(unsigned char) 'S'] = "served or connection opened successfully";
      viaTable->viaData[(unsigned char) 'N'] = "not-modified";
      break;
    case 'e':
      viaTable = new VIA("Error codes (if any)");
      viaTable->viaData[(unsigned char) 'A'] = "authorization failure";
      viaTable->viaData[(unsigned char) 'H'] = "header syntax unacceptable";
      viaTable->viaData[(unsigned char) 'C'] = "connection to server failed";
      viaTable->viaData[(unsigned char) 'T'] = "connection timed out";
      viaTable->viaData[(unsigned char) 'S'] = "server related error";
      viaTable->viaData[(unsigned char) 'D'] = "dns failure";
      viaTable->viaData[(unsigned char) 'N'] = "no error";
      viaTable->viaData[(unsigned char) 'F'] = "request forbidden";
      viaTable->viaData[(unsigned char) ' '] = "unknown";
      break;
    default:
      viaTable = new VIA();
      fprintf(stderr, "%s: %s: %c\n", programName, "Invalid VIA header character",flag);
      break;
  }
  return viaTable;
}

//Function to print via header
static void
printViaHeader(const char * header)
{
  VIA * viaTable = NULL;
  VIA * viaEntry = NULL;
  bool isDetail = false;

  printf("Via Header Details:\n");

  //Loop through input via header flags
  for (const char * c = header; *c; ++c) {

    if (*c == ':') {
      isDetail = true;
      continue;
    }

    if (islower(*c)) {
      //Get the via header table
      delete viaTable;
      viaEntry = viaTable = isDetail ? detailViaLookup(*c) : standardViaLookup(*c);
    } else {
      // This is a one of the sequence of (uppercase) VIA codes.
      if (viaEntry) {
        printf("%-55s:", viaEntry->title);
        printf("%s\n", viaEntry->viaData[(unsigned char)*c]);
        viaEntry = viaEntry->next;
      }
    }
  }

  delete viaTable;
}

//Check validity of via header and then decode it
static TSMgmtError
decodeViaHeader(char * Via)
{
  size_t viaHdrLength = strlen(Via);

#ifdef DEBUG
  printf("Via header is %s, Length is %zu\n",Via, viaHdrLength);
#endif

  // Via header inside square brackets ...
  if (viaHdrLength > 2 && Via[0] == '[' && Via[viaHdrLength - 1] == ']') {
    viaHdrLength = viaHdrLength - 2;
    Via++;
    Via[viaHdrLength] = '\0'; //null terminate the string after trimming
  }

  if (viaHdrLength == 24 || viaHdrLength == 6) {
    //Decode via header
    printViaHeader(Via);
    return TS_ERR_OKAY;
  }

  // Be kind to people who did not quote the via argument correctly
  // by appending one space character before decoding via header.
  if (viaHdrLength == 23 || viaHdrLength == 5) {
    Via = strcat(Via, " ");
    printViaHeader(Via);
    return TS_ERR_OKAY;
  }

  printf("\nInvalid VIA header. VIA header length should be 6 or 24 characters\n");
  printf("Valid via header format is [u<client-stuff>c<cache-lookup-stuff>s<server-stuff>f<cache-fill-stuff>p<proxy-stuff>]e<error-codes>:t<tunneling-info>c<cache type><cache-lookup-result>i<icp-conn-info>p<parent-proxy-conn-info>s<server-conn-info>]");
  return TS_ERR_FAIL;
}

static TSMgmtError
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
    return TSStatsReset(true, NULL);
  } else if (ClearNode == 1) {
    return TSStatsReset(false, NULL);
  } else if (*ZeroNode != '\0' || *ZeroCluster != '\0') {
    TSMgmtError err;
    TSRecordEle *rec_ele = TSRecordEleCreate();
    char *name = *ZeroNode ? ZeroNode : ZeroCluster;

    if ((err = TSRecordGet(name, rec_ele)) != TS_ERR_OKAY) {
      fprintf(stderr, "%s: %s\n", programName, TSGetErrorMessage(err));
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
        char* name = static_cast<char *>(TSListDequeue(events));
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
    size_t len = strlen(ClearAlarms);

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
    bool all = false;
    int num = -1;

    if ((3 == len) && (0 == strncasecmp(ClearAlarms, "all", len))) {
      all = true;
    } else  {
      num = strtol(ClearAlarms, NULL, 10) - 1;
      if (num <= 0)
        num = -1;
    }

    for (int i = 0; i < count; i++) {
      char* name = static_cast<char*>(TSListDequeue(events));

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
    return (errors > 0 ? TS_ERR_FAIL: TS_ERR_OKAY);
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
  } else if (*ReadVar != '\0') {        // Handle a value read
    if (*SetVar != '\0' || *VarValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read and set values at the same time\n", programName);
      return TS_ERR_FAIL;
    } else {
      TSMgmtError err;
      TSRecordEle *rec_ele = TSRecordEleCreate();

      if ((err = TSRecordGet(ReadVar, rec_ele)) != TS_ERR_OKAY) {
        fprintf(stderr, "%s: %s\n", programName, TSGetErrorMessage(err));
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
          fprintf(stderr, "%s: unknown record type (%d)\n", programName, rec_ele->rec_type);
          err = TS_ERR_FAIL;
          break;
        }
      }
      TSRecordEleDestroy(rec_ele);
      return err;
    }
  } else if (*MatchVar != '\0') {        // Handle a value read
    if (*SetVar != '\0' || *VarValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read and set values at the same time\n", programName);
      return TS_ERR_FAIL;
    } else {
      TSMgmtError err;
      TSList list = TSListCreate();

      if ((err = TSRecordGetMatchMlt(MatchVar, list)) != TS_ERR_OKAY) {
        char* msg = TSGetErrorMessage(err);
        fprintf(stderr, "%s: %s\n", programName, msg);
        ats_free(msg);
      }

      // If the RPC call failed, the list will be empty, so we won't print anything. Otherwise,
      // print all the results, freeing them as we go.
      for (TSRecordEle * rec_ele = (TSRecordEle *) TSListDequeue(list); rec_ele;
          rec_ele = (TSRecordEle *) TSListDequeue(list)) {
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
      fprintf(stderr, "%s: Set requires a -v argument\n", programName);
      return TS_ERR_FAIL;
    } else {
      TSMgmtError err;
      TSActionNeedT action;

      if ((err = TSRecordSet(SetVar, VarValue, &action)) != TS_ERR_OKAY) {
        fprintf(stderr, "%s: Please correct your variable name and|or value\n", programName);
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
        // printf("Set %s, reconfiguration required\n", SetVar);
        break;
      case TS_ACTION_DYNAMIC:
      default:
        printf("Set %s\n", SetVar);
        break;
      }

      return err;
    }
  } else if (*VarValue != '\0') {       // We have a value but no variable to set
    fprintf(stderr, "%s: Must specify variable to set with -s when using -v\n", programName);
    return TS_ERR_FAIL;
  } else if (*viaHeader != '\0') {        // Read via header and decode
    TSMgmtError rc;
    rc = decodeViaHeader(viaHeader);
    return rc;
  }

  fprintf(stderr, "%s: No arguments specified\n", programName);
  return TS_ERR_FAIL;
}

int
main(int /* argc ATS_UNUSED */, char **argv)
{
  AppVersionInfo appVersionInfo;
  TSMgmtError status;

  programName = argv[0];

  ReadVar[0] = '\0';
  MatchVar[0] = '\0';
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
  ZeroCluster[0] = '\0';
  ZeroNode[0] = '\0';
  VersionFlag = 0;
  *StorageCmdOffline = 0;
  ShowAlarms = 0;
  ShowStatus = 0;
  ClearAlarms[0] = '\0';
  viaHeader[0] = '\0';

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,"traffic_line", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

/* Argument description table used to describe how to parse command line args, */
/* see 'ink_args.h' for meanings of the various fields */
  ArgumentDescription argument_descriptions[] = {
    {"query_deadhosts", 'q', "Query congested sites", "F", &QueryDeadhosts, NULL, NULL},
    {"read_var", 'r', "Read Variable", "S1024", &ReadVar, NULL, NULL},
    {"match_var", 'm', "Match Variable", "S1024", &MatchVar, NULL, NULL},
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
    {"zero_cluster", 'Z', "Zero Specific Statistic (cluster wide)", "S1024", &ZeroCluster, NULL, NULL},
    {"zero_node", 'z', "Zero Specific Statistic (local node)", "S1024", &ZeroNode, NULL, NULL},
    {"offline", '-', "Mark cache storage offline", "S1024", &StorageCmdOffline, NULL, NULL},
    {"alarms", '-', "Show all alarms", "F", &ShowAlarms, NULL, NULL},
    {"clear_alarms", '-', "Clear specified, or all,  alarms", "S1024", &ClearAlarms, NULL, NULL},
    {"status", '-', "Show proxy server status", "F", &ShowStatus, NULL, NULL},
    {"version", 'V', "Print Version Id", "T", &VersionFlag, NULL, NULL},
    {"decode_via", '-', "Decode Via Header", "S1024", &viaHeader, NULL, NULL},
  };

  // Process command line arguments and dump into variables
  process_args(argument_descriptions, countof(argument_descriptions), argv);

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
