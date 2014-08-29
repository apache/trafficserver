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

#include "libts.h"
#include "ink_sys_control.h"

#include "MgmtUtils.h"
#include "WebMgmtUtils.h"
#include "WebIntrMain.h"
#include "WebOverview.h"
#include "ClusterCom.h"
#include "VMap.h"
#include "FileManager.h"
#include "I_Layout.h"
#include "I_Version.h"
#include "Diags.h"
#include "DiagsConfig.h"
#include "URL.h"
#include "MIME.h"
#include "HTTP.h"

// Needs LibRecordsConfigInit()
#include "RecordsConfig.h"

#include "StatProcessor.h"
#include "P_RecLocal.h"
#include "P_RecCore.h"

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
static void runAsUser(char *userName);
static void printUsage(void);

#if defined(freebsd)
extern "C" int getpwnam_r(const char *name, struct passwd *result, char *buffer, size_t buflen, struct passwd **resptr);
#endif

static void extractConfigInfo(char *mgmt_path, const char *recs_conf, char *userName, int *fds_throttle);

static StatProcessor *statProcessor;   // Statistics Processors
static AppVersionInfo appVersionInfo;  // Build info for this application

static inkcoreapi DiagsConfig *diagsConfig;
static char debug_tags[1024] = "";
static char action_tags[1024] = "";
static bool proxy_on = true;

static char mgmt_path[PATH_NAME_MAX + 1];

// By default, set the current directory as base
static const char *recs_conf = "records.config";

static int fds_limit;

// TODO: Use positive instead negative selection
//       Thsis should just be #if defined(solaris)
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
static void SignalHandler(int sig, siginfo_t * t, void *f);
static void SignalAlrmHandler(int sig, siginfo_t * t, void *f);
#else
static void SignalHandler(int sig);
static void SignalAlrmHandler(int sig);
#endif

static volatile int sigHupNotifier = 0;
static volatile int sigUsr2Notifier = 0;
static void SigChldHandler(int sig);

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
    server_lockfile.Close();    // no server running
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
      mgmt_elog(stderr, 0, "FATAL: Can't open server lockfile '%s' (%s)\n",
                lockfile, (reason ? reason : "Unknown Reason"));
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
  sigHandler.sa_handler = NULL;
  sigHandler.sa_sigaction = SignalHandler;
#else
  sigHandler.sa_handler = SignalHandler;
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
  sigHandler.sa_flags = SA_RESETHAND;
#endif
  sigaction(SIGINT, &sigHandler, NULL);
  sigaction(SIGQUIT, &sigHandler, NULL);
  sigaction(SIGILL, &sigHandler, NULL);
  sigaction(SIGBUS, &sigHandler, NULL);
  sigaction(SIGSEGV, &sigHandler, NULL);
  sigaction(SIGTERM, &sigHandler, NULL);

#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigAlrmHandler.sa_handler = NULL;
  sigAlrmHandler.sa_sigaction = SignalAlrmHandler;
#else
  sigAlrmHandler.sa_handler = SignalAlrmHandler;
#endif

  sigemptyset(&sigAlrmHandler.sa_mask);
#if !defined(linux) && !defined(freebsd) && !defined(darwin)
  sigAlrmHandler.sa_flags = SA_SIGINFO;
#else
  sigAlrmHandler.sa_flags = 0;
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
  sigChldHandler.sa_flags = SA_RESTART;
  sigemptyset(&sigChldHandler.sa_mask);
  sigaction(SIGCHLD, &sigChldHandler, NULL);
}

#if defined(linux)
#include <sys/prctl.h>
#endif
static int
setup_coredump()
{
#if defined(linux)
#ifndef PR_SET_DUMPABLE
#define PR_SET_DUMPABLE 4       /* Ugly, but we cannot compile with 2.2.x otherwise.
                                   Should be removed when we compile only on 2.4.x */
#endif
  prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif  // linux check
  return 0;
}

static void
init_dirs()
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());

  if (access(Layout::get()->sysconfdir, R_OK) == -1) {
    mgmt_elog(0, "unable to access() config dir '%s': %d, %s\n", Layout::get()->sysconfdir, errno, strerror(errno));
    mgmt_elog(0, "please set the 'TS_ROOT' environment variable\n");
    _exit(1);
  }

  if (access(rundir, R_OK) == -1) {
    mgmt_elog(0, "unable to access() local state dir '%s': %d, %s\n", (const char *)rundir, errno, strerror(errno));
    mgmt_elog(0, "please set 'proxy.config.local_state_dir'\n");
    _exit(1);
  }
}

static void
chdir_root()
{
  const char * prefix = Layout::get()->prefix;

  if (chdir(prefix) < 0) {
    mgmt_elog(0, "unable to change to root directory \"%s\" [%d '%s']\n", prefix, errno, strerror(errno));
    mgmt_elog(0, " please set correct path in env variable TS_ROOT \n");
    exit(1);
  } else {
    mgmt_log("[TrafficManager] using root directory '%s'\n", prefix);
  }
}

static void
set_process_limits(int fds_throttle)
{
  struct rlimit lim;

  // Set needed rlimits (root)
  ink_max_out_rlimit(RLIMIT_NOFILE, true, false);
  ink_max_out_rlimit(RLIMIT_STACK, true, true);
  ink_max_out_rlimit(RLIMIT_DATA, true, true);
  ink_max_out_rlimit(RLIMIT_FSIZE, true, false);
#ifdef RLIMIT_RSS
  ink_max_out_rlimit(RLIMIT_RSS, true, true);
#endif

#if defined(linux)
  float file_max_pct = 0.9;
  FILE *fd;

  if ((fd = fopen("/proc/sys/fs/file-max","r"))) {
    ATS_UNUSED_RETURN(fscanf(fd, "%lu", &lim.rlim_max));
    fclose(fd);
    REC_ReadConfigFloat(file_max_pct, "proxy.config.system.file_max_pct");
    lim.rlim_cur = lim.rlim_max = static_cast<rlim_t>(lim.rlim_max * file_max_pct);
    if (!setrlimit(RLIMIT_NOFILE, &lim) && !getrlimit(RLIMIT_NOFILE, &lim)) {
      fds_limit = (int) lim.rlim_cur;
      syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)",RLIMIT_NOFILE, (int)lim.rlim_cur, (int)lim.rlim_max);
    } else {
      syslog(LOG_NOTICE, "NOTE: Unable to set RLIMIT_NOFILE(%d):cur(%d),max(%d)", RLIMIT_NOFILE, (int)lim.rlim_cur, (int)lim.rlim_max);
    }
  } else {
    syslog(LOG_NOTICE, "NOTE: Unable to open /proc/sys/fs/file-max");
  }
#endif // linux

  if (!getrlimit(RLIMIT_NOFILE, &lim)) {
    if (fds_throttle > (int) (lim.rlim_cur + FD_THROTTLE_HEADROOM)) {
      lim.rlim_cur = (lim.rlim_max = (rlim_t) fds_throttle);
      if (!setrlimit(RLIMIT_NOFILE, &lim) && !getrlimit(RLIMIT_NOFILE, &lim)) {
        fds_limit = (int) lim.rlim_cur;
	syslog(LOG_NOTICE, "NOTE: RLIMIT_NOFILE(%d):cur(%d),max(%d)",RLIMIT_NOFILE, (int)lim.rlim_cur, (int)lim.rlim_max);
      }
    }
  }

}

#if TS_HAS_WCCP
static void
Errata_Logger(ts::Errata const& err) {
  size_t n;
  static size_t const SIZE = 4096;
  char buff[SIZE];
  if (err.size()) {
    ts::Errata::Code code = err.top().getCode();
    n = err.write(buff, SIZE, 1, 0, 2, "> ");
    // strip trailing newlines.
    while (n && (buff[n-1] == '\n' || buff[n-1] == '\r'))
      buff[--n] = 0;
    // log it.
    if (code > 1) mgmt_elog(0, "[WCCP]%s", buff);
    else if (code > 0) mgmt_log("[WCCP]%s", buff);
    else Debug("WCCP", "%s", buff);
  }
}

static void
Init_Errata_Logging() {
  ts::Errata::registerSink(&Errata_Logger);
}
#endif

int
main(int argc, char **argv)
{
  // Before accessing file system initialize Layout engine
  Layout::create();
  ink_strlcpy(mgmt_path, Layout::get()->sysconfdir, sizeof(mgmt_path));

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

  bool found = false;
  int just_started = 0;
  int cluster_mcport = -1, cluster_rsport = -1;
  // TODO: This seems completely incomplete, disabled for now
  //  int dump_config = 0, dump_process = 0, dump_node = 0, dump_cluster = 0, dump_local = 0;
  char* proxy_port = 0;
  int proxy_backdoor = -1;
  char *envVar = NULL, *group_addr = NULL, *tsArgs = NULL;
  bool log_to_syslog = true;
  char userToRunAs[80];
  int  fds_throttle = -1;
  time_t ticker;
  ink_thread webThrId;

  // Set up the application version info
  appVersionInfo.setup(PACKAGE_NAME,"traffic_manager", PACKAGE_VERSION,
                       __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  initSignalHandlers();

  // Process Environment Variables
  if ((envVar = getenv("MGMT_ACONF_PORT")) != NULL) {
    aconf_port_arg = atoi(envVar);
  }

  if ((envVar = getenv("MGMT_CLUSTER_MC_PORT")) != NULL) {
    cluster_mcport = atoi(envVar);
  }

  if ((envVar = getenv("MGMT_CLUSTER_RS_PORT")) != NULL) {
    cluster_rsport = atoi(envVar);
  }

  if ((envVar = getenv("MGMT_GROUP_ADDR")) != NULL) {
    group_addr = envVar;
  }

  for (int i = 1; i < argc; i++) {      /* Process command line args */

    if (argv[i][0] == '-') {
      if ((strcmp(argv[i], "-version") == 0) || (strcmp(argv[i], "-V") == 0)) {
        fprintf(stderr, "%s\n", appVersionInfo.FullVersionInfoStr);
        exit(0);
      } else if (strcmp(argv[i], "-proxyOff") == 0) {
        proxy_on = false;
      } else if (strcmp(argv[i], "-nosyslog") == 0) {
        log_to_syslog = false;
      } else {
        // The rest of the options require an argument in the form of -<Flag> <val>
        if ((i + 1) < argc) {

          if (strcmp(argv[i], "-aconfPort") == 0) {
            ++i;
            aconf_port_arg = atoi(argv[i]);
          } else if (strcmp(argv[i], "-clusterMCPort") == 0) {
            ++i;
            cluster_mcport = atoi(argv[i]);
          } else if (strcmp(argv[i], "-groupAddr") == 0) {
            ++i;
            group_addr = argv[i];
          } else if (strcmp(argv[i], "-clusterRSPort") == 0) {
            ++i;
            cluster_rsport = atoi(argv[i]);
#if TS_USE_DIAGS
          } else if (strcmp(argv[i], "-debug") == 0) {
            ++i;
            ink_strlcpy(debug_tags, argv[i], sizeof(debug_tags));
          } else if (strcmp(argv[i], "-action") == 0) {
            ++i;
            ink_strlcpy(action_tags, argv[i], sizeof(debug_tags));
#endif
          } else if (strcmp(argv[i], "-path") == 0) {
            ++i;
            //bugfixed by YTS Team, yamsat(id-59703)
            if ((strlen(argv[i]) > PATH_NAME_MAX)) {
              fprintf(stderr, "\n   Path exceeded the maximum allowed characters.\n");
              exit(1);
            }

            ink_strlcpy(mgmt_path, argv[i], sizeof(mgmt_path));
            /*
               } else if(strcmp(argv[i], "-lmConf") == 0) {
               ++i;
               lm_conf = argv[i];
             */
          } else if (strcmp(argv[i], "-recordsConf") == 0) {
            ++i;
            recs_conf = argv[i];
            // TODO: This seems completely incomplete, disabled for now
#if 0
          } else if (strcmp(argv[i], "-printRecords") == 0) {
            ++i;
            while (i < argc && argv[i][0] != '-') {
              if (strcasecmp(argv[i], "config") == 0) {
                dump_config = 1;
              } else if (strcasecmp(argv[i], "process") == 0) {
                dump_process = 1;
              } else if (strcasecmp(argv[i], "node") == 0) {
                dump_node = 1;
              } else if (strcasecmp(argv[i], "cluster") == 0) {
                dump_cluster = 1;
              } else if (strcasecmp(argv[i], "local") == 0) {
                dump_local = 1;
              } else if (strcasecmp(argv[i], "all") == 0) {
                dump_config = dump_node = dump_process = dump_cluster = dump_local = 1;
              }
              ++i;
            }
            --i;
#endif
          } else if (strcmp(argv[i], "-tsArgs") == 0) {
            int size_of_args = 0, j = (++i);
            while (j < argc) {
              size_of_args += 1;
              size_of_args += strlen((argv[j++]));
            }
            tsArgs = (char *)ats_malloc(size_of_args + 1);

            j = 0;
            while (i < argc) {
              snprintf(&tsArgs[j], ((size_of_args + 1) - j), " %s", argv[i]);
              j += strlen(argv[i]) + 1;
              ++i;
            }
          } else if (strcmp(argv[i], "-proxyPort") == 0) {
            ++i;
            proxy_port = argv[i];
          } else if (strcmp(argv[i], "-proxyBackDoor") == 0) {
            ++i;
            proxy_backdoor = atoi(argv[i]);
          } else {
            printUsage();
          }
        } else {
          printUsage();
        }
      }
    }
  }

  // Bootstrap with LOG_DAEMON until we've read our configuration
  if (log_to_syslog) {
    openlog("traffic_manager", LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);
    mgmt_use_syslog();
    syslog(LOG_NOTICE, "NOTE: --- Manager Starting ---");
    syslog(LOG_NOTICE, "NOTE: Manager Version: %s", appVersionInfo.FullVersionInfoStr);
  }

  // Bootstrap the Diags facility so that we can use it while starting
  //  up the manager
  diagsConfig = new DiagsConfig(DIAGS_LOG_FILENAME, debug_tags, action_tags, false);
  diags = diagsConfig->diags;
  diags->prefix_str = "Manager ";

  RecLocalInit();
  LibRecordsConfigInit();
  RecordsConfigOverrideFromEnvironment();

  init_dirs();// setup critical directories, needs LibRecords

  // Get the config info we need while we are still root
  extractConfigInfo(mgmt_path, recs_conf, userToRunAs, &fds_throttle);

  set_process_limits(fds_throttle); // as root
  runAsUser(userToRunAs);
  setup_coredump();
  check_lockfile();

  url_init();
  mime_init();
  http_init();

#if TS_HAS_WCCP
  Init_Errata_Logging();
#endif
  ts_host_res_global_init();
  ts_session_protocol_well_known_name_indices_init();
  lmgmt = new LocalManager(proxy_on);
  RecLocalInitMessage();
  lmgmt->initAlarm();

  if (diags) {
    delete diagsConfig;
    // diagsConfig->reconfigure_diags(); INKqa11968
    /*
       delete diags;
       diags = new Diags(debug_tags,action_tags);
     */
  }
  // INKqa11968: need to set up callbacks and diags data structures
  // using configuration in records.config
  diagsConfig = new DiagsConfig(DIAGS_LOG_FILENAME, debug_tags, action_tags, true);
  diags = diagsConfig->diags;
  RecSetDiags(diags);
  diags->prefix_str = "Manager ";

  if (is_debug_tag_set("diags"))
    diags->dump();
  diags->cleanup_func = mgmt_cleanup;

  // Setup the exported manager version records.
  RecSetRecordString("proxy.node.version.manager.short", appVersionInfo.VersionStr);
  RecSetRecordString("proxy.node.version.manager.long", appVersionInfo.FullVersionInfoStr);
  RecSetRecordString("proxy.node.version.manager.build_number", appVersionInfo.BldNumStr);
  RecSetRecordString("proxy.node.version.manager.build_time", appVersionInfo.BldTimeStr);
  RecSetRecordString("proxy.node.version.manager.build_date", appVersionInfo.BldDateStr);
  RecSetRecordString("proxy.node.version.manager.build_machine", appVersionInfo.BldMachineStr);
  RecSetRecordString("proxy.node.version.manager.build_person", appVersionInfo.BldPersonStr);
//    RecSetRecordString("proxy.node.version.manager.build_compile_flags",
//                       appVersionInfo.BldCompileFlagsStr);

  if (log_to_syslog) {
    char sys_var[] = "proxy.config.syslog_facility";
    char *facility_str = NULL;
    int facility_int;
    facility_str = REC_readString(sys_var, &found);
    ink_assert(found);

    if (!found) {
      mgmt_elog(0, "Could not read %s.  Defaulting to DAEMON\n", sys_var);
      facility_int = LOG_DAEMON;
    } else {
      facility_int = facility_string_to_int(facility_str);
      ats_free(facility_str);
      if (facility_int < 0) {
        mgmt_elog(0, "Bad syslog facility specified.  Defaulting to DAEMON\n");
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
  if (tsArgs) {                 /* Passed command line args for proxy */
    ats_free(lmgmt->proxy_options);
    lmgmt->proxy_options = tsArgs;
    mgmt_log(stderr, "[main] Traffic Server Args: '%s'\n", lmgmt->proxy_options);
  }
  if (proxy_port) {
    HttpProxyPort::loadValue(lmgmt->m_proxy_ports, proxy_port);
  }

  if (proxy_backdoor != -1) {
    RecSetRecordInt("proxy.config.process_manager.mgmt_port", proxy_backdoor);
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

  in_addr_t min_ip = inet_network("224.0.0.255");
  in_addr_t max_ip = inet_network("239.255.255.255");
  in_addr_t group_addr_ip = inet_network(group_addr);

  if (!(min_ip < group_addr_ip && group_addr_ip < max_ip)) {
    mgmt_fatal(0, "[TrafficManager] Multi-Cast group addr '%s' is not in the permitted range of %s\n",
               group_addr, "224.0.1.0 - 239.255.255.255");
  }

  /* TODO: Do we really need to init cluster communication? */
  lmgmt->initCCom(appVersionInfo, configFiles, cluster_mcport, group_addr, cluster_rsport);       /* Setup cluster communication */

  lmgmt->initMgmtProcessServer();       /* Setup p-to-p process server */

  // Now that we know our cluster ip address, add the
  //   UI record for this machine
  overviewGenerator->addSelfRecord();

  lmgmt->listenForProxy();

  //
  // As listenForProxy() may change/restore euid, we should put
  // the creation of webIntr_main thread after it. So that we
  // can keep a consistent euid when create mgmtapi/eventapi unix
  // sockets in webIntr_main thread.
  //
  webThrId = ink_thread_create(webIntr_main, NULL);     /* Spin web agent thread */
  Debug("lm", "Created Web Agent thread (%"  PRId64 ")", (int64_t)webThrId);

  ticker = time(NULL);
  mgmt_log("[TrafficManager] Setup complete\n");

  statProcessor = new StatProcessor(configFiles);

  for (;;) {
    lmgmt->processEventQueue();
    lmgmt->pollMgmtProcessServer();

    // Check for a SIGHUP
    if (sigHupNotifier != 0) {
      mgmt_log(stderr, "[main] Reading Configuration Files due to SIGHUP\n");
      configFiles->rereadConfig();
      lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, "*");
      sigHupNotifier = 0;
      mgmt_log(stderr, "[main] Reading Configuration Files Reread\n");
    }
    // Check for SIGUSR2
    if (sigUsr2Notifier != 0) {
      ink_stack_trace_dump();
      sigUsr2Notifier = 0;
    }

    lmgmt->ccom->generateClusterDelta();

    if (lmgmt->run_proxy && lmgmt->processRunning()) {
      lmgmt->ccom->sendSharedData();
      lmgmt->virt_map->lt_runGambit();
    } else {
      if (!lmgmt->run_proxy) {  /* Down if we are not going to start another immed. */
        /* Proxy is not up, so no addrs should be */
        lmgmt->virt_map->downOurAddrs();
      }

      /* Proxy is not up, but we should still exchange config and alarm info */
      lmgmt->ccom->sendSharedData(false);
    }

    lmgmt->ccom->checkPeers(&ticker);
    overviewGenerator->checkForUpdates();

    if (statProcessor) {
      statProcessor->processStat();
    }

    if (lmgmt->mgmt_shutdown_outstanding == true) {
      lmgmt->mgmtShutdown(true);
      _exit(0);
    }

    if (lmgmt->run_proxy && !lmgmt->processRunning()) { /* Make sure we still have a proxy up */
      if (lmgmt->startProxy())
        just_started = 0;
      else
        just_started++;
    } else {                    /* Give the proxy a chance to fire up */
      just_started++;
    }

    /* This will catch the case were the proxy dies before it can connect to manager */
    if (lmgmt->proxy_launch_outstanding && !lmgmt->processRunning() && just_started >= 120) {
      just_started = 0;
      lmgmt->proxy_launch_outstanding = false;
      if (lmgmt->proxy_launch_pid != -1) {
        int res;
        kill(lmgmt->proxy_launch_pid, 9);
        waitpid(lmgmt->proxy_launch_pid, &res, 0);
        if (WIFSIGNALED(res)) {
          int sig = WTERMSIG(res);
#ifdef NEED_PSIGNAL
          mgmt_log(stderr, "[main] Proxy terminated due to Sig %d\n", sig);
#else
          mgmt_log(stderr, "[main] Proxy terminated due to Sig %d: %s\n", sig, strsignal(sig));
#endif /* NEED_PSIGNAL */
        }
      }
      mgmt_log(stderr, "[main] Proxy launch failed, retrying...\n");
    }

  }

  if (statProcessor) {
    delete(statProcessor);
  }

#ifndef MGMT_SERVICE
  return 0;
#endif

}                               /* End main */

#if !defined(linux) && !defined(freebsd) && !defined(darwin)
static void
SignalAlrmHandler(int /* sig ATS_UNUSED */, siginfo_t * t, void * /* c ATS_UNUSED */)
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
SignalHandler(int sig, siginfo_t * t, void *c)
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

  if (sig == SIGUSR2) {
    sigUsr2Notifier = 1;
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
}                               /* End SignalHandler */

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
printUsage()
{
  fprintf(stderr, "----------------------------------------------------------------------------\n");
  fprintf(stderr, " Traffic Manager Usage: (all args are optional)\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "   traffic_manager [options]\n");
  fprintf(stderr, "     -proxyPort     <port>  Port to have proxy listen on, overrides records.config.\n");
  /* Function is currently #ifdef'ed out so no reason to advertise
     fprintf(stderr,
     "     -proxyBackdoor <port>  Port to put proxy mgmt port on.\n");
   */
  /* Commented out because this option is used for debugging only.
     fprintf(stderr,
     "     -noProxy               Do not launch the proxy process.\n");
   */
  fprintf(stderr, "     -tsArgs        [...]   Args to proxy, everything till eol is passed.\n");
  fprintf(stderr, "     -webPort       <port>  Port for web interface.\n");
  /*
     fprintf(stderr,
     "     -graphPort     <port>  Port for dynamic graphs.\n");
   */
  fprintf(stderr, "     -clusterPort   <port>  Cluster Multicast port\n");
  fprintf(stderr, "     -groupAddr     <addr>  Cluster Multicast group, example: \"225.0.0.37\".\n");
  fprintf(stderr, "     -clusterRSPort <port>  Cluster Multicast port.\n");
  fprintf(stderr, "     -path          <path>  Root path for config files.\n");
  /*
     fprintf(stderr,
     "     -lmConf        <fname> Local Management config file.\n");
   */
  fprintf(stderr, "     -recordsConf   <fname> General config file.\n");
  // TODO: This seems completely incomplete, disabled for now
  // fprintf(stderr, "     -printRecords  [...]   Print flags, default all are off.\n");
  fprintf(stderr, "     -debug         <tags>  Enable the given debug tags\n");
  fprintf(stderr, "     -action        <tags>  Enable the given action tags.\n");
  fprintf(stderr, "     -version or -V         Print version id and exit.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "   [...] can be one+ of: [config process node cluster local all]\n");
  fprintf(stderr, "----------------------------------------------------------------------------\n");
  exit(0);
}                               /* End printUsage */

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

  } else if (strcmp(fname, "proxy.pac") == 0) {
    mgmt_log(stderr, "[fileUpdated] proxy.pac file has been modified\n");

  } else if (strcmp(fname, "icp.config") == 0) {
    lmgmt->signalFileChange("proxy.config.icp.icp_configuration");

  } else if (strcmp(fname, "update.config") == 0) {
    lmgmt->signalFileChange("proxy.config.update.update_configuration");

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
  } else if (strcmp(fname, "congestion.config") == 0) {
    lmgmt->signalFileChange("proxy.config.http.congestion_control.filename");
  } else if (strcmp(fname, "prefetch.config") == 0) {
    lmgmt->signalFileChange("proxy.config.prefetch.config_file");
  } else {
    mgmt_elog(stderr, 0, "[fileUpdated] Unknown config file updated '%s'\n", fname);

  }
  return;
}                               /* End fileUpdate */

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
restoreCapabilities() {
  int zret = 0; // return value.
  cap_t cap_set = cap_get_proc(); // current capabilities
  // Make a list of the capabilities we want turned on.
  cap_value_t cap_list[] = {
    CAP_NET_ADMIN, ///< Set socket transparency.
    CAP_NET_BIND_SERVICE, ///< Low port (e.g. 80) binding.
    CAP_IPC_LOCK ///< Lock IPC objects.
  };
  static int const CAP_COUNT = sizeof(cap_list)/sizeof(*cap_list);

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
runAsUser(char *userName)
{
  uid_t uid, euid;
  struct passwd *result;
  const int bufSize = 1024;
  char buf[bufSize];

  uid = getuid();
  euid = geteuid();

  if (uid == 0 || euid == 0) {

    /* Figure out what user we should run as */

    Debug("lm", "[runAsUser] Attempting to run as user '%s'\n", userName);

    if (userName == NULL || userName[0] == '\0') {
      mgmt_elog(stderr, 0, "[runAsUser] Fatal Error: proxy.config.admin.user_id is not set\n");
      _exit(1);
    }

    struct passwd passwdInfo;
    struct passwd *ppasswd = NULL;
    result = NULL;
    int res;
    if (*userName == '#') {
      int uuid = atoi(userName + 1);
      if (uuid == -1)
        uuid = (int)uid;
      res = getpwuid_r((uid_t)uuid, &passwdInfo, buf, bufSize, &ppasswd);
    }
    else {
      res = getpwnam_r(&userName[0], &passwdInfo, buf, bufSize, &ppasswd);
    }

    if (!res && ppasswd) {
      result = ppasswd;
    }

    if (result == NULL) {
      mgmt_elog(stderr, 0, "[runAsUser] Fatal Error: Unable to get info about user %s : %s\n", userName, strerror(errno));
      _exit(1);
    }

    if (setegid(result->pw_gid) != 0 || seteuid(result->pw_uid) != 0) {
      mgmt_elog(stderr, 0, "[runAsUser] Fatal Error: Unable to switch to user %s : %s\n", userName, strerror(errno));
      _exit(1);
    }

    uid = getuid();
    euid = geteuid();

    Debug("lm", "[runAsUser] Running with uid: '%d' euid: '%d'\n", uid, euid);

    if (uid != result->pw_uid && euid != result->pw_uid) {
      mgmt_elog(stderr, 0, "[runAsUser] Fatal Error: Failed to switch to user %s\n", userName);
      _exit(1);
    }

    // setup supplementary groups if it is not set.
    if (0 == getgroups(0, NULL)) {
      initgroups(&userName[0],result->pw_gid);
    }

#if TS_USE_POSIX_CAP
    if (0 != restoreCapabilities()) {
      mgmt_elog(stderr, 0, "[runAsUser] Error: Failed to restore capabilities after switch to user %s.\n", userName);
    }
#endif

  }
}                               /* End runAsUser() */

//  void extractConfigInfo(...)
//
//  We need to get certain records.config values while we are
//   root.  We can not use LMRecords to get them because the constructor
//   for LMRecords creates the mgmt DBM and we do not want that to
//   be owned as root.  This function extracts that info from
//   records.config
//
//
void
extractConfigInfo(char *mgmt_path, const char *recs_conf, char *userName, int *fds_throttle)
{
  char file[1024];
  bool useridFound = false;
  bool throttleFound = false;

  /* Figure out what user we should run as */
  if (mgmt_path && recs_conf) {
    FILE *fin;
    snprintf(file, sizeof(file), "%s/%s.shadow", mgmt_path, recs_conf);
    if (!(fin = fopen(file, "r"))) {
      ink_filepath_make(file, sizeof(file), mgmt_path, recs_conf);
      if (!(fin = fopen(file, "r"))) {
        mgmt_elog(stderr, errno, "[extractConfigInfo] Unable to open config file(%s)\n", file);
        _exit(1);
      }
    }
    // Get 'user id' and 'network connections throttle limit'
    while (((!useridFound) || (!throttleFound)) && fgets(file, 1024, fin)) {
      if (strstr(file, "CONFIG proxy.config.admin.user_id STRING")) {
        //coverity[secure_coding]
        if ((sscanf(file, "CONFIG proxy.config.admin.user_id STRING %1023s\n", userName) == 1) &&
            strcmp(userName, "NULL") != 0) {
          useridFound = true;
        }
      } else if (strstr(file, "CONFIG proxy.config.net.connections_throttle INT")) {
        if ((sscanf(file, "CONFIG proxy.config.net.connections_throttle INT %d\n", fds_throttle) == 1)) {
          throttleFound = true;
        }
      }

    }
    fclose(fin);
  } else {
    mgmt_elog(stderr, 0, "[extractConfigInfo] Fatal Error: unable to access records file\n");
    _exit(1);
  }

  if (useridFound == false) {
    mgmt_elog(stderr, 0, "[extractConfigInfo] Fatal Error: proxy.config.admin.user_id is not set\n");
    _exit(1);
  }

}                               /* End extractConfigInfo() */
