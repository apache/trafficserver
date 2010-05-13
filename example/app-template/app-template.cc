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

/*
 * Example application template to be used with the standalone iocore
 *
 */

#include "inktomi++.h"

#include "I_Version.h"
#include "I_EventSystem.h"
#include "I_Net.h"
#include "I_HostDB.h"
#include "I_DNS.h"
#include "I_SplitDNS.h"
#include "I_Cache.h"

//#include "ts.h" // plugin API's

#include "Initialize.h" // TODO: move to I_Initialize.h ???
#include "signals.h" // TODO: move to I_Signals.h ???

#include "app-template.h"

#define PROGRAM_NAME		"tsapp"

extern int use_accept_thread; // TODO: rename and move to I_UnixNetProcessor.h
extern void RecDumpRecordsHt(RecT rec_type); // TODO: move from P_RecCore.h -> I_RecCore.h

int system_num_of_processors  = ink_number_of_processors();
int system_num_of_net_threads = DEFAULT_NUMBER_OF_THREADS;
int system_num_of_udp_threads = DEFAULT_NUMBER_OF_UDP_THREADS;

char system_root_dir[PATH_NAME_MAX + 1]         = DEFAULT_ROOT_DIRECTORY;
char system_local_state_dir[PATH_NAME_MAX + 1]  = DEFAULT_LOCAL_STATE_DIRECTORY;
char system_config_directory[PATH_NAME_MAX + 1] = DEFAULT_SYSTEM_CONFIG_DIRECTORY;
char system_log_dir[PATH_NAME_MAX + 1]          = DEFAULT_LOG_DIRECTORY;

//int system_remote_management_flag = DEFAULT_REMOTE_MANAGEMENT_FLAG;

Diags *diags = NULL;
char debug_tags[1024]  = "";
char action_tags[1024] = "";

int  version_flag = 0;
int  tsapp_port = 12345;

AppVersionInfo appVersionInfo;

ArgumentDescription argument_descriptions[] = {
  {"version", 'V', "Print Version Id", "T",
   &version_flag, NULL, NULL},
  {"tsapp_port", 'p', "tsapp port", "I",
   &tsapp_port, "TSAPP_PORT",  NULL},
  {"net_threads", 'n', "Number of Net Threads", "I",
   &system_num_of_net_threads, "TSAPP_NET_THREADS", NULL},
  {"accept_thread", 'a', "Use an Accept Thread", "T",
   &use_accept_thread, "TSAPP_ACCEPT_THREAD", NULL},
  {"poll_timeout", 't', "poll timeout in milliseconds", "I",
   &net_config_poll_timeout, NULL, NULL},
  {"debug_tags", 'T', "Debug Tags ('|' separated)", "S1023",
   debug_tags, "TSAPP_DEBUG_TAGS",  NULL},
  {"action_tags", 'T', "Action Tags ('|' separated)", "S1023",
   action_tags, "TSAPP_ACTION_TAGS",  NULL},
  {"help", 'h', "Help", NULL, NULL, NULL, usage}
};
int n_argument_descriptions = SIZE(argument_descriptions);

void init_app_config() {

  // Net
  RecRegisterConfigInt(
    RECT_CONFIG,
    "proxy.config.net.max_poll_delay",
    128,
    RECU_DYNAMIC, RECC_NULL, NULL);

   RecRegisterConfigInt(
    RECT_CONFIG,
    "proxy.config.net.listen_backlog",
    1024,
    RECU_DYNAMIC, RECC_NULL, NULL);

   RecRegisterConfigInt(
    RECT_CONFIG,
    "proxy.config.net.connections_throttle",
    8000,
    RECU_DYNAMIC, RECC_NULL, NULL);

   RecRegisterConfigInt(
    RECT_CONFIG,
    "proxy.config.accept_threads",
    0,
    RECU_DYNAMIC, RECC_NULL, NULL);

   // IO
   RecRegisterConfigInt(
    RECT_CONFIG,
    "proxy.config.io.max_buffer_size",
    32768,
    RECU_DYNAMIC, RECC_NULL, NULL);

   // Cache, etc
}

// TODO: Move to I_Util.cc file ???
int
get_ts_directory(char *ts_path, size_t ts_path_len)
{
  FILE *fp;
  char *env_path;
  struct stat s;
  int err;

  if ((env_path = getenv("TS_ROOT"))) {
    ink_strncpy(ts_path, env_path, ts_path_len);
  } else {
    if ((fp = fopen(DEFAULT_TS_DIRECTORY_FILE, "r")) != NULL) {
      if (fgets(ts_path, ts_path_len, fp) == NULL) {
        fclose(fp);
        fprintf(stderr,"\nInvalid contents in %s\n",DEFAULT_TS_DIRECTORY_FILE);
        fprintf(stderr," Please set correct path in env variable TS_ROOT \n");
        return -1;
      }
      // strip newline if it exists
      int len = strlen(ts_path);
      if (ts_path[len - 1] == '\n') {
        ts_path[len - 1] = '\0';
      }
      // strip trailing "/" if it exists
      len = strlen(ts_path);
      if (ts_path[len - 1] == '/') {
        ts_path[len - 1] = '\0';
      }

      fclose(fp);
    } else {
      ink_strncpy(ts_path, PREFIX, ts_path_len);
    }
  }

  if ((err = stat(ts_path, &s)) < 0) {
    fprintf(stderr,"unable to stat() TS PATH '%s': %d %d, %s\n",
              ts_path, err, errno, strerror(errno));
    fprintf(stderr," Please set correct path in env variable TS_ROOT \n");
    return -1;
  }

  return 0;
}


int
MyAccept::main_event(int event, void *data) {
  if (event == NET_EVENT_ACCEPT) {
    //NetVConnection *netvc = (NetVConnection*)data;
    NOWARN_UNUSED(data);
    // allocate continuation to handle this connection/request
    //
    // ..handle request, etc
    //
    return EVENT_CONT;
  } else {
    Fatal("tsapp accept received fatal error: errno = %d", -((int)(intptr_t)data));
    return EVENT_CONT;
  }
}


//
// Shutdown, called from signals interrupt_handler()
// TODO: rename, consolidate signals stuff, etc
void
shutdown_system()
{
}

int main(int argc, char * argv[])
{
  char ts_path[PATH_NAME_MAX + 1];
  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,PROGRAM_NAME, PACKAGE_VERSION, __DATE__,
                       __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  process_args(argument_descriptions, n_argument_descriptions, argv);

  // check for the version number request
  if (version_flag) {
    fprintf(stderr, "%s\n", appVersionInfo.FullVersionInfoStr);
    _exit(0);
  }

  // Get TS directory
  if (0 == get_ts_directory(ts_path,sizeof(ts_path))) {
    ink_strncpy(system_root_dir, ts_path, sizeof(system_root_dir));
    ink_snprintf(system_config_directory, sizeof(system_config_directory), "%s/etc/trafficserver", system_root_dir);
    ink_snprintf(system_local_state_dir, sizeof(system_local_state_dir), "%s/var/trafficserver", system_root_dir);
    ink_snprintf(system_log_dir, sizeof(system_log_dir), "%s/var/log/trafficserver", system_root_dir);
  }

  if (system_root_dir[0] && (chdir(system_root_dir) < 0)) {
    fprintf(stderr,"unable to change to root directory \"%s\" [%d '%s']\n", system_root_dir, errno, strerror(errno));
    fprintf(stderr," please set correct path in env variable TS_ROOT \n");
    _exit(1);
  } else {
    printf("[tsapp] using root directory '%s'\n",system_root_dir);
  }

  // Diags
  init_system_diags(debug_tags, NULL);
  if (is_debug_tag_set("tsapp"))
    diags->dump(stdout);

  // Config & Stats
  RecModeT mode_type = RECM_STAND_ALONE;
  RecProcessInit(mode_type,diags);
  //RecProcessInitMessage(mode_type);

  signal(SIGPIPE, SIG_IGN); // ignore broken pipe

  init_buffer_allocators();

  // Initialze iocore modules
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  ink_net_init(NET_SYSTEM_MODULE_VERSION);
  ink_aio_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  ink_cache_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  ink_hostdb_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  ink_dns_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  //ink_split_dns_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));

  init_app_config(); // initialize stats and configs

  if (!use_accept_thread) {
    RecInt tmp = 0;
    RecGetRecordInt("proxy.config.accept_threads",(RecInt*) &tmp);
    use_accept_thread = (ink32)tmp;                          \
  }

  if (initialize_store()) { // cache
    printf("unable to initialize storage, (Re)Configuration required\n");
    _exit(0);
  }

  // Start processors
  eventProcessor.start(system_num_of_net_threads);

  RecProcessStart();

  init_signals2();

  netProcessor.start();

  dnsProcessor.start();

  if (hostDBProcessor.start() < 0)
    printf("bad hostdb or storage configuration, hostdb disabled");

  //clusterProcessor.init();

  cacheProcessor.start();

  //udpNet.start(system_num_of_udp_threads); // XXX : broken

  //sslNetProcessor.start(getNumSSLThreads());

  // initialize logging (after event and net processor)
  //Log::init(system_remote_management_flag ? 0 : Log::NO_REMOTE_MANAGEMENT);

#if !defined(TS_NO_API)
  //plugin_init(system_config_directory, true); // extensions.config
#endif

#ifndef TS_NO_API
  //plugin_init(system_config_directory, false); // plugin.config
#else
  //api_init();  // still need to initialize some of the data structure other module needs.
  //extern void init_inkapi_stat_system();
  //init_inkapi_stat_system();   // i.e. http_global_hooks
#endif

  // Create accept continuation
  MyAccept *a = new MyAccept;
  a->accept_port = tsapp_port;
  a->mutex = new_ProxyMutex();
  Action *act= netProcessor.accept(a, a->accept_port, true);

  RecDumpRecordsHt(RECT_NULL); // debugging, specify '-T "rec.*"' to see records

  Debug("tsapp","listening port %d, started %d ethreads, use_accept_thread (%d), act(%p)\n" ,
        tsapp_port,system_num_of_net_threads,use_accept_thread, act);

  this_thread()->execute();
}


