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

#include "iocore/aio/AIO.h"
#include "iocore/cache/Store.h"
#include "tscore/TSSystemState.h"
#include "tscore/Version.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_sys_control.h"
#include "tscore/ink_args.h"
#include "tscore/ink_hw.h"
#include "tscore/ink_lockfile.h"
#include "tscore/ink_stack_trace.h"
#include "tscore/ink_syslog.h"
#include "tscore/hugepages.h"
#include "tscore/runroot.h"
#include "tscore/Filenames.h"
#include "../iocore/net/P_Socks.h"

#include "ts/ts.h" // This is sadly needed because of us using TSThreadInit() for some reason.
#include "swoc/swoc_file.h"

#include <syslog.h>
#include <algorithm>
#include <atomic>
#include <list>
#include <string>

using namespace std::literals;

#if !defined(__linux__)
#include <sys/lock.h>
#endif

#if defined(__linux__)
extern "C" int plock(int);
#else
#include <sys/filio.h>
#endif

#if __has_include(<mcheck.h>)
#include <mcheck.h>
#endif

#include "Crash.h"
#include "tscore/signals.h"
#include "../iocore/eventsystem/P_EventSystem.h"
#include "../iocore/net/P_Net.h"
#if TS_HAS_QUICHE
#include "../iocore/net/P_QUICNetProcessor.h"
#endif
#include "../iocore/net/P_UDPNet.h"
#include "../iocore/net/P_UnixNet.h"
#include "../iocore/net/P_SSLUtils.h"
#include "../iocore/dns/P_SplitDNSProcessor.h"
#include "../iocore/hostdb/P_HostDB.h"
#include "../records/P_RecCore.h"
#include "tscore/Layout.h"
#include "iocore/utils/Machine.h"
#include "records/RecordsConfig.h"
#include "iocore/eventsystem/RecProcess.h"
#include "proxy/Transform.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/http/HttpProxyServerMain.h"
#include "proxy/http/HttpBodyFactory.h"
#include "proxy/ProxySession.h"
#include "proxy/logging/Log.h"
#include "proxy/CacheControl.h"
#include "proxy/IPAllow.h"
#include "proxy/ParentSelection.h"
#include "proxy/HostStatus.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HuffmanCodec.h"
#include "proxy/Plugin.h"
#include "proxy/shared/DiagsConfig.h"
#include "proxy/http/remap/RemapConfig.h"
#include "proxy/http/remap/RemapPluginInfo.h"
#include "proxy/http/remap/RemapProcessor.h"
#include "iocore/eventsystem/Tasks.h"
#include "api/InkAPIInternal.h"
#include "api/LifecycleAPIHooks.h"
#include "proxy/http2/HTTP2.h"
#include "tscore/ink_config.h"
#include "../iocore/net/P_SSLClientUtils.h"

// Mgmt Admin public handlers
#include "RpcAdminPubHandlers.h"

// Json Rpc stuffs
#include "mgmt/rpc/jsonrpc/JsonRPCManager.h"
#include "mgmt/rpc/server/RPCServer.h"

#include "mgmt/config/FileManager.h"

#if TS_USE_QUIC == 1
#include "proxy/http3/Http3.h"
#include "proxy/http3/Http3Config.h"
#endif

#include "tscore/ink_cap.h"

#if TS_HAS_PROFILER
#include <gperftools/profiler.h>
#include <gperftools/heap-profiler.h>
#endif

extern void load_config_file_callback(const char *parent_file, const char *remap_file);

extern HttpBodyFactory *body_factory;

extern void initializeRegistry();

extern void Initialize_Errata_Settings();

namespace
{

//
// Global Data
//
#define DEFAULT_COMMAND_FLAG 0

#define DEFAULT_DIAGS_LOG_FILENAME "diags.log"
char diags_log_filename[PATH_NAME_MAX] = DEFAULT_DIAGS_LOG_FILENAME;

const long MAX_LOGIN = ink_login_name_max();

void init_ssl_ctx_callback(void *ctx, bool server);

void        load_ssl_file_callback(const char *ssl_file);
void        task_threads_started_callback();
static void check_max_records_argument(const ArgumentDescription *arg, unsigned int nargs, const char *val);

int num_of_net_threads = 0;
int num_accept_threads = 0;

int num_of_udp_threads = 0;
int num_task_threads   = 0;

char *http_accept_port_descriptor;
bool  enable_core_file_p = false; // Enable core file dump?
int   command_flag       = DEFAULT_COMMAND_FLAG;
int   command_index      = -1;
bool  command_valid      = false;
// Commands that have special processing / requirements.
const char *CMD_VERIFY_CONFIG = "verify_config";
#if TS_HAS_TESTS
char regression_test[1024] = "";
int  regression_list       = 0;
int  regression_level      = REGRESSION_TEST_NONE;
#endif

char command_string[512] = "";
char conf_dir[512]       = "";
char bind_stdout[512]    = "";
char bind_stderr[512]    = "";

char         error_tags[1024]  = "";
char         action_tags[1024] = "";
int          show_statistics   = 0;
DiagsConfig *diagsConfig       = nullptr;

int  accept_mss           = 0;
int  poll_timeout         = -1; // No value set.
int  cmd_disable_freelist = 0;
bool signal_received[NSIG];

std::mutex              pluginInitMutex;
std::condition_variable pluginInitCheck;
bool                    plugin_init_done = false;

/*
To be able to attach with a debugger to traffic_server running in an Au test case, temporarily add the
parameter block_for_debug=True to the call to Test.MakeATSProcess().  This means Au test will wait
effectively indefinitely (10 hours) for traffic_server to initialize itself.  Run the modified Au test,
attach the debugger to the traffic_server process, set one or more breakpoints, set the variable
cmd_block to 0, then continue.  On linux, the command 'ps -ef | fgrep -e --block' will help identify the
PID of the traffic_server process (second column of output).
*/
int cmd_block = 0;

// 1: the main thread delayed accepting, start accepting.
// 0: delay accept, wait for cache initialization.
// -1: cache is already initialized, don't delay.
int delay_listen_for_cache = 0;

ArgumentDescription argument_descriptions[] = {
  {"net_threads",       'n', "Number of Net Threads",                                                                 "I",     &num_of_net_threads,             "PROXY_NET_THREADS",       nullptr                    },
  {"udp_threads",       'U', "Number of UDP Threads",                                                                 "I",     &num_of_udp_threads,             "PROXY_UDP_THREADS",       nullptr                    },
  {"accept_thread",     'a', "Use an Accept Thread",                                                                  "T",     &num_accept_threads,             "PROXY_ACCEPT_THREAD",     nullptr                    },
  {"httpport",          'p', "Port descriptor for HTTP Accept",                                                       "S*",    &http_accept_port_descriptor,    "PROXY_HTTP_ACCEPT_PORT",  nullptr                    },
  {"disable_freelist",  'f', "Disable the freelist memory allocator",                                                 "T",     &cmd_disable_freelist,           "PROXY_DPRINTF_LEVEL",     nullptr                    },
  {"disable_pfreelist", 'F', "Disable the freelist memory allocator in ProxyAllocator",                               "T",     &cmd_disable_pfreelist,
   "PROXY_DPRINTF_LEVEL",                                                                                                                                                                  nullptr                    },
  {"maxRecords",        'm', "Max number of librecords metrics and configurations (default & minimum: 2048)",         "I",     &max_records_entries,
   "PROXY_MAX_RECORDS",                                                                                                                                                                    &check_max_records_argument},

#if TS_HAS_TESTS
  {"regression",        'R', "Regression Level (quick:1..long:3)",                                                    "I",     &regression_level,               "PROXY_REGRESSION",        nullptr                    },
  {"regression_test",   'r', "Run Specific Regression Test",                                                          "S512",  regression_test,                 "PROXY_REGRESSION_TEST",   nullptr                    },
  {"regression_list",   'l', "List Regression Tests",                                                                 "T",     &regression_list,                "PROXY_REGRESSION_LIST",   nullptr                    },
#endif  // TS_HAS_TESTS

#if TS_USE_DIAGS
  {"debug_tags",        'T', "Vertical-bar-separated Debug Tags",                                                     "S1023", error_tags,                      "PROXY_DEBUG_TAGS",        nullptr                    },
  {"action_tags",       'B', "Vertical-bar-separated Behavior Tags",                                                  "S1023", action_tags,                     "PROXY_BEHAVIOR_TAGS",     nullptr                    },
#endif

  {"interval",          'i', "Statistics Interval",                                                                   "I",     &show_statistics,                "PROXY_STATS_INTERVAL",    nullptr                    },
  {"command",           'C',
   "Maintenance Command to Execute\n"
   "      Commands: list, check, clear, clear_cache, verify_config, verify_global_plugin, verify_remap_plugin, help", "S511",  &command_string,                 "PROXY_COMMAND_STRING",    nullptr                    },
  {"conf_dir",          'D', "config dir to verify",                                                                  "S511",  &conf_dir,                       "PROXY_CONFIG_CONFIG_DIR", nullptr                    },
  {"clear_cache",       'K', "Clear Cache on Startup",                                                                "F",     &cacheProcessor.auto_clear_flag, "PROXY_CLEAR_CACHE",       nullptr                    },
  {"bind_stdout",       '-', "Regular file to bind stdout to",                                                        "S512",  &bind_stdout,                    "PROXY_BIND_STDOUT",       nullptr                    },
  {"bind_stderr",       '-', "Regular file to bind stderr to",                                                        "S512",  &bind_stderr,                    "PROXY_BIND_STDERR",       nullptr                    },
  {"accept_mss",        '-', "MSS for client connections",                                                            "I",     &accept_mss,                     nullptr,                   nullptr                    },
  {"poll_timeout",      't', "poll timeout in milliseconds",                                                          "I",     &poll_timeout,                   nullptr,                   nullptr                    },
  {"block",             '-', "block for debug attach",                                                                "T",     &cmd_block,                      nullptr,                   nullptr                    },
  HELP_ARGUMENT_DESCRIPTION(),
  VERSION_ARGUMENT_DESCRIPTION(),
  RUNROOT_ARGUMENT_DESCRIPTION(),
};

DbgCtl dbg_ctl_log{"log"};
DbgCtl dbg_ctl_server{"server"};
DbgCtl dbg_ctl_tracker{"tracker"};
DbgCtl dbg_ctl_http_listen{"http_listen"};
DbgCtl dbg_ctl_threads{"threads"};
DbgCtl dbg_ctl_privileges{"privileges"};
DbgCtl dbg_ctl_diags{"diags"};
DbgCtl dbg_ctl_hugepages{"hugepages"};
DbgCtl dbg_ctl_rpc_init{"rpc.init"};
DbgCtl dbg_ctl_statsproc{"statsproc"};

struct AutoStopCont : public Continuation {
  int
  mainEvent(int /* event */, Event * /* e */)
  {
    TSSystemState::stop_ssl_handshaking();

    APIHook *hook = g_lifecycle_hooks->get(TS_LIFECYCLE_SHUTDOWN_HOOK);
    while (hook) {
      WEAK_SCOPED_MUTEX_LOCK(lock, hook->m_cont->mutex, this_ethread());
      hook->invoke(TS_EVENT_LIFECYCLE_SHUTDOWN, nullptr);
      hook = hook->next();
    }

    // if the jsonrpc feature was disabled, the object will not be created.
    if (jsonrpcServer != nullptr) {
      jsonrpcServer->stop_thread();
    }

    TSSystemState::shut_down_event_system();
    delete this;
    return EVENT_CONT;
  }

  AutoStopCont() : Continuation(new_ProxyMutex()) { SET_HANDLER(&AutoStopCont::mainEvent); }
};

class SignalContinuation : public Continuation
{
public:
  SignalContinuation() : Continuation(new_ProxyMutex()) { SET_HANDLER(&SignalContinuation::periodic); }

  int
  periodic(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    ts::Metrics &metrics  = ts::Metrics::instance();
    static auto  drain_id = metrics.lookup("proxy.process.proxy.draining");

    if (signal_received[SIGUSR1]) {
      signal_received[SIGUSR1] = false;

#if TS_HAS_JEMALLOC
      char buf[PATH_NAME_MAX] = "";
      RecGetRecordString("proxy.config.memory.malloc_stats_print_opts", buf, PATH_NAME_MAX);
      malloc_stats_print(nullptr, nullptr, buf);
#endif

      // TODO: TS-567 Integrate with debugging allocators "dump" features?
      ink_freelists_dump(stderr);
      ResourceTracker::dump(stderr);
    }

    if (signal_received[SIGUSR2]) {
      signal_received[SIGUSR2] = false;

      Dbg(dbg_ctl_log, "received SIGUSR2, reloading traffic.out");
      // reload output logfile (file is usually called traffic.out)
      diags()->set_std_output(StdStream::STDOUT, bind_stdout);
      diags()->set_std_output(StdStream::STDERR, bind_stderr);
      if (diags()->reseat_diagslog()) {
        Note("Reseated %s", diags_log_filename);
      } else {
        Note("Could not reseat %s", diags_log_filename);
      }
      // Reload any of the other moved log files (such as the ones in logging.yaml).
      Log::handle_log_rotation_request();
    }

    if (signal_received[SIGTERM] || signal_received[SIGINT]) {
      signal_received[SIGTERM] = false;
      signal_received[SIGINT]  = false;

      auto timeout{RecGetRecordInt("proxy.config.stop.shutdown_timeout")};
      if (timeout && timeout.value()) {
        metrics[drain_id].store(1);
        TSSystemState::drain(true);
        // Close listening sockets here only if TS is running standalone
        if (auto close_sockets{RecGetRecordInt("proxy.config.restart.stop_listening")}; close_sockets && close_sockets.value()) {
          stop_HttpProxyServer();
        }
      }

      Dbg(dbg_ctl_server, "received exit signal, shutting down in %" PRId64 "secs", timeout.value());

      // Shutdown in `timeout` seconds (or now if that is 0).
      eventProcessor.schedule_in(new AutoStopCont(), HRTIME_SECONDS(timeout.value()));
    }

    return EVENT_CONT;
  }
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
  DiagsLogContinuation() : Continuation(new_ProxyMutex())
  {
    SET_HANDLER(&DiagsLogContinuation::periodic);

    auto str{RecGetRecordStringAlloc("proxy.config.output.logfile.name")};
    traffic_out_name = str ? std::move(str.value()) : std::string{};
  }

  int
  periodic(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    Dbg(dbg_ctl_log, "in DiagsLogContinuation, checking on diags.log");

    // First, let us update the rolling config values for diagslog.
    int diags_log_roll_int;
    diags_log_roll_int = RecGetRecordInt("proxy.config.diags.logfile.rolling_interval_sec").value_or(0);
    int diags_log_roll_size;
    diags_log_roll_size = RecGetRecordInt("proxy.config.diags.logfile.rolling_size_mb").value_or(0);
    int diags_log_roll_enable;
    diags_log_roll_enable = RecGetRecordInt("proxy.config.diags.logfile.rolling_enabled").value_or(0);
    diags()->config_roll_diagslog(static_cast<RollingEnabledValues>(diags_log_roll_enable), diags_log_roll_int,
                                  diags_log_roll_size);

    if (diags()->should_roll_diagslog()) {
      Note("Rolled %s", diags_log_filename);
    }

    int output_log_roll_int;
    output_log_roll_int = RecGetRecordInt("proxy.config.output.logfile.rolling_interval_sec").value_or(0);
    int output_log_roll_size;
    output_log_roll_size = RecGetRecordInt("proxy.config.output.logfile.rolling_size_mb").value_or(0);
    int output_log_roll_enable;
    output_log_roll_enable = RecGetRecordInt("proxy.config.output.logfile.rolling_enabled").value_or(0);
    diags()->config_roll_outputlog(static_cast<RollingEnabledValues>(output_log_roll_enable), output_log_roll_int,
                                   output_log_roll_size);

    if (diags()->should_roll_outputlog()) {
      Note("Rolled %s", traffic_out_name.c_str());
    }
    return EVENT_CONT;
  }

private:
  std::string traffic_out_name;
};

class MemoryLimit : public Continuation
{
public:
  MemoryLimit() : Continuation(new_ProxyMutex())
  {
    memset(&_usage, 0, sizeof(_usage));
    SET_HANDLER(&MemoryLimit::periodic);
    memory_rss = Metrics::Gauge::createPtr("proxy.process.traffic_server.memory.rss");
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
    _memory_limit = RecGetRecordInt("proxy.config.memory.max_usage").value_or(0);
    _memory_limit = _memory_limit >> 10; // divide by 1024

    if (getrusage(RUSAGE_SELF, &_usage) == 0) {
      ts::Metrics::Gauge::store(memory_rss, _usage.ru_maxrss << 10); // * 1024
      Dbg(dbg_ctl_server, "memory usage - ru_maxrss: %ld memory limit: %" PRId64, _usage.ru_maxrss, _memory_limit);
      if (_memory_limit > 0) {
        if (_usage.ru_maxrss > _memory_limit) {
          if (net_memory_throttle == false) {
            net_memory_throttle = true;
            Dbg(dbg_ctl_server, "memory usage exceeded limit - ru_maxrss: %ld memory limit: %" PRId64, _usage.ru_maxrss,
                _memory_limit);
          }
        } else {
          if (net_memory_throttle == true) {
            net_memory_throttle = false;
            Dbg(dbg_ctl_server, "memory usage under limit - ru_maxrss: %ld memory limit: %" PRId64, _usage.ru_maxrss,
                _memory_limit);
          }
        }
      } else {
        // this feature has not been enabled
        Dbg(dbg_ctl_server, "limiting connections based on memory usage has been disabled");
        e->cancel();
        delete this;
        return EVENT_DONE;
      }
    }
    return EVENT_CONT;
  }

private:
  int64_t                     _memory_limit = 0;
  struct rusage               _usage;
  Metrics::Gauge::AtomicType *memory_rss;
};

/** Gate the emission of the "Traffic Server is fuly initialized" log message.
 *
 * This message is intended to be helpful to users who want to know that
 * Traffic Server is not just running but has become fully initialized and is
 * ready to optimize traffic. This is in contrast to the "traffic server is
 * running" message which can be printed before either of these conditions.
 *
 * This function is called on each initialization state transition. Currently,
 * the two state transitions of interest are:
 *
 * 1. The cache is initialized.
 * 2. The ports are open and accept has been called upon them.
 *
 * Note that Traffic Server configures the port objects and may even open the
 * ports before calling accept on those ports. The difference between these two
 * events is communicated to plugins via the
 * TS_LIFECYCLE_PORTS_INITIALIZED_HOOK and TS_LIFECYCLE_PORTS_READY_HOOK hooks.
 * If wait_for_cache is enabled, the difference in time between these events
 * may measure in the tens of milliseconds.  The message emitted by this
 * function happens after this full lifecycle takes place on these ports and
 * after cache is initialized.
 */
void
emit_fully_initialized_message()
{
  static std::atomic<unsigned int> initialization_state_counter = 0;

  // See the doxygen comment above explaining what the states are that
  // constitute Traffic Server being fully initialized.
  constexpr unsigned int num_initialization_states = 2;

  if (++initialization_state_counter == num_initialization_states) {
    Note("Traffic Server is fully initialized.");
  }
}

void
set_debug_ip(const char *ip_string)
{
  if (ip_string) {
    diags()->debug_client_ip.load(ip_string);
  } else {
    diags()->debug_client_ip.invalidate();
  }
}

int
update_debug_client_ip(const char * /*name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
                       void * /* data_type ATS_UNUSED */)
{
  set_debug_ip(data.rec_string);
  return 0;
}

int
init_memory_tracker(const char *config_var, RecDataT /* type ATS_UNUSED */, RecData data, void * /* cookie ATS_UNUSED */)
{
  static Event *tracker_event = nullptr;
  Event        *preE;
  int           dump_mem_info_frequency = 0;

  // set tracker_event to NULL, and return previous value
  preE = ink_atomic_swap(&tracker_event, static_cast<Event *>(nullptr));

  if (config_var) {
    dump_mem_info_frequency = data.rec_int;
  } else {
    dump_mem_info_frequency = RecGetRecordInt("proxy.config.dump_mem_info_frequency").value_or(0);
  }

  Dbg(dbg_ctl_tracker, "init_memory_tracker called [%d]", dump_mem_info_frequency);

  if (preE) {
    eventProcessor.schedule_imm(preE->continuation, ET_CALL);
    preE->cancel();
  }

  if (dump_mem_info_frequency > 0) {
    tracker_event = eventProcessor.schedule_every(new TrackerContinuation, HRTIME_SECONDS(dump_mem_info_frequency), ET_CALL);
  }

  return 1;
}

void
proxy_signal_handler(int signo, siginfo_t *info, void *ctx)
{
  if (static_cast<unsigned>(signo) < countof(signal_received)) {
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

  auto &version = AppVersionInfo::get_version();
  signal_format_siginfo(signo, info, version.application());

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
void
init_system()
{
  signal_register_default_handler(proxy_signal_handler);
  signal_register_crash_handler(signal_crash_handler);

  auto &version = AppVersionInfo::get_version();
  syslog(LOG_NOTICE, "NOTE: --- %s Starting ---", version.application());
  syslog(LOG_NOTICE, "NOTE: %s Version: %s", version.application(), version.full_version());

  //
  // Delimit file Descriptors
  //
  ink_set_fds_limit(ink_max_out_rlimit(RLIMIT_NOFILE));
}

void
check_lockfile()
{
  std::string rundir(RecConfigReadRuntimeDir());
  std::string lockfile;
  pid_t       holding_pid;
  int         err;

  lockfile = Layout::relative_to(rundir, SERVER_LOCK);

  Lockfile server_lockfile(lockfile.c_str());
  err = server_lockfile.Get(&holding_pid);

  if (err != 1) {
    char *reason = strerror(-err);
    fprintf(stderr, "WARNING: Can't acquire lockfile '%s'", lockfile.c_str());

    if ((err == 0) && (holding_pid != -1)) {
      fprintf(stderr, " (Lock file held by process ID %ld)\n", static_cast<long>(holding_pid));
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

void
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
// Initialize records related features.
//
static void
initialize_records()
{
  RecProcessInit(diags());
  LibRecordsConfigInit();

  check_config_directories();

  //
  // Define version info records
  //
  auto &version = AppVersionInfo::get_version();
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.short", version.version(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.long", version.full_version(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_number", version.build_number(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_time", version.build_time(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_date", version.build_date(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_machine", version.build_machine(), RECP_NON_PERSISTENT);
  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.build_person", version.build_person(), RECP_NON_PERSISTENT);
}

void
initialize_file_manager()
{
  initializeRegistry();
}

std::tuple<bool, std::string>
initialize_jsonrpc_server()
{
  std::tuple<bool, std::string> ok{true, {}};
  auto                          filePath = RecConfigReadConfigPath("proxy.config.jsonrpc.filename", ts::filename::JSONRPC);

  auto serverConfig = rpc::config::RPCConfig{};
  serverConfig.load_from_file(filePath);
  if (!serverConfig.is_enabled()) {
    Dbg(dbg_ctl_rpc_init, "JSONRPC Disabled");
    return ok;
  }

  // create and start the server.
  try {
    jsonrpcServer = new rpc::RPCServer{serverConfig};
    jsonrpcServer->start_thread(TSThreadInit, TSThreadDestroy);
  } catch (std::exception const &ex) {
    // Only the constructor throws, so if we are here there should be no
    // jsonrpcServer object.
    ink_assert(jsonrpcServer == nullptr);
    std::string msg;
    return {false, swoc::bwprint(msg, "Server failed: '{}'", ex.what())};
  }
  // Register admin handlers.
  rpc::admin::register_admin_jsonrpc_handlers();
  Dbg(dbg_ctl_rpc_init, "JSONRPC. Public admin handlers registered.");

  return ok;
}

#define CMD_ERROR       -2 // serious error, exit maintenance mode
#define CMD_FAILED      -1 // error, but recoverable
#define CMD_OK          0  // ok, or minor (user) error
#define CMD_HELP        1  // ok, print help
#define CMD_IN_PROGRESS 2  // task not completed. don't exit

int
cmd_list(char * /* cmd ATS_UNUSED */)
{
  printf("LIST\n\n");

  // show hostdb size

  int h_size = 120000;
  h_size     = RecGetRecordInt("proxy.config.hostdb.size").value_or(0);
  printf("Host Database size:\t%d\n", h_size);

  // show cache config information....

  Note("Cache Storage:");
  Store  tStore;
  Result result = tStore.read_config();

  if (result.failed()) {
    Note("Failed to read cache storage configuration: %s", result.message());
    return CMD_FAILED;
  } else {
    tStore.write_config_data(fileno(stdout));
    return CMD_OK;
  }
}

/** Parse the given string and skip the first word.
 *
 * Words are assumed to be separated by spaces or tabs.
 *
 * @param[in] cmd The string whose first word will be skipped.
 *
 * @return The pointer in the string cmd to the second word in the string, or
 * nullptr if there is no second word.
 */
char *
skip(char *cmd)
{
  // Skip initial white space.
  cmd += strspn(cmd, " \t");
  // Point to the beginning of the next white space.
  cmd = strpbrk(cmd, " \t");
  if (!cmd) {
    return cmd;
  }
  // Skip the second white space so that cmd now points to the beginning of the
  // second word.
  cmd += strspn(cmd, " \t");
  return cmd;
}

// Handler for things that need to wait until the cache is initialized.
void
CB_After_Cache_Init()
{
  APIHook *hook;
  int      start;

  start = ink_atomic_swap(&delay_listen_for_cache, -1);
  emit_fully_initialized_message();

  if (1 == start) {
    // The delay_listen_for_cache value was 1, therefore the main function
    // delayed the call to start_HttpProxyServer until we got here. We must
    // call accept on the ports now that the cache is initialized.
    Dbg(dbg_ctl_http_listen, "Delayed listen enable, cache initialization finished");
    start_HttpProxyServer();
    emit_fully_initialized_message();
  }

  ts::Metrics &metrics = ts::Metrics::instance();
  auto         id      = metrics.lookup("proxy.process.proxy.cache_ready_time");

  metrics[id].store(time(nullptr));

  // Alert the plugins the cache is initialized.
  hook = g_lifecycle_hooks->get(TS_LIFECYCLE_CACHE_READY_HOOK);
  while (hook) {
    hook->invoke(TS_EVENT_LIFECYCLE_CACHE_READY, nullptr);
    hook = hook->next();
  }
}

void
CB_cmd_cache_clear()
{
  if (cacheProcessor.IsCacheEnabled() == CacheInitState::INITIALIZED) {
    Note("CLEAR, succeeded");
    ::exit(0);
  } else if (cacheProcessor.IsCacheEnabled() == CacheInitState::FAILED) {
    Note("unable to open Cache, CLEAR failed");
    ::exit(1);
  }
}

void
CB_cmd_cache_check()
{
  int res = 0;
  if (cacheProcessor.IsCacheEnabled() == CacheInitState::INITIALIZED) {
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
  } else if (cacheProcessor.IsCacheEnabled() == CacheInitState::FAILED) {
    Note("unable to open Cache, Check failed");
    ::exit(1);
  }
}

int
cmd_check_internal(char * /* cmd ATS_UNUSED */, bool fix = false)
{
  const char *n = fix ? "REPAIR" : "CHECK";

  printf("%s\n\n", n);

  cacheProcessor.afterInitCallbackSet(&CB_cmd_cache_check);
  if (cacheProcessor.start_internal(PROCESSOR_CHECK) < 0) {
    printf("\nbad cache configuration, %s failed\n", n);
    return CMD_FAILED;
  }
  return CMD_IN_PROGRESS;
}

int
cmd_check(char *cmd)
{
  return cmd_check_internal(cmd, false);
}

#ifdef UNUSED_FUNCTION
int
cmd_repair(char *cmd)
{
  return cmd_check_internal(cmd, true);
}
#endif

int
cmd_clear(char *cmd)
{
  Note("CLEAR");

  bool c_all   = !strcmp(cmd, "clear");
  bool c_cache = !strcmp(cmd, "clear_cache");

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

int
cmd_verify(char * /* cmd ATS_UNUSED */)
{
  unsigned char exitStatus = 0; // exit status is 8 bits

  fprintf(stderr, "NOTE: VERIFY\n\n");

  // initialize logging since a plugin
  // might call TS_ERROR which needs
  // log_rsb to be init'ed
  Log::init();

  if (*conf_dir) {
    fprintf(stderr, "NOTE: VERIFY config dir: %s...\n\n", conf_dir);
    Layout::get()->update_sysconfdir(conf_dir);
  }

  api_init();
  if (!plugin_init(true)) {
    exitStatus |= (1 << 2);
    fprintf(stderr, "ERROR: Failed to load %s, exitStatus %d\n\n", ts::filename::PLUGIN, exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully loaded %s\n\n", ts::filename::PLUGIN);
  }

  if (!urlRewriteVerify()) {
    exitStatus |= (1 << 0);
    fprintf(stderr, "ERROR: Failed to load %s, exitStatus %d\n\n", ts::filename::REMAP, exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully loaded %s\n\n", ts::filename::REMAP);
  }

  if (auto ret = RecReadYamlConfigFile(); !ret.empty()) {
    exitStatus |= (1 << 1);
    fprintf(stderr, "ERROR: Failed to load %s, exitStatus %d\n\n", ts::filename::RECORDS, exitStatus);
  } else {
    fprintf(stderr, "INFO: Successfully loaded %s\n\n", ts::filename::RECORDS);
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

enum class plugin_type_t {
  GLOBAL,
  REMAP,
};

/** Attempt to load a plugin shared object file.
 *
 * Note that this function is only used to load plugins for the purpose of
 * verifying that they are valid plugins. It is not used to load plugins for
 * normal operation. Any loaded plugin will be closed immediately after loading
 * it.
 *
 * @param[in] plugin_type The type of plugin for which to create a PluginInfo.
 * @param[in] plugin_path The path to the plugin's shared object file.
 * @param[out] error Some description of why the plugin failed to load if
 * loading it fails.
 *
 * @return True if the plugin loaded successfully, false otherwise.
 */
bool
try_loading_plugin(plugin_type_t plugin_type, const fs::path &plugin_path, std::string &error)
{
  // At least one plugin checks this during plugin unload, so act like the event system is shutdown
  TSSystemState::shut_down_event_system();

  switch (plugin_type) {
  case plugin_type_t::GLOBAL: {
    void      *handle        = nullptr;
    void      *initptr       = nullptr;
    bool const plugin_loaded = plugin_dso_load(plugin_path.c_str(), handle, initptr, error);
    if (handle != nullptr) {
      dlclose(handle);
      handle = nullptr;
    }
    return plugin_loaded;
  }
  case plugin_type_t::REMAP: {
    auto temporary_directory  = fs::temp_directory_path();
    temporary_directory      /= fs::path(std::string("verify_plugin_") + std::to_string(getpid()));
    std::error_code ec;
    if (!fs::create_directories(temporary_directory, ec)) {
      std::ostringstream error_os;
      error_os << "Could not create temporary directory " << temporary_directory.string() << ": " << ec.message();
      error = error_os.str();
      return false;
    }
    const auto     runtime_path = temporary_directory / plugin_path.filename();
    const fs::path unused_config;
    auto           plugin_info = std::make_unique<RemapPluginInfo>(unused_config, plugin_path, runtime_path);
    bool           loaded      = plugin_info->load(error, unused_config); // ToDo: Will this ever need support for cripts
    if (!fs::remove(temporary_directory, ec)) {
      fprintf(stderr, "ERROR: could not remove temporary directory '%s': %s\n", temporary_directory.c_str(), ec.message().c_str());
    }
    return loaded;
  }
  }
  // Unreached.
  return false;
}

/** A helper for the verify plugin command functions.
 *
 * @param[in] args The arguments passed to the -C command option. This includes
 * verify_global_plugin.
 *
 * @param[in] symbols The expected symbols to verify exist in the plugin file.
 *
 * @return a CMD status code. See the CMD_ defines above in this file.
 */
int
verify_plugin_helper(char *args, plugin_type_t plugin_type)
{
  const auto *plugin_filename = skip(args);
  if (!plugin_filename) {
    fprintf(stderr, "ERROR: verifying a plugin requires a plugin SO file path argument\n");
    return CMD_FAILED;
  }

  fs::path plugin_path(plugin_filename);
  fprintf(stderr, "NOTE: verifying plugin '%s'...\n", plugin_filename);

  if (!fs::exists(plugin_path)) {
    fprintf(stderr, "ERROR: verifying plugin '%s' Fail: No such file or directory\n", plugin_filename);
    return CMD_FAILED;
  }

  auto        ret = CMD_OK;
  std::string error;
  if (try_loading_plugin(plugin_type, plugin_path, error)) {
    fprintf(stderr, "NOTE: verifying plugin '%s' Success\n", plugin_filename);
  } else {
    fprintf(stderr, "ERROR: verifying plugin '%s' Fail: %s\n", plugin_filename, error.c_str());
    ret = CMD_FAILED;
  }
  return ret;
}

/** Verify whether a given SO file looks like a valid global plugin.
 *
 * @param[in] args The arguments passed to the -C command option. This includes
 * verify_global_plugin.
 *
 * @return a CMD status code. See the CMD_ defines above in this file.
 */
int
cmd_verify_global_plugin(char *args)
{
  return verify_plugin_helper(args, plugin_type_t::GLOBAL);
}

/** Verify whether a given SO file looks like a valid remap plugin.
 *
 * @param[in] args The arguments passed to the -C command option. This includes
 * verify_global_plugin.
 *
 * @return a CMD status code. See the CMD_ defines above in this file.
 */
int
cmd_verify_remap_plugin(char *args)
{
  return verify_plugin_helper(args, plugin_type_t::REMAP);
}

int cmd_help(char *cmd);

const struct CMD {
  const char *n; // name
  const char *d; // description (part of a line)
  const char *h; // help string (multi-line)
  int (*f)(char *);
  bool no_process_lock; /// If set this command doesn't need a process level lock.
  bool preinit = false;
} commands[] = {
  {"list", "List cache configuration",
   "LIST\n"
   "\n"
   "FORMAT: list\n"
   "\n"
   "List the sizes of the Host Database and Cache Index,\n"
   "and the storage available to the cache.\n", cmd_list, false},
  {"check", "Check the cache (do not make any changes)",
   "CHECK\n"
   "\n"
   "FORMAT: check\n"
   "\n"
   "Check the cache for inconsistencies or corruption.\n"
   "CHECK does not make any changes to the data stored in\n"
   "the cache. CHECK requires a scan of the contents of the\n"
   "cache and may take a long time for large caches.\n", cmd_check, true},
  {"clear", "Clear the entire cache",
   "CLEAR\n"
   "\n"
   "FORMAT: clear\n"
   "\n"
   "Clear the entire cache.  All data in the cache is\n"
   "lost and the cache is reconfigured based on the current\n"
   "description of database sizes and available storage.\n", cmd_clear, false},
  {"clear_cache", "Clear the document cache",
   "CLEAR_CACHE\n"
   "\n"
   "FORMAT: clear_cache\n"
   "\n"
   "Clear the document cache.  All documents in the cache are\n"
   "lost and the cache is reconfigured based on the current\n"
   "description of database sizes and available storage.\n", cmd_clear, false},
  {"clear_hostdb", "Clear the hostdb cache",
   "CLEAR_HOSTDB\n"
   "\n"
   "FORMAT: clear_hostdb\n"
   "\n"
   "Clear the entire hostdb cache.  All host name resolution\n"
   "information is lost.\n", cmd_clear, false},
  {CMD_VERIFY_CONFIG, "Verify the config",
   "\n"
   "\n"
   "FORMAT: verify_config\n"
   "\n"
   "Load the config and verify traffic_server comes up correctly. \n", cmd_verify, true},
  {"verify_global_plugin", "Verify a global plugin's shared object file",
   "VERIFY_GLOBAL_PLUGIN\n"
   "\n"
   "FORMAT: verify_global_plugin [global_plugin_so_file]\n"
   "\n"
   "Load a global plugin's shared object file and verify it meets\n"
   "minimal plugin API requirements. \n", cmd_verify_global_plugin, true, true},
  {"verify_remap_plugin", "Verify a remap plugin's shared object file",
   "VERIFY_REMAP_PLUGIN\n"
   "\n"
   "FORMAT: verify_remap_plugin [remap_plugin_so_file]\n"
   "\n"
   "Load a remap plugin's shared object file and verify it meets\n"
   "minimal plugin API requirements. \n", cmd_verify_remap_plugin, true, true},
  {"help", "Obtain a short description of a command (e.g. 'help clear')",
   "HELP\n"
   "\n"
   "FORMAT: help [command_name]\n"
   "\n"
   "EXAMPLES: help help\n"
   "          help commit\n"
   "\n"
   "Provide a short description of a command (like this).\n", cmd_help, false},
};

int
find_cmd_index(const char *p)
{
  p += strspn(p, " \t");
  for (unsigned c = 0; c < countof(commands); c++) {
    const char *l = commands[c].n;
    while (l) {
      const char *s    = strchr(l, '/');
      const char *e    = strpbrk(p, " \t\n");
      int         len  = s ? s - l : strlen(l);
      int         lenp = e ? e - p : strlen(p);
      if ((len == lenp) && !strncasecmp(p, l, len)) {
        return c;
      }
      l = s ? s + 1 : nullptr;
    }
  }
  return -1;
}

/** Print the maintenance command help output.
 */
void
print_cmd_help()
{
  for (unsigned i = 0; i < countof(commands); i++) {
    printf("%25s  %s\n", commands[i].n, commands[i].d);
  }
}

int
cmd_help(char *cmd)
{
  (void)cmd;
  printf("HELP\n\n");
  cmd = skip(cmd);
  if (!cmd) {
    print_cmd_help();
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

void
check_fd_limit()
{
  int check_throttle = -1;
  int fds_limit      = static_cast<int>(ink_get_fds_limit());
  check_throttle     = RecGetRecordInt("proxy.config.net.connections_throttle").value_or(0);
  if (check_throttle > fds_limit - THROTTLE_FD_HEADROOM) {
    int new_fds_throttle = fds_limit - THROTTLE_FD_HEADROOM;
    if (new_fds_throttle < 1) {
      ink_abort("too few file descriptors (%d) available", fds_limit);
    }
    char msg[256];
    snprintf(msg, sizeof(msg),
             "connection throttle too high, "
             "%d (throttle) + %d (internal use) > %d (file descriptor limit), "
             "using throttle of %d",
             check_throttle, THROTTLE_FD_HEADROOM, fds_limit, new_fds_throttle);
    Warning("%s", msg);
  }
}

//
// Command mode
//
int
cmd_mode()
{
  if (command_index >= 0) {
    return commands[command_index].f(command_string);
  } else if (*command_string) {
    Warning("unrecognized command: '%s'", command_string);
    printf("\n");
    printf("WARNING: Unrecognized command: '%s'\n", command_string);
    printf("\n");
    print_cmd_help();
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
void
check_for_root_uid()
{
  if ((getuid() == 0) || (geteuid() == 0)) {
    ProcessFatal("Traffic Server must not be run as root");
  }
}
#endif

int
set_core_size(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
              void * /* opaque_token ATS_UNUSED */)
{
  RecInt        size = data.rec_int;
  struct rlimit lim;
  bool          failed = false;

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

void
init_core_size()
{
  auto coreSize{RecGetRecordInt("proxy.config.core_limit")};
  auto found{coreSize.has_value()};

  if (!found) {
    Warning("Unable to determine core limit");
  } else {
    RecData rec_temp;
    rec_temp.rec_int = coreSize.value();
    set_core_size(nullptr, RECD_INT, rec_temp, nullptr);
    found = (RecRegisterConfigUpdateCb("proxy.config.core_limit", set_core_size, nullptr) == REC_ERR_OKAY);

    ink_assert(found);
  }
}

void
adjust_sys_settings()
{
  struct rlimit lim;
  int           cfg_fds_throttle = -1;
  rlim_t        maxfiles;

  maxfiles = ink_get_max_files();
  if (maxfiles != RLIM_INFINITY) {
    float file_max_pct = 0.9;

    if (auto tmp{RecGetRecordFloat("proxy.config.system.file_max_pct")}; tmp) {
      file_max_pct = tmp.value();
    }
    if (file_max_pct > 1.0) {
      file_max_pct = 1.0;
    }

    lim.rlim_cur = lim.rlim_max = static_cast<rlim_t>(maxfiles * file_max_pct);
    if (setrlimit(RLIMIT_NOFILE, &lim) == 0 && getrlimit(RLIMIT_NOFILE, &lim) == 0) {
      ink_set_fds_limit(lim.rlim_cur);
      syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)", RLIMIT_NOFILE, static_cast<int>(lim.rlim_cur),
             static_cast<int>(lim.rlim_max));
    }
  }

  cfg_fds_throttle = RecGetRecordInt("proxy.config.net.connections_throttle").value_or(0);

  if (getrlimit(RLIMIT_NOFILE, &lim) == 0) {
    if (cfg_fds_throttle > static_cast<int>(lim.rlim_cur - THROTTLE_FD_HEADROOM)) {
      lim.rlim_cur = (lim.rlim_max = static_cast<rlim_t>(cfg_fds_throttle + THROTTLE_FD_HEADROOM));
      if (setrlimit(RLIMIT_NOFILE, &lim) == 0 && getrlimit(RLIMIT_NOFILE, &lim) == 0) {
        ink_set_fds_limit(lim.rlim_cur);
        syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)", RLIMIT_NOFILE, static_cast<int>(lim.rlim_cur),
               static_cast<int>(lim.rlim_max));
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
  int     cycle    = 0;
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
    int64_t d_rb  = Metrics::Counter::load(net_rsb.calls_to_readfromnet) - last_rb;
    last_rb      += d_rb;

    int64_t d_wb  = Metrics::Counter::load(net_rsb.calls_to_writetonet) - last_wb;
    last_wb      += d_wb;

    int64_t d_nrb  = Metrics::Counter::load(net_rsb.read_bytes) - last_nrb;
    last_nrb      += d_nrb;
    int64_t d_nr   = Metrics::Counter::load(net_rsb.read_bytes_count) - last_nr;
    last_nr       += d_nr;

    int64_t d_nwb  = Metrics::Counter::load(net_rsb.write_bytes) - last_nwb;
    last_nwb      += d_nwb;
    int64_t d_nw   = Metrics::Counter::load(net_rsb.write_bytes_count) - last_nw;
    last_nw       += d_nw;

    int64_t d_o = Metrics::Gauge::load(net_rsb.connections_currently_open);
    int64_t d_p = Metrics::Counter::load(net_rsb.handler_run) - last_p;

    last_p += d_p;
    printf("%" PRId64 ":%" PRId64 ":%" PRId64 ":%" PRId64 " %" PRId64 ":%" PRId64 " %" PRId64 " %" PRId64 "\n", d_rb, d_wb, d_nrb,
           d_nr, d_nwb, d_nw, d_o, d_p);
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
void
syslog_log_configure()
{
  char sys_var[] = "proxy.config.syslog_facility";
  if (auto facility_str{RecGetRecordStringAlloc(sys_var)}; facility_str) {
    int facility = facility_string_to_int(ats_as_c_str(facility_str));
    if (facility < 0) {
      syslog(LOG_WARNING, "Bad syslog facility in %s. Keeping syslog at LOG_DAEMON", ts::filename::RECORDS);
    } else {
      Dbg(dbg_ctl_server, "Setting syslog facility to %d", facility);
      closelog();
      openlog("traffic_server", LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility);
    }
  } else {
    syslog(LOG_WARNING, "Missing syslog facility config %s. Keeping syslog at LOG_DAEMON", sys_var);
  }
}

void
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
    if (!initialized && (cacheProcessor.IsCacheEnabled() != CacheInitState::INITIALIZED)) {
      printf("Regression waiting for the cache to be ready... %d\n", ++waits);
      return EVENT_CONT;
    }

    char *rt = const_cast<char *>(regression_test[0] == 0 ? "" : regression_test);
    if (!initialized && RegressionTest::run(rt, regression_level) == REGRESSION_TEST_INPROGRESS) {
      initialized = 1;
      return EVENT_CONT;
    }

    if ((res = RegressionTest::check_status(regression_level)) == REGRESSION_TEST_INPROGRESS) {
      return EVENT_CONT;
    }

    TSSystemState::shut_down_event_system();
    fprintf(stderr, "REGRESSION_TEST DONE: %s\n", regression_status_string(res));

    return EVENT_DONE;
  }

  RegressionCont() : Continuation(new_ProxyMutex()) { SET_HANDLER(&RegressionCont::mainEvent); }
};

void
run_RegressionTest()
{
  if (regression_level) {
    // Call this so that Diags.cc will send diagnostic output to stderr.
    tell_diags_regression_testing_is_on();

    eventProcessor.schedule_every(new RegressionCont(), HRTIME_SECONDS(1));
  }
}
#endif // TS_HAS_TESTS

void
chdir_root()
{
  std::string prefix = Layout::get()->prefix;

  auto &version = AppVersionInfo::get_version();
  if (chdir(prefix.c_str()) < 0) {
    fprintf(stderr, "%s: unable to change to root directory \"%s\" [%d '%s']\n", version.application(), prefix.c_str(), errno,
            strerror(errno));
    fprintf(stderr, "%s: please correct the path or set the TS_ROOT environment variable\n", version.application());
    ::exit(1);
  } else {
    printf("%s: using root directory '%s'\n", version.application(), prefix.c_str());
  }
}

int
adjust_num_of_net_threads(int nthreads)
{
  float autoconfig_scale   = 1.0;
  int   nth_auto_config    = 1;
  int   num_of_threads_tmp = 1;

  nth_auto_config = RecGetRecordInt("proxy.config.exec_thread.autoconfig.enabled").value_or(0);

  Dbg(dbg_ctl_threads, "initial number of net threads: %d", nthreads);
  Dbg(dbg_ctl_threads, "net threads auto-configuration: %s", nth_auto_config ? "enabled" : "disabled");

  if (!nth_auto_config) {
    num_of_threads_tmp = RecGetRecordInt("proxy.config.exec_thread.limit").value_or(0);

    if (num_of_threads_tmp <= 0) {
      num_of_threads_tmp = 1;
    } else if (num_of_threads_tmp > MAX_EVENT_THREADS) {
      num_of_threads_tmp = MAX_EVENT_THREADS;
    }

    nthreads = num_of_threads_tmp;
  } else { /* autoconfig is enabled */
    num_of_threads_tmp = nthreads;
    if (auto tmp{RecGetRecordFloat("proxy.config.exec_thread.autoconfig.scale")}; tmp) {
      autoconfig_scale = tmp.value();
    }
    num_of_threads_tmp = static_cast<int>(static_cast<float>(num_of_threads_tmp) * autoconfig_scale);

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

  Dbg(dbg_ctl_threads, "adjusted number of net threads: %d", nthreads);
  return nthreads;
}

/**
 * Change the uid and gid to what is in the passwd entry for supplied user name.
 * @param user User name in the passwd file to change the uid and gid to.
 */
void
change_uid_gid(const char *user)
{
#if !TS_USE_POSIX_CAP
  if (auto enabled{RecGetRecordInt("proxy.config.ssl.cert.load_elevated")}; enabled && enabled.value()) {
    Warning("ignoring proxy.config.ssl.cert.load_elevated because Traffic Server was built without POSIX capabilities support");
  }

  if (auto enabled{RecGetRecordInt("proxy.config.plugin.load_elevated")}; enabled && enabled.value()) {
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

  Dbg(dbg_ctl_privileges, "switching to unprivileged user '%s'", user);
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
              "\t%s file to list a non-root user.\n",
              ts::filename::RECORDS);
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
bind_outputs(const char *bind_stdout_p, const char *bind_stderr_p)
{
  int          log_fd;
  unsigned int flags = O_WRONLY | O_APPEND | O_CREAT | O_SYNC;

  if (*bind_stdout_p != 0) {
    Dbg(dbg_ctl_log, "binding stdout to %s", bind_stdout_p);
    log_fd = elevating_open(bind_stdout_p, flags, 0644);
    if (log_fd < 0) {
      fprintf(stdout, "[Warning]: TS unable to open log file \"%s\" [%d '%s']\n", bind_stdout_p, errno, strerror(errno));
    } else {
      Dbg(dbg_ctl_log, "duping stdout");
      dup2(log_fd, STDOUT_FILENO);
      close(log_fd);
    }
  }
  if (*bind_stderr_p != 0) {
    Dbg(dbg_ctl_log, "binding stderr to %s", bind_stderr_p);
    log_fd = elevating_open(bind_stderr_p, O_WRONLY | O_APPEND | O_CREAT | O_SYNC, 0644);
    if (log_fd < 0) {
      fprintf(stdout, "[Warning]: TS unable to open log file \"%s\" [%d '%s']\n", bind_stderr_p, errno, strerror(errno));
    } else {
      Dbg(dbg_ctl_log, "duping stderr");
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }
  }
}

#if TS_USE_LINUX_IO_URING
// Load config items for io_uring
void
configure_io_uring()
{
  IOUringConfig cfg;

  RecInt aio_io_uring_queue_entries = cfg.queue_entries;
  RecInt aio_io_uring_sq_poll_ms    = cfg.sq_poll_ms;
  RecInt aio_io_uring_attach_wq     = cfg.attach_wq;
  RecInt aio_io_uring_wq_bounded    = cfg.wq_bounded;
  RecInt aio_io_uring_wq_unbounded  = cfg.wq_unbounded;

  aio_io_uring_queue_entries = RecGetRecordInt("proxy.config.io_uring.entries").value_or(0);
  aio_io_uring_sq_poll_ms    = RecGetRecordInt("proxy.config.io_uring.sq_poll_ms").value_or(0);
  aio_io_uring_attach_wq     = RecGetRecordInt("proxy.config.io_uring.attach_wq").value_or(0);
  aio_io_uring_wq_bounded    = RecGetRecordInt("proxy.config.io_uring.wq_workers_bounded").value_or(0);
  aio_io_uring_wq_unbounded  = RecGetRecordInt("proxy.config.io_uring.wq_workers_unbounded").value_or(0);

  cfg.queue_entries = aio_io_uring_queue_entries;
  cfg.sq_poll_ms    = aio_io_uring_sq_poll_ms;
  cfg.attach_wq     = aio_io_uring_attach_wq;
  cfg.wq_bounded    = aio_io_uring_wq_bounded;
  cfg.wq_unbounded  = aio_io_uring_wq_unbounded;

  IOUringContext::set_config(cfg);
}
#endif

} // end anonymous namespace

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

  // Override default swoc::Errata settings.
  Initialize_Errata_Settings();

  pcre_malloc = ats_malloc;
  pcre_free   = ats_free;

  // Define the version info
  auto &version = AppVersionInfo::setup_version("traffic_server");

  runroot_handler(argv);
  // Before accessing file system initialize Layout engine
  Layout::create();
  // Let's be clear on what exactly is starting up.
  printf("Traffic Server " PACKAGE_VERSION "-" BUILD_NUMBER " " __DATE__ " " __TIME__ " " BUILD_MACHINE "\n");
  chdir_root(); // change directory to the install root of traffic server.

  std::sort(argument_descriptions, argument_descriptions + countof(argument_descriptions),
            [](ArgumentDescription const &a, ArgumentDescription const &b) { return 0 > strcasecmp(a.name, b.name); });

  process_args(&version, argument_descriptions, countof(argument_descriptions), argv);
  command_flag  = command_flag || *command_string;
  command_index = find_cmd_index(command_string);
  command_valid = command_flag && command_index >= 0;

  // Attach point when TS is blocked for debugging is in this loop.
  //
  while (cmd_block) {
    sleep(1);
  }

  ink_freelist_init_ops(cmd_disable_freelist, cmd_disable_pfreelist);

#if TS_HAS_TESTS
  if (regression_list) {
    RegressionTest::list();
    ::exit(0);
  }
#endif

  // Bootstrap syslog.  Since we haven't read records.yaml
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
  diagsConfig = new DiagsConfig("Server", DEFAULT_DIAGS_LOG_FILENAME, error_tags, action_tags, false);
  diags()->set_std_output(StdStream::STDOUT, bind_stdout);
  diags()->set_std_output(StdStream::STDERR, bind_stderr);
  if (dbg_ctl_diags.on()) {
    diags()->dump();
  }

  // Bind stdout and stderr to specified switches
  // Still needed despite the set_std{err,out}_output() calls later since there are
  // fprintf's before those calls
  bind_outputs(bind_stdout, bind_stderr);

  if (command_valid && commands[command_index].preinit) {
    int cmd_ret = cmd_mode();
    if (cmd_ret >= 0) {
      ::exit(0); // everything is OK
    } else {
      ::exit(1); // in error
    }
  }

  // Records init
  initialize_records();

  // Initialize file manager for TS.
  initialize_file_manager();

  // Set the core limit for the process
  init_core_size();
  init_system();

  // Adjust system and process settings
  adjust_sys_settings();

  // Restart syslog now that we have configuration info
  syslog_log_configure();

  // Register stats
  ts::Metrics &metrics = ts::Metrics::instance();
  int32_t      id;

  id = Metrics::Gauge::create("proxy.process.proxy.reconfigure_time");
  metrics[id].store(time(nullptr));
  id = Metrics::Gauge::create("proxy.process.proxy.start_time");
  metrics[id].store(time(nullptr));
  // These all gets initialied to 0
  Metrics::Gauge::create("proxy.process.proxy.reconfigure_required");
  Metrics::Gauge::create("proxy.process.proxy.restart_required");
  Metrics::Gauge::create("proxy.process.proxy.draining");
  // This gets updated later (in the callback)
  Metrics::Gauge::create("proxy.process.proxy.cache_ready_time");

  // init huge pages
  int enabled;
  enabled = RecGetRecordInt("proxy.config.allocator.hugepages").value_or(0);
  ats_hugepage_init(enabled);
  Dbg(dbg_ctl_hugepages, "ats_pagesize reporting %zu", ats_pagesize());
  Dbg(dbg_ctl_hugepages, "ats_hugepage_size reporting %zu", ats_hugepage_size());

  if (!num_accept_threads) {
    num_accept_threads = RecGetRecordInt("proxy.config.accept_threads").value_or(0);
  }

  if (!num_task_threads) {
    num_task_threads = RecGetRecordInt("proxy.config.task_threads").value_or(0);
  }

  ats_scoped_str user(MAX_LOGIN + 1);
  *user = '\0';
  auto user_view{RecGetRecordString("proxy.config.admin.user_id", user, MAX_LOGIN)};

  admin_user_p = (user_view && !user_view.value().empty() && user_view.value() != "#-1"sv);

  // Set up crash logging. We need to do this while we are still privileged so that the crash
  // logging helper runs as root. Don't bother setting up a crash logger if we are going into
  // command mode since that's not going to daemonize or run for a long time unattended.
  if (!command_flag) {
    crash_logger_init(user);
    signal_register_crash_handler(crash_logger_invoke);
  }

  // Clean out any remnant temporary plugin (UUID named) directories.
  PluginFactory::cleanup();

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
  EThread *main_thread = new EThread;
  main_thread->set_specific();

  // Re-initialize diagsConfig based on records.yaml configuration
  RecGetRecordString("proxy.config.diags.logfile.filename", diags_log_filename, sizeof(diags_log_filename));
  if (strnlen(diags_log_filename, sizeof(diags_log_filename)) == 0) {
    strncpy(diags_log_filename, DEFAULT_DIAGS_LOG_FILENAME, sizeof(diags_log_filename));
  }
  DiagsConfig *old_log = diagsConfig;
  diagsConfig          = new DiagsConfig("Server", diags_log_filename, error_tags, action_tags, true);
  RecSetDiags(diags());
  diags()->set_std_output(StdStream::STDOUT, bind_stdout);
  diags()->set_std_output(StdStream::STDERR, bind_stderr);
  if (dbg_ctl_diags.on()) {
    diags()->dump();
  }

  if (old_log) {
    delete (old_log);
    old_log = nullptr;
  }

  DebugCapabilities(dbg_ctl_privileges); // Can do this now, logging is up.

// Check if we should do mlockall()
#if defined(MCL_FUTURE)
  int mlock_flags = 0;
  mlock_flags     = RecGetRecordInt("proxy.config.mlock_enabled").value_or(0);

  if (mlock_flags == 2) {
    if (0 != mlockall(MCL_CURRENT | MCL_FUTURE)) {
      Warning("Unable to mlockall() on startup");
    } else {
      Dbg(dbg_ctl_server, "Successfully called mlockall()");
    }
  }
#endif

  // Pick the system clock to choose, likely only on Linux. See <linux/time.h>.
  extern int gSystemClock; // 0 == CLOCK_REALTIME, the default
  gSystemClock = RecGetRecordInt("proxy.config.system_clock").value_or(0);

  if (!command_flag) { // No need if we are going into command mode.
    // JSONRPC server and handlers
    if (auto &&[ok, msg] = initialize_jsonrpc_server(); !ok) {
      Warning("JSONRPC server could not be started.\n  Why?: '%s' ... Continuing without it.", msg.c_str());
    }
  }

  // setup callback for tracking remap included files
  load_remap_file_cb = load_config_file_callback;

  // We need to do this early so we can initialize the Machine
  // singleton, which depends on configuration values loaded in this.
  // We want to initialize Machine as early as possible because it
  // has other dependencies. Hopefully not in prep_HttpProxyServer().
  HttpConfig::startup();
#if TS_USE_QUIC == 1
  ts::Http3Config::startup();
#endif

  /* Set up the machine with the outbound address if that's set,
     or the inbound address if set, otherwise let it default.
  */
  swoc::IPEndpoint machine_addr;
  ink_zero(machine_addr);
  if (HttpConfig::m_master.outbound.has_ip4()) {
    machine_addr.assign(HttpConfig::m_master.outbound.ip4());
  } else if (HttpConfig::m_master.outbound.has_ip6()) {
    machine_addr.assign(HttpConfig::m_master.outbound.ip6());
  } else if (HttpConfig::m_master.inbound.has_ip4()) {
    machine_addr.assign(HttpConfig::m_master.inbound.ip4());
  } else if (HttpConfig::m_master.inbound.has_ip6()) {
    machine_addr.assign(HttpConfig::m_master.inbound.ip6());
  }

  {
    auto rec_str{RecGetRecordStringAlloc("proxy.config.log.hostname")};
    auto hostname{ats_as_c_str(rec_str)};
    if (hostname != nullptr || std::string_view(hostname) == "localhost"sv) {
      // The default value was used. Let Machine::init derive the hostname.
      hostname = nullptr;
    }
    Machine::init(hostname, &machine_addr.sa);
  }

  RecRegisterStatString(RECT_PROCESS, "proxy.process.version.server.uuid", (char *)Machine::instance()->uuid.getString(),
                        RECP_NON_PERSISTENT);

  res_track_memory = RecGetRecordInt("proxy.config.res_track_memory").value_or(0);

  init_http_header();
  ts_session_protocol_well_known_name_indices_init();

  // Sanity checks
  check_fd_limit();

// Alter the frequencies at which the update threads will trigger
#define SET_INTERVAL(scope, name, var)                \
  do {                                                \
    Dbg(dbg_ctl_statsproc, "Looking for %s", name);   \
    if (auto tmpint{RecGetRecordInt(name)}; tmpint) { \
      Dbg(dbg_ctl_statsproc, "Found %s", name);       \
      scope##_set_##var(tmpint.value());              \
    }                                                 \
  } while (0)
  SET_INTERVAL(RecProcess, "proxy.config.config_update_interval_ms", config_update_interval_ms);
  SET_INTERVAL(RecProcess, "proxy.config.raw_stat_sync_interval_ms", raw_stat_sync_interval_ms);
  SET_INTERVAL(RecProcess, "proxy.config.remote_sync_interval_ms", remote_sync_interval_ms);

  num_of_net_threads = ink_number_of_processors();
  Dbg(dbg_ctl_threads, "number of processors: %d", num_of_net_threads);
  num_of_net_threads = adjust_num_of_net_threads(num_of_net_threads);

  size_t stacksize;
  stacksize = RecGetRecordInt("proxy.config.thread.default.stacksize").value_or(0);

  // This has some special semantics, in that providing this configuration on
  // command line has higher priority than what is set in records.yaml.
  if (-1 != poll_timeout) {
    EThread::default_wait_interval_ms = poll_timeout;
  } else {
    EThread::default_wait_interval_ms = RecGetRecordInt("proxy.config.net.poll_timeout").value_or(0);
  }

  // This shouldn't happen, but lets make sure we run somewhat reasonable.
  if (EThread::default_wait_interval_ms < 0) {
    EThread::default_wait_interval_ms = 10; // Default value for all platform.
  }

  thread_max_heartbeat_mseconds = RecGetRecordInt("proxy.config.thread.max_heartbeat_mseconds").value_or(0);

#if TS_USE_LINUX_IO_URING
  configure_io_uring();
#endif

  ink_event_system_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_net_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_aio_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_cache_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));
  ink_hostdb_init(
    ts::ModuleVersion(HOSTDB_MODULE_INTERNAL_VERSION._major, HOSTDB_MODULE_INTERNAL_VERSION._minor, ts::ModuleVersion::PRIVATE));
  ink_dns_init(
    ts::ModuleVersion(HOSTDB_MODULE_INTERNAL_VERSION._major, HOSTDB_MODULE_INTERNAL_VERSION._minor, ts::ModuleVersion::PRIVATE));
  ink_split_dns_init(ts::ModuleVersion(1, 0, ts::ModuleVersion::PRIVATE));

  // Do the inits for NetProcessors that use ET_NET threads. MUST be before starting those threads.
  netProcessor.init();
  prep_HttpProxyServer();

#if TS_USE_QUIC == 1
  // OK, pushing a spawn scheduling here
  quic_NetProcessor.init();
#endif

  // If num_accept_threads == 0, let the ET_NET threads set the condition variable,
  // Else we set it here so when checking the condition variable later it returns immediately.
  if (num_accept_threads == 0 || command_flag) {
    eventProcessor.thread_group[ET_NET]._afterStartCallback = init_HttpProxyServer;
  } else {
    std::unique_lock<std::mutex> lock(proxyServerMutex);
    et_net_threads_ready = true;
    lock.unlock();
    proxyServerCheck.notify_one();
  }

#if TS_USE_LINUX_IO_URING == 1
  IOUringContext *ur = IOUringContext::local_context();
  if (ur->valid()) {
    IOUringContext::set_main_queue(ur);
    auto [bounded, unbounded] = ur->get_wq_max_workers();
    Note("io_uring: WQ workers - bounded = %d, unbounded = %d", bounded, unbounded);
  } else {
    Note("io_uring: Not supported");
  }
#endif

  // !! ET_NET threads start here !!
  // This means any spawn scheduling must be done before this point.
  eventProcessor.start(num_of_net_threads, stacksize);

  eventProcessor.schedule_every(new SignalContinuation, HRTIME_MSECOND * 500, ET_CALL);
  eventProcessor.schedule_every(new DiagsLogContinuation, HRTIME_SECOND, ET_TASK);
  eventProcessor.schedule_every(new MemoryLimit, HRTIME_SECOND * 10, ET_TASK);
  RecRegisterConfigUpdateCb("proxy.config.dump_mem_info_frequency", init_memory_tracker, nullptr);
  init_memory_tracker(nullptr, RECD_NULL, RecData(), nullptr);

  {
    auto s{RecGetRecordStringAlloc("proxy.config.diags.debug.client_ip")};
    if (auto p{ats_as_c_str(s)}; p) {
      // Translate string to IpAddr
      set_debug_ip(p);
    }
  }
  RecRegisterConfigUpdateCb("proxy.config.diags.debug.client_ip", update_debug_client_ip, nullptr);

  // log initialization moved down

  if (command_flag) {
    int cmd_ret = cmd_mode();

    if (cmd_ret != CMD_IN_PROGRESS) {
      // Check the condition variable.
      {
        std::unique_lock<std::mutex> lock(proxyServerMutex);
        proxyServerCheck.wait(lock, [] { return et_net_threads_ready; });
      }

      {
        std::unique_lock<std::mutex> lock(pluginInitMutex);
        plugin_init_done = true;
      }

      pluginInitCheck.notify_one();

      if (cmd_ret >= 0) {
        ::exit(0); // everything is OK
      } else {
        ::exit(1); // in error
      }
    }
  } else {
    // "Task" processor, possibly with its own set of task threads.
    // We don't need task threads in the "command_flag" case.
    tasksProcessor.register_event_type();
    eventProcessor.thread_group[ET_TASK]._afterStartCallback = task_threads_started_callback;
    tasksProcessor.start(num_task_threads, stacksize);

    RecProcessStart();
    initCacheControl();
    IpAllow::startup();
    HostStatus::instance().loadFromPersistentStore();
    netProcessor.init_socks();
    ParentConfig::startup();
    SplitDNSConfig::startup();

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

    hostDBProcessor.start();

    // initialize logging (after event and net processor)
    Log::init();

    (void)parsePluginConfig();

    // Init plugins as soon as logging is ready.
    api_init();
    (void)plugin_init(); // plugin.config

    {
      std::unique_lock<std::mutex> lock(pluginInitMutex);
      plugin_init_done = true;
      lock.unlock();
      pluginInitCheck.notify_one();
    }

    if (IpAllow::has_no_rules()) {
      Error("No ip_allow.yaml entries found.  All requests will be denied!");
    }

    SSLConfigParams::init_ssl_ctx_cb  = init_ssl_ctx_callback;
    SSLConfigParams::load_ssl_file_cb = load_ssl_file_callback;
    sslNetProcessor.start(-1, stacksize);
#if TS_USE_QUIC == 1
    quic_NetProcessor.start(-1, stacksize);
#endif
    FileManager::instance().registerConfigPluginCallbacks(global_config_cbs);
    cacheProcessor.afterInitCallbackSet(&CB_After_Cache_Init);
    cacheProcessor.start();

    // UDP net-threads are turned off by default.
    if (!num_of_udp_threads) {
      num_of_udp_threads = RecGetRecordInt("proxy.config.udp.threads").value_or(0);
    }

    udpNet.register_event_type();
    if (num_of_udp_threads) {
      udpNet.start(num_of_udp_threads, stacksize);
      eventProcessor.thread_group[ET_UDP]._afterStartCallback = init_HttpProxyServer;
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
    http_enabled     = RecGetRecordInt("proxy.config.http.enabled").value_or(0);

    if (http_enabled) {
      // call the ready hooks before we start accepting connections.
      APIHook *hook = g_lifecycle_hooks->get(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK);
      while (hook) {
        hook->invoke(TS_EVENT_LIFECYCLE_PORTS_INITIALIZED, nullptr);
        hook = hook->next();
      }

      int delay_p = 0;
      delay_p     = RecGetRecordInt("proxy.config.http.wait_for_cache").value_or(0);

      // Check the condition variable.
      {
        std::unique_lock<std::mutex> lock(proxyServerMutex);
        proxyServerCheck.wait(lock, [] { return et_net_threads_ready; });
      }

#if TS_USE_QUIC == 1
      if (num_of_udp_threads) {
        std::unique_lock<std::mutex> lock(etUdpMutex);
        etUdpCheck.wait(lock, [] { return et_udp_threads_ready; });
      }
#endif
      // Delay only if config value set and flag value is zero
      // (-1 => cache already initialized)
      if (delay_p && ink_atomic_cas(&delay_listen_for_cache, 0, 1)) {
        Dbg(dbg_ctl_http_listen, "Delaying listen, waiting for cache initialization");
      } else {
        // If we've come here, either:
        //
        // 1. The user did not configure wait_for_cache, and/or
        // 2. The previous delay_listen_for_cache value was not 0, thus the cache
        //    must have been initialized already.
        //
        // In either case we should not delay to accept the ports.
        Dbg(dbg_ctl_http_listen, "Not delaying listen");
        start_HttpProxyServer(); // PORTS_READY_HOOK called from in here
        emit_fully_initialized_message();
      }
    }
    // Plugins can register their own configuration names so now after they've done that
    // check for unexpected names. This is very late because remap plugins must be allowed to
    // fire up as well.
    RecConfigWarnIfUnregistered();

    if (netProcessor.socks_conf_stuff->accept_enabled) {
      start_SocksProxy(netProcessor.socks_conf_stuff->accept_port);
    }

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

  TSSystemState::initialization_done();

  while (!TSSystemState::is_event_system_shut_down()) {
#if TS_USE_LINUX_IO_URING == 1
    if (ur->valid()) {
      ur->submit_and_wait(1 * HRTIME_SECOND);
    } else {
      sleep(1);
    }
#else
    sleep(1);
#endif
  }

  delete main_thread;

#if TS_HAS_TESTS
  if (RegressionTest::check_status(regression_level) == REGRESSION_TEST_PASSED) {
    std::exit(0);
  } else {
    std::exit(1);
  }
#endif
}

namespace
{

void
init_ssl_ctx_callback(void *ctx, bool server)
{
  TSEvent  event = server ? TS_EVENT_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED : TS_EVENT_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED;
  APIHook *hook =
    g_lifecycle_hooks->get(server ? TS_LIFECYCLE_SERVER_SSL_CTX_INITIALIZED_HOOK : TS_LIFECYCLE_CLIENT_SSL_CTX_INITIALIZED_HOOK);

  while (hook) {
    hook->invoke(event, ctx);
    hook = hook->next();
  }
}

void
load_ssl_file_callback(const char *ssl_file)
{
  FileManager::instance().configFileChild(ts::filename::SSL_MULTICERT, ssl_file);
}

void
task_threads_started_callback()
{
  {
    std::unique_lock<std::mutex> lock(pluginInitMutex);
    pluginInitCheck.wait(lock, [] { return plugin_init_done; });
  }

  APIHook *hook = g_lifecycle_hooks->get(TS_LIFECYCLE_TASK_THREADS_READY_HOOK);
  while (hook) {
    WEAK_SCOPED_MUTEX_LOCK(lock, hook->m_cont->mutex, this_ethread());
    hook->invoke(TS_EVENT_LIFECYCLE_TASK_THREADS_READY, nullptr);
    hook = hook->next();
  }
}
static void
check_max_records_argument(const ArgumentDescription * /* ATS_UNUSED arg*/, unsigned int /* nargs ATS_UNUSED */, const char *val)
{
  int32_t cmd_arg{0};
  try {
    cmd_arg = std::stoi(val);
    if (cmd_arg < REC_DEFAULT_ELEMENTS_SIZE) {
      fprintf(stderr, "[WARNING] Passed maxRecords value=%d is lower than the default value %d. Default will be used.\n", cmd_arg,
              REC_DEFAULT_ELEMENTS_SIZE);
      max_records_entries = REC_DEFAULT_ELEMENTS_SIZE;
    }
    // max_records_entries keeps the passed value.
  } catch (std::exception const &ex) {
    fprintf(stderr, "[ERROR] Invalid %d value for maxRecords. Default  %d will be used.\n", cmd_arg, REC_DEFAULT_ELEMENTS_SIZE);
    max_records_entries = REC_DEFAULT_ELEMENTS_SIZE;
  }
}

} // end anonymous namespace
