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

#include "libts.h"

#include "I_Version.h"
#include "I_EventSystem.h"
#include "I_Layout.h"
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

int use_accept_thread = 0; // TODO: rename and move to I_UnixNetProcessor.h
extern void RecDumpRecordsHt(RecT rec_type); // TODO: move from P_RecCore.h -> I_RecCore.h

int system_num_of_processors  = ink_number_of_processors();
int system_num_of_net_threads = DEFAULT_NUMBER_OF_THREADS;
int system_num_of_udp_threads = DEFAULT_NUMBER_OF_UDP_THREADS;

char system_root_dir[PATH_NAME_MAX + 1];
char system_runtime_dir[PATH_NAME_MAX + 1];
char system_config_directory[PATH_NAME_MAX + 1];
char system_log_dir[PATH_NAME_MAX + 1];

//int system_remote_management_flag = DEFAULT_REMOTE_MANAGEMENT_FLAG;

//Diags *diags = NULL;
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
  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,PROGRAM_NAME, PACKAGE_VERSION, __DATE__,
                       __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  // create the layout engine
  Layout::create();
  process_args(argument_descriptions, n_argument_descriptions, argv);

  // check for the version number request
  if (version_flag) {
    fprintf(stderr, "%s\n", appVersionInfo.FullVersionInfoStr);
    _exit(0);
  }

  // Get TS directories
  ink_strlcpy(system_root_dir, Layout::get()->prefix, sizeof(system_root_dir));
  ink_strlcpy(system_config_directory, Layout::get()->sysconfdir, sizeof(system_config_directory));
  ink_strlcpy(system_runtime_dir, Layout::get()->runtimedir, sizeof(system_runtime_dir));
  ink_strlcpy(system_log_dir, Layout::get()->logdir, sizeof(system_log_dir));

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
    use_accept_thread = (int32_t)tmp;                          \
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
#endif

  // Create accept continuation
  MyAccept *a = new MyAccept;
  a->accept_port = tsapp_port;
  a->mutex = new_ProxyMutex();
  Action *act= netProcessor.accept(a, a->accept_port, AF_INET, true);

  RecDumpRecordsHt(RECT_NULL); // debugging, specify '-T "rec.*"' to see records

  Debug("tsapp","listening port %d, started %d ethreads, use_accept_thread (%d), act(%p)\n" ,
        tsapp_port,system_num_of_net_threads,use_accept_thread, act);

  this_thread()->execute();
}


