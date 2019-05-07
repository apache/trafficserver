/** @file

  Entry point to the traffic manager.

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

#include "tscore/ink_sys_control.h"
#include "tscore/ink_cap.h"
#include "tscore/ink_lockfile.h"
#include "tscore/ink_sock.h"
#include "tscore/ink_args.h"
#include "tscore/ink_syslog.h"
#include "tscore/runroot.h"

#include "WebMgmtUtils.h"
#include "MgmtUtils.h"
#include "MgmtSocket.h"
#include "NetworkUtilsRemote.h"
#include "FileManager.h"
#include "tscore/I_Layout.h"
#include "tscore/I_Version.h"
#include "tscore/TextBuffer.h"
#include "DiagsConfig.h"
#include "HTTP.h"
#include "CoreAPI.h"

#include "LocalManager.h"
#include "TSControlMain.h"
#include "EventControlMain.h"

// Needs LibRecordsConfigInit()
#include "RecordsConfig.h"

#include "records/P_RecLocal.h"
#include "DerivativeMetrics.h"

#if TS_USE_POSIX_CAP
#include <sys/capability.h>
#endif
#include <grp.h>
#include <atomic>
#include "tscore/bwf_std_format.h"

#define FD_THROTTLE_HEADROOM (128 + 64) // TODO: consolidate with THROTTLE_FD_HEADROOM
#define DIAGS_LOG_FILENAME "manager.log"

#if ATOMIC_INT_LOCK_FREE != 2
#error "Need lock free std::atomic<int>"
#endif

using namespace std::literals;

// These globals are still referenced directly by management API.
LocalManager *lmgmt = nullptr;
FileManager *configFiles;

static void fileUpdated(char *fname, char *configName, bool incVersion);
static void runAsUser(const char *userName);

#if defined(freebsd)
extern "C" int getpwnam_r(const char *name, struct passwd *result, char *buffer, size_t buflen, struct passwd **resptr);
#endif

static AppVersionInfo appVersionInfo; // Build info for this application

static inkcoreapi DiagsConfig *diagsConfig;
static char debug_tags[1024]  = "";
static char action_tags[1024] = "";
static int proxy_off          = false;
static int listen_off         = false;
static char bind_stdout[512]  = "";
static char bind_stderr[512]  = "";

static const char *mgmt_path = nullptr;

// By default, set the current directory as base
static const char *recs_conf = "records.config";

static int fds_limit;

// TODO: Use positive instead negative selection
//       This should just be #if defined(solaris)
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
static void SignalHandler(int sig, siginfo_t *t, void *f);
static void SignalAlrmHandler(int sig, siginfo_t *t, void *f);
#else
static void SignalHandler(int sig);
static void SignalAlrmHandler(int sig);
#endif

static std::atomic<int> sigHupNotifier;
static void SigChldHandler(int sig);

static void
rotateLogs()
{
  // First, let us synchronously update the rolling config values for both diagslog
  // and outputlog. Note that the config values for outputlog in traffic_server
  // are never updated past the original instantiation of Diags. This shouldn't
  // be an issue since we're never rolling outputlog from traffic_server anyways.
  // The reason being is that it is difficult to send a notification from TS to
  // TM, informing TM that outputlog has been rolled. It is much easier sending
  // a notification (in the form of SIGUSR2) from TM -> TS.
  int output_log_roll_int    = (int)REC_ConfigReadInteger("proxy.config.output.logfile.rolling_interval_sec");
  int output_log_roll_size   = (int)REC_ConfigReadInteger("proxy.config.output.logfile.rolling_size_mb");
  int output_log_roll_enable = (int)REC_ConfigReadInteger("proxy.config.output.logfile.rolling_enabled");
  int diags_log_roll_int     = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_interval_sec");
  int diags_log_roll_size    = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_size_mb");
  int diags_log_roll_enable  = (int)REC_ConfigReadInteger("proxy.config.diags.logfile.rolling_enabled");
  diags->config_roll_diagslog((RollingEnabledValues)diags_log_roll_enable, diags_log_roll_int, diags_log_roll_size);
  diags->config_roll_outputlog((RollingEnabledValues)output_log_roll_enable, output_log_roll_int, output_log_roll_size);

  // Now we can actually roll the logs (if necessary)
  if (diags->should_roll_diagslog()) {
    mgmt_log("Rotated %s", DIAGS_LOG_FILENAME);
  }

  if (diags->should_roll_outputlog()) {
    // send a signal to TS to reload traffic.out, so the logfile is kept
    // synced across processes
    mgmt_log("Sending SIGUSR2 to TS");
    pid_t tspid = lmgmt->watched_process_pid;
    if (tspid <= 0) {
      return;
    }
    if (kill(tspid, SIGUSR2) != 0) {
      mgmt_log("Could not send SIGUSR2 to TS: %s", strerror(errno));
    } else {
      mgmt_log("Successfully sent SIGUSR2 to TS!");
    }
  }
}

static bool
is_server_idle()
{
  RecInt active    = 0;
  RecInt threshold = 0;

  if (RecGetRecordInt("proxy.config.restart.active_client_threshold", &threshold) != REC_ERR_OKAY) {
    return false;
  }

  if (RecGetRecordInt("proxy.process.http.current_active_client_connections", &active) != REC_ERR_OKAY) {
    return false;
  }

  Debug("lm", "%" PRId64 " active clients, threshold is %" PRId64, active, threshold);
  return active <= threshold;
}

static bool
is_server_idle_from_new_connection()
{
  RecInt active    = 0;
  RecInt threshold = 0;
  // TODO implement with the right metric

  Debug("lm", "%" PRId64 " active clients, threshold is %" PRId64, active, threshold);

  return active <= threshold;
}

static bool
is_server_draining()
{
  RecInt draining = 0;
  if (RecGetRecordInt("proxy.node.config.draining", &draining) != REC_ERR_OKAY) {
    return false;
  }
  return draining != 0;
}

static bool
waited_enough()
{
  RecInt timeout = 0;
  if (RecGetRecordInt("proxy.config.stop.shutdown_timeout", &timeout) != REC_ERR_OKAY) {
    return false;
  }

  return (lmgmt->mgmt_shutdown_triggered_at + timeout >= time(nullptr));
}

static void
check_lockfile()
{
  std::string rundir(RecConfigReadRuntimeDir());
  char lockfile[PATH_NAME_MAX];
  int err;
  pid_t holding_pid;

  //////////////////////////////////////
  // test for presence of server lock //
  //////////////////////////////////////
  Layout::relative_to(lockfile, sizeof(lockfile), rundir, SERVER_LOCK);
  Lockfile server_lockfile(lockfile);
  err = server_lockfile.Open(&holding_pid);
  if (err == 1) {
    server_lockfile.Close(); // no server running
  } else {
    char *reason = strerror(-err);
    if (err == 0) {
      fprintf(stderr, "FATAL: Lockfile '%s' says server already running as PID %ld\n", lockfile, (long)holding_pid);
      mgmt_log("FATAL: Lockfile '%s' says server already running as PID %d\n", lockfile, holding_pid);
    } else {
      fprintf(stderr, "FATAL: Can't open server lockfile '%s' (%s)\n", lockfile, (reason ? reason : "Unknown Reason"));
      mgmt_log("FATAL: Can't open server lockfile '%s' (%s)\n", lockfile, (reason ? reason : "Unknown Reason"));
    }
    exit(1);
  }

  ///////////////////////////////////////////
  // try to get the exclusive manager lock //
  ///////////////////////////////////////////
  Layout::relative_to(lockfile, sizeof(lockfile), rundir, MANAGER_LOCK);
  Lockfile manager_lockfile(lockfile);
  err = manager_lockfile.Get(&holding_pid);
  if (err != 1) {
    char *reason = strerror(-err);
    fprintf(stderr, "FATAL: Can't acquire manager lockfile '%s'", lockfile);
    mgmt_log("FATAL: Can't acquire manager lockfile '%s'", lockfile);
    if (err == 0) {
      fprintf(stderr, " (Lock file held by process ID %ld)\n", (long)holding_pid);
      mgmt_log(" (Lock file held by process ID %d)\n", holding_pid);
    } else if (reason) {
      fprintf(stderr, " (%s)\n", reason);
      mgmt_log(" (%s)\n", reason);
    } else {
      fprintf(stderr, "\n");
    }
    exit(1);

    fprintf(stderr, "unable to acquire manager lock [%d]\n", -err);
    exit(1);
  }
}

static void
initSignalHandlers()
{
  struct sigaction sigHandler, sigChldHandler, sigAlrmHandler;
  sigset_t sigsToBlock;

// Set up the signal handler
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigHandler.sa_handler   = nullptr;
  sigHandler.sa_sigaction = SignalHandler;
#else
  sigHandler.sa_handler     = SignalHandler;
#endif
  sigemptyset(&sigHandler.sa_mask);

  // We want the handler to remain in place on
  //  SIGHUP to avoid any races with the signals
  //  coming too quickly.  Also restart systems calls
  //  after the signal since not all calls are wrapped
  //  to check errno for EINTR
  sigHandler.sa_flags = SA_RESTART;
  sigaction(SIGHUP, &sigHandler, nullptr);
  sigaction(SIGUSR2, &sigHandler, nullptr);

// Don't block the signal on entry to the signal
//   handler so we can reissue it and get a core
//   file in the appropriate circumstances
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigHandler.sa_flags = SA_RESETHAND | SA_SIGINFO;
#else
  sigHandler.sa_flags       = SA_RESETHAND;
#endif
  sigaction(SIGINT, &sigHandler, nullptr);
  sigaction(SIGQUIT, &sigHandler, nullptr);
  sigaction(SIGILL, &sigHandler, nullptr);
  sigaction(SIGBUS, &sigHandler, nullptr);
  sigaction(SIGSEGV, &sigHandler, nullptr);
  sigaction(SIGTERM, &sigHandler, nullptr);

#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigAlrmHandler.sa_handler   = nullptr;
  sigAlrmHandler.sa_sigaction = SignalAlrmHandler;
#else
  sigAlrmHandler.sa_handler = SignalAlrmHandler;
#endif

  sigemptyset(&sigAlrmHandler.sa_mask);
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigAlrmHandler.sa_flags = SA_SIGINFO;
#else
  sigAlrmHandler.sa_flags   = 0;
#endif
  sigaction(SIGALRM, &sigAlrmHandler, nullptr);

  // Block the delivery of any signals we are not catching
  //
  //  except for SIGALRM since we use it
  //    to break out of deadlock on semaphore
  //    we share with the proxy
  //
  sigfillset(&sigsToBlock);
  sigdelset(&sigsToBlock, SIGHUP);
  sigdelset(&sigsToBlock, SIGUSR2);
  sigdelset(&sigsToBlock, SIGINT);
  sigdelset(&sigsToBlock, SIGQUIT);
  sigdelset(&sigsToBlock, SIGILL);
  sigdelset(&sigsToBlock, SIGABRT);
  sigdelset(&sigsToBlock, SIGBUS);
  sigdelset(&sigsToBlock, SIGSEGV);
  sigdelset(&sigsToBlock, SIGTERM);
  sigdelset(&sigsToBlock, SIGALRM);
  ink_thread_sigsetmask(SIG_SETMASK, &sigsToBlock, nullptr);

  // Set up the SIGCHLD handler so we do not get into
  //   a problem with Solaris 2.6 and strange waitpid()
  //   behavior
  sigChldHandler.sa_handler = SigChldHandler;
  sigChldHandler.sa_flags   = SA_RESTART;
  sigemptyset(&sigChldHandler.sa_mask);
  sigaction(SIGCHLD, &sigChldHandler, nullptr);
}

static void
init_dirs()
{
  std::string rundir(RecConfigReadRuntimeDir());
  std::string sysconfdir(RecConfigReadConfigDir());

  if (access(sysconfdir.c_str(), R_OK) == -1) {
    mgmt_elog(0, "unable to access() config directory '%s': %d, %s\n", sysconfdir.c_str(), errno, strerror(errno));
    mgmt_elog(0, "please set the 'TS_ROOT' environment variable\n");
    ::exit(1);
  }

  if (access(rundir.c_str(), R_OK) == -1) {
    mgmt_elog(0, "unable to access() local state directory '%s': %d, %s\n", rundir.c_str(), errno, strerror(errno));
    mgmt_elog(0, "please set 'proxy.config.local_state_dir'\n");
    ::exit(1);
  }
}

static void
chdir_root()
{
  std::string prefix = Layout::get()->prefix;

  if (chdir(prefix.c_str()) < 0) {
    mgmt_elog(0, "unable to change to root directory \"%s\" [%d '%s']\n", prefix.c_str(), errno, strerror(errno));
    mgmt_elog(0, " please set correct path in env variable TS_ROOT \n");
    exit(1);
  } else {
    mgmt_log("[TrafficManager] using root directory '%s'\n", prefix.c_str());
  }
}

static void
set_process_limits(RecInt fds_throttle)
{
  struct rlimit lim;
  rlim_t maxfiles;

  // Set needed rlimits (root)
  ink_max_out_rlimit(RLIMIT_NOFILE);
  ink_max_out_rlimit(RLIMIT_STACK);
  ink_max_out_rlimit(RLIMIT_DATA);
  ink_max_out_rlimit(RLIMIT_FSIZE);
#ifdef RLIMIT_RSS
  ink_max_out_rlimit(RLIMIT_RSS);
#endif

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

  if (getrlimit(RLIMIT_NOFILE, &lim) == 0) {
    if (fds_throttle > (int)(lim.rlim_cur + FD_THROTTLE_HEADROOM)) {
      lim.rlim_cur = (lim.rlim_max = (rlim_t)fds_throttle);
      if (!setrlimit(RLIMIT_NOFILE, &lim) && !getrlimit(RLIMIT_NOFILE, &lim)) {
        fds_limit = (int)lim.rlim_cur;
        syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)", RLIMIT_NOFILE, (int)lim.rlim_cur, (int)lim.rlim_max);
      }
    }
  }
}

#if TS_HAS_WCCP
static void
Errata_Logger(ts::Errata const &err)
{
  size_t n;
  static size_t const SIZE = 4096;
  char buff[SIZE];
  if (err.size()) {
    ts::Errata::Code code = err.top().getCode();
    n                     = err.write(buff, SIZE, 1, 0, 2, "> ");
    // strip trailing newlines.
    while (n && (buff[n - 1] == '\n' || buff[n - 1] == '\r'))
      buff[--n] = 0;
    // log it.
    if (code > 1)
      mgmt_elog(0, "[WCCP]%s", buff);
    else if (code > 0)
      mgmt_log("[WCCP]%s", buff);
    else
      Debug("WCCP", "%s", buff);
  }
}

static void
Init_Errata_Logging()
{
  ts::Errata::registerSink(&Errata_Logger);
}
#endif

static void
millisleep(int ms)
{
  struct timespec ts;

  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = (ms - ts.tv_sec * 1000) * 1000 * 1000;
  nanosleep(&ts, nullptr); // we use nanosleep instead of sleep because it does not interact with signals
}

bool
api_socket_is_restricted()
{
  RecInt intval;

  // If the socket is not administratively restricted, check whether we have platform
  // support. Otherwise, default to making it restricted.
  if (RecGetRecordInt("proxy.config.admin.api.restricted", &intval) == REC_ERR_OKAY) {
    if (intval == 0) {
      return !mgmt_has_peereid();
    }
  }

  return true;
}

int
main(int argc, const char **argv)
{
  const long MAX_LOGIN = ink_login_name_max();

  runroot_handler(argv);

  // Before accessing file system initialize Layout engine
  Layout::create();
  mgmt_path = Layout::get()->sysconfdir.c_str();

  // Set up the application version info
  appVersionInfo.setup(PACKAGE_NAME, "traffic_manager", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  bool found       = false;
  int just_started = 0;
  // TODO: This seems completely incomplete, disabled for now
  //  int dump_config = 0, dump_process = 0, dump_node = 0, dump_local = 0;
  char *proxy_port   = nullptr;
  char *tsArgs       = nullptr;
  int disable_syslog = false;
  char userToRunAs[MAX_LOGIN + 1];
  RecInt fds_throttle = -1;

  ArgumentDescription argument_descriptions[] = {
    {"proxyOff", '-', "Disable proxy", "F", &proxy_off, nullptr, nullptr},
    {"listenOff", '-', "Disable traffic manager listen to proxy ports", "F", &listen_off, nullptr, nullptr},
    {"path", '-', "Path to the management socket", "S*", &mgmt_path, nullptr, nullptr},
    {"recordsConf", '-', "Path to records.config", "S*", &recs_conf, nullptr, nullptr},
    {"tsArgs", '-', "Additional arguments for traffic_server", "S*", &tsArgs, nullptr, nullptr},
    {"proxyPort", '-', "HTTP port descriptor", "S*", &proxy_port, nullptr, nullptr},
    {TM_OPT_BIND_STDOUT, '-', "Regular file to bind stdout to", "S512", &bind_stdout, "PROXY_BIND_STDOUT", nullptr},
    {TM_OPT_BIND_STDERR, '-', "Regular file to bind stderr to", "S512", &bind_stderr, "PROXY_BIND_STDERR", nullptr},
#if TS_USE_DIAGS
    {"debug", 'T', "Vertical-bar-separated Debug Tags", "S1023", debug_tags, nullptr, nullptr},
    {"action", 'B', "Vertical-bar-separated Behavior Tags", "S1023", action_tags, nullptr, nullptr},
#endif
    {"nosyslog", '-', "Do not log to syslog", "F", &disable_syslog, nullptr, nullptr},
    HELP_ARGUMENT_DESCRIPTION(),
    VERSION_ARGUMENT_DESCRIPTION(),
    RUNROOT_ARGUMENT_DESCRIPTION()
  };

  // Process command line arguments and dump into variables
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  // change the directory to the "root" directory
  chdir_root();

  // Line buffer standard output & standard error
  int status;
  status = setvbuf(stdout, nullptr, _IOLBF, 0);
  if (status != 0) {
    perror("WARNING: can't line buffer stdout");
  }
  status = setvbuf(stderr, nullptr, _IOLBF, 0);
  if (status != 0) {
    perror("WARNING: can't line buffer stderr");
  }

  initSignalHandlers();

  // Bootstrap with LOG_DAEMON until we've read our configuration
  if (!disable_syslog) {
    openlog("traffic_manager", LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);
    mgmt_use_syslog();
    syslog(LOG_NOTICE, "NOTE: --- Manager Starting ---");
    syslog(LOG_NOTICE, "NOTE: Manager Version: %s", appVersionInfo.FullVersionInfoStr);
  }

  // Bootstrap the Diags facility so that we can use it while starting
  //  up the manager
  diagsConfig = new DiagsConfig("Manager", DIAGS_LOG_FILENAME, debug_tags, action_tags, false);
  diags       = diagsConfig->diags;
  diags->set_std_output(StdStream::STDOUT, bind_stdout);
  diags->set_std_output(StdStream::STDERR, bind_stderr);

  RecLocalInit();
  LibRecordsConfigInit();

  init_dirs(); // setup critical directories, needs LibRecords

  if (RecGetRecordString("proxy.config.admin.user_id", userToRunAs, sizeof(userToRunAs)) != REC_ERR_OKAY ||
      strlen(userToRunAs) == 0) {
    mgmt_fatal(0, "proxy.config.admin.user_id is not set\n");
  }

  RecGetRecordInt("proxy.config.net.connections_throttle", &fds_throttle);

  set_process_limits(fds_throttle); // as root

  // A user of #-1 means to not attempt to switch user. Yes, it's documented ;)
  if (strcmp(userToRunAs, "#-1") != 0) {
    runAsUser(userToRunAs);
  }

  EnableCoreFile(true);
  check_lockfile();

  url_init();
  mime_init();
  http_init();

#if TS_HAS_WCCP
  Init_Errata_Logging();
#endif
  ts_host_res_global_init();
  ts_session_protocol_well_known_name_indices_init();
  lmgmt = new LocalManager(proxy_off == false, listen_off == false);
  RecLocalInitMessage();
  lmgmt->initAlarm();

  if (diags) {
    delete diagsConfig;
  }
  // INKqa11968: need to set up callbacks and diags data structures
  // using configuration in records.config
  diagsConfig = new DiagsConfig("Manager", DIAGS_LOG_FILENAME, debug_tags, action_tags, true);
  diags       = diagsConfig->diags;
  RecSetDiags(diags);
  diags->set_std_output(StdStream::STDOUT, bind_stdout);
  diags->set_std_output(StdStream::STDERR, bind_stderr);

  if (is_debug_tag_set("diags")) {
    diags->dump();
  }
  diags->cleanup_func = mgmt_cleanup;

  // Setup the exported manager version records.
  RecSetRecordString("proxy.node.version.manager.short", appVersionInfo.VersionStr, REC_SOURCE_DEFAULT);
  RecSetRecordString("proxy.node.version.manager.long", appVersionInfo.FullVersionInfoStr, REC_SOURCE_DEFAULT);
  RecSetRecordString("proxy.node.version.manager.build_number", appVersionInfo.BldNumStr, REC_SOURCE_DEFAULT);
  RecSetRecordString("proxy.node.version.manager.build_time", appVersionInfo.BldTimeStr, REC_SOURCE_DEFAULT);
  RecSetRecordString("proxy.node.version.manager.build_date", appVersionInfo.BldDateStr, REC_SOURCE_DEFAULT);
  RecSetRecordString("proxy.node.version.manager.build_machine", appVersionInfo.BldMachineStr, REC_SOURCE_DEFAULT);
  RecSetRecordString("proxy.node.version.manager.build_person", appVersionInfo.BldPersonStr, REC_SOURCE_DEFAULT);

  if (!disable_syslog) {
    char sys_var[]     = "proxy.config.syslog_facility";
    char *facility_str = nullptr;
    int facility_int;

    facility_str = REC_readString(sys_var, &found);
    ink_assert(found);

    if (!found) {
      mgmt_elog(0, "Could not read %s.  Defaulting to LOG_DAEMON\n", sys_var);
      facility_int = LOG_DAEMON;
    } else {
      facility_int = facility_string_to_int(facility_str);
      ats_free(facility_str);
      if (facility_int < 0) {
        mgmt_elog(0, "Bad syslog facility specified.  Defaulting to LOG_DAEMON\n");
        facility_int = LOG_DAEMON;
      }
    }

    // NOTE: do NOT call closelog() here.  Solaris gets confused.
    openlog("traffic_manager", LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility_int);

    lmgmt->syslog_facility = facility_int;
  } else {
    lmgmt->syslog_facility = -1;
  }

  // Find out our hostname so we can use it as part of the initialization
  setHostnameVar();

  // Initialize the Config Object bindings before
  //   starting any other threads
  lmgmt->configFiles = configFiles = new FileManager();
  initializeRegistry();
  configFiles->registerCallback(fileUpdated);

  // RecLocal's 'sync_thr' depends on 'configFiles', so we can't
  // stat the 'sync_thr' until 'configFiles' has been initialized.
  RecLocalStart(configFiles);

  // TS needs to be started up with the same outputlog bindings each time,
  // so we append the outputlog location to the persistent proxy options
  //
  // TS needs them to be able to create BaseLogFiles for each value
  ts::bwprint(lmgmt->proxy_options, "{}{}{}", ts::bwf::OptionalAffix(tsArgs),
              ts::bwf::OptionalAffix(bind_stdout, " "sv, "--bind_stdout "sv),
              ts::bwf::OptionalAffix(bind_stderr, " "sv, "--bind_stderr "sv));

  if (proxy_port) {
    HttpProxyPort::loadValue(lmgmt->m_proxy_ports, proxy_port);
  }

  lmgmt->initMgmtProcessServer(); /* Setup p-to-p process server */

  lmgmt->listenForProxy();

  // Setup the API and event sockets
  std::string rundir(RecConfigReadRuntimeDir());
  std::string apisock(Layout::relative_to(rundir, MGMTAPI_MGMT_SOCKET_NAME));
  std::string eventsock(Layout::relative_to(rundir, MGMTAPI_EVENT_SOCKET_NAME));

  Debug("lm", "using main socket file '%s'", apisock.c_str());
  Debug("lm", "using event socket file '%s'", eventsock.c_str());

  mode_t oldmask = umask(0);
  mode_t newmode = api_socket_is_restricted() ? 00700 : 00777;

  int mgmtapiFD         = -1; // FD for the api interface to issue commands
  int eventapiFD        = -1; // FD for the api and clients to handle event callbacks
  char mgmtapiFailMsg[] = "Traffic server management API service Interface Failed to Initialize.";

  mgmtapiFD = bind_unix_domain_socket(apisock.c_str(), newmode);
  if (mgmtapiFD == -1) {
    mgmt_log("[WebIntrMain] Unable to set up socket for handling management API calls. API socket path = %s\n", apisock.c_str());
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, mgmtapiFailMsg);
  }

  eventapiFD = bind_unix_domain_socket(eventsock.c_str(), newmode);
  if (eventapiFD == -1) {
    mgmt_log("[WebIntrMain] Unable to set up so for handling management API event calls. Event Socket path: %s\n",
             eventsock.c_str());
  }

  umask(oldmask);
  ink_thread_create(nullptr, ts_ctrl_main, &mgmtapiFD, 0, 0, nullptr);
  ink_thread_create(nullptr, event_callback_main, &eventapiFD, 0, 0, nullptr);

  mgmt_log("[TrafficManager] Setup complete\n");

  RecRegisterStatInt(RECT_NODE, "proxy.node.config.reconfigure_time", time(nullptr), RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_NODE, "proxy.node.config.reconfigure_required", 0, RECP_NON_PERSISTENT);

  RecRegisterStatInt(RECT_NODE, "proxy.node.config.restart_required.proxy", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_NODE, "proxy.node.config.restart_required.manager", 0, RECP_NON_PERSISTENT);

  RecRegisterStatInt(RECT_NODE, "proxy.node.config.draining", 0, RECP_NON_PERSISTENT);

  const int MAX_SLEEP_S      = 60; // Max sleep duration
  int sleep_time             = 0;  // sleep_time given in sec
  uint64_t last_start_epoc_s = 0;  // latest start attempt in seconds since epoc

  DerivativeMetrics derived; // This is simple class to calculate some useful derived metrics

  for (;;) {
    lmgmt->processEventQueue();
    lmgmt->pollMgmtProcessServer();

    // Handle rotation of output log (aka traffic.out) as well as DIAGS_LOG_FILENAME (aka manager.log)
    rotateLogs();

    // Check for a SIGHUP
    if (sigHupNotifier) {
      mgmt_log("[main] Reading Configuration Files due to SIGHUP\n");
      Reconfigure();
      sigHupNotifier = 0;
      mgmt_log("[main] Reading Configuration Files Reread\n");
    }

    // Update the derived metrics. ToDo: this runs once a second, that might be excessive, maybe it should be
    // done more like every config_update_interval_ms (proxy.config.config_update_interval_ms) ?
    derived.Update();

    if (lmgmt->mgmt_shutdown_outstanding != MGMT_PENDING_NONE) {
      Debug("lm", "pending shutdown %d", lmgmt->mgmt_shutdown_outstanding);
    }
    switch (lmgmt->mgmt_shutdown_outstanding) {
    case MGMT_PENDING_RESTART:
      lmgmt->mgmtShutdown();
      ::exit(0);
      break;
    case MGMT_PENDING_IDLE_RESTART:
      if (!is_server_draining()) {
        lmgmt->processDrain();
      }
      if (is_server_idle() || waited_enough()) {
        lmgmt->mgmtShutdown();
        ::exit(0);
      }
      break;
    case MGMT_PENDING_BOUNCE:
      lmgmt->processBounce();
      lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      break;
    case MGMT_PENDING_IDLE_BOUNCE:
      if (!is_server_draining()) {
        lmgmt->processDrain();
      }
      if (is_server_idle() || waited_enough()) {
        lmgmt->processBounce();
        lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      }
      break;
    case MGMT_PENDING_STOP:
      lmgmt->processShutdown();
      lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      break;
    case MGMT_PENDING_IDLE_STOP:
      if (!is_server_draining()) {
        lmgmt->processDrain();
      }
      if (is_server_idle() || waited_enough()) {
        lmgmt->processShutdown();
        lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      }
      break;
    case MGMT_PENDING_DRAIN:
      if (!is_server_draining()) {
        lmgmt->processDrain();
      }
      lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      break;
    case MGMT_PENDING_IDLE_DRAIN:
      if (is_server_idle_from_new_connection()) {
        lmgmt->processDrain();
        lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      }
      break;
    case MGMT_PENDING_UNDO_DRAIN:
      if (is_server_draining()) {
        lmgmt->processDrain(0);
        lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      }
      break;
    default:
      break;
    }

    if (lmgmt->run_proxy && !lmgmt->processRunning() && lmgmt->proxy_recoverable) { /* Make sure we still have a proxy up */
      const uint64_t now = static_cast<uint64_t>(time(nullptr));
      if (sleep_time && ((now - last_start_epoc_s) < MAX_SLEEP_S)) {
        mgmt_log("Relaunching proxy after %d sec...", sleep_time);
        millisleep(1000 * sleep_time); // we use millisleep instead of sleep because it doesnt interfere with signals
        sleep_time = std::min(sleep_time * 2, MAX_SLEEP_S);
      } else {
        sleep_time = 1;
      }
      if (ProxyStateSet(TS_PROXY_ON, TS_CACHE_CLEAR_NONE) == TS_ERR_OKAY) {
        just_started      = 0;
        last_start_epoc_s = static_cast<uint64_t>(time(nullptr));
      } else {
        just_started++;
      }
    } else { /* Give the proxy a chance to fire up */
      if (!lmgmt->proxy_recoverable) {
        mgmt_log("[main] Proxy is un-recoverable. Proxy will not be relaunched.\n");
      }

      just_started++;
    }

    /* This will catch the case were the proxy dies before it can connect to manager */
    if (lmgmt->proxy_launch_outstanding && !lmgmt->processRunning() && just_started >= 120) {
      just_started                    = 0;
      lmgmt->proxy_launch_outstanding = false;
      if (lmgmt->proxy_launch_pid != -1) {
        int res;
        kill(lmgmt->proxy_launch_pid, 9);
        waitpid(lmgmt->proxy_launch_pid, &res, 0);
        if (WIFSIGNALED(res)) {
          int sig = WTERMSIG(res);
#ifdef NEED_PSIGNAL
          mgmt_log("[main] Proxy terminated due to Sig %d. Relaunching after %d sec...\n", sig, sleep_time);
#else
          mgmt_log("[main] Proxy terminated due to Sig %d: %s. Relaunching after %d sec...\n", sig, strsignal(sig), sleep_time);
#endif /* NEED_PSIGNAL */
        }
      }
      mgmt_log("[main] Proxy launch failed, retrying after %d sec...\n", sleep_time);
    }
  }

  // ToDo: Here we should delete anything related to calculated metrics.

#ifndef MGMT_SERVICE
  return 0;
#endif

} /* End main */

#if !defined(linux) && !defined(freebsd) && !defined(darwin)
static void
SignalAlrmHandler(int /* sig ATS_UNUSED */, siginfo_t *t, void * /* c ATS_UNUSED */)
#else
static void
SignalAlrmHandler(int /* sig ATS_UNUSED */)
#endif
{
/*
   fprintf("[TrafficManager] ==> SIGALRM received\n");
   mgmt_elog(0, "[TrafficManager] ==> SIGALRM received\n");
 */
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  if (t) {
    if (t->si_code <= 0) {
      fprintf(stderr, "[TrafficManager] ==> User Alarm from pid: %ld uid: %d\n", (long)t->si_pid, t->si_uid);
      mgmt_log("[TrafficManager] ==> User Alarm from pid: %d uid: %d\n", t->si_pid, t->si_uid);
    } else {
      fprintf(stderr, "[TrafficManager] ==> Kernel Alarm Reason: %d\n", t->si_code);
      mgmt_log("[TrafficManager] ==> Kernel Alarm Reason: %d\n", t->si_code);
    }
  }
#endif

  return;
}

#if !defined(linux) && !defined(freebsd) && !defined(darwin)
static void
SignalHandler(int sig, siginfo_t *t, void *c)
#else
static void
SignalHandler(int sig)
#endif
{
  static int clean = 0;
  int status;

#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  if (t) {
    if (t->si_code <= 0) {
      fprintf(stderr, "[TrafficManager] ==> User Sig %d from pid: %ld uid: %d\n", sig, (long)t->si_pid, t->si_uid);
      mgmt_log("[TrafficManager] ==> User Sig %d from pid: %ld uid: %d\n", sig, (long)t->si_pid, t->si_uid);
    } else {
      fprintf(stderr, "[TrafficManager] ==> Kernel Sig %d; Reason: %d\n", sig, t->si_code);
      mgmt_log("[TrafficManager] ==> Kernel Sig %d; Reason: %d\n", sig, t->si_code);
    }
  }
#endif

  if (sig == SIGHUP) {
    sigHupNotifier = 1;
    return;
  }

  fprintf(stderr, "[TrafficManager] ==> Cleaning up and reissuing signal #%d\n", sig);
  mgmt_log("[TrafficManager] ==> Cleaning up and reissuing signal #%d\n", sig);

  if (lmgmt && !clean) {
    clean = 1;
    if (lmgmt->watched_process_pid != -1) {
      if (sig == SIGTERM || sig == SIGINT) {
        kill(lmgmt->watched_process_pid, sig);
        waitpid(lmgmt->watched_process_pid, &status, 0);
      }
    }
    lmgmt->mgmtCleanup();
  }

  switch (sig) {
  case SIGQUIT:
  case SIGILL:
  case SIGTRAP:
#if !defined(linux)
  case SIGEMT:
  case SIGSYS:
#endif
  case SIGFPE:
  case SIGBUS:
  case SIGSEGV:
  case SIGXCPU:
  case SIGXFSZ:
    abort();
  default:
    fprintf(stderr, "[TrafficManager] ==> signal #%d\n", sig);
    mgmt_log("[TrafficManager] ==> signal #%d\n", sig);
    ::exit(sig);
  }
  fprintf(stderr, "[TrafficManager] ==> signal2 #%d\n", sig);
  mgmt_log("[TrafficManager] ==> signal2 #%d\n", sig);
  ::exit(sig);
} /* End SignalHandler */

// void SigChldHandler(int sig)
//
//   An empty handler needed so that we catch SIGCHLD
//    With Solaris 2.6, ignoring sig child changes the behavior
//    of waitpid() so that if there are no unwaited children,
//    waitpid() blocks until all child are transformed into
//    zombies which is bad for us
//
static void
SigChldHandler(int /* sig ATS_UNUSED */)
{
}

void
fileUpdated(char *fname, char *configName, bool incVersion)
{
  // If there is no config name recorded, assume this file is not reloadable
  // Just log a message
  if (configName == nullptr || configName[0] == '\0') {
    mgmt_log("[fileUpdated] %s changed, need restart", fname);
  } else {
    // Signal based on the config entry that has the changed file name
    lmgmt->signalFileChange(configName);
  }
  return;
} /* End fileUpdate */

#if TS_USE_POSIX_CAP
/** Restore capabilities after user id change.
    This manipulates LINUX capabilities so that this process
    can perform certain privileged operations even if it is
    no longer running as a privilege user.

    @internal
    I tried using
    @code
    prctl(PR_SET_KEEPCAPS, 1);
    @endcode
    but that had no effect even though the call reported success.
    Only explicit capability manipulation was effective.

    It does not appear to be necessary to set the capabilities on the
    executable if originally run as root. That may be needed if
    started as a user without that capability.
 */

int
restoreCapabilities()
{
  int zret      = 0;              // return value.
  cap_t cap_set = cap_get_proc(); // current capabilities
  // Make a list of the capabilities we want turned on.
  cap_value_t cap_list[] = {
    CAP_NET_ADMIN,        ///< Set socket transparency.
    CAP_NET_BIND_SERVICE, ///< Low port (e.g. 80) binding.
    CAP_IPC_LOCK          ///< Lock IPC objects.
  };
  static int const CAP_COUNT = sizeof(cap_list) / sizeof(*cap_list);

  for (int i = 0; i < CAP_COUNT; i++) {
    if (cap_set_flag(cap_set, CAP_EFFECTIVE, 1, cap_list + i, CAP_SET) < 0) {
      Warning("restore CAP_EFFECTIVE failed for option %d", i);
    }
    if (cap_set_proc(cap_set) == -1) { // it failed, back out
      cap_set_flag(cap_set, CAP_EFFECTIVE, 1, cap_list + i, CAP_CLEAR);
    }
  }
  for (int i = 0; i < CAP_COUNT; i++) {
    cap_flag_value_t val;
    if (cap_get_flag(cap_set, cap_list[i], CAP_EFFECTIVE, &val) < 0) {
    } else {
      Warning("CAP_EFFECTIVE offiset %d is %s", i, val == CAP_SET ? "set" : "unset");
    }
  }
  zret = cap_set_proc(cap_set);
  cap_free(cap_set);
  return zret;
}
#endif

//  void runAsUser(...)
//
//  If we are root, switched to user to run as
//    specified in records.config
//
//  If we are not root, do nothing
//
void
runAsUser(const char *userName)
{
  if (getuid() == 0 || geteuid() == 0) {
    ImpersonateUser(userName, IMPERSONATE_EFFECTIVE);

#if TS_USE_POSIX_CAP
    if (0 != restoreCapabilities()) {
      mgmt_log("[runAsUser] Error: Failed to restore capabilities after switch to user %s.\n", userName);
    }
#endif
  }
} /* End runAsUser() */
