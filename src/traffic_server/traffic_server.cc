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

#include "tscore/ink_platform.h"
#include "tscore/ink_sys_control.h"
#include "tscore/ink_args.h"
#include "tscore/ink_lockfile.h"
#include "tscore/ink_stack_trace.h"
#include "tscore/ink_syslog.h"
#include "tscore/hugepages.h"
#include "tscore/runroot.h"

#include "ts/ts.h" // This is sadly needed because of us using TSThreadInit() for some reason.

#include <syslog.h>
#include <algorithm>

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
#include "tscore/signals.h"
#include "P_EventSystem.h"
#include "P_Net.h"
#include "P_UDPNet.h"
#include "P_DNS.h"
#include "P_SplitDNS.h"
#include "P_HostDB.h"
#include "P_Cache.h"
#include "tscore/I_Layout.h"
#include "I_Machine.h"
#include "RecordsConfig.h"
#include "records/I_RecProcess.h"
#include "Transform.h"
#include "ProcessManager.h"
#include "ProxyConfig.h"
#include "HttpProxyServerMain.h"
#include "HttpBodyFactory.h"
#include "ProxySession.h"
#include "logging/Log.h"
#include "CacheControl.h"
#include "IPAllow.h"
#include "ParentSelection.h"
#include "HostStatus.h"
#include "MgmtUtils.h"
#include "StatPages.h"
#include "HTTP.h"
#include "HuffmanCodec.h"
#include "Plugin.h"
#include "DiagsConfig.h"
#include "CoreUtils.h"
#include "RemapConfig.h"
#include "RemapProcessor.h"
#include "I_Tasks.h"
#include "InkAPIInternal.h"
#include "HTTP2.h"
#include "tscore/ink_config.h"
#include "P_SSLSNI.h"
#include "P_SSLClientUtils.h"

#if TS_USE_QUIC == 1
#include "Http3.h"
#include "Http3Config.h"
#endif

#include "tscore/ink_cap.h"

#if TS_HAS_PROFILER
#include <gperftools/profiler.h>
#include <gperftools/heap-profiler.h>
#endif

//
// Global Data
//
#define DEFAULT_COMMAND_FLAG 0

#define DEFAULT_REMOTE_MANAGEMENT_FLAG 0
#define DIAGS_LOG_FILENAME "diags.log"

static const long MAX_LOGIN = ink_login_name_max();

static void mgmt_restart_shutdown_callback(ts::MemSpan<void>);
static void mgmt_drain_callback(ts::MemSpan<void>);
static void mgmt_storage_device_cmd_callback(int cmd, std::string_view const &arg);
static void mgmt_lifecycle_msg_callback(ts::MemSpan<void>);
static void init_ssl_ctx_callback(void *ctx, bool server);
static void load_ssl_file_callback(const char *ssl_file);
static void load_remap_file_callback(const char *remap_file);
static void task_threads_started_callback();

// We need these two to be accessible somewhere else now
int num_of_net_threads = ink_number_of_processors();
int num_accept_threads = 0;

static int num_of_udp_threads = 0;
static int num_task_threads   = 0;

static char *http_accept_port_descriptor;
int http_accept_file_descriptor = NO_FD;
static char core_file[255]      = "";
static bool enable_core_file_p  = false; // Enable core file dump?
int command_flag                = DEFAULT_COMMAND_FLAG;
int command_index               = -1;
bool command_valid              = false;
// Commands that have special processing / requirements.
static const char *CMD_VERIFY_CONFIG = "verify_config";
#if TS_HAS_TESTS
static char regression_test[1024] = "";
static int regression_list        = 0;
static int regression_level       = REGRESSION_TEST_NONE;
#endif
int auto_clear_hostdb_flag = 0;
extern int fds_limit;

static char command_string[512] = "";
static char conf_dir[512]       = "";
int remote_management_flag      = DEFAULT_REMOTE_MANAGEMENT_FLAG;
static char bind_stdout[512]    = "";
static char bind_stderr[512]    = "";

static char error_tags[1024]               = "";
static char action_tags[1024]              = "";
static int show_statistics                 = 0;
static inkcoreapi DiagsConfig *diagsConfig = nullptr;
HttpBodyFactory *body_factory              = nullptr;

static int accept_mss           = 0;
static int poll_timeout         = -1; // No value set.
static int cmd_disable_freelist = 0;
static bool signal_received[NSIG];

// 1: delay listen, wait for cache.
// 0: Do not delay, start listen ASAP.
// -1: cache is already initialized, don't delay.
static int delay_listen_for_cache_p;

AppVersionInfo appVersionInfo; // Build info for this application

static ArgumentDescription argument_descriptions[] = {
  {"net_threads", 'n', "Number of Net Threads", "I", &num_of_net_threads, "PROXY_NET_THREADS", nullptr},
  {"udp_threads", 'U', "Number of UDP Threads", "I", &num_of_udp_threads, "PROXY_UDP_THREADS", nullptr},
  {"accept_thread", 'a', "Use an Accept Thread", "T", &num_accept_threads, "PROXY_ACCEPT_THREAD", nullptr},
  {"accept_till_done", 'b', "Accept Till Done", "T", &accept_till_done, "PROXY_ACCEPT_TILL_DONE", nullptr},
  {"httpport", 'p', "Port descriptor for HTTP Accept", "S*", &http_accept_port_descriptor, "PROXY_HTTP_ACCEPT_PORT", nullptr},
  {"disable_freelist", 'f', "Disable the freelist memory allocator", "T", &cmd_disable_freelist, "PROXY_DPRINTF_LEVEL", nullptr},
  {"disable_pfreelist", 'F', "Disable the freelist memory allocator in ProxyAllocator", "T", &cmd_disable_pfreelist,
   "PROXY_DPRINTF_LEVEL", nullptr},
  {"maxRecords", 'm', "Max number of librecords metrics and configurations (default & minimum: 1600)", "I", &max_records_entries,
   "PROXY_MAX_RECORDS", nullptr},

#if TS_HAS_TESTS
  {"regression", 'R', "Regression Level (quick:1..long:3)", "I", &regression_level, "PROXY_REGRESSION", nullptr},
  {"regression_test", 'r', "Run Specific Regression Test", "S512", regression_test, "PROXY_REGRESSION_TEST", nullptr},
  {"regression_list", 'l', "List Regression Tests", "T", &regression_list, "PROXY_REGRESSION_LIST", nullptr},
#endif // TS_HAS_TESTS

#if TS_USE_DIAGS
  {"debug_tags", 'T', "Vertical-bar-separated Debug Tags", "S1023", error_tags, "PROXY_DEBUG_TAGS", nullptr},
  {"action_tags", 'B', "Vertical-bar-separated Behavior Tags", "S1023", action_tags, "PROXY_BEHAVIOR_TAGS", nullptr},
#endif

  {"interval", 'i', "Statistics Interval", "I", &show_statistics, "PROXY_STATS_INTERVAL", nullptr},
  {"remote_management", 'M', "Remote Management", "T", &remote_management_flag, "PROXY_REMOTE_MANAGEMENT", nullptr},
  {"command", 'C',
   "Maintenance Command to Execute\n"
   "      Commands: list, check, clear, clear_cache, clear_hostdb, verify_config, help",
   "S511", &command_string, "PROXY_COMMAND_STRING", nullptr},
  {"conf_dir", 'D', "config dir to verify", "S511", &conf_dir, "PROXY_CONFIG_CONFIG_DIR", nullptr},
  {"clear_hostdb", 'k', "Clear HostDB on Startup", "F", &auto_clear_hostdb_flag, "PROXY_CLEAR_HOSTDB", nullptr},
  {"clear_cache", 'K', "Clear Cache on Startup", "F", &cacheProcessor.auto_clear_flag, "PROXY_CLEAR_CACHE", nullptr},
  {"bind_stdout", '-', "Regular file to bind stdout to", "S512", &bind_stdout, "PROXY_BIND_STDOUT", nullptr},
  {"bind_stderr", '-', "Regular file to bind stderr to", "S512", &bind_stderr, "PROXY_BIND_STDERR", nullptr},
#if defined(linux)
  {"read_core", 'c', "Read Core file", "S255", &core_file, nullptr, nullptr},
#endif

  {"accept_mss", '-', "MSS for client connections", "I", &accept_mss, nullptr, nullptr},
  {"poll_timeout", 't', "poll timeout in milliseconds", "I", &poll_timeout, nullptr, nullptr},
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION(),
  RUNROOT_ARGUMENT_DESCRIPTION(),
};

struct AutoStopCont : public Continuation {
  int
  mainEvent(int /* event */, Event * /* e */)
  {
    TSSystemState::stop_ssl_handshaking();

    APIHook *hook = lifecycle_hooks->get(TS_LIFECYCLE_SHUTDOWN_HOOK);
    while (hook) {
      SCOPED_MUTEX_LOCK(lock, hook->m_cont->mutex, this_ethread());
      hook->invoke(TS_EVENT_LIFECYCLE_SHUTDOWN, nullptr);
      hook = hook->next();
    }

    pmgmt->stop();
    TSSystemState::shut_down_event_system();
    delete this;
    return EVENT_CONT;
  }

  AutoStopCont() : Continuation(new_ProxyMutex()) { SET_HANDLER(&AutoStopCont::mainEvent); }
};

class SignalContinuation : public Continuation
{
public:
  SignalContinuation() : Continuation(new_ProxyMutex())
  {
    end = snap = nullptr;
    SET_HANDLER(&SignalContinuation::periodic);
  }

  int
  periodic(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    if (signal_received[SIGUSR1]) {
      signal_received[SIGUSR1] = false;

      // TODO: TS-567 Integrate with debugging allocators "dump" features?
      ink_freelists_dump(stderr);
      ResourceTracker::dump(stderr);

      if (!end) {
        end = (char *)sbrk(0);
      }

      if (!snap) {
        snap = (char *)sbrk(0);
      }

      char *now = (char *)sbrk(0);
      Note("sbrk 0x%" PRIu64 " from first %" PRIu64 " from last %" PRIu64 "\n", (uint64_t)((ptrdiff_t)now),
           (uint64_t)((ptrdiff_t)(now - end)), (uint64_t)((ptrdiff_t)(now - snap)));
      snap = now;
    }

    if (signal_received[SIGUSR2]) {
      signal_received[SIGUSR2] = false;

      Debug("log", "received SIGUSR2, reloading traffic.out");

      // reload output logfile (file is usually called traffic.out)
      diags->set_std_output(StdStream::STDOUT, bind_stdout);
      diags->set_std_output(StdStream::STDERR, bind_stderr);
    }

    if (signal_received[SIGTERM] || signal_received[SIGINT]) {
      signal_received[SIGTERM] = false;
      signal_received[SIGINT]  = false;

      RecInt timeout = 0;
      if (RecGetRecordInt("proxy.config.stop.shutdown_timeout", &timeout) == REC_ERR_OKAY && timeout) {
        RecSetRecordInt("proxy.node.config.draining", 1, REC_SOURCE_DEFAULT);
        TSSystemState::drain(true);
        if (!remote_management_flag) {
          // Close listening sockets here only if TS is running standalone
          RecInt close_sockets = 0;
          if (RecGetRecordInt("proxy.config.restart.stop_listening", &close_sockets) == REC_ERR_OKAY && close_sockets) {
            stop_HttpProxyServer();
          }
        }
      }

      Debug("server", "received exit signal, shutting down in %" PRId64 "secs", timeout);

      // Shutdown in `timeout` seconds (or now if that is 0).
      eventProcessor.schedule_in(new AutoStopCont(), HRTIME_SECONDS(timeout));
    }

    return EVENT_CONT;
  }

private:
  const char *end;
  const char *snap;
};

class TrackerContinuation : public Continuation
{
public:
  int baseline_taken;
  int use_baseline;

  TrackerContinuation() : Continuation(new_ProxyMutex())
  {
    SET_HANDLER(&TrackerContinuation::periodic);
    use_baseline = 0;
    // TODO: ATS prefix all those environment stuff or
    //       even better use config since env can be
    //       different for parent and child process users.
    //
    if (getenv("MEMTRACK_BASELINE")) {
      use_baseline = 1;
    }

    baseline_taken = 0;
  }

  ~TrackerContinuation() override { mutex = nullptr; }
  int
  periodic(int event, Event * /* e ATS_UNUSED */)
  {
    if (event == EVENT_IMMEDIATE) {
      // rescheduled from periodic to immediate event
      // this is the indication to terminate this tracker.
      delete this;
      return EVENT_DONE;
    }
    if (use_baseline) {
      // TODO: TS-567 Integrate with debugging allocators "dump" features?
      ink_freelists_dump_baselinerel(stderr);
    } else {
      // TODO: TS-567 Integrate with debugging allocators "dump" features?
      ink_freelists_dump(stderr);
      ResourceTracker::dump(stderr);
    }
    if (!baseline_taken && use_baseline) {
      ink_freelists_snap_baseline();
      // TODO: TS-567 Integrate with debugging allocators "dump" features?
      baseline_taken = 1;
    }
    return EVENT_CONT;
  }
};

// This continuation is used to periodically check on diags.log, and rotate
// the logs if necessary
class DiagsLogContinuation : public Continuation
{
public:
  DiagsLogContinuation() : Continuation(new_ProxyMutex()) { SET_HANDLER(&DiagsLogContinuation::periodic); }
  int
  periodic(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    Debug("log", "in DiagsLogContinuation, checking on diags.log");

    // First, let us update the rolling config values for diagslog. We
    // do not need to update the config values for outputlog because
    // traffic_server never actually rotates outputlog. outputlog is always
    // rotated in traffic_manager. The reason being is that it is difficult
    // to send a notification from TS to TM, informing TM that outputlog has
    // been rolled. It is much easier sending a notification (in the form
    // of SIGUSR2) from TM -> TS.
    int diags_log_roll_int    = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_interval_sec");
    int diags_log_roll_size   = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_size_mb");
    int diags_log_roll_enable = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_enabled");
    diags->config_roll_diagslog((RollingEnabledValues)diags_log_roll_enable, diags_log_roll_int, diags_log_roll_size);

    if (diags->should_roll_diagslog()) {
      Note("Rolled %s", DIAGS_LOG_FILENAME);
    }
    return EVENT_CONT;
  }
};

class MemoryLimit : public Continuation
{
public:
  MemoryLimit() : Continuation(new_ProxyMutex())
  {
    memset(&_usage, 0, sizeof(_usage));
    SET_HANDLER(&MemoryLimit::periodic);
    RecRegisterStatInt(RECT_PROCESS, "proxy.process.traffic_server.memory.rss", static_cast<RecInt>(0), RECP_NON_PERSISTENT);
  }

  ~MemoryLimit() override { mutex = nullptr; }

  int
  periodic(int event, Event *e)
  {
    if (event == EVENT_IMMEDIATE) {
      // rescheduled from periodic to immediate event
      // this is the indication to terminate
      delete this;
      return EVENT_DONE;
    }

    // "reload" the setting, we don't do this often so not expensive
    _memory_limit = REC_ConfigReadInteger("proxy.config.memory.max_usage");
    _memory_limit = _memory_limit >> 10; // divide by 1024

    if (getrusage(RUSAGE_SELF, &_usage) == 0) {
      RecSetRecordInt("proxy.process.traffic_server.memory.rss", _usage.ru_maxrss << 10, REC_SOURCE_DEFAULT); // * 1024
      Debug("server", "memory usage - ru_maxrss: %ld memory limit: %" PRId64, _usage.ru_maxrss, _memory_limit);
      if (_memory_limit > 0) {
        if (_usage.ru_maxrss > _memory_limit) {
          if (net_memory_throttle == false) {
            net_memory_throttle = true;
            Debug("server", "memory usage exceeded limit - ru_maxrss: %ld memory limit: %" PRId64, _usage.ru_maxrss, _memory_limit);
          }
        } else {
          if (net_memory_throttle == true) {
            net_memory_throttle = false;
            Debug("server", "memory usage under limit - ru_maxrss: %ld memory limit: %" PRId64, _usage.ru_maxrss, _memory_limit);
          }
        }
      } else {
        // this feature has not been enabled
        Debug("server", "limiting connections based on memory usage has been disabled");
        e->cancel();
        delete this;
        return EVENT_DONE;
      }
    }
    return EVENT_CONT;
  }

private:
  int64_t _memory_limit = 0;
  struct rusage _usage;
};

void
set_debug_ip(const char *ip_string)
{
  if (ip_string) {
    diags->debug_client_ip.load(ip_string);
  } else {
    diags->debug_client_ip.invalidate();
  }
}

static int
update_debug_client_ip(const char * /*name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
                       void * /* data_type ATS_UNUSED */)
{
  set_debug_ip(data.rec_string);
  return 0;
}

static int
init_memory_tracker(const char *config_var, RecDataT /* type ATS_UNUSED */, RecData data, void * /* cookie ATS_UNUSED */)
{
  static Event *tracker_event = nullptr;
  Event *preE;
  int dump_mem_info_frequency = 0;

  // set tracker_event to NULL, and return previous value
  preE = ink_atomic_swap(&tracker_event, static_cast<Event *>(nullptr));

  if (config_var) {
    dump_mem_info_frequency = data.rec_int;
  } else {
    dump_mem_info_frequency = REC_ConfigReadInteger("proxy.config.dump_mem_info_frequency");
  }

  Debug("tracker", "init_memory_tracker called [%d]", dump_mem_info_frequency);

  if (preE) {
    eventProcessor.schedule_imm(preE->continuation, ET_CALL);
    preE->cancel();
  }

  if (dump_mem_info_frequency > 0) {
    tracker_event = eventProcessor.schedule_every(new TrackerContinuation, HRTIME_SECONDS(dump_mem_info_frequency), ET_CALL);
  }

  return 1;
}

static void
proxy_signal_handler(int signo, siginfo_t *info, void *ctx)
{
  if ((unsigned)signo < countof(signal_received)) {
    signal_received[signo] = true;
  }

  // These signals are all handled by SignalContinuation.
  switch (signo) {
  case SIGHUP:
  case SIGINT:
  case SIGTERM:
  case SIGUSR1:
  case SIGUSR2:
    return;
  }

  signal_format_siginfo(signo, info, appVersionInfo.AppStr);

#if TS_HAS_PROFILER
  HeapProfilerDump("/tmp/ts_end.hprof");
  HeapProfilerStop();
  ProfilerStop();
#endif

  // We don't expect any crashing signals here because, but
  // forward to the default handler just to be robust.
  if (signal_is_crash(signo)) {
    signal_crash_handler(signo, info, ctx);
  }
}

//
// Initialize operating system related information/services
//
static void
init_system()
{
  signal_register_default_handler(proxy_signal_handler);
  signal_register_crash_handler(signal_crash_handler);

  syslog(LOG_NOTICE, "NOTE: --- %s Starting ---", appVersionInfo.AppStr);
  syslog(LOG_NOTICE, "NOTE: %s Version: %s", appVersionInfo.AppStr, appVersionInfo.FullVersionInfoStr);

  //
  // Delimit file Descriptors
  //
  fds_limit = ink_max_out_rlimit(RLIMIT_NOFILE);
}

static void
check_lockfile()
{
  std::string rundir(RecConfigReadRuntimeDir());
  std::string lockfile;
  pid_t holding_pid;
  int err;

  lockfile = Layout::relative_to(rundir, SERVER_LOCK);

  Lockfile server_lockfile(lockfile.c_str());
  err = server_lockfile.Get(&holding_pid);

  if (err != 1) {
    char *reason = strerror(-err);
    fprintf(stderr, "WARNING: Can't acquire lockfile '%s'", lockfile.c_str());

    if ((err == 0) && (holding_pid != -1)) {
      fprintf(stderr, " (Lock file held by process ID %ld)\n", (long)holding_pid);
    } else if ((err == 0) && (holding_pid == -1)) {
      fprintf(stderr, " (Lock file exists, but can't read process ID)\n");
    } else if (reason) {
      fprintf(stderr, " (%s)\n", reason);
    } else {
      fprintf(stderr, "\n");
    }
    ::exit(1);
  }
}

static void
check_config_directories()
{
  std::string rundir(RecConfigReadRuntimeDir());
  std::string sysconfdir(RecConfigReadConfigDir());

  if (access(sysconfdir.c_str(), R_OK) == -1) {
    fprintf(stderr, "unable to access() config dir '%s': %d, %s\n", sysconfdir.c_str(), errno, strerror(errno));
    fprintf(stderr, "please set the 'TS_ROOT' environment variable\n");
    ::exit(1);
  }

  if (access(rundir.c_str(), R_OK | W_OK) == -1) {
    fprintf(stderr, "unable to access() local state dir '%s': %d, %s\n", rundir.c_str(), errno, strerror(errno));
    fprintf(stderr, "please set 'proxy.config.local_state_dir'\n");
    ::exit(1);
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

  if (remote_management_flag) {
    // We are being managed by traffic_manager, TERM ourselves if it goes away.
    EnableDeathSignal(SIGTERM);
  }

  RecProcessInit(remote_management_flag ? RECM_CLIENT : RECM_STAND_ALONE, diags);
  LibRecordsConfigInit();

  // Start up manager
  pmgmt = new ProcessManager(remote_management_flag);

  // Lifecycle callbacks can potentially be invoked from this thread, so force thread initialization
  // to make the TS API work. Use a lambda to avoid dealing with compiler dependent casting issues.
  pmgmt->start([]() -> void { TSThreadInit(); });

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
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_machine", appVersionInfo.BldMachineStr,
                        RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_person", appVersionInfo.BldPersonStr,
                        RECP_NON_PERSISTENT);
}

#define CMD_ERROR -2      // serious error, exit maintenance mode
#define CMD_FAILED -1     // error, but recoverable
#define CMD_OK 0          // ok, or minor (user) error
#define CMD_HELP 1        // ok, print help
#define CMD_IN_PROGRESS 2 // task not completed. don't exit

static int
cmd_list(char * /* cmd ATS_UNUSED */)
{
  printf("LIST\n\n");

  // show hostdb size

  int h_size = 120000;
  REC_ReadConfigInteger(h_size, "proxy.config.hostdb.size");
  printf("Host Database size:\t%d\n", h_size);

  // show cache config information....

  Note("Cache Storage:");
  Store tStore;
  Result result = tStore.read_config();

  if (result.failed()) {
    Note("Failed to read cache storage configuration: %s", result.message());
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
    if (!null_ok) {
      printf("Error: argument missing\n");
    }
    return cmd;
  }
  cmd += strspn(cmd, " \t");
  return cmd;
}

// Handler for things that need to wait until the cache is initialized.
static void
CB_After_Cache_Init()
{
  APIHook *hook;
  int start;

  start = ink_atomic_swap(&delay_listen_for_cache_p, -1);

#if TS_ENABLE_FIPS == 0
  // Check for cache BC after the cache is initialized and before listen, if possible.
  if (cacheProcessor.min_stripe_version._major < CACHE_DB_MAJOR_VERSION) {
    // Versions before 23 need the MMH hash.
    if (cacheProcessor.min_stripe_version._major < 23) {
      Debug("cache_bc", "Pre 4.0 stripe (cache version %d.%d) found, forcing MMH hash for cache URLs",
            cacheProcessor.min_stripe_version._major, cacheProcessor.min_stripe_version._minor);
      URLHashContext::Setting = URLHashContext::MMH;
    }
  }
#endif

  if (1 == start) {
    Debug("http_listen", "Delayed listen enable, cache initialization finished");
    start_HttpProxyServer();
  }

  time_t cache_ready_at = time(nullptr);
  RecSetRecordInt("proxy.node.restarts.proxy.cache_ready_time", cache_ready_at, REC_SOURCE_DEFAULT);

  // Alert the plugins the cache is initialized.
  hook = lifecycle_hooks->get(TS_LIFECYCLE_CACHE_READY_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_LIFECYCLE_CACHE_READY, nullptr);
    hook = hook->next();
  }
}

void
CB_cmd_cache_clear()
{
  if (cacheProcessor.IsCacheEnabled() == CACHE_INITIALIZED) {
    Note("CLEAR, succeeded");
    ::exit(0);
  } else if (cacheProcessor.IsCacheEnabled() == CACHE_INIT_FAILED) {
    Note("unable to open Cache, CLEAR failed");
    ::exit(1);
  }
}

void
CB_cmd_cache_check()
{
  int res = 0;
  if (cacheProcessor.IsCacheEnabled() == CACHE_INITIALIZED) {
    res = cacheProcessor.dir_check(false) < 0 || res;
    cacheProcessor.stop();
    const char *n = "CHECK";

    if (res) {
      printf("\n%s failed", n);
      ::exit(1);
    } else {
      printf("\n%s succeeded\n", n);
      ::exit(0);
    }
  } else if (cacheProcessor.IsCacheEnabled() == CACHE_INIT_FAILED) {
    Note("unable to open Cache, Check failed");
    ::exit(1);
  }
}

static int
cmd_check_internal(char * /* cmd ATS_UNUSED */, bool fix = false)
{
  const char *n = fix ? "REPAIR" : "CHECK";

  printf("%s\n\n", n);

#if 0
  printf("Host Database\n");
  HostDBCache hd;
  if (hd.start(fix) < 0) {
    printf("\tunable to open Host Database, %s failed\n", n);
    return CMD_OK;
  }
  hd.check("hostdb.config", fix);
  hd.reset();
#endif

  cacheProcessor.afterInitCallbackSet(&CB_cmd_cache_check);
  if (cacheProcessor.start_internal(PROCESSOR_CHECK) < 0) {
    printf("\nbad cache configuration, %s failed\n", n);
    return CMD_FAILED;
  }
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

  bool c_all   = !strcmp(cmd, "clear");
  bool c_hdb   = !strcmp(cmd, "clear_hostdb");
  bool c_cache = !strcmp(cmd, "clear_cache");

  if (c_all || c_hdb) {
    std::string rundir(RecConfigReadRuntimeDir());
    std::string config(Layout::relative_to(rundir, "hostdb.config"));

    Note("Clearing HostDB Configuration");
    if (unlink(config.c_str()) < 0) {
      Note("unable to unlink %s", config.c_str());
    }
  }

  if (c_hdb || c_all) {
    Note("Clearing Host Database");
    if (hostDBProcessor.cache()->start(PROCESSOR_RECONFIGURE) < 0) {
      Note("unable to open Host Database, CLEAR failed");
      return CMD_FAILED;
    }
    hostDBProcessor.cache()->refcountcache->clear();
    if (c_hdb) {
      return CMD_OK;
    }
  }

  if (c_all || c_cache) {
    Note("Clearing Cache");

    cacheProcessor.afterInitCallbackSet(&CB_cmd_cache_clear);
    if (cacheProcessor.start_internal(PROCESSOR_RECONFIGURE) < 0) {
      Note("unable to open Cache, CLEAR failed");
      return CMD_FAILED;
    }
    return CMD_IN_PROGRESS;
  }

  return CMD_OK;
}

static int
cmd_verify(char * /* cmd ATS_UNUSED */)
{
  unsigned char exitStatus = 0; // exit status is 8 bits

  fprintf(stderr, "NOTE: VERIFY\n\n");

  // initialize logging since a plugin
  // might call TS_ERROR which needs
  // log_rsb to be init'ed
  Log::init(DEFAULT_REMOTE_MANAGEMENT_FLAG);

  if (*conf_dir) {
    fprintf(stderr, "NOTE: VERIFY config dir: %s...\n\n", conf_dir);
    Layout::get()->update_sysconfdir(conf_dir);
  }

  if (!urlRewriteVerify()) {
    exitStatus |= (1 << 0);
    fprintf(stderr, "ERROR: Failed to load remap.config, exitStatus %d\n\n", exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully loaded remap.config\n\n");
  }

  if (RecReadConfigFile() != REC_ERR_OKAY) {
    exitStatus |= (1 << 1);
    fprintf(stderr, "ERROR: Failed to load records.config, exitStatus %d\n\n", exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully loaded records.config\n\n");
  }

  if (!plugin_init(true)) {
    exitStatus |= (1 << 2);
    fprintf(stderr, "ERROR: Failed to load plugin.config, exitStatus %d\n\n", exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully loaded plugin.config\n\n");
  }

  SSLInitializeLibrary();
  SSLConfig::startup();
  if (!SSLCertificateConfig::startup()) {
    exitStatus |= (1 << 3);
    fprintf(stderr, "ERROR: Failed to load ssl multicert.config, exitStatus %d\n\n", exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully loaded ssl multicert.config\n\n");
  }

  SSLConfig::scoped_config params;
  if (!SSLInitClientContext(params)) {
    exitStatus |= (1 << 4);
    fprintf(stderr, "Can't initialize the SSL client, HTTPS in remap rules will not function %d\n\n", exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully initialized SSL client context\n\n");
  }

  // TODO: Add more config validation..

  ::exit(exitStatus);

  return 0;
}

static int cmd_help(char *cmd);

static const struct CMD {
  const char *n; // name
  const char *d; // description (part of a line)
  const char *h; // help string (multi-line)
  int (*f)(char *);
  bool no_process_lock; /// If set this command doesn't need a process level lock.
} commands[] = {
  {"list", "List cache configuration",
   "LIST\n"
   "\n"
   "FORMAT: list\n"
   "\n"
   "List the sizes of the Host Database and Cache Index,\n"
   "and the storage available to the cache.\n",
   cmd_list, false},
  {"check", "Check the cache (do not make any changes)",
   "CHECK\n"
   "\n"
   "FORMAT: check\n"
   "\n"
   "Check the cache for inconsistencies or corruption.\n"
   "CHECK does not make any changes to the data stored in\n"
   "the cache. CHECK requires a scan of the contents of the\n"
   "cache and may take a long time for large caches.\n",
   cmd_check, true},
  {"clear", "Clear the entire cache",
   "CLEAR\n"
   "\n"
   "FORMAT: clear\n"
   "\n"
   "Clear the entire cache.  All data in the cache is\n"
   "lost and the cache is reconfigured based on the current\n"
   "description of database sizes and available storage.\n",
   cmd_clear, false},
  {"clear_cache", "Clear the document cache",
   "CLEAR_CACHE\n"
   "\n"
   "FORMAT: clear_cache\n"
   "\n"
   "Clear the document cache.  All documents in the cache are\n"
   "lost and the cache is reconfigured based on the current\n"
   "description of database sizes and available storage.\n",
   cmd_clear, false},
  {"clear_hostdb", "Clear the hostdb cache",
   "CLEAR_HOSTDB\n"
   "\n"
   "FORMAT: clear_hostdb\n"
   "\n"
   "Clear the entire hostdb cache.  All host name resolution\n"
   "information is lost.\n",
   cmd_clear, false},
  {CMD_VERIFY_CONFIG, "Verify the config",
   "\n"
   "\n"
   "FORMAT: verify_config\n"
   "\n"
   "Load the config and verify traffic_server comes up correctly. \n",
   cmd_verify, true},
  {"help", "Obtain a short description of a command (e.g. 'help clear')",
   "HELP\n"
   "\n"
   "FORMAT: help [command_name]\n"
   "\n"
   "EXAMPLES: help help\n"
   "          help commit\n"
   "\n"
   "Provide a short description of a command (like this).\n",
   cmd_help, false},
};

static int
find_cmd_index(const char *p)
{
  p += strspn(p, " \t");
  for (unsigned c = 0; c < countof(commands); c++) {
    const char *l = commands[c].n;
    while (l) {
      const char *s = strchr(l, '/');
      const char *e = strpbrk(p, " \t\n");
      int len       = s ? s - l : strlen(l);
      int lenp      = e ? e - p : strlen(p);
      if ((len == lenp) && !strncasecmp(p, l, len)) {
        return c;
      }
      l = s ? s + 1 : nullptr;
    }
  }
  return -1;
}

static int
cmd_help(char *cmd)
{
  (void)cmd;
  printf("HELP\n\n");
  cmd = skip(cmd, true);
  if (!cmd) {
    for (unsigned i = 0; i < countof(commands); i++) {
      printf("%15s  %s\n", commands[i].n, commands[i].d);
    }
  } else {
    int i;
    if ((i = find_cmd_index(cmd)) < 0) {
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
  REC_ReadConfigInteger(fds_throttle, "proxy.config.net.connections_throttle");
  if (fds_throttle > fds_limit - THROTTLE_FD_HEADROOM) {
    int new_fds_throttle = fds_limit - THROTTLE_FD_HEADROOM;
    if (new_fds_throttle < 1) {
      ink_abort("too few file descriptors (%d) available", fds_limit);
    }
    char msg[256];
    snprintf(msg, sizeof(msg),
             "connection throttle too high, "
             "%d (throttle) + %d (internal use) > %d (file descriptor limit), "
             "using throttle of %d",
             fds_throttle, THROTTLE_FD_HEADROOM, fds_limit, new_fds_throttle);
    SignalWarning(MGMT_SIGNAL_SYSTEM_ERROR, msg);
  }
}

//
// Command mode
//
static int
cmd_mode()
{
  if (command_index >= 0) {
    return commands[command_index].f(command_string);
  } else if (*command_string) {
    Warning("unrecognized command: '%s'", command_string);
    return CMD_FAILED; // in error
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
set_core_size(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
              void * /* opaque_token ATS_UNUSED */)
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
      lim.rlim_cur = (rlim_t)size;
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
    set_core_size(nullptr, RECD_INT, rec_temp, nullptr);
    found = (REC_RegisterConfigUpdateFunc("proxy.config.core_limit", set_core_size, nullptr) == REC_ERR_OKAY);

    ink_assert(found);
  }
}

static void
adjust_sys_settings()
{
  struct rlimit lim;
  int fds_throttle = -1;
  rlim_t maxfiles;

  maxfiles = ink_get_max_files();
  if (maxfiles != RLIM_INFINITY) {
    float file_max_pct = 0.9;

    REC_ReadConfigFloat(file_max_pct, "proxy.config.system.file_max_pct");
    if (file_max_pct > 1.0) {
      file_max_pct = 1.0;
    }

    lim.rlim_cur = lim.rlim_max = static_cast<rlim_t>(maxfiles * file_max_pct);
    if (setrlimit(RLIMIT_NOFILE, &lim) == 0 && getrlimit(RLIMIT_NOFILE, &lim) == 0) {
      fds_limit = (int)lim.rlim_cur;
      syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)", RLIMIT_NOFILE, (int)lim.rlim_cur, (int)lim.rlim_max);
    }
  }

  REC_ReadConfigInteger(fds_throttle, "proxy.config.net.connections_throttle");

  if (getrlimit(RLIMIT_NOFILE, &lim) == 0) {
    if (fds_throttle > (int)(lim.rlim_cur - THROTTLE_FD_HEADROOM)) {
      lim.rlim_cur = (lim.rlim_max = (rlim_t)(fds_throttle + THROTTLE_FD_HEADROOM));
      if (setrlimit(RLIMIT_NOFILE, &lim) == 0 && getrlimit(RLIMIT_NOFILE, &lim) == 0) {
        fds_limit = (int)lim.rlim_cur;
        syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)", RLIMIT_NOFILE, (int)lim.rlim_cur, (int)lim.rlim_max);
      }
    }
  }

  ink_max_out_rlimit(RLIMIT_STACK);
  ink_max_out_rlimit(RLIMIT_DATA);
  ink_max_out_rlimit(RLIMIT_FSIZE);

#ifdef RLIMIT_RSS
  ink_max_out_rlimit(RLIMIT_RSS);
#endif
}

struct ShowStats : public Continuation {
#ifdef ENABLE_TIME_TRACE
  FILE *fp;
#endif
  int cycle        = 0;
  int64_t last_cc  = 0;
  int64_t last_rb  = 0;
  int64_t last_w   = 0;
  int64_t last_r   = 0;
  int64_t last_wb  = 0;
  int64_t last_nrb = 0;
  int64_t last_nw  = 0;
  int64_t last_nr  = 0;
  int64_t last_nwb = 0;
  int64_t last_p   = 0;
  int64_t last_o   = 0;
  int
  mainEvent(int event, Event *e)
  {
    (void)event;
    (void)e;
    if (!(cycle++ % 24)) {
      printf("r:rr w:ww r:rbs w:wbs open polls\n");
    }
    int64_t sval, cval;

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
    printf("%" PRId64 ":%" PRId64 " %" PRId64 ":%" PRId64 " %" PRId64 ":%" PRId64 " %" PRId64 ":%" PRId64 " %" PRId64 " %" PRId64
           "\n",
           d_rb, d_r, d_wb, d_w, d_nrb, d_nr, d_nwb, d_nw, d_o, d_p);
#ifdef ENABLE_TIME_TRACE
    int i;
    fprintf(fp, "immediate_events_time_dist\n");
    for (i = 0; i < TIME_DIST_BUCKETS_SIZE; i++) {
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

    fflush(fp);
#endif
    return EVENT_CONT;
  }
  ShowStats() : Continuation(nullptr)

  {
    SET_HANDLER(&ShowStats::mainEvent);
#ifdef ENABLE_TIME_TRACE
    fp = fopen("./time_trace.out", "a");
#endif
  }
};

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
  bool found         = false;
  char sys_var[]     = "proxy.config.syslog_facility";
  char *facility_str = REC_readString(sys_var, &found);

  if (found) {
    int facility = facility_string_to_int(facility_str);

    ats_free(facility_str);
    if (facility < 0) {
      syslog(LOG_WARNING, "Bad syslog facility in records.config. Keeping syslog at LOG_DAEMON");
    } else {
      Debug("server", "Setting syslog facility to %d", facility);
      closelog();
      openlog("traffic_server", LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility);
    }
  } else {
    syslog(LOG_WARNING, "Missing syslog facility config %s. Keeping syslog at LOG_DAEMON", sys_var);
  }
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
  hpack_huffman_init();
}

#if TS_HAS_TESTS
struct RegressionCont : public Continuation {
  int initialized = 0;
  int waits       = 0;
  int started     = 0;

  int
  mainEvent(int event, Event *e)
  {
    (void)event;
    (void)e;
    int res = 0;
    if (!initialized && (cacheProcessor.IsCacheEnabled() != CACHE_INITIALIZED)) {
      printf("Regression waiting for the cache to be ready... %d\n", ++waits);
      return EVENT_CONT;
    }

    char *rt = (char *)(regression_test[0] == 0 ? "" : regression_test);
    if (!initialized && RegressionTest::run(rt, regression_level) == REGRESSION_TEST_INPROGRESS) {
      initialized = 1;
      return EVENT_CONT;
    }

    if ((res = RegressionTest::check_status(regression_level)) == REGRESSION_TEST_INPROGRESS) {
      return EVENT_CONT;
    }

    TSSystemState::shut_down_event_system();
    fprintf(stderr, "REGRESSION_TEST DONE: %s\n", regression_status_string(res));
    ::exit(res == REGRESSION_TEST_PASSED ? 0 : 1);
    return EVENT_CONT;
  }

  RegressionCont() : Continuation(new_ProxyMutex()) { SET_HANDLER(&RegressionCont::mainEvent); }
};

static void
run_RegressionTest()
{
  if (regression_level) {
    eventProcessor.schedule_every(new RegressionCont(), HRTIME_SECONDS(1));
  }
}
#endif // TS_HAS_TESTS

static void
chdir_root()
{
  std::string prefix = Layout::get()->prefix;

  if (chdir(prefix.c_str()) < 0) {
    fprintf(stderr, "%s: unable to change to root directory \"%s\" [%d '%s']\n", appVersionInfo.AppStr, prefix.c_str(), errno,
            strerror(errno));
    fprintf(stderr, "%s: please correct the path or set the TS_ROOT environment variable\n", appVersionInfo.AppStr);
    ::exit(1);
  } else {
    printf("%s: using root directory '%s'\n", appVersionInfo.AppStr, prefix.c_str());
  }
}

static int
adjust_num_of_net_threads(int nthreads)
{
  float autoconfig_scale = 1.0;
  int nth_auto_config    = 1;
  int num_of_threads_tmp = 1;

  REC_ReadConfigInteger(nth_auto_config, "proxy.config.exec_thread.autoconfig");

  Debug("threads", "initial number of net threads is %d", nthreads);
  Debug("threads", "net threads auto-configuration %s", nth_auto_config ? "enabled" : "disabled");

  if (!nth_auto_config) {
    REC_ReadConfigInteger(num_of_threads_tmp, "proxy.config.exec_thread.limit");

    if (num_of_threads_tmp <= 0) {
      num_of_threads_tmp = 1;
    } else if (num_of_threads_tmp > MAX_EVENT_THREADS) {
      num_of_threads_tmp = MAX_EVENT_THREADS;
    }

    nthreads = num_of_threads_tmp;
  } else { /* autoconfig is enabled */
    num_of_threads_tmp = nthreads;
    REC_ReadConfigFloat(autoconfig_scale, "proxy.config.exec_thread.autoconfig.scale");
    num_of_threads_tmp = (int)((float)num_of_threads_tmp * autoconfig_scale);

    if (unlikely(num_of_threads_tmp > MAX_EVENT_THREADS)) {
      num_of_threads_tmp = MAX_EVENT_THREADS;
    }

    if (num_of_threads_tmp) {
      nthreads = num_of_threads_tmp;
    }
  }

  if (unlikely(nthreads <= 0)) { /* impossible case -just for protection */
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
#if !TS_USE_POSIX_CAP
  RecInt enabled;

  if (RecGetRecordInt("proxy.config.ssl.cert.load_elevated", &enabled) == REC_ERR_OKAY && enabled) {
    Warning("ignoring proxy.config.ssl.cert.load_elevated because Traffic Server was built without POSIX capabilities support");
  }

  if (RecGetRecordInt("proxy.config.plugin.load_elevated", &enabled) == REC_ERR_OKAY && enabled) {
    Warning("ignoring proxy.config.plugin.load_elevated because Traffic Server was built without POSIX capabilities support");
  }
#endif /* TS_USE_POSIX_CAP */

  // This is primarily for regression tests, where people just run "traffic_server -R1" as a regular user. Dropping
  // privilege is never going to succeed unless we were privileged in the first place. I guess we ought to check
  // capabilities as well :-/
  if (getuid() != 0 && geteuid() != 0) {
    Note("Traffic Server is running unprivileged, not switching to user '%s'", user);
    return;
  }

  Debug("privileges", "switching to unprivileged user '%s'", user);
  ImpersonateUser(user, IMPERSONATE_PERMANENT);

#if !defined(BIG_SECURITY_HOLE) || (BIG_SECURITY_HOLE != 0)
  if (getuid() == 0 || geteuid() == 0) {
    ink_fatal("Trafficserver has not been designed to serve pages while\n"
              "\trunning as root. There are known race conditions that\n"
              "\twill allow any local user to read any file on the system.\n"
              "\tIf you still desire to serve pages as root then\n"
              "\tadd -DBIG_SECURITY_HOLE to the CFLAGS env variable\n"
              "\tand then rebuild the server.\n"
              "\tIt is strongly suggested that you instead modify the\n"
              "\tproxy.config.admin.user_id directive in your\n"
              "\trecords.config file to list a non-root user.\n");
  }
#endif
}

/*
 * Binds stdout and stderr to files specified by the parameters
 *
 * On failure to bind, emits a warning and whatever is being bound
 * just isn't bound
 *
 * This must work without the ability to elevate privilege if the files are accessible without.
 */
void
bind_outputs(const char *bind_stdout, const char *bind_stderr)
{
  int log_fd;
  unsigned int flags = O_WRONLY | O_APPEND | O_CREAT | O_SYNC;

  if (*bind_stdout != 0) {
    Debug("log", "binding stdout to %s", bind_stdout);
    log_fd = elevating_open(bind_stdout, flags, 0644);
    if (log_fd < 0) {
      fprintf(stdout, "[Warning]: TS unable to open log file \"%s\" [%d '%s']\n", bind_stdout, errno, strerror(errno));
    } else {
      Debug("log", "duping stdout");
      dup2(log_fd, STDOUT_FILENO);
      close(log_fd);
    }
  }
  if (*bind_stderr != 0) {
    Debug("log", "binding stderr to %s", bind_stderr);
    log_fd = elevating_open(bind_stderr, O_WRONLY | O_APPEND | O_CREAT | O_SYNC, 0644);
    if (log_fd < 0) {
      fprintf(stdout, "[Warning]: TS unable to open log file \"%s\" [%d '%s']\n", bind_stderr, errno, strerror(errno));
    } else {
      Debug("log", "duping stderr");
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }
  }
}

//
// Main
//

int
main(int /* argc ATS_UNUSED */, const char **argv)
{
#if TS_HAS_PROFILER
  HeapProfilerStart("/tmp/ts.hprof");
  ProfilerStart("/tmp/ts.prof");
#endif
  bool admin_user_p = false;

#if defined(DEBUG) && defined(HAVE_MCHECK_PEDANTIC)
  mcheck_pedantic(NULL);
#endif

  pcre_malloc = ats_malloc;
  pcre_free   = ats_free;

  // Verify system dependent 'constants'
  check_system_constants();

  // Define the version info
  appVersionInfo.setup(PACKAGE_NAME, "traffic_server", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  runroot_handler(argv);
  // Before accessing file system initialize Layout engine
  Layout::create();
  // Let's be clear on what exactly is starting up.
  printf("Traffic Server " PACKAGE_VERSION BUILD_NUMBER " " __DATE__ " " __TIME__ " " BUILD_MACHINE "\n");
  chdir_root(); // change directory to the install root of traffic server.

  std::sort(argument_descriptions, argument_descriptions + countof(argument_descriptions),
            [](ArgumentDescription const &a, ArgumentDescription const &b) { return 0 > strcasecmp(a.name, b.name); });

  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);
  command_flag  = command_flag || *command_string;
  command_index = find_cmd_index(command_string);
  command_valid = command_flag && command_index >= 0;

  ink_freelist_init_ops(cmd_disable_freelist, cmd_disable_pfreelist);

#if TS_HAS_TESTS
  if (regression_list) {
    RegressionTest::list();
    ::exit(0);
  }
#endif

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
  // This is also needed for log rotation - setting up the file can cause privilege
  // related errors and if diagsConfig isn't get up yet that will crash on a NULL pointer.
  diagsConfig = new DiagsConfig("Server", DIAGS_LOG_FILENAME, error_tags, action_tags, false);
  diags       = diagsConfig->diags;
  diags->set_std_output(StdStream::STDOUT, bind_stdout);
  diags->set_std_output(StdStream::STDERR, bind_stderr);
  if (is_debug_tag_set("diags")) {
    diags->dump();
  }

  // Bind stdout and stderr to specified switches
  // Still needed despite the set_std{err,out}_output() calls later since there are
  // fprintf's before those calls
  bind_outputs(bind_stdout, bind_stderr);

  // Local process manager
  initialize_process_manager();

  // Set the core limit for the process
  init_core_size();
  init_system();

  // Adjust system and process settings
  adjust_sys_settings();

  // Restart syslog now that we have configuration info
  syslog_log_configure();

  // Register stats if standalone
  if (DEFAULT_REMOTE_MANAGEMENT_FLAG == remote_management_flag) {
    RecRegisterStatInt(RECT_NODE, "proxy.node.config.reconfigure_time", time(nullptr), RECP_NON_PERSISTENT);
    RecRegisterStatInt(RECT_NODE, "proxy.node.config.reconfigure_required", 0, RECP_NON_PERSISTENT);
    RecRegisterStatInt(RECT_NODE, "proxy.node.config.restart_required.proxy", 0, RECP_NON_PERSISTENT);
    RecRegisterStatInt(RECT_NODE, "proxy.node.config.restart_required.manager", 0, RECP_NON_PERSISTENT);
    RecRegisterStatInt(RECT_NODE, "proxy.node.config.draining", 0, RECP_NON_PERSISTENT);
  }

  // init huge pages
  int enabled;
  REC_ReadConfigInteger(enabled, "proxy.config.allocator.hugepages");
  ats_hugepage_init(enabled);
  Debug("hugepages", "ats_pagesize reporting %zu", ats_pagesize());
  Debug("hugepages", "ats_hugepage_size reporting %zu", ats_hugepage_size());

  if (!num_accept_threads) {
    REC_ReadConfigInteger(num_accept_threads, "proxy.config.accept_threads");
  }

  if (!num_task_threads) {
    REC_ReadConfigInteger(num_task_threads, "proxy.config.task_threads");
  }

  ats_scoped_str user(MAX_LOGIN + 1);

  *user        = '\0';
  admin_user_p = ((REC_ERR_OKAY == REC_ReadConfigString(user, "proxy.config.admin.user_id", MAX_LOGIN)) && (*user != '\0') &&
                  (0 != strcmp(user, "#-1")));

  // Set up crash logging. We need to do this while we are still privileged so that the crash
  // logging helper runs as root. Don't bother setting up a crash logger if we are going into
  // command mode since that's not going to daemonize or run for a long time unattended.
  if (!command_flag) {
    crash_logger_init(user);
    signal_register_crash_handler(crash_logger_invoke);
  }

#if TS_USE_POSIX_CAP
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
#endif

  // Ensure only one copy of traffic server is running, unless it's a command
  // that doesn't require a lock.
  if (!(command_valid && commands[command_index].no_process_lock)) {
    check_lockfile();
  }

  // Can't generate a log message yet, do that right after Diags is
  // setup.

  // This call is required for win_9xMe
  // without this this_ethread() is failing when
  // start_HttpProxyServer is called from main thread
  Thread *main_thread = new EThread;
  main_thread->set_specific();

  // Re-initialize diagsConfig based on records.config configuration
  DiagsConfig *old_log = diagsConfig;
  diagsConfig          = new DiagsConfig("Server", DIAGS_LOG_FILENAME, error_tags, action_tags, true);
  diags                = diagsConfig->diags;
  RecSetDiags(diags);
  diags->set_std_output(StdStream::STDOUT, bind_stdout);
  diags->set_std_output(StdStream::STDERR, bind_stderr);
  if (is_debug_tag_set("diags")) {
    diags->dump();
  }

  if (old_log) {
    delete (old_log);
    old_log = nullptr;
  }

  DebugCapabilities("privileges"); // Can do this now, logging is up.

// Check if we should do mlockall()
#if defined(MCL_FUTURE)
  int mlock_flags = 0;
  REC_ReadConfigInteger(mlock_flags, "proxy.config.mlock_enabled");

  if (mlock_flags == 2) {
    if (0 != mlockall(MCL_CURRENT | MCL_FUTURE)) {
      Warning("Unable to mlockall() on startup");
    } else {
      Debug("server", "Successfully called mlockall()");
    }
  }
#endif

  // Check for core file
  if (core_file[0] != '\0') {
    process_core(core_file);
    ::exit(0);
  }

  // setup callback for tracking remap included files
  load_remap_file_cb = load_remap_file_callback;

  // We need to do this early so we can initialize the Machine
  // singleton, which depends on configuration values loaded in this.
  // We want to initialize Machine as early as possible because it
  // has other dependencies. Hopefully not in prep_HttpProxyServer().
  HttpConfig::startup();
#if TS_USE_QUIC == 1
  Http3Config::startup();
#endif

  /* Set up the machine with the outbound address if that's set,
     or the inbound address if set, otherwise let it default.
  */
  IpEndpoint machine_addr;
  ink_zero(machine_addr);
  if (HttpConfig::m_master.outbound_ip4.isValid()) {
    machine_addr.assign(HttpConfig::m_master.outbound_ip4);
  } else if (HttpConfig::m_master.outbound_ip6.isValid()) {
    machine_addr.assign(HttpConfig::m_master.outbound_ip6);
  } else if (HttpConfig::m_master.inbound_ip4.isValid()) {
    machine_addr.assign(HttpConfig::m_master.inbound_ip4);
  } else if (HttpConfig::m_master.inbound_ip6.isValid()) {
    machine_addr.assign(HttpConfig::m_master.inbound_ip6);
  }
  Machine::init(nullptr, &machine_addr.sa);

  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.uuid", (char *)Machine::instance()->uuid.getString(),
                        RECP_NON_PERSISTENT);

  // pmgmt->start() must occur after initialization of Diags but
  // before calling RecProcessInit()

  REC_ReadConfigInteger(res_track_memory, "proxy.config.res_track_memory");

  init_http_header();
  ts_session_protocol_well_known_name_indices_init();

  // Sanity checks
  check_fd_limit();

// Alter the frequencies at which the update threads will trigger
#define SET_INTERVAL(scope, name, var)                    \
  do {                                                    \
    RecInt tmpint;                                        \
    Debug("statsproc", "Looking for %s", name);           \
    if (RecGetRecordInt(name, &tmpint) == REC_ERR_OKAY) { \
      Debug("statsproc", "Found %s", name);               \
      scope##_set_##var(tmpint);                          \
    }                                                     \
  } while (0)
  SET_INTERVAL(RecProcess, "proxy.config.config_update_interval_ms", config_update_interval_ms);
  SET_INTERVAL(RecProcess, "proxy.config.raw_stat_sync_interval_ms", raw_stat_sync_interval_ms);
  SET_INTERVAL(RecProcess, "proxy.config.remote_sync_interval_ms", remote_sync_interval_ms);

  // Initialize the stat pages manager
  statPagesManager.init();

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

  REC_ReadConfigInteger(thread_max_heartbeat_mseconds, "proxy.config.thread.max_heartbeat_mseconds");

  ink_event_system_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_net_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_aio_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_cache_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_hostdb_init(
    ts::ModuleVersion(HOSTDB_MODULE_INTERNAL_VERSION._major, HOSTDB_MODULE_INTERNAL_VERSION._minor, ts::ModuleVersion::PRIVATE));
  ink_dns_init(
    ts::ModuleVersion(HOSTDB_MODULE_INTERNAL_VERSION._major, HOSTDB_MODULE_INTERNAL_VERSION._minor, ts::ModuleVersion::PRIVATE));
  ink_split_dns_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));

  naVecMutex = new_ProxyMutex();

  // Do the inits for NetProcessors that use ET_NET threads. MUST be before starting those threads.
  netProcessor.init();
  prep_HttpProxyServer();

#if TS_USE_QUIC == 1
  // OK, pushing a spawn scheduling here
  quic_NetProcessor.init();
#endif

  // If num_accept_threads == 0, let the ET_NET threads to set the condition variable,
  // Else we set it here so when checking the condition variable later it returns immediately.
  if (num_accept_threads == 0) {
    eventProcessor.schedule_spawn(&init_HttpProxyServer, ET_NET);
  } else {
    std::unique_lock<std::mutex> lock(proxyServerMutex);
    et_net_threads_ready = true;
    lock.unlock();
    proxyServerCheck.notify_one();
  }

  // !! ET_NET threads start here !!
  // This means any spawn scheduling must be done before this point.
  eventProcessor.start(num_of_net_threads, stacksize);

  int num_remap_threads = 0;
  REC_ReadConfigInteger(num_remap_threads, "proxy.config.remap.num_remap_threads");
  if (num_remap_threads < 1) {
    num_remap_threads = 0;
  }

  if (num_remap_threads > 0) {
    Note("using the new remap processor system with %d threads", num_remap_threads);
    remapProcessor.setUseSeparateThread();
  }

  eventProcessor.schedule_every(new SignalContinuation, HRTIME_MSECOND * 500, ET_CALL);
  eventProcessor.schedule_every(new DiagsLogContinuation, HRTIME_SECOND, ET_TASK);
  eventProcessor.schedule_every(new MemoryLimit, HRTIME_SECOND * 10, ET_TASK);
  REC_RegisterConfigUpdateFunc("proxy.config.dump_mem_info_frequency", init_memory_tracker, nullptr);
  init_memory_tracker(nullptr, RECD_NULL, RecData(), nullptr);

  char *p = REC_ConfigReadString("proxy.config.diags.debug.client_ip");
  if (p) {
    // Translate string to IpAddr
    set_debug_ip(p);
  }
  REC_RegisterConfigUpdateFunc("proxy.config.diags.debug.client_ip", update_debug_client_ip, nullptr);

  // log initialization moved down

  if (command_flag) {
    int cmd_ret = cmd_mode();

    if (cmd_ret != CMD_IN_PROGRESS) {
      if (cmd_ret >= 0) {
        ::exit(0); // everything is OK
      } else {
        ::exit(1); // in error
      }
    }
  } else {
    remapProcessor.start(num_remap_threads, stacksize);
    RecProcessStart();
    initCacheControl();
    IpAllow::startup();
    HostStatus::instance().loadHostStatusFromStats();
    netProcessor.init_socks();
    ParentConfig::startup();
#ifdef SPLIT_DNS
    SplitDNSConfig::startup();
#endif

    // Initialize HTTP/2
    Http2::init();
#if TS_USE_QUIC == 1
    // Initialize HTTP/QUIC
    Http3::init();
#endif

    if (!HttpProxyPort::loadValue(http_accept_port_descriptor)) {
      HttpProxyPort::loadConfig();
    }
    HttpProxyPort::loadDefaultIfEmpty();

    dnsProcessor.start(0, stacksize);
    if (hostDBProcessor.start() < 0)
      SignalWarning(MGMT_SIGNAL_SYSTEM_ERROR, "bad hostdb or storage configuration, hostdb disabled");

    // initialize logging (after event and net processor)
    Log::init(remote_management_flag ? 0 : Log::NO_REMOTE_MANAGEMENT);

    // Init plugins as soon as logging is ready.
    (void)plugin_init(); // plugin.config

    SSLConfigParams::init_ssl_ctx_cb  = init_ssl_ctx_callback;
    SSLConfigParams::load_ssl_file_cb = load_ssl_file_callback;
    sslNetProcessor.start(-1, stacksize);
#if TS_USE_QUIC == 1
    quic_NetProcessor.start(-1, stacksize);
#endif
    pmgmt->registerPluginCallbacks(global_config_cbs);

    cacheProcessor.afterInitCallbackSet(&CB_After_Cache_Init);
    cacheProcessor.start();

    // UDP net-threads are turned off by default.
    if (!num_of_udp_threads) {
      REC_ReadConfigInteger(num_of_udp_threads, "proxy.config.udp.threads");
    }
    if (num_of_udp_threads) {
      udpNet.start(num_of_udp_threads, stacksize);
    }

    // Initialize Response Body Factory
    body_factory = new HttpBodyFactory;

    // Continuation Statistics Dump
    if (show_statistics) {
      eventProcessor.schedule_every(new ShowStats(), HRTIME_SECONDS(show_statistics), ET_CALL);
    }

    //////////////////////////////////////
    // main server logic initiated here //
    //////////////////////////////////////

    init_accept_HttpProxyServer(num_accept_threads);
    transformProcessor.start();

    int http_enabled = 1;
    REC_ReadConfigInteger(http_enabled, "proxy.config.http.enabled");

    if (http_enabled) {
      // call the ready hooks before we start accepting connections.
      APIHook *hook = lifecycle_hooks->get(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK);
      while (hook) {
        hook->invoke(TS_EVENT_LIFECYCLE_PORTS_INITIALIZED, nullptr);
        hook = hook->next();
      }

      int delay_p = 0;
      REC_ReadConfigInteger(delay_p, "proxy.config.http.wait_for_cache");

      // Check the condition variable.
      {
        std::unique_lock<std::mutex> lock(proxyServerMutex);
        proxyServerCheck.wait(lock, [] { return et_net_threads_ready; });
      }

      // Delay only if config value set and flag value is zero
      // (-1 => cache already initialized)
      if (delay_p && ink_atomic_cas(&delay_listen_for_cache_p, 0, 1)) {
        Debug("http_listen", "Delaying listen, waiting for cache initialization");
      } else {
        start_HttpProxyServer(); // PORTS_READY_HOOK called from in here
      }
    }
    SNIConfig::cloneProtoSet();
    // Plugins can register their own configuration names so now after they've done that
    // check for unexpected names. This is very late because remap plugins must be allowed to
    // fire up as well.
    RecConfigWarnIfUnregistered();

    // "Task" processor, possibly with its own set of task threads
    tasksProcessor.register_event_type();
    eventProcessor.thread_group[ET_TASK]._afterStartCallback = task_threads_started_callback;
    tasksProcessor.start(num_task_threads, stacksize);

    if (netProcessor.socks_conf_stuff->accept_enabled) {
      start_SocksProxy(netProcessor.socks_conf_stuff->accept_port);
    }

    pmgmt->registerMgmtCallback(MGMT_EVENT_SHUTDOWN, &mgmt_restart_shutdown_callback);
    pmgmt->registerMgmtCallback(MGMT_EVENT_RESTART, &mgmt_restart_shutdown_callback);
    pmgmt->registerMgmtCallback(MGMT_EVENT_DRAIN, &mgmt_drain_callback);

    // Callback for various storage commands. These all go to the same function so we
    // pass the event code along so it can do the right thing. We cast that to <int> first
    // just to be safe because the value is a #define, not a typed value.
    pmgmt->registerMgmtCallback(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, [](ts::MemSpan<void> span) -> void {
      mgmt_storage_device_cmd_callback(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, span.view());
    });
    pmgmt->registerMgmtCallback(MGMT_EVENT_LIFECYCLE_MESSAGE, &mgmt_lifecycle_msg_callback);

    ink_set_thread_name("[TS_MAIN]");

    Note("traffic server running");

#if TS_HAS_TESTS
    TransformTest::run();
    //  run_SimpleHttp();
    run_RegressionTest();
#endif

    if (getenv("PROXY_AUTO_EXIT")) {
      eventProcessor.schedule_in(new AutoStopCont(), HRTIME_SECONDS(atoi(getenv("PROXY_AUTO_EXIT"))));
    }
  }

#if !TS_USE_POSIX_CAP
  if (admin_user_p) {
    change_uid_gid(user);
  }
#endif

  while (!TSSystemState::is_event_system_shut_down()) {
    sleep(1);
  }

  delete main_thread;
}

#if TS_HAS_TESTS
//////////////////////////////
// Unit Regression Test Hook //
//////////////////////////////

#include "HdrTest.h"

REGRESSION_TEST(Hdrs)(RegressionTest *t, int atype, int *pstatus)
{
  HdrTest ht;
  *pstatus = ht.go(t, atype);
  return;
}
#endif

static void
mgmt_restart_shutdown_callback(ts::MemSpan<void>)
{
  sync_cache_dir_on_shutdown();
}

static void
mgmt_drain_callback(ts::MemSpan<void> span)
{
  char *arg = span.rebind<char>().data();
  TSSystemState::drain(span.size() == 2 && arg[0] == '1');
  RecSetRecordInt("proxy.node.config.draining", TSSystemState::is_draining() ? 1 : 0, REC_SOURCE_DEFAULT);
}

static void
mgmt_storage_device_cmd_callback(int cmd, std::string_view const &arg)
{
  // data is the device name to control
  CacheDisk *d = cacheProcessor.find_by_path(arg.data(), int(arg.size()));

  if (d) {
    switch (cmd) {
    case MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE:
      Debug("server", "Marking %.*s offline", int(arg.size()), arg.data());
      cacheProcessor.mark_storage_offline(d, /* admin */ true);
      break;
    }
  }
}

static void
mgmt_lifecycle_msg_callback(ts::MemSpan<void> span)
{
  APIHook *hook = lifecycle_hooks->get(TS_LIFECYCLE_MSG_HOOK);
  TSPluginMsg msg;
  MgmtInt op;
  MgmtMarshallString tag;
  MgmtMarshallData payload;
  static const MgmtMarshallType fields[] = {MGMT_MARSHALL_INT, MGMT_MARSHALL_STRING, MGMT_MARSHALL_DATA};

  if (mgmt_message_parse(span.data(), span.size(), fields, countof(fields), &op, &tag, &payload) == -1) {
    Error("Plugin message - RPC parsing error - message discarded.");
  } else {
    msg.tag       = tag;
    msg.data      = payload.ptr;
    msg.data_size = payload.len;
    while (hook) {
      TSPluginMsg tmp(msg); // Just to make sure plugins don't mess this up for others.
      hook->invoke(TS_EVENT_LIFECYCLE_MSG, &tmp);
      hook = hook->next();
    }
  }
}

static void
init_ssl_ctx_callback(void *ctx, bool server)
{
  TSEvent event = server ? TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED : TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED;
  APIHook *hook =
    lifecycle_hooks->get(server ? TS_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED_HOOK : TS_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED_HOOK);

  while (hook) {
    hook->invoke(event, ctx);
    hook = hook->next();
  }
}

static void
load_ssl_file_callback(const char *ssl_file)
{
  pmgmt->signalConfigFileChild("ssl_multicert.config", ssl_file);
}

static void
load_remap_file_callback(const char *remap_file)
{
  pmgmt->signalConfigFileChild("remap.config", remap_file);
}

static void
task_threads_started_callback()
{
  APIHook *hook = lifecycle_hooks->get(TS_LIFECYCLE_TASK_THREADS_READY_HOOK);
  while (hook) {
    SCOPED_MUTEX_LOCK(lock, hook->m_cont->mutex, this_ethread());
    hook->invoke(TS_EVENT_LIFECYCLE_TASK_THREADS_READY, nullptr);
    hook = hook->next();
  }
}
