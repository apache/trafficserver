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

/****************************************************************************

  Main.cc

  This is the primary source file for the proxy cache system.


 ****************************************************************************/

#include "ink_config.h"

#include "libts.h"
#include "ink_sys_control.h"
#include <syslog.h>

#if !defined(linux)
#include <sys/lock.h>
#endif

#if defined(linux)
extern "C" int plock(int);
#else
#include <sys/filio.h>
#endif

#if HAVE_MCHECK_H
#include <mcheck.h>
#endif

#include "Main.h"
#include "signals.h"
#include "Error.h"
#include "StatSystem.h"
#include "P_EventSystem.h"
#include "P_Net.h"
#include "P_UDPNet.h"
#include "P_DNS.h"
#include "P_SplitDNS.h"
#include "P_Cluster.h"
#include "P_HostDB.h"
#include "P_Cache.h"
#include "I_Layout.h"
#include "I_Machine.h"
#include "RecordsConfig.h"
#include "I_RecProcess.h"
#include "Transform.h"
#include "ProcessManager.h"
#include "ProxyConfig.h"
#include "HttpProxyServerMain.h"
#include "HttpBodyFactory.h"
#include "logging/Log.h"
#include "ICPProcessor.h"
//#include "ClusterTest.h"
#include "CacheControl.h"
#include "IPAllow.h"
#include "ParentSelection.h"
#include "MgmtUtils.h"
#include "StatPages.h"
#include "HTTP.h"
#include "Plugin.h"
#include "DiagsConfig.h"
#include "CoreUtils.h"
#include "Update.h"
#include "congest/Congestion.h"
#include "RemapProcessor.h"
#include "I_Tasks.h"
#include "InkAPIInternal.h"

#include <ts/ink_cap.h>

#if TS_HAS_PROFILER
#include <google/profiler.h>
#endif

//
// Global Data
//
#define DEFAULT_HTTP_ACCEPT_PORT_NUMBER   0
#define DEFAULT_COMMAND_FLAG              0

#define DEFAULT_VERBOSE_FLAG              0
#define DEFAULT_VERSION_FLAG              0
#define DEFAULT_STACK_TRACE_FLAG          0

#if DEFAULT_COMMAND_FLAG
# define DEFAULT_COMMAND_FLAG_TYPE        "f"
#else
# define DEFAULT_COMMAND_FLAG_TYPE        "F"
#endif

#define DEFAULT_REMOTE_MANAGEMENT_FLAG    0
#define DIAGS_LOG_FILENAME                "diags.log"

static const long MAX_LOGIN =  sysconf(_SC_LOGIN_NAME_MAX) <= 0 ? _POSIX_LOGIN_NAME_MAX :  sysconf(_SC_LOGIN_NAME_MAX);

static void * mgmt_restart_shutdown_callback(void *, char *, int data_len);
static void*  mgmt_storage_device_cmd_callback(void* x, char* data, int len);

static int version_flag = DEFAULT_VERSION_FLAG;

static int num_of_net_threads = ink_number_of_processors();
static int num_of_udp_threads = 0;
static int num_accept_threads  = 0;
static int num_task_threads = 0;

extern int num_of_cluster_threads;

static char * http_accept_port_descriptor;
int http_accept_file_descriptor = NO_FD;
static char core_file[255] = "";
static bool enable_core_file_p = false; // Enable core file dump?
int command_flag = DEFAULT_COMMAND_FLAG;
#if TS_HAS_TESTS
static char regression_test[1024] = "";
#endif
int auto_clear_hostdb_flag = 0;
extern int fds_limit;
extern int cluster_port_number;
extern int cache_clustering_enabled;
char cluster_host[MAXDNAME + 1] = DEFAULT_CLUSTER_HOST;

//         = DEFAULT_CLUSTER_PORT_NUMBER;
static char command_string[512] = "";
int remote_management_flag = DEFAULT_REMOTE_MANAGEMENT_FLAG;

static char error_tags[1024] = "";
static char action_tags[1024] = "";
static int show_statistics = 0;
static inkcoreapi DiagsConfig *diagsConfig = NULL;
HttpBodyFactory *body_factory = NULL;

static int accept_mss = 0;
static int cmd_line_dprintf_level = 0;  // default debug output level from ink_dprintf function
static int poll_timeout = -1; // No value set.

// 1: delay listen, wait for cache.
// 0: Do not delay, start listen ASAP.
// -1: cache is already initialized, don't delay.
static volatile int delay_listen_for_cache_p = 0;

AppVersionInfo appVersionInfo;  // Build info for this application

const Version version = {
  {CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION},     // cacheDB
  {CACHE_DIR_MAJOR_VERSION, CACHE_DIR_MINOR_VERSION},   // cacheDir
  {CLUSTER_MAJOR_VERSION, CLUSTER_MINOR_VERSION},       // current clustering
  {MIN_CLUSTER_MAJOR_VERSION, MIN_CLUSTER_MINOR_VERSION},       // min clustering
};

static const ArgumentDescription argument_descriptions[] = {
  {"net_threads", 'n', "Number of Net Threads", "I", &num_of_net_threads, "PROXY_NET_THREADS", NULL},
  {"cluster_threads", 'Z', "Number of Cluster Threads", "I", &num_of_cluster_threads, "PROXY_CLUSTER_THREADS", NULL},
  {"udp_threads", 'U', "Number of UDP Threads", "I", &num_of_udp_threads, "PROXY_UDP_THREADS", NULL},
  {"accept_thread", 'a', "Use an Accept Thread", "T", &num_accept_threads, "PROXY_ACCEPT_THREAD", NULL},
  {"accept_till_done", 'b', "Accept Till Done", "T", &accept_till_done, "PROXY_ACCEPT_TILL_DONE", NULL},
  {"httpport", 'p', "Port descriptor for HTTP Accept", "S*", &http_accept_port_descriptor,
   "PROXY_HTTP_ACCEPT_PORT", NULL},
  {"cluster_port", 'P', "Cluster Port Number", "I", &cluster_port_number, "PROXY_CLUSTER_PORT", NULL},
  {"dprintf_level", 'o', "Debug output level", "I", &cmd_line_dprintf_level, "PROXY_DPRINTF_LEVEL", NULL},
  {"version", 'V', "Print Version String", "T", &version_flag, NULL, NULL},

#if TS_HAS_TESTS
  {"regression", 'R',
#ifdef DEBUG
   "Regression Level (quick:1..long:3)",
#else
   0,
#endif
   "I", &regression_level, "PROXY_REGRESSION", NULL},
  {"regression_test", 'r',

#ifdef DEBUG
   "Run Specific Regression Test",
#else
   0,
#endif
   "S512", regression_test, "PROXY_REGRESSION_TEST", NULL},
#endif //TS_HAS_TESTS

#if TS_USE_DIAGS
  {"debug_tags", 'T', "Vertical-bar-separated Debug Tags", "S1023", error_tags, "PROXY_DEBUG_TAGS", NULL},
  {"action_tags", 'B', "Vertical-bar-separated Behavior Tags", "S1023", action_tags, "PROXY_BEHAVIOR_TAGS", NULL},
#endif

  {"interval", 'i', "Statistics Interval", "I", &show_statistics, "PROXY_STATS_INTERVAL", NULL},
  {"remote_management", 'M', "Remote Management", "T", &remote_management_flag, "PROXY_REMOTE_MANAGEMENT", NULL},
  {"command", 'C', "Maintenance Command to Execute", "S511", &command_string, "PROXY_COMMAND_STRING", NULL},
  {"clear_hostdb", 'k', "Clear HostDB on Startup", "F", &auto_clear_hostdb_flag, "PROXY_CLEAR_HOSTDB", NULL},
  {"clear_cache", 'K', "Clear Cache on Startup", "F", &cacheProcessor.auto_clear_flag, "PROXY_CLEAR_CACHE", NULL},
#if defined(linux)
  {"read_core", 'c', "Read Core file", "S255", &core_file, NULL, NULL},
#endif

  {"accept_mss", ' ', "MSS for client connections", "I", &accept_mss, NULL, NULL},
  {"poll_timeout", 't', "poll timeout in milliseconds", "I", &poll_timeout, NULL, NULL},
  {"help", 'h', "HELP!", NULL, NULL, NULL, usage},
};

//
// Initialize operating system related information/services
//
static void
init_system()
{
  RecInt stackDump;
  bool found = (RecGetRecordInt("proxy.config.stack_dump_enabled", &stackDump) == REC_ERR_OKAY);

  if (found == false) {
    Warning("Unable to determine stack_dump_enabled , assuming enabled");
    stackDump = 1;
  }

  init_signals(stackDump == 1);

  syslog(LOG_NOTICE, "NOTE: --- %s Starting ---", appVersionInfo.AppStr);
  syslog(LOG_NOTICE, "NOTE: %s Version: %s", appVersionInfo.AppStr, appVersionInfo.FullVersionInfoStr);

  //
  // Delimit file Descriptors
  //
  fds_limit = ink_max_out_rlimit(RLIMIT_NOFILE, true, false);
}

static void
check_lockfile()
{
  xptr<char> rundir(RecConfigReadRuntimeDir());
  xptr<char> lockfile;
  pid_t holding_pid;
  int err;

  lockfile = Layout::relative_to(rundir, SERVER_LOCK);

  Lockfile server_lockfile(lockfile);
  err = server_lockfile.Get(&holding_pid);

  if (err != 1) {
    char *reason = strerror(-err);
    fprintf(stderr, "WARNING: Can't acquire lockfile '%s'", (const char *)lockfile);

    if ((err == 0) && (holding_pid != -1)) {
      fprintf(stderr, " (Lock file held by process ID %ld)\n", (long)holding_pid);
    } else if ((err == 0) && (holding_pid == -1)) {
      fprintf(stderr, " (Lock file exists, but can't read process ID)\n");
    } else if (reason) {
      fprintf(stderr, " (%s)\n", reason);
    } else {
      fprintf(stderr, "\n");
    }
    _exit(1);
  }
}

static void
check_config_directories(void)
{
  xptr<char> rundir(RecConfigReadRuntimeDir());

  if (access(Layout::get()->sysconfdir, R_OK) == -1) {
    fprintf(stderr,"unable to access() config dir '%s': %d, %s\n",
            Layout::get()->sysconfdir, errno, strerror(errno));
    fprintf(stderr, "please set the 'TS_ROOT' environment variable\n");
    _exit(1);
  }

  if (access(rundir, R_OK | W_OK) == -1) {
    fprintf(stderr,"unable to access() local state dir '%s': %d, %s\n",
            (const char *)rundir, errno, strerror(errno));
    fprintf(stderr,"please set 'proxy.config.local_state_dir'\n");
    _exit(1);
  }

}

//
// Startup process manager
//
static void
initialize_process_manager()
{
  mgmt_use_syslog();

  // Temporary Hack to Enable Communication with LocalManager
  if (getenv("PROXY_REMOTE_MGMT")) {
    remote_management_flag = true;
  }

  RecProcessInit(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE, diags);

  if (!remote_management_flag) {
    LibRecordsConfigInit();
    RecordsConfigOverrideFromEnvironment();
  }

  // Start up manager
  pmgmt = NEW(new ProcessManager(remote_management_flag));

  pmgmt->start();
  RecProcessInitMessage(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE);
  pmgmt->reconfigure();
  check_config_directories();

  //
  // Define version info records
  //
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.short", appVersionInfo.VersionStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.long", appVersionInfo.FullVersionInfoStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_number", appVersionInfo.BldNumStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_time", appVersionInfo.BldTimeStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_date", appVersionInfo.BldDateStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_machine", appVersionInfo.BldMachineStr, RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_person", appVersionInfo.BldPersonStr, RECP_NON_PERSISTENT);
}

//
// Shutdown
//
void
shutdown_system()
{
}

#define CMD_ERROR    -2         // serious error, exit maintaince mode
#define CMD_FAILED   -1         // error, but recoverable
#define CMD_OK        0         // ok, or minor (user) error
#define CMD_HELP      1         // ok, print help
#define CMD_IN_PROGRESS 2       // task not completed. don't exit

static int
cmd_list(char * /* cmd ATS_UNUSED */)
{
  printf("LIST\n\n");

  // show hostdb size

  int h_size = 120000;
  TS_ReadConfigInteger(h_size, "proxy.config.hostdb.size");
  printf("Host Database size:\t%d\n", h_size);

  // show cache config information....

  Note("Cache Storage:");
  Store tStore;
  if (tStore.read_config() < 0) {
    Note("config read failure");
    return CMD_FAILED;
  } else {
    tStore.write_config_data(fileno(stdout));
    return CMD_OK;
  }
}

static char *
skip(char *cmd, int null_ok = 0)
{
  cmd += strspn(cmd, " \t");
  cmd = strpbrk(cmd, " \t");
  if (!cmd) {
    if (!null_ok)
      printf("Error: argument missing\n");
    return cmd;
  }
  cmd += strspn(cmd, " \t");
  return cmd;
}

// Handler for things that need to wait until the cache is initialized.
static void
CB_After_Cache_Init()
{
  APIHook* hook;
  int start;

  start = ink_atomic_swap(&delay_listen_for_cache_p, -1);
  if (1 == start) {
    Debug("http_listen", "Delayed listen enable, cache initialization finished");
    start_HttpProxyServer();
  }

  time_t cache_ready_at = time(NULL);
  RecSetRecordInt("proxy.node.restarts.proxy.cache_ready_time", cache_ready_at);

  // Alert the plugins the cache is initialized.
  hook = lifecycle_hooks->get(TS_LIFECYCLE_CACHE_READY_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_LIFECYCLE_CACHE_READY, NULL);
    hook = hook->next();
  }
}

struct CmdCacheCont: public Continuation
{

  int cache_fix;

  int ClearEvent(int event, Event * e)
  {
    (void) event;
    (void) e;
    if (cacheProcessor.IsCacheEnabled() == CACHE_INITIALIZED) {
      Note("CLEAR, succeeded");
      _exit(0);
    } else if (cacheProcessor.IsCacheEnabled() == CACHE_INIT_FAILED) {
      Note("unable to open Cache, CLEAR failed");
      _exit(1);
    }
    return EVENT_CONT;
  }

  int CheckEvent(int event, Event * e)
  {
    (void) event;
    (void) e;
    int res = 0;
    Note("Cache Directory");
    if (cacheProcessor.IsCacheEnabled() == CACHE_INITIALIZED) {

      res = cacheProcessor.dir_check(cache_fix) < 0 || res;

      Note("Cache");
      res = cacheProcessor.db_check(cache_fix) < 0 || res;

      cacheProcessor.stop();

      const char *n = cache_fix ? "REPAIR" : "CHECK";

      if (res) {
        printf("\n%s failed", n);
        _exit(1);
      } else {
        printf("\n%s succeeded\n", n);
        _exit(0);
      }
    } else if (cacheProcessor.IsCacheEnabled() == CACHE_INIT_FAILED) {
      Note("unable to open Cache, Check failed");
      _exit(1);
    }
    return EVENT_CONT;
  }

  CmdCacheCont(bool check, bool fix = false):Continuation(new_ProxyMutex()) {
    cache_fix = fix;
    if (check)
      SET_HANDLER(&CmdCacheCont::CheckEvent);
    else
      SET_HANDLER(&CmdCacheCont::ClearEvent);
  }

};

static int
cmd_check_internal(char * /* cmd ATS_UNUSED */, bool fix = false)
{
  const char *n = fix ? "REPAIR" : "CHECK";

  printf("%s\n\n", n);
  int res = 0;

  hostdb_current_interval = (ink_get_based_hrtime() / HRTIME_MINUTE);

//#ifndef INK_NO_ACC
//  acc.clear_cache();
//#endif

  const char *err = NULL;
  theStore.delete_all();
  if ((err = theStore.read_config())) {
    printf("%s, %s failed\n", err, n);
    return CMD_FAILED;
  }
  printf("Host Database\n");
  HostDBCache hd;
  if (hd.start(fix) < 0) {
    printf("\tunable to open Host Database, %s failed\n", n);
    return CMD_OK;
  }
  res = hd.check("hostdb.config", fix) < 0 || res;
  hd.reset();

  if (cacheProcessor.start() < 0) {
    printf("\nbad cache configuration, %s failed\n", n);
    return CMD_FAILED;
  }
  eventProcessor.schedule_every(NEW(new CmdCacheCont(true, fix)), HRTIME_SECONDS(1));

  return CMD_IN_PROGRESS;
}

static int
cmd_check(char *cmd)
{
  return cmd_check_internal(cmd, false);
}

#ifdef UNUSED_FUNCTION
static int
cmd_repair(char *cmd)
{
  return cmd_check_internal(cmd, true);
}
#endif

static int
cmd_clear(char *cmd)
{
  Note("CLEAR");

  bool c_all = !strcmp(cmd, "clear");
  bool c_hdb = !strcmp(cmd, "clear_hostdb");
  //bool c_adb = !strcmp(cmd, "clear_authdb");
  bool c_cache = !strcmp(cmd, "clear_cache");

  if (c_all || c_hdb) {
    xptr<char> rundir(RecConfigReadRuntimeDir());
    xptr<char> config(Layout::relative_to(rundir, "hostdb.config"));

    Note("Clearing HostDB Configuration");
    if (unlink(config) < 0)
      Note("unable to unlink %s", (const char *)config);
  }

  if (c_all || c_cache) {
    const char *err = NULL;
    theStore.delete_all();
    if ((err = theStore.read_config())) {
      printf("%s, CLEAR failed\n", err);
      return CMD_FAILED;
    }
  }

  if (c_hdb || c_all) {
    Note("Clearing Host Database");
    if (hostDBProcessor.cache()->start(PROCESSOR_RECONFIGURE) < 0) {
      Note("unable to open Host Database, CLEAR failed");
      return CMD_FAILED;
    }
    hostDBProcessor.cache()->reset();
    if (c_hdb)
      return CMD_OK;
  }

//#ifndef INK_NO_ACC
//  if (c_adb || c_all) {
 //   if (!acc.clear_cache()) {
  //    return CMD_FAILED;
  //  }
   // if (c_adb)
    //  return CMD_OK;
//  }
//#endif

  if (c_all || c_cache) {
    Note("Clearing Cache");

    if (cacheProcessor.start_internal(PROCESSOR_RECONFIGURE) < 0) {
      Note("unable to open Cache, CLEAR failed");
      return CMD_FAILED;
    }
    eventProcessor.schedule_every(NEW(new CmdCacheCont(false)), HRTIME_SECONDS(1));
    return CMD_IN_PROGRESS;
  }

  return CMD_OK;
}

static int cmd_help(char *cmd);

static const struct CMD
{
  const char *n;                      // name
  const char *d;                      // description (part of a line)
  const char *h;                      // help string (multi-line)
  int (*f) (char *);
}
commands[] = {
  {
  "list",
      "List cache configuration",
      "LIST\n"
      "\n"
      "FORMAT: list\n"
      "\n"
      "List the sizes of the Host Database and Cache Index,\n" "and the storage available to the cache.\n", cmd_list}, {
  "check",
      "Check the cache (do not make any changes)",
      "CHECK\n"
      "\n"
      "FORMAT: check\n"
      "\n"
      "Check the cache for inconsistencies or corruption.\n"
      "CHECK does not make any changes to the data stored in\n"
      "the cache. CHECK requires a scan of the contents of the\n"
      "cache and may take a long time for large caches.\n", cmd_check}, {
  "clear",
      "Clear the entire cache",
      "CLEAR\n"
      "\n"
      "FORMAT: clear\n"
      "\n"
      "Clear the entire cache.  All data in the cache is\n"
      "lost and the cache is reconfigured based on the current\n"
      "description of database sizes and available storage.\n", cmd_clear}, {
  "clear_cache",
      "Clear the document cache",
      "CLEAR_CACHE\n"
      "\n"
      "FORMAT: clear_cache\n"
      "\n"
      "Clear the document cache.  All documents in the cache are\n"
      "lost and the cache is reconfigured based on the current\n"
      "description of database sizes and available storage.\n", cmd_clear}, {
  "clear_hostdb",
      "Clear the hostdb cache",
      "CLEAR_HOSTDB\n"
      "\n"
      "FORMAT: clear_hostdb\n"
      "\n" "Clear the entire hostdb cache.  All host name resolution\n" "information is lost.\n", cmd_clear}, {
"help",
      "Obtain a short description of a command (e.g. 'help clear')",
      "HELP\n"
      "\n"
      "FORMAT: help [command_name]\n"
      "\n"
      "EXAMPLES: help help\n"
      "          help commit\n" "\n" "Provide a short description of a command (like this).\n", cmd_help},};

static int
cmd_index(char *p)
{
  p += strspn(p, " \t");
  for (unsigned c = 0; c < countof(commands); c++) {
    const char *l = commands[c].n;
    while (l) {
      const char *s = strchr(l, '/');
      char *e = strpbrk(p, " \t\n");
      int len = s ? s - l : strlen(l);
      int lenp = e ? e - p : strlen(p);
      if ((len == lenp) && !strncasecmp(p, l, len))
        return c;
      l = s ? s + 1 : 0;
    }
  }
  return -1;
}

static int
cmd_help(char *cmd)
{
  (void) cmd;
  printf("HELP\n\n");
  cmd = skip(cmd, true);
  if (!cmd) {
    for (unsigned i = 0; i < countof(commands); i++) {
      printf("%15s  %s\n", commands[i].n, commands[i].d);
    }
  } else {
    int i;
    if ((i = cmd_index(cmd)) < 0) {
      printf("\nno help found for: %s\n", cmd);
      return CMD_FAILED;
    }
    printf("Help for: %s\n\n", commands[i].n);
    printf("%s", commands[i].h);
  }
  return CMD_OK;
}

static void
check_fd_limit()
{
  int fds_throttle = -1;
  TS_ReadConfigInteger(fds_throttle, "proxy.config.net.connections_throttle");
  if (fds_throttle > fds_limit + THROTTLE_FD_HEADROOM) {
    int new_fds_throttle = fds_limit - THROTTLE_FD_HEADROOM;
    if (new_fds_throttle < 1)
      MachineFatal("too few file descritors (%d) available", fds_limit);
    char msg[256];
    snprintf(msg, sizeof(msg), "connection throttle too high, "
             "%d (throttle) + %d (internal use) > %d (file descriptor limit), "
             "using throttle of %d", fds_throttle, THROTTLE_FD_HEADROOM, fds_limit, new_fds_throttle);
    SignalWarning(MGMT_SIGNAL_SYSTEM_ERROR, msg);
  }
}

//
// Command mode
//
static int
cmd_mode()
{
  if (*command_string) {
    int c = cmd_index(command_string);
    if (c >= 0) {
      return commands[c].f(command_string);
    } else {
      Warning("unrecognized command: '%s'", command_string);
      return CMD_FAILED;        // in error
    }
  } else {
    printf("\n");
    printf("WARNING\n");
    printf("\n");
    printf("The interactive command mode no longer exists.\n");
    printf("Use '-C <command>' to execute a command from the shell prompt.\n");
    printf("For example: 'traffic_server -C clear' will clear the cache.\n");
    return 1;
  }
}

#ifdef UNUSED_FUNCTION
static void
check_for_root_uid()
{
  if ((getuid() == 0) || (geteuid() == 0)) {
    ProcessFatal("Traffic Server must not be run as root");
  }
}
#endif

static int
set_core_size(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
              RecData data, void * /* opaque_token ATS_UNUSED */)
{
  RecInt size = data.rec_int;
  struct rlimit lim;
  bool failed = false;

  if (getrlimit(RLIMIT_CORE, &lim) < 0) {
    failed = true;
  } else {
    if (size < 0) {
      lim.rlim_cur = lim.rlim_max;
    } else {
      lim.rlim_cur = (rlim_t) size;
    }
    if (setrlimit(RLIMIT_CORE, &lim) < 0) {
      failed = true;
    }
    enable_core_file_p = size != 0;
    EnableCoreFile(enable_core_file_p);
  }

  if (failed == true) {
    Warning("Failed to set Core Limit : %s", strerror(errno));
  }
  return 0;
}

static void
init_core_size()
{
  bool found;
  RecInt coreSize;
  found = (RecGetRecordInt("proxy.config.core_limit", &coreSize) == REC_ERR_OKAY);

  if (found == false) {
    Warning("Unable to determine core limit");
  } else {
    RecData rec_temp;
    rec_temp.rec_int = coreSize;
    set_core_size(NULL, RECD_INT, rec_temp, NULL);
    found = (TS_RegisterConfigUpdateFunc("proxy.config.core_limit", set_core_size, NULL) == REC_ERR_OKAY);

    ink_assert(found);
  }
}

static void
adjust_sys_settings(void)
{
#if defined(linux)
  struct rlimit lim;
  int mmap_max = -1;
  int fds_throttle = -1;

  // TODO: I think we might be able to get rid of this?
  TS_ReadConfigInteger(mmap_max, "proxy.config.system.mmap_max");
  if (mmap_max >= 0)
    ats_mallopt(ATS_MMAP_MAX, mmap_max);

  TS_ReadConfigInteger(fds_throttle, "proxy.config.net.connections_throttle");

  if (!getrlimit(RLIMIT_NOFILE, &lim)) {
    if (fds_throttle > (int) (lim.rlim_cur + THROTTLE_FD_HEADROOM)) {
      lim.rlim_cur = (lim.rlim_max = (rlim_t) fds_throttle);
      if (!setrlimit(RLIMIT_NOFILE, &lim) && !getrlimit(RLIMIT_NOFILE, &lim)) {
        fds_limit = (int) lim.rlim_cur;
	syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)",RLIMIT_NOFILE, (int)lim.rlim_cur, (int)lim.rlim_max);
      }
    }
  }

  ink_max_out_rlimit(RLIMIT_STACK,true,true);
  ink_max_out_rlimit(RLIMIT_DATA,true,true);
  ink_max_out_rlimit(RLIMIT_FSIZE, true, false);
#ifdef RLIMIT_RSS
  ink_max_out_rlimit(RLIMIT_RSS,true,true);
#endif

#endif  // linux check
}

struct ShowStats: public Continuation
{
#ifdef ENABLE_TIME_TRACE
  FILE *fp;
#endif
  int cycle;
  int64_t last_cc;
  int64_t last_rb;
  int64_t last_w;
  int64_t last_r;
  int64_t last_wb;
  int64_t last_nrb;
  int64_t last_nw;
  int64_t last_nr;
  int64_t last_nwb;
  int64_t last_p;
  int64_t last_o;
  int mainEvent(int event, Event * e)
  {
    (void) event;
    (void) e;
    if (!(cycle++ % 24))
      printf("r:rr w:ww r:rbs w:wbs open polls\n");
    ink_statval_t sval, cval;

    NET_READ_DYN_SUM(net_calls_to_readfromnet_stat, sval);
    int64_t d_rb = sval - last_rb;
    last_rb += d_rb;
    NET_READ_DYN_SUM(net_calls_to_readfromnet_afterpoll_stat, sval);
    int64_t d_r = sval - last_r;
    last_r += d_r;

    NET_READ_DYN_SUM(net_calls_to_writetonet_stat, sval);
    int64_t d_wb = sval - last_wb;
    last_wb += d_wb;
    NET_READ_DYN_SUM(net_calls_to_writetonet_afterpoll_stat, sval);
    int64_t d_w = sval - last_w;
    last_w += d_w;

    NET_READ_DYN_STAT(net_read_bytes_stat, sval, cval);
    int64_t d_nrb = sval - last_nrb;
    last_nrb += d_nrb;
    int64_t d_nr = cval - last_nr;
    last_nr += d_nr;

    NET_READ_DYN_STAT(net_write_bytes_stat, sval, cval);
    int64_t d_nwb = sval - last_nwb;
    last_nwb += d_nwb;
    int64_t d_nw = cval - last_nw;
    last_nw += d_nw;

    NET_READ_GLOBAL_DYN_SUM(net_connections_currently_open_stat, sval);
    int64_t d_o = sval;

    NET_READ_DYN_STAT(net_handler_run_stat, sval, cval);
    int64_t d_p = cval - last_p;
    last_p += d_p;
    printf("%" PRId64 ":%" PRId64 " %" PRId64 ":%" PRId64 " %" PRId64 ":%" PRId64 " %" PRId64 ":%" PRId64 " %" PRId64 " %" PRId64 "\n",
           d_rb, d_r, d_wb, d_w, d_nrb, d_nr, d_nwb, d_nw, d_o, d_p);
#ifdef ENABLE_TIME_TRACE
    int i;
    fprintf(fp, "immediate_events_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++)
    {
      if ((i % 10) == 0)
        fprintf(fp, "\n");
      fprintf(fp, "%5d ", immediate_events_time_dist[i]);
    }
    fprintf(fp, "\ncnt_immediate_events=%d\n", cnt_immediate_events);

    fprintf(fp, "cdb_callback_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++) {
      if ((i % 10) == 0)
        fprintf(fp, "\n");
      fprintf(fp, "%5d ", cdb_callback_time_dist[i]);
    }
    fprintf(fp, "\ncdb_cache_callbacks=%d\n", cdb_cache_callbacks);

    fprintf(fp, "callback_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++) {
      if ((i % 10) == 0)
        printf("\n");
      fprintf(fp, "%5d ", callback_time_dist[i]);
    }
    fprintf(fp, "\ncache_callbacks=%d\n", cache_callbacks);

    fprintf(fp, "rmt_callback_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++) {
      if ((i % 10) == 0)
        fprintf(fp, "\n");
      fprintf(fp, "%5d ", rmt_callback_time_dist[i]);
    }
    fprintf(fp, "\nrmt_cache_callbacks=%d\n", rmt_cache_callbacks);

    fprintf(fp, "inmsg_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++) {
      if ((i % 10) == 0)
        fprintf(fp, "\n");
      fprintf(fp, "%5d ", inmsg_time_dist[i]);
    }
    fprintf(fp, "\ninmsg_events=%d\n", inmsg_events);

    fprintf(fp, "open_delay_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++) {
      if ((i % 10) == 0)
        fprintf(fp, "\n");
      fprintf(fp, "%5d ", open_delay_time_dist[i]);
    }
    fprintf(fp, "\nopen_delay_events=%d\n", open_delay_events);

    fprintf(fp, "cluster_send_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++) {
      if ((i % 10) == 0)
        fprintf(fp, "\n");
      fprintf(fp, "%5d ", cluster_send_time_dist[i]);
    }
    fprintf(fp, "\ncluster_send_events=%d\n", cluster_send_events);
    fflush(fp);
#endif
    return EVENT_CONT;
  }
ShowStats():Continuation(NULL),
    cycle(0),
    last_cc(0),
    last_rb(0),
    last_w(0), last_r(0), last_wb(0), last_nrb(0), last_nw(0), last_nr(0), last_nwb(0), last_p(0), last_o(0) {
    SET_HANDLER(&ShowStats::mainEvent);
#ifdef ENABLE_TIME_TRACE
    fp = fopen("./time_trace.out", "a");
#endif

  }
};


// TODO: How come this is never used ??
static int syslog_facility = LOG_DAEMON;

// static void syslog_log_configure()
//
//   Reads the syslog configuration variable
//     and sets the global integer for the
//     facility and calls open log with the
//     new facility
//
static void
syslog_log_configure()
{
  char *facility_str = NULL;
  int facility;

  TS_ReadConfigStringAlloc(facility_str, "proxy.config.syslog_facility");

  if (facility_str == NULL || (facility = facility_string_to_int(facility_str)) < 0) {
    syslog(LOG_WARNING, "Bad or missing syslog facility.  " "Defaulting to LOG_DAEMON");
  } else {
    syslog_facility = facility;
    closelog();
    openlog("traffic_server", LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility);
  }
  // TODO: Not really, what's up with this?
  Debug("server", "Setting syslog facility to %d\n", syslog_facility);
  ats_free(facility_str);
}

static void
check_system_constants()
{
}

static void
init_http_header()
{
  url_init();
  mime_init();
  http_init();
}

struct AutoStopCont: public Continuation
{
  int mainEvent(int event, Event * e)
  {
    (void) event;
    (void) e;
    _exit(0);
    return 0;
  }
  AutoStopCont():Continuation(new_ProxyMutex())
  {
    SET_HANDLER(&AutoStopCont::mainEvent);
  }
};

static void
run_AutoStop()
{
  if (getenv("PROXY_AUTO_EXIT"))
    eventProcessor.schedule_in(NEW(new AutoStopCont), HRTIME_SECONDS(atoi(getenv("PROXY_AUTO_EXIT"))));
}

#if TS_HAS_TESTS
struct RegressionCont: public Continuation
{
  int initialized;
  int waits;
  int started;
  int mainEvent(int event, Event * e)
  {
    (void) event;
    (void) e;
    int res = 0;
    if (!initialized && (cacheProcessor.IsCacheEnabled() != CACHE_INITIALIZED))
    {
      printf("Regression waiting for the cache to be ready... %d\n", ++waits);
      return EVENT_CONT;
    }
    char *rt = (char *) (regression_test[0] == 0 ? "" : regression_test);
    if (!initialized && RegressionTest::run(rt) == REGRESSION_TEST_INPROGRESS) {
      initialized = 1;
      return EVENT_CONT;
    }
    if ((res = RegressionTest::check_status()) == REGRESSION_TEST_INPROGRESS)
      return EVENT_CONT;
    fprintf(stderr, "REGRESSION_TEST DONE: %s\n", regression_status_string(res));
    _exit(res == REGRESSION_TEST_PASSED ? 0 : 1);
    return EVENT_CONT;
  }
RegressionCont():Continuation(new_ProxyMutex()), initialized(0), waits(0), started(0) {
    SET_HANDLER(&RegressionCont::mainEvent);
  }
};

static void
run_RegressionTest()
{
  if (regression_level)
    eventProcessor.schedule_every(NEW(new RegressionCont), HRTIME_SECONDS(1));
}
#endif //TS_HAS_TESTS


static void
chdir_root()
{
  const char * prefix = Layout::get()->prefix;

  if (chdir(prefix) < 0) {
    fprintf(stderr,"unable to change to root directory \"%s\" [%d '%s']\n",
            prefix, errno, strerror(errno));
    fprintf(stderr," please set correct path in env variable TS_ROOT \n");
    _exit(1);
  } else {
    printf("[TrafficServer] using root directory '%s'\n", prefix);
  }
}


static int
getNumSSLThreads(void)
{
  int num_of_ssl_threads = 0;

  // Set number of ssl threads equal to num of processors if
  // SSL is enabled so it will scale properly. If SSL is not
  // enabled, leave num of ssl threads one, incase a remap rule
  // requires traffic server to act as an ssl client.
  if (HttpProxyPort::hasSSL()) {
    int config_num_ssl_threads = 0;

    TS_ReadConfigInteger(config_num_ssl_threads, "proxy.config.ssl.number.threads");

    if (config_num_ssl_threads != 0) {
      num_of_ssl_threads = config_num_ssl_threads;
    } else {
      float autoconfig_scale = 1.5;

      TS_ReadConfigFloat(autoconfig_scale, "proxy.config.exec_thread.autoconfig.scale");
      num_of_ssl_threads = (int)((float)ink_number_of_processors() * autoconfig_scale);

      // Last resort
      if (num_of_ssl_threads <= 0)
        num_of_ssl_threads = config_num_ssl_threads * 2;
    }
  }

  return num_of_ssl_threads;
}

static int
adjust_num_of_net_threads(int nthreads)
{
  float autoconfig_scale = 1.0;
  int nth_auto_config = 1;
  int num_of_threads_tmp = 1;

  TS_ReadConfigInteger(nth_auto_config, "proxy.config.exec_thread.autoconfig");

  Debug("threads", "initial number of net threads is %d", nthreads);
  Debug("threads", "net threads auto-configuration %s", nth_auto_config ? "enabled" : "disabled");

  if (!nth_auto_config) {
    TS_ReadConfigInteger(num_of_threads_tmp, "proxy.config.exec_thread.limit");

    if (num_of_threads_tmp <= 0) {
      num_of_threads_tmp = 1;
    } else if (num_of_threads_tmp > MAX_EVENT_THREADS) {
      num_of_threads_tmp = MAX_EVENT_THREADS;
    }

    nthreads = num_of_threads_tmp;
  } else {                      /* autoconfig is enabled */
    num_of_threads_tmp = nthreads;
    TS_ReadConfigFloat(autoconfig_scale, "proxy.config.exec_thread.autoconfig.scale");
    num_of_threads_tmp = (int) ((float) num_of_threads_tmp * autoconfig_scale);

    if (num_of_threads_tmp) {
      nthreads = num_of_threads_tmp;
    }

    if (unlikely(num_of_threads_tmp > MAX_EVENT_THREADS)) {
      num_of_threads_tmp = MAX_EVENT_THREADS;
    }
  }

  if (unlikely(nthreads <= 0)) {      /* impossible case -just for protection */
    Warning("number of net threads must be greater than 0, resetting to 1");
    nthreads = 1;
  }

  Debug("threads", "adjusted number of net threads is %d", nthreads);
  return nthreads;
}

/**
 * Change the uid and gid to what is in the passwd entry for supplied user name.
 * @param user User name in the passwd file to change the uid and gid to.
 */
static void
change_uid_gid(const char *user)
{
  struct passwd pwbuf;
  struct passwd *pwbufp = NULL;
#if defined(freebsd) // TODO: investigate sysconf(_SC_GETPW_R_SIZE_MAX)) failure
  long buflen = 1024; // or 4096?
#else
  long buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
#endif
  if (buflen < 0) {
    ink_fatal_die("sysconf() failed for _SC_GETPW_R_SIZE_MAX");
  }

  char *buf = (char *)ats_malloc(buflen);

  if (0 != geteuid() && 0 == getuid())
    ATS_UNUSED_RETURN(seteuid(0)); // revert euid if possible.
  if (0 != geteuid()) {
    // Not root so can't change user ID. Logging isn't operational yet so
    // we have to write directly to stderr. Perhaps this should be fatal?
    fprintf(stderr,
          "Can't change user to '%s' because running with effective uid=%d\n",
          user, geteuid());
  }
  else {
    if (user[0] == '#') {
      // numeric user notation
      uid_t uid = (uid_t)atoi(&user[1]);
      getpwuid_r(uid, &pwbuf, buf, buflen, &pwbufp);
    }
    else {
      // read the entry from the passwd file
      getpwnam_r(user, &pwbuf, buf, buflen, &pwbufp);
    }
    // check to see if we found an entry
    if (pwbufp == NULL) {
      ink_fatal_die("Can't find entry in password file for user: %s", user);
    }
#if !defined (BIG_SECURITY_HOLE)
    if (pwbuf.pw_uid == 0) {
      ink_fatal_die("Trafficserver has not been designed to serve pages while\n"
        "\trunning as root.  There are known race conditions that\n"
        "\twill allow any local user to read any file on the system.\n"
        "\tIf you still desire to serve pages as root then\n"
        "\tadd -DBIG_SECURITY_HOLE to the CFLAGS env variable\n"
        "\tand then rebuild the server.\n"
        "\tIt is strongly suggested that you instead modify the\n"
        "\tproxy.config.admin.user_id  directive in your\n"
        "\trecords.config file to list a non-root user.\n");
    }
#endif
    // change the gid to passwd entry if we are not already running as that gid
    if (getgid() != pwbuf.pw_gid) {
      if (setgid(pwbuf.pw_gid) != 0) {
        ink_fatal_die("Can't change group to user: %s, gid: %d",
                      user, pwbuf.pw_gid);
      }
    }
    // change the uid to passwd entry if we are not already running as that uid
    if (getuid() != pwbuf.pw_uid) {
      if (setuid(pwbuf.pw_uid) != 0) {
        ink_fatal_die("Can't change uid to user: %s, uid: %d",
                      user, pwbuf.pw_uid);
      }
    }
  }
  ats_free(buf);

  // Ugly but this gets reset when the process user ID is changed so
  // it must be udpated here.
  EnableCoreFile(enable_core_file_p);
}

//
// Main
//

int
main(int /* argc ATS_UNUSED */, char **argv)
{
#if TS_HAS_PROFILER
  ProfilerStart("/tmp/ts.prof");
#endif
  bool admin_user_p = false;

#if defined(DEBUG) && defined(HAVE_MCHECK_PEDANTIC)
  mcheck_pedantic(NULL);
#endif

  pcre_malloc = ats_malloc;
  pcre_free = ats_free;

  // Verify system dependent 'constants'
  check_system_constants();

  // Define the version info
  appVersionInfo.setup(PACKAGE_NAME,"traffic_server", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();
  chdir_root(); // change directory to the install root of traffic server.

  process_args(argument_descriptions, countof(argument_descriptions), argv);

  // Check for version number request
  if (version_flag) {
    fprintf(stderr, "%s\n", appVersionInfo.FullVersionInfoStr);
    _exit(0);
  }

  // Set stdout/stdin to be unbuffered
  setbuf(stdout, NULL);
  setbuf(stdin, NULL);

  // Set new debug output level (from command line arg)
  // Only for debug purposes. We should do it as early as possible.
  ink_set_dprintf_level(cmd_line_dprintf_level);

  // Bootstrap syslog.  Since we haven't read records.config
  //   yet we do not know where
  openlog("traffic_server", LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);

  // Setup Diags temporary to allow librecords to be initialized.
  // We will re-configure Diags again with proper configurations after
  // librecords initialized. This is needed because:
  //   - librecords needs diags to initialize
  //   - diags needs to read some configuration records to initial
  // We cannot mimic whatever TM did (start Diag, init. librecords, and
  // re-start Diag completely) because at initialize, TM only has 1 thread.
  // In TS, some threads have already created, so if we delete Diag and
  // re-start it again, TS will crash.
  diagsConfig = NEW(new DiagsConfig(DIAGS_LOG_FILENAME, error_tags, action_tags, false));
  diags = diagsConfig->diags;
  diags->prefix_str = "Server ";
  if (is_debug_tag_set("diags"))
    diags->dump();

  // Local process manager
  initialize_process_manager();

  // Ensure only one copy of traffic server is running
  check_lockfile();

  // Set the core limit for the process
  init_core_size();
  init_system();

  // Adjust system and process settings
  adjust_sys_settings();

  // Restart syslog now that we have configuration info
  syslog_log_configure();

  if (!num_accept_threads)
    TS_ReadConfigInteger(num_accept_threads, "proxy.config.accept_threads");

  if (!num_task_threads)
    TS_ReadConfigInteger(num_task_threads, "proxy.config.task_threads");

  xptr<char> user(MAX_LOGIN + 1);

  *user = '\0';
  admin_user_p = ((REC_ERR_OKAY == TS_ReadConfigString(user, "proxy.config.admin.user_id", MAX_LOGIN)) &&
                  (*user != '\0') && (0 != strcmp(user, "#-1")));

# if TS_USE_POSIX_CAP
  // Change the user of the process.
  // Do this before we start threads so we control the user id of the
  // threads (rather than have it change asynchronously during thread
  // execution). We also need to do this before we fiddle with capabilities
  // as those are thread local and if we change the user id it will
  // modify the capabilities in other threads, breaking things.
  if (admin_user_p) {
    PreserveCapabilities();
    change_uid_gid(user);
    RestrictCapabilities();
  }
# endif

  // Can't generate a log message yet, do that right after Diags is
  // setup.

  // This call is required for win_9xMe
  //without this this_ethread() is failing when
  //start_HttpProxyServer is called from main thread
  Thread *main_thread = NEW(new EThread);
  main_thread->set_specific();

  // Re-initialize diagsConfig based on records.config configuration
  if (diagsConfig) {
    RecDebugOff();
    delete(diagsConfig);
  }
  diagsConfig = NEW(new DiagsConfig(DIAGS_LOG_FILENAME, error_tags, action_tags, true));
  diags = diagsConfig->diags;
  RecSetDiags(diags);
  diags->prefix_str = "Server ";
  if (is_debug_tag_set("diags"))
    diags->dump();
# if TS_USE_POSIX_CAP
  if (is_debug_tag_set("server"))
    DebugCapabilities("server"); // Can do this now, logging is up.
# endif

  // Check if we should do mlockall()
#if defined(MCL_FUTURE)
  int mlock_flags = 0;
  TS_ReadConfigInteger(mlock_flags, "proxy.config.mlock_enabled");

  if (mlock_flags == 2) {
    if (0 != mlockall(MCL_CURRENT | MCL_FUTURE))
      Warning("Unable to mlockall() on startup");
    else
      Debug("server", "Successfully called mlockall()");
  }
#endif

  // Check for core file
  if (core_file[0] != '\0') {
    process_core(core_file);
    _exit(0);
  }

  // We need to do this early so we can initialize the Machine
  // singleton, which depends on configuration values loaded in this.
  // We want to initialize Machine as early as possible because it
  // has other dependencies. Hopefully not in init_HttpProxyServer().
  HttpConfig::startup();
  /* Set up the machine with the outbound address if that's set,
     or the inbound address if set, otherwise let it default.
  */
  IpEndpoint machine_addr;
  ink_zero(machine_addr);
  if (HttpConfig::m_master.outbound_ip4.isValid())
    machine_addr.assign(HttpConfig::m_master.outbound_ip4);
  else if (HttpConfig::m_master.outbound_ip6.isValid())
    machine_addr.assign(HttpConfig::m_master.outbound_ip6);
  else if (HttpConfig::m_master.inbound_ip4.isValid())
    machine_addr.assign(HttpConfig::m_master.inbound_ip4);
  else if (HttpConfig::m_master.inbound_ip6.isValid())
    machine_addr.assign(HttpConfig::m_master.inbound_ip6);
  Machine::init(0, &machine_addr.sa);

  // pmgmt->start() must occur after initialization of Diags but
  // before calling RecProcessInit()

  TS_ReadConfigInteger(res_track_memory, "proxy.config.res_track_memory");

  init_http_header();

  // Sanity checks
  check_fd_limit();
  command_flag = command_flag || *command_string;

  // Set up store
  if (!command_flag && initialize_store())
    ProcessFatal("unable to initialize storage, (Re)Configuration required\n");

  // Alter the frequecies at which the update threads will trigger
#define SET_INTERVAL(scope, name, var) do { \
  RecInt tmpint; \
  Debug("statsproc", "Looking for %s\n", name); \
  if (RecGetRecordInt(name, &tmpint) == REC_ERR_OKAY) { \
    Debug("statsproc", "Found %s\n", name); \
    scope##_set_##var(tmpint); \
  } \
} while(0)
  SET_INTERVAL(RecProcess, "proxy.config.config_update_interval_ms", config_update_interval_ms);
  SET_INTERVAL(RecProcess, "proxy.config.raw_stat_sync_interval_ms", raw_stat_sync_interval_ms);
  SET_INTERVAL(RecProcess, "proxy.config.remote_sync_interval_ms", remote_sync_interval_ms);

  // Initialize the stat pages manager
  statPagesManager.init();

  //////////////////////////////////////////////////////////////////////
  // Determine if Cache Clustering is enabled, since the transaction
  // on a thread changes require special consideration to allow
  // minimial Cache Clustering functionality.
  //////////////////////////////////////////////////////////////////////
  RecInt cluster_type;
  cache_clustering_enabled = 0;

  if (RecGetRecordInt("proxy.local.cluster.type", &cluster_type) == REC_ERR_OKAY) {
    if (cluster_type == 1)
      cache_clustering_enabled = 1;
  }
  Note("cache clustering %s", cache_clustering_enabled ? "enabled" : "disabled");

  // Initialize New Stat system
  initialize_all_global_stats();

  num_of_net_threads = adjust_num_of_net_threads(num_of_net_threads);

  size_t stacksize;
  REC_ReadConfigInteger(stacksize, "proxy.config.thread.default.stacksize");

  // This has some special semantics, in that providing this configuration on
  // command line has higher priority than what is set in records.config.
  if (-1 != poll_timeout) {
    net_config_poll_timeout = poll_timeout;
  } else {
    REC_ReadConfigInteger(net_config_poll_timeout, "proxy.config.net.poll_timeout");
  }

  // This shouldn't happen, but lets make sure we run somewhat reasonable.
  if (net_config_poll_timeout < 0) {
    net_config_poll_timeout = 10; // Default value for all platform.
  }

  ink_event_system_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  ink_net_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  ink_aio_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  ink_cache_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  ink_hostdb_init(makeModuleVersion(HOSTDB_MODULE_MAJOR_VERSION, HOSTDB_MODULE_MINOR_VERSION , PRIVATE_MODULE_HEADER));
  ink_dns_init(makeModuleVersion(HOSTDB_MODULE_MAJOR_VERSION, HOSTDB_MODULE_MINOR_VERSION , PRIVATE_MODULE_HEADER));
  ink_split_dns_init(makeModuleVersion(1, 0, PRIVATE_MODULE_HEADER));
  eventProcessor.start(num_of_net_threads, stacksize);

  int num_remap_threads = 0;
  TS_ReadConfigInteger(num_remap_threads, "proxy.config.remap.num_remap_threads");
  if (num_remap_threads < 1)
    num_remap_threads = 0;

  if (num_remap_threads > 0) {
    Note("using the new remap processor system with %d threads", num_remap_threads);
    remapProcessor.setUseSeparateThread();
  }

  init_signals2();
  // log initialization moved down

  if (command_flag) {
    int cmd_ret = cmd_mode();

    if (cmd_ret != CMD_IN_PROGRESS) {
      if (cmd_ret >= 0)
        _exit(0);               // everything is OK
      else
        _exit(1);               // in error
    }
  } else {
    remapProcessor.start(num_remap_threads, stacksize);
    RecProcessStart();
    initCacheControl();
    initCongestionControl();
    IpAllow::startup();
    ParentConfig::startup();
#ifdef SPLIT_DNS
    SplitDNSConfig::startup();
#endif

    // Load HTTP port data. getNumSSLThreads depends on this.
    if (!HttpProxyPort::loadValue(http_accept_port_descriptor))
      HttpProxyPort::loadConfig();
    HttpProxyPort::loadDefaultIfEmpty();

    if (!accept_mss)
      TS_ReadConfigInteger(accept_mss, "proxy.config.net.sock_mss_in");

    NetProcessor::accept_mss = accept_mss;
    netProcessor.start(0, stacksize);

    sslNetProcessor.start(getNumSSLThreads(), stacksize);

    dnsProcessor.start(0, stacksize);
    if (hostDBProcessor.start() < 0)
      SignalWarning(MGMT_SIGNAL_SYSTEM_ERROR, "bad hostdb or storage configuration, hostdb disabled");
    clusterProcessor.init();

    // initialize logging (after event and net processor)
    Log::init(remote_management_flag ? 0 : Log::NO_REMOTE_MANAGEMENT);

    // Init plugins as soon as logging is ready.
    plugin_init();        // plugin.config
    pmgmt->registerPluginCallbacks(global_config_cbs);

    cacheProcessor.set_after_init_callback(&CB_After_Cache_Init);
    cacheProcessor.start();

    // UDP net-threads are turned off by default.
    if (!num_of_udp_threads)
      TS_ReadConfigInteger(num_of_udp_threads, "proxy.config.udp.threads");
    if (num_of_udp_threads)
      udpNet.start(num_of_udp_threads, stacksize);

    //acc.init();
    //if (auto_clear_authdb_flag)
     // acc.clear_cache();
    //acc.start();
    // pmgmt initialization moved up, needed by RecProcessInit
    //pmgmt->start();
    start_stats_snap();

    // Initialize Response Body Factory
    body_factory = NEW(new HttpBodyFactory);

    // Start IP to userName cache processor used
    // by RADIUS and FW1 plug-ins.
    //ipToUserNameCacheProcessor.start();

    // Initialize the system for SIMPLE support
    //  Simple::init();

    // Initialize the system for RAFT support
    // All this is handled by plugin support code
    //   Raft::init();

    // Continuation Statistics Dump
    if (show_statistics)
      eventProcessor.schedule_every(NEW(new ShowStats), HRTIME_SECONDS(show_statistics), ET_CALL);


    //////////////////////////////////////
    // main server logic initiated here //
    //////////////////////////////////////

    transformProcessor.start();

    init_HttpProxyServer(num_accept_threads);

    int http_enabled = 1;
    TS_ReadConfigInteger(http_enabled, "proxy.config.http.enabled");

    if (http_enabled) {
      int icp_enabled = 0;
      TS_ReadConfigInteger(icp_enabled, "proxy.config.icp.enabled");

      // call the ready hooks before we start accepting connections.
      APIHook* hook = lifecycle_hooks->get(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK);
      while (hook) {
        hook->invoke(TS_EVENT_LIFECYCLE_PORTS_INITIALIZED, NULL);
        hook = hook->next();
      }

      int delay_p = 0;
      TS_ReadConfigInteger(delay_p, "proxy.config.http.wait_for_cache");

      // Delay only if config value set and flag value is zero
      // (-1 => cache already initialized)
      if (delay_p && ink_atomic_cas(&delay_listen_for_cache_p, 0, 1)) {
        Debug("http_listen", "Delaying listen, waiting for cache initialization");
      } else {
        start_HttpProxyServer(); // PORTS_READY_HOOK called from in here
      }
      if (icp_enabled)
        icpProcessor.start();
    }

    // "Task" processor, possibly with its own set of task threads
    tasksProcessor.start(num_task_threads, stacksize);

    int back_door_port = NO_FD;
    TS_ReadConfigInteger(back_door_port, "proxy.config.process_manager.mgmt_port");
    if (back_door_port != NO_FD)
      start_HttpProxyServerBackDoor(back_door_port, num_accept_threads > 0 ? 1 : 0); // One accept thread is enough

    if (netProcessor.socks_conf_stuff->accept_enabled) {
      start_SocksProxy(netProcessor.socks_conf_stuff->accept_port);
    }

    ///////////////////////////////////////////
    // Initialize Scheduled Update subsystem
    ///////////////////////////////////////////
    updateManager.start();

    pmgmt->registerMgmtCallback(MGMT_EVENT_SHUTDOWN, mgmt_restart_shutdown_callback, NULL);
    pmgmt->registerMgmtCallback(MGMT_EVENT_RESTART, mgmt_restart_shutdown_callback, NULL);

    // Callback for various storage commands. These all go to the same function so we
    // pass the event code along so it can do the right thing. We cast that to <int> first
    // just to be safe because the value is a #define, not a typed value.
    pmgmt->registerMgmtCallback(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, mgmt_storage_device_cmd_callback, reinterpret_cast<void*>(static_cast<int>(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE)));

    // The main thread also becomes a net thread.
    ink_set_thread_name("[ET_NET 0]");

    Note("traffic server running");

#if TS_HAS_TESTS
    TransformTest::run();
    run_HostDBTest();
    //  run_SimpleHttp();
    run_RegressionTest();
#endif

    run_AutoStop();
  }

# if ! TS_USE_POSIX_CAP
  if (admin_user_p) {
    change_uid_gid(user);
    DebugCapabilities("server");
  }
# endif

  this_thread()->execute();
}


#if TS_HAS_TESTS
//////////////////////////////
// Unit Regresion Test Hook //
//////////////////////////////

#include "HdrTest.h"

REGRESSION_TEST(Hdrs) (RegressionTest * t, int atype, int *pstatus) {
  HdrTest ht;
  *pstatus = ht.go(t, atype);
  return;
}
#endif

static void *
mgmt_restart_shutdown_callback(void *, char *, int /* data_len ATS_UNUSED */)
{
  sync_cache_dir_on_shutdown();
  return NULL;
}

static void*
mgmt_storage_device_cmd_callback(void* data, char* arg, int len)
{
  // data is the device name to control
  CacheDisk* d = cacheProcessor.find_by_path(arg, len);
  // Actual command is in @a data.
  intptr_t cmd = reinterpret_cast<intptr_t>(data);

  if (d) {
    switch (cmd) {
    case MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE:
      Debug("server", "Marking %.*s offline", len, arg);
      cacheProcessor.mark_storage_offline(d);
      break;
    }
  }
  return NULL;
}
