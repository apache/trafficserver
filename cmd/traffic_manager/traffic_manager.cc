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

#include "ts/ink_sys_control.h"
#include "ts/ink_cap.h"
#include "ts/ink_lockfile.h"
#include "ts/ink_sock.h"
#include "ts/ink_args.h"
#include "ts/ink_syslog.h"

#include "WebMgmtUtils.h"
#include "NetworkUtilsRemote.h"
#include "ClusterCom.h"
#include "VMap.h"
#include "FileManager.h"
#include "ts/I_Layout.h"
#include "ts/I_Version.h"
#include "DiagsConfig.h"
#include "HTTP.h"
#include "CoreAPI.h"

#include "LocalManager.h"
#include "TSControlMain.h"
#include "EventControlMain.h"

#include "MgmtHandlers.h"

// Needs LibRecordsConfigInit()
#include "RecordsConfig.h"

#include "StatProcessor.h"
#include "P_RecLocal.h"

#if TS_USE_LUAJIT
#include "bindings/bindings.h"
#include "bindings/metrics.h"
#endif

#include "metrics.h"

#if TS_USE_POSIX_CAP
#include <sys/capability.h>
#endif
#include <grp.h>

#define FD_THROTTLE_HEADROOM (128 + 64) // TODO: consolidate with THROTTLE_FD_HEADROOM
#define DIAGS_LOG_FILENAME "manager.log"

// These globals are still referenced directly by management API.
LocalManager *lmgmt = NULL;
FileManager *configFiles;

static void fileUpdated(char *fname, bool incVersion);
static void runAsUser(const char *userName);

#if defined(freebsd)
extern "C" int getpwnam_r(const char *name, struct passwd *result, char *buffer, size_t buflen, struct passwd **resptr);
#endif

static StatProcessor *statProcessor;  // Statistics Processors
static AppVersionInfo appVersionInfo; // Build info for this application

static inkcoreapi DiagsConfig *diagsConfig;
static char debug_tags[1024]  = "";
static char action_tags[1024] = "";
static bool proxy_off         = false;
static char bind_stdout[512]  = "";
static char bind_stderr[512]  = "";

static const char *mgmt_path = NULL;

// By default, set the current directory as base
static const char *recs_conf = "records.config";

static int fds_limit;

static int metrics_version;

// TODO: Use positive instead negative selection
//       This should just be #if defined(solaris)
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
static void SignalHandler(int sig, siginfo_t *t, void *f);
static void SignalAlrmHandler(int sig, siginfo_t *t, void *f);
#else
static void SignalHandler(int sig);
static void SignalAlrmHandler(int sig);
#endif

static volatile int sigHupNotifier = 0;
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
    if (tspid <= 0)
      return;
    if (kill(tspid, SIGUSR2) != 0)
      mgmt_log("Could not send SIGUSR2 to TS: %s", strerror(errno));
    else
      mgmt_log("Succesfully sent SIGUSR2 to TS!");
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

static void
check_lockfile()
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
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
// TODO: Add PID_FMT_T instead duplicating code just for printing
#if defined(solaris)
      fprintf(stderr, "FATAL: Lockfile '%s' says server already running as PID %d\n", lockfile, (int)holding_pid);
#else
      fprintf(stderr, "FATAL: Lockfile '%s' says server already running as PID %d\n", lockfile, holding_pid);
#endif
      mgmt_elog(stderr, 0, "FATAL: Lockfile '%s' says server already running as PID %d\n", lockfile, holding_pid);
    } else {
      fprintf(stderr, "FATAL: Can't open server lockfile '%s' (%s)\n", lockfile, (reason ? reason : "Unknown Reason"));
      mgmt_elog(stderr, 0, "FATAL: Can't open server lockfile '%s' (%s)\n", lockfile, (reason ? reason : "Unknown Reason"));
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
    mgmt_elog(stderr, 0, "FATAL: Can't acquire manager lockfile '%s'", lockfile);
    if (err == 0) {
#if defined(solaris)
      fprintf(stderr, " (Lock file held by process ID %d)\n", (int)holding_pid);
#else
      fprintf(stderr, " (Lock file held by process ID %d)\n", holding_pid);
#endif
      mgmt_elog(stderr, 0, " (Lock file held by process ID %d)\n", holding_pid);
    } else if (reason) {
      fprintf(stderr, " (%s)\n", reason);
      mgmt_elog(stderr, 0, " (%s)\n", reason);
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
  sigHandler.sa_handler   = NULL;
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
  sigaction(SIGHUP, &sigHandler, NULL);
  sigaction(SIGUSR2, &sigHandler, NULL);

// Don't block the signal on entry to the signal
//   handler so we can reissue it and get a core
//   file in the appropriate circumstances
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigHandler.sa_flags = SA_RESETHAND | SA_SIGINFO;
#else
  sigHandler.sa_flags       = SA_RESETHAND;
#endif
  sigaction(SIGINT, &sigHandler, NULL);
  sigaction(SIGQUIT, &sigHandler, NULL);
  sigaction(SIGILL, &sigHandler, NULL);
  sigaction(SIGBUS, &sigHandler, NULL);
  sigaction(SIGSEGV, &sigHandler, NULL);
  sigaction(SIGTERM, &sigHandler, NULL);

#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigAlrmHandler.sa_handler   = NULL;
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
  sigaction(SIGALRM, &sigAlrmHandler, NULL);

  // Block the delivery of any signals we are not catching
  //
  //  except for SIGALARM since we use it
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
  ink_thread_sigsetmask(SIG_SETMASK, &sigsToBlock, NULL);

  // Set up the SIGCHLD handler so we do not get into
  //   a problem with Solaris 2.6 and strange waitpid()
  //   behavior
  sigChldHandler.sa_handler = SigChldHandler;
  sigChldHandler.sa_flags   = SA_RESTART;
  sigemptyset(&sigChldHandler.sa_mask);
  sigaction(SIGCHLD, &sigChldHandler, NULL);
}

static void
init_dirs()
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  ats_scoped_str sysconfdir(RecConfigReadConfigDir());

  if (access(sysconfdir, R_OK) == -1) {
    mgmt_elog(0, "unable to access() config directory '%s': %d, %s\n", (const char *)sysconfdir, errno, strerror(errno));
    mgmt_elog(0, "please set the 'TS_ROOT' environment variable\n");
    _exit(1);
  }

  if (access(rundir, R_OK) == -1) {
    mgmt_elog(0, "unable to access() local state directory '%s': %d, %s\n", (const char *)rundir, errno, strerror(errno));
    mgmt_elog(0, "please set 'proxy.config.local_state_dir'\n");
    _exit(1);
  }
}

static void
chdir_root()
{
  const char *prefix = Layout::get()->prefix;

  if (chdir(prefix) < 0) {
    mgmt_elog(0, "unable to change to root directory \"%s\" [%d '%s']\n", prefix, errno, strerror(errno));
    mgmt_elog(0, " please set correct path in env variable TS_ROOT \n");
    exit(1);
  } else {
    mgmt_log("[TrafficManager] using root directory '%s'\n", prefix);
  }
}

static void
set_process_limits(RecInt fds_throttle)
{
  struct rlimit lim;
  rlim_t maxfiles;

  // Set needed rlimits (root)
  ink_max_out_rlimit(RLIMIT_NOFILE, true, false);
  ink_max_out_rlimit(RLIMIT_STACK, true, true);
  ink_max_out_rlimit(RLIMIT_DATA, true, true);
  ink_max_out_rlimit(RLIMIT_FSIZE, true, false);
#ifdef RLIMIT_RSS
  ink_max_out_rlimit(RLIMIT_RSS, true, true);
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
  nanosleep(&ts, NULL); // we use nanosleep instead of sleep because it does not interact with signals
}

int
main(int argc, const char **argv)
{
  const long MAX_LOGIN = ink_login_name_max();

  // Before accessing file system initialize Layout engine
  Layout::create();
  mgmt_path = Layout::get()->sysconfdir;

  // Set up the application version info
  appVersionInfo.setup(PACKAGE_NAME, "traffic_manager", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  bool found         = false;
  int just_started   = 0;
  int cluster_mcport = -1, cluster_rsport = -1;
  // TODO: This seems completely incomplete, disabled for now
  //  int dump_config = 0, dump_process = 0, dump_node = 0, dump_cluster = 0, dump_local = 0;
  char *proxy_port   = 0;
  int proxy_backdoor = -1;
  char *group_addr = NULL, *tsArgs = NULL;
  bool disable_syslog = false;
  RecBool enable_lua  = false;
  char userToRunAs[MAX_LOGIN + 1];
  RecInt fds_throttle = -1;
  time_t ticker;
  ink_thread synthThrId;

  int binding_version      = 0;
  BindingInstance *binding = NULL;

  ArgumentDescription argument_descriptions[] = {
    {"proxyOff", '-', "Disable proxy", "F", &proxy_off, NULL, NULL},
    {"aconfPort", '-', "Autoconf port", "I", &aconf_port_arg, "MGMT_ACONF_PORT", NULL},
    {"clusterMCPort", '-', "Cluster multicast port", "I", &cluster_mcport, "MGMT_CLUSTER_MC_PORT", NULL},
    {"clusterRSPort", '-', "Cluster reliable service port", "I", &cluster_rsport, "MGMT_CLUSTER_RS_PORT", NULL},
    {"groupAddr", '-', "Multicast group address", "S*", &group_addr, "MGMT_GROUP_ADDR", NULL},
    {"path", '-', "Path to the management socket", "S*", &mgmt_path, NULL, NULL},
    {"recordsConf", '-', "Path to records.config", "S*", &recs_conf, NULL, NULL},
    {"tsArgs", '-', "Additional arguments for traffic_server", "S*", &tsArgs, NULL, NULL},
    {"proxyPort", '-', "HTTP port descriptor", "S*", &proxy_port, NULL, NULL},
    {"proxyBackDoor", '-', "Management port", "I", &proxy_backdoor, NULL, NULL},
    {TM_OPT_BIND_STDOUT, '-', "Regular file to bind stdout to", "S512", &bind_stdout, "PROXY_BIND_STDOUT", NULL},
    {TM_OPT_BIND_STDERR, '-', "Regular file to bind stderr to", "S512", &bind_stderr, "PROXY_BIND_STDERR", NULL},
#if TS_USE_DIAGS
    {"debug", 'T', "Vertical-bar-separated Debug Tags", "S1023", debug_tags, NULL, NULL},
    {"action", 'B', "Vertical-bar-separated Behavior Tags", "S1023", action_tags, NULL, NULL},
#endif
    {"nosyslog", '-', "Do not log to syslog", "F", &disable_syslog, NULL, NULL},
    HELP_ARGUMENT_DESCRIPTION(),
    VERSION_ARGUMENT_DESCRIPTION()
  };

  // Process command line arguments and dump into variables
  process_args(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv);

  // change the directory to the "root" directory
  chdir_root();

  // Line buffer standard output & standard error
  int status;
  status = setvbuf(stdout, NULL, _IOLBF, 0);
  if (status != 0)
    perror("WARNING: can't line buffer stdout");
  status = setvbuf(stderr, NULL, _IOLBF, 0);
  if (status != 0)
    perror("WARNING: can't line buffer stderr");

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
  diagsConfig = new DiagsConfig(DIAGS_LOG_FILENAME, debug_tags, action_tags, false);
  diags       = diagsConfig->diags;
  diags->set_stdout_output(bind_stdout);
  diags->set_stderr_output(bind_stderr);
  diags->prefix_str = "Manager ";

  RecLocalInit();
  LibRecordsConfigInit();

  init_dirs(); // setup critical directories, needs LibRecords

  if (RecGetRecordString("proxy.config.admin.user_id", userToRunAs, sizeof(userToRunAs)) != TS_ERR_OKAY ||
      strlen(userToRunAs) == 0) {
    mgmt_fatal(stderr, 0, "proxy.config.admin.user_id is not set\n");
  }

  RecGetRecordInt("proxy.config.net.connections_throttle", &fds_throttle);
  RecGetRecordBool("proxy.config.stats.enable_lua", &enable_lua);

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
  lmgmt = new LocalManager(proxy_off == false);
  RecLocalInitMessage();
  lmgmt->initAlarm();

  if (diags) {
    delete diagsConfig;
  }
  // INKqa11968: need to set up callbacks and diags data structures
  // using configuration in records.config
  diagsConfig = new DiagsConfig(DIAGS_LOG_FILENAME, debug_tags, action_tags, true);
  diags       = diagsConfig->diags;
  RecSetDiags(diags);
  diags->prefix_str = "Manager ";
  diags->set_stdout_output(bind_stdout);
  diags->set_stderr_output(bind_stderr);

  if (is_debug_tag_set("diags"))
    diags->dump();
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
    char *facility_str = NULL;
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

    // NOTE: do NOT call closelog() here.  Solaris gets confused
    //   and some how it hoses later calls to readdir_r.
    openlog("traffic_manager", LOG_PID | LOG_NDELAY | LOG_NOWAIT, facility_int);

    lmgmt->syslog_facility = facility_int;
  } else {
    lmgmt->syslog_facility = -1;
  }

  // Find out our hostname so we can use it as part of the initialization
  setHostnameVar();

  // Create the data structure for overview page
  //   Do this before the rest of the set up since it needs
  //   to created to handle any alarms thrown by later
  //   initialization
  overviewGenerator = new overviewPage();

  // Initialize the Config Object bindings before
  //   starting any other threads
  lmgmt->configFiles = configFiles = new FileManager();
  initializeRegistry();
  configFiles->registerCallback(fileUpdated);

  // RecLocal's 'sync_thr' depends on 'configFiles', so we can't
  // stat the 'sync_thr' until 'configFiles' has been initialized.
  RecLocalStart(configFiles);

  /* Update cmd line overrides/environmental overrides/etc */
  if (tsArgs) { /* Passed command line args for proxy */
    ats_free(lmgmt->proxy_options);
    lmgmt->proxy_options = tsArgs;
    mgmt_log(stderr, "[main] Traffic Server Args: '%s'\n", lmgmt->proxy_options);
  }

  // we must pass in bind_stdout and bind_stderr values to TS
  // we do it so TS is able to create BaseLogFiles for each value
  if (*bind_stdout != 0) {
    size_t l = strlen(lmgmt->proxy_options);
    size_t n = 3                            /* " --" */
               + sizeof(TM_OPT_BIND_STDOUT) /* nul accounted for here */
               + 1                          /* space */
               + strlen(bind_stdout);
    lmgmt->proxy_options = static_cast<char *>(ats_realloc(lmgmt->proxy_options, n + l));
    snprintf(lmgmt->proxy_options + l, n, " --%s %s", TM_OPT_BIND_STDOUT, bind_stdout);
  }

  if (*bind_stderr != 0) {
    size_t l = strlen(lmgmt->proxy_options);
    size_t n = 3                            /* space dash dash */
               + sizeof(TM_OPT_BIND_STDERR) /* nul accounted for here */
               + 1                          /* space */
               + strlen(bind_stderr);
    lmgmt->proxy_options = static_cast<char *>(ats_realloc(lmgmt->proxy_options, n + l));
    snprintf(lmgmt->proxy_options + l, n, " --%s %s", TM_OPT_BIND_STDERR, bind_stderr);
  }

  if (proxy_port) {
    HttpProxyPort::loadValue(lmgmt->m_proxy_ports, proxy_port);
  }

  if (proxy_backdoor != -1) {
    RecSetRecordInt("proxy.config.process_manager.mgmt_port", proxy_backdoor, REC_SOURCE_DEFAULT);
  }

  if (cluster_rsport == -1) {
    cluster_rsport = REC_readInteger("proxy.config.cluster.rsport", &found);
    ink_assert(found);
  }

  if (cluster_mcport == -1) {
    cluster_mcport = REC_readInteger("proxy.config.cluster.mcport", &found);
    ink_assert(found);
  }

  if (!group_addr) {
    group_addr = REC_readString("proxy.config.cluster.mc_group_addr", &found);
    ink_assert(found);
  }

  in_addr_t min_ip        = inet_network("224.0.0.255");
  in_addr_t max_ip        = inet_network("239.255.255.255");
  in_addr_t group_addr_ip = inet_network(group_addr);

  if (!(min_ip < group_addr_ip && group_addr_ip < max_ip)) {
    mgmt_fatal(0, "[TrafficManager] Multi-Cast group addr '%s' is not in the permitted range of %s\n", group_addr,
               "224.0.1.0 - 239.255.255.255");
  }

  /* TODO: Do we really need to init cluster communication? */
  lmgmt->initCCom(appVersionInfo, configFiles, cluster_mcport, group_addr, cluster_rsport); /* Setup cluster communication */

  lmgmt->initMgmtProcessServer(); /* Setup p-to-p process server */

  // Now that we know our cluster ip address, add the
  //   UI record for this machine
  overviewGenerator->addSelfRecord();
  lmgmt->listenForProxy();

  //
  // As listenForProxy() may change/restore euid, we should put
  // the creation of mgmt_synthetic_main thread after it. So that we
  // can keep a consistent euid when create mgmtapi/eventapi unix
  // sockets in mgmt_synthetic_main thread.
  //
  synthThrId = ink_thread_create(mgmt_synthetic_main, NULL); /* Spin web agent thread */
  Debug("lm", "Created Web Agent thread (%" PRId64 ")", (int64_t)synthThrId);

  // Setup the API and event sockets
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  ats_scoped_str apisock(Layout::relative_to(rundir, MGMTAPI_MGMT_SOCKET_NAME));
  ats_scoped_str eventsock(Layout::relative_to(rundir, MGMTAPI_EVENT_SOCKET_NAME));

  mode_t oldmask = umask(0);
  mode_t newmode = api_socket_is_restricted() ? 00700 : 00777;

  int mgmtapiFD         = -1; // FD for the api interface to issue commands
  int eventapiFD        = -1; // FD for the api and clients to handle event callbacks
  char mgmtapiFailMsg[] = "Traffic server management API service Interface Failed to Initialize.";

  mgmtapiFD = bind_unix_domain_socket(apisock, newmode);
  if (mgmtapiFD == -1) {
    mgmt_log(stderr, "[WebIntrMain] Unable to set up socket for handling management API calls. API socket path = %s\n",
             (const char *)apisock);
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_WEB_ERROR, mgmtapiFailMsg);
  }

  eventapiFD = bind_unix_domain_socket(eventsock, newmode);
  if (eventapiFD == -1) {
    mgmt_log(stderr, "[WebIntrMain] Unable to set up so for handling management API event calls. Event Socket path: %s\n",
             (const char *)eventsock);
  }

  umask(oldmask);
  ink_thread_create(ts_ctrl_main, &mgmtapiFD);
  ink_thread_create(event_callback_main, &eventapiFD);

  ticker = time(NULL);
  mgmt_log("[TrafficManager] Setup complete\n");

  statProcessor = new StatProcessor(configFiles);

  RecRegisterStatInt(RECT_NODE, "proxy.node.config.reconfigure_time", time(NULL), RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_NODE, "proxy.node.config.reconfigure_required", 0, RECP_NON_PERSISTENT);

  RecRegisterStatInt(RECT_NODE, "proxy.node.config.restart_required.proxy", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_NODE, "proxy.node.config.restart_required.manager", 0, RECP_NON_PERSISTENT);
  RecRegisterStatInt(RECT_NODE, "proxy.node.config.restart_required.cop", 0, RECP_NON_PERSISTENT);

#if !TS_USE_LUAJIT
  if (enable_lua) {
    static bool warned = false;
    enable_lua         = false;

    if (!warned) {
      Warning("missing Lua support, disabling Lua derived metrics");
      warned = true;
    }
  }
#endif

  if (enable_lua) {
    binding = new BindingInstance;
    metrics_binding_initialize(*binding);
  }

  int sleep_time = 0; // sleep_time given in sec

  for (;;) {
    lmgmt->processEventQueue();
    lmgmt->pollMgmtProcessServer();

    if (enable_lua) {
      if (binding_version != metrics_version) {
        metrics_binding_destroy(*binding);
        delete binding;

        binding = new BindingInstance;
        metrics_binding_initialize(*binding);
        binding_version = metrics_version;
      }
    }

    // Handle rotation of output log (aka traffic.out) as well as DIAGS_LOG_FILENAME (aka manager.log)
    rotateLogs();

    // Check for a SIGHUP
    if (sigHupNotifier != 0) {
      mgmt_log(stderr, "[main] Reading Configuration Files due to SIGHUP\n");
      Reconfigure();
      sigHupNotifier = 0;
      mgmt_log(stderr, "[main] Reading Configuration Files Reread\n");
    }

    lmgmt->ccom->generateClusterDelta();

    if (lmgmt->run_proxy && lmgmt->processRunning()) {
      lmgmt->ccom->sendSharedData();
      lmgmt->virt_map->lt_runGambit();
    } else {
      if (!lmgmt->run_proxy) { /* Down if we are not going to start another immed. */
        /* Proxy is not up, so no addrs should be */
        lmgmt->virt_map->downOurAddrs();
      }

      /* Proxy is not up, but we should still exchange config and alarm info */
      lmgmt->ccom->sendSharedData(false);
    }

    lmgmt->ccom->checkPeers(&ticker);
    overviewGenerator->checkForUpdates();

    if (enable_lua) {
      metrics_binding_evaluate(*binding);
    } else {
      if (statProcessor) {
        statProcessor->processStat();
      }
    }

    if (lmgmt->mgmt_shutdown_outstanding != MGMT_PENDING_NONE) {
      Debug("lm", "pending shutdown %d", lmgmt->mgmt_shutdown_outstanding);
    }
    switch (lmgmt->mgmt_shutdown_outstanding) {
    case MGMT_PENDING_RESTART:
      lmgmt->mgmtShutdown();
      _exit(0);
      break;
    case MGMT_PENDING_IDLE_RESTART:
      if (is_server_idle()) {
        lmgmt->mgmtShutdown();
        _exit(0);
      }
      break;
    case MGMT_PENDING_BOUNCE:
      lmgmt->processBounce();
      lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      break;
    case MGMT_PENDING_IDLE_BOUNCE:
      if (is_server_idle()) {
        lmgmt->processBounce();
        lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
      }
      break;
    default:
      break;
    }

    if (lmgmt->run_proxy && !lmgmt->processRunning()) { /* Make sure we still have a proxy up */
      if (sleep_time) {
        mgmt_log(stderr, "Relaunching proxy after %d sec...", sleep_time);
        millisleep(1000 * sleep_time); // we use millisleep instead of sleep because it doesnt interfere with signals
        sleep_time = (sleep_time > 30) ? 60 : sleep_time * 2;
      } else {
        sleep_time = 1;
      }
      if (lmgmt->startProxy()) {
        just_started = 0;
        sleep_time   = 0;
      } else {
        just_started++;
      }
    } else { /* Give the proxy a chance to fire up */
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
          mgmt_log(stderr, "[main] Proxy terminated due to Sig %d. Relaunching after %d sec...\n", sig, sleep_time);
#else
          mgmt_log(stderr, "[main] Proxy terminated due to Sig %d: %s. Relaunching after %d sec...\n", sig, strsignal(sig),
                   sleep_time);
#endif /* NEED_PSIGNAL */
        }
      }
      mgmt_log(stderr, "[main] Proxy launch failed, retrying after %d sec...\n", sleep_time);
    }
  }

  if (binding) {
    metrics_binding_destroy(*binding);
    delete binding;
  }

  if (statProcessor) {
    delete statProcessor;
  }

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
   fprintf(stderr, "[TrafficManager] ==> SIGALRM received\n");
   mgmt_elog(stderr, 0, "[TrafficManager] ==> SIGALRM received\n");
 */
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  if (t) {
    if (t->si_code <= 0) {
#if defined(solaris)
      fprintf(stderr, "[TrafficManager] ==> User Alarm from pid: %d uid: %d\n", (int)t->si_pid, t->si_uid);
#else
      fprintf(stderr, "[TrafficManager] ==> User Alarm from pid: %d uid: %d\n", t->si_pid, t->si_uid);
#endif
      mgmt_elog(stderr, 0, "[TrafficManager] ==> User Alarm from pid: %d uid: %d\n", t->si_pid, t->si_uid);
    } else {
      fprintf(stderr, "[TrafficManager] ==> Kernel Alarm Reason: %d\n", t->si_code);
      mgmt_elog(stderr, 0, "[TrafficManager] ==> Kernel Alarm Reason: %d\n", t->si_code);
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
#if defined(solaris)
      fprintf(stderr, "[TrafficManager] ==> User Sig %d from pid: %d uid: %d\n", sig, (int)t->si_pid, t->si_uid);
#else
      fprintf(stderr, "[TrafficManager] ==> User Sig %d from pid: %d uid: %d\n", sig, t->si_pid, t->si_uid);
#endif
      mgmt_elog(stderr, 0, "[TrafficManager] ==> User Sig %d from pid: %d uid: %d\n", sig, t->si_pid, t->si_uid);
    } else {
      fprintf(stderr, "[TrafficManager] ==> Kernel Sig %d; Reason: %d\n", sig, t->si_code);
      mgmt_elog(stderr, 0, "[TrafficManager] ==> Kernel Sig %d; Reason: %d\n", sig, t->si_code);
    }
  }
#endif

  if (sig == SIGHUP) {
    sigHupNotifier = 1;
    return;
  }

  fprintf(stderr, "[TrafficManager] ==> Cleaning up and reissuing signal #%d\n", sig);
  mgmt_elog(stderr, 0, "[TrafficManager] ==> Cleaning up and reissuing signal #%d\n", sig);

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
    mgmt_elog(stderr, 0, "[TrafficManager] ==> signal #%d\n", sig);
    _exit(sig);
  }
  fprintf(stderr, "[TrafficManager] ==> signal2 #%d\n", sig);
  mgmt_elog(stderr, 0, "[TrafficManager] ==> signal2 #%d\n", sig);
  _exit(sig);
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
fileUpdated(char *fname, bool incVersion)
{
  if (strcmp(fname, "cluster.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cluster.cluster_configuration");

  } else if (strcmp(fname, "remap.config") == 0) {
    lmgmt->signalFileChange("proxy.config.url_remap.filename");

  } else if (strcmp(fname, "socks.config") == 0) {
    lmgmt->signalFileChange("proxy.config.socks.socks_config_file");

  } else if (strcmp(fname, "records.config") == 0) {
    lmgmt->signalFileChange("records.config", incVersion);

  } else if (strcmp(fname, "cache.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cache.control.filename");

  } else if (strcmp(fname, "parent.config") == 0) {
    lmgmt->signalFileChange("proxy.config.http.parent_proxy.file");

  } else if (strcmp(fname, "ip_allow.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cache.ip_allow.filename");
  } else if (strcmp(fname, "vaddrs.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] vaddrs.config updated\n");
    lmgmt->virt_map->lt_readAListFile(fname);

  } else if (strcmp(fname, "storage.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] storage.config changed, need restart auto-rebuild mode\n");

  } else if (strcmp(fname, "icp.config") == 0) {
    lmgmt->signalFileChange("proxy.config.icp.icp_configuration");

  } else if (strcmp(fname, "volume.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] volume.config changed, need restart\n");

  } else if (strcmp(fname, "hosting.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cache.hosting_filename");

  } else if (strcmp(fname, "log_hosts.config") == 0) {
    lmgmt->signalFileChange("proxy.config.log.hosts_config_file");

  } else if (strcmp(fname, "logs_xml.config") == 0) {
    lmgmt->signalFileChange("proxy.config.log.xml_config_file");

  } else if (strcmp(fname, "splitdns.config") == 0) {
    lmgmt->signalFileChange("proxy.config.dns.splitdns.filename");

  } else if (strcmp(fname, "plugin.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] plugin.config file has been modified\n");

  } else if (strcmp(fname, "ssl_multicert.config") == 0) {
    lmgmt->signalFileChange("proxy.config.ssl.server.multicert.filename");

  } else if (strcmp(fname, "proxy.config.body_factory.template_sets_dir") == 0) {
    lmgmt->signalFileChange("proxy.config.body_factory.template_sets_dir");

  } else if (strcmp(fname, "stats.config.xml") == 0) {
    if (statProcessor) {
      statProcessor->rereadConfig(configFiles);
    }
    mgmt_log(stderr, "[fileUpdated] stats.config.xml file has been modified\n");
  } else if (strcmp(fname, "metrics.config") == 0) {
    ink_atomic_increment(&metrics_version, 1);
    mgmt_log(stderr, "[fileUpdated] metrics.config file has been modified\n");
  } else if (strcmp(fname, "congestion.config") == 0) {
    lmgmt->signalFileChange("proxy.config.http.congestion_control.filename");
  } else {
    mgmt_elog(stderr, 0, "[fileUpdated] Unknown config file updated '%s'\n", fname);
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

  cap_set_flag(cap_set, CAP_EFFECTIVE, CAP_COUNT, cap_list, CAP_SET);
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
      mgmt_elog(stderr, 0, "[runAsUser] Error: Failed to restore capabilities after switch to user %s.\n", userName);
    }
#endif
  }
} /* End runAsUser() */
