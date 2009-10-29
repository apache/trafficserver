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
 *
 * Main.cc
 * - Entry point to the traffic manager.
 * - Splitted off from LocalManager.cc on 10/23/98.
 *
 *
 */

#include "Main.h"
#include "MgmtUtils.h"
#include "MgmtSchema.h"
#include "MgmtConverter.h"
#include "WebMgmtUtils.h"
#include "WebIntrMain.h"
#include "MgmtRaf.h"
#include "WebOverview.h"
#include "FileManager.h"
#include "WebReconfig.h"
#include "I_Version.h"
#include "ink_syslog.h"
#include "ink_lockfile.h"
#include "Diags.h"
#include "DiagsConfig.h"
#include "URL.h"
#include "MIME.h"
#include "HTTP.h"
#include "RecordsConfig.h"

#ifdef BSD_TCP
#include "inkio.h"
#endif

#if defined(HAVE_LIBSSL) && defined(TRAFFIC_NET)
#include "traffic_net/tn_Thread.h"
#endif

#if defined(MGMT_API)
#include "TSControlMain.h"
#endif


#define USE_STATPRO
#if defined(USE_STATPRO)
#include "StatProcessor.h"
#else
#include "StatAggregation.h"
#endif

#include "P_RecLocal.h"
#include "P_RecCore.h"


#if defined(OEM)
#include "tools/ConfigAPI.h"
#include "tools/SysAPI.h"
#endif


#if (HOST_OS != linux)
extern "C"
{
  int gethostname(char *name, int namelen);
}
#endif

#if (HOST_OS == freebsd)
extern "C" int getpwnam_r(const char *name, struct passwd *result, char *buffer, size_t buflen, struct passwd **resptr);
#endif

//SNMP *snmp;                     // global binding with SNMP.cc
LocalManager *lmgmt = NULL;
MgmtPing *icmp_ping;
FileManager *configFiles;

#if defined(USE_STATPRO)
StatProcessor *statProcessor;   // Statistics Processors
#endif

AppVersionInfo appVersionInfo;  // Build info for this application

inkcoreapi Diags *diags;
inkcoreapi DiagsConfig *diagsConfig;
char debug_tags[1024] = "";
char action_tags[1024] = "";
int diags_init = 0;
int snmpLogLevel = 0;
bool proxy_on = true;
bool forceProcessRecordsSnap = false;

bool schema_on = false;
char *schema_path = NULL;

bool xml_on = false;
char *xml_path = NULL;

char *mgmt_path = "./conf/yts/";

// By default, set the current directory as base
char *ts_base_dir = ".";
char *recs_conf = "records.config";

typedef void (*PFV) (int);
#if (HOST_OS != linux) && (HOST_OS != freebsd)
void SignalHandler(int sig, siginfo_t * t, void *f);
void SignalAlrmHandler(int sig, siginfo_t * t, void *f);
#else
void SignalHandler(int sig);
void SignalAlrmHandler(int sig);
#endif

volatile int sigHupNotifier = 0;
volatile int sigUsr2Notifier = 0;
void SigChldHandler(int sig);

void
check_lockfile()
{
  char lockfile[PATH_MAX];
  int err;
  pid_t holding_pid;

  //////////////////////////////////////
  // test for presence of server lock //
  //////////////////////////////////////
  ink_snprintf(lockfile, PATH_MAX, "%sinternal/%s", mgmt_path, SERVER_LOCK);
  Lockfile server_lockfile(lockfile);
  err = server_lockfile.Open(&holding_pid);
  if (err == 1) {
    server_lockfile.Close();    // no server running
  } else {
    char *reason = strerror(-err);
    if (err == 0) {
      fprintf(stderr, "FATAL: Lockfile '%s' says server already running as PID %d\n", lockfile, holding_pid);
      mgmt_elog(stderr, "FATAL: Lockfile '%s' says server already running as PID %d\n", lockfile, holding_pid);
    } else {
      fprintf(stderr, "FATAL: Can't open server lockfile '%s' (%s)\n", lockfile, (reason ? reason : "Unknown Reason"));
      mgmt_elog(stderr, "FATAL: Can't open server lockfile '%s' (%s)\n",
                lockfile, (reason ? reason : "Unknown Reason"));
    }
    exit(1);
  }

  ///////////////////////////////////////////
  // try to get the exclusive manager lock //
  ///////////////////////////////////////////
  ink_snprintf(lockfile, sizeof(lockfile), "%sinternal/%s", mgmt_path, MANAGER_LOCK);
  Lockfile manager_lockfile(lockfile);
  err = manager_lockfile.Get(&holding_pid);
  if (err != 1) {
    char *reason = strerror(-err);
    fprintf(stderr, "FATAL: Can't acquire manager lockfile '%s'", lockfile);
    mgmt_elog(stderr, "FATAL: Can't acquire manager lockfile '%s'", lockfile);
    if (err == 0) {
      fprintf(stderr, " (Lock file held by process ID %d)\n", holding_pid);
      mgmt_elog(stderr, " (Lock file held by process ID %d)\n", holding_pid);
    } else if (reason) {
      fprintf(stderr, " (%s)\n", reason);
      mgmt_elog(stderr, " (%s)\n", reason);
    } else {
      fprintf(stderr, "\n");
    }
    exit(1);

    fprintf(stderr, "unable to acquire manager lock [%d]\n", -err);
    exit(1);
  }
}


void
initSignalHandlers()
{
#ifndef _WIN32
  struct sigaction sigHandler, sigChldHandler, sigAlrmHandler;
  sigset_t sigsToBlock;

  // Set up the signal handler
#if (HOST_OS != linux) && (HOST_OS != freebsd)
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
#if (HOST_OS != linux) && (HOST_OS != freebsd)
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

#if (HOST_OS != linux) && (HOST_OS != freebsd)
  sigAlrmHandler.sa_handler = NULL;
  sigAlrmHandler.sa_sigaction = SignalAlrmHandler;
#else
  sigAlrmHandler.sa_handler = SignalAlrmHandler;
#endif

  sigemptyset(&sigAlrmHandler.sa_mask);
#if (HOST_OS != linux) && (HOST_OS != freebsd)
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
#endif /* !_WIN32 */
}

#if (HOST_OS == linux)
#include <sys/prctl.h>
#endif
static int
setup_coredump()
{
#if (HOST_OS == linux)
#ifndef PR_SET_DUMPABLE
#define PR_SET_DUMPABLE 4       /* Ugly, but we cannot compile with 2.2.x otherwise.
                                   Should be removed when we compile only on 2.4.x */
#endif
  prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif  // linux check
  return 0;
}


void
chdir_root()
{
  char root_dir[PATH_MAX];
  char buffer[1024];
  char *env_path;
  FILE *ts_file;
  int i = 0;

  root_dir[0] = '\0';
  if ((env_path = getenv("ROOT")) || (env_path = getenv("INST_ROOT"))) {
    strncpy(root_dir, env_path, PATH_MAX);
  } else {
    if ((ts_file = fopen("/etc/traffic_server", "r")) != NULL) {
      fgets(buffer, 1024, ts_file);
      fclose(ts_file);
      while (!isspace(buffer[i])) {
        root_dir[i] = buffer[i];
        i++;
      }
      root_dir[i] = '\0';
    } else {
      ink_strncpy(root_dir, "/home/trafficserver", PATH_MAX);
    }
  }

  if (root_dir[0] && (chdir(root_dir) < 0)) {
    mgmt_elog("unable to change to root directory \"%s\" [%d '%s']\n", root_dir, errno, strerror(errno));
    exit(1);
  }
}


int
main(int argc, char **argv)
{
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
  int cluster_port = -1, cluster_server_port = -1;
  int dump_config = 0, dump_process = 0, dump_node = 0, proxy_port = -1;
  int dump_cluster = 0, dump_local = 0, proxy_backdoor = -1;
  char *envVar = NULL, *group_addr = NULL, *tsArgs = NULL;
  bool log_to_syslog = true;
  char userToRunAs[80];
  char config_internal_dir[PATH_MAX];

  time_t ticker;
  ink_thread webThrId;

  while ((setsid() == (pid_t) - 1) && (errno == EINTR)) {
  }

  // Set up the application version info
  appVersionInfo.setup("traffic_manager", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
#ifdef BSD_TCP
  inkio_initialize();
#endif
  initSignalHandlers();

  // Process Environment Variables
  if ((envVar = getenv("MGMT_WEB_PORT")) != NULL) {
    web_port_arg = atoi(envVar);
  }

  if ((envVar = getenv("MGMT_ACONF_PORT")) != NULL) {
    aconf_port_arg = atoi(envVar);
  }

  if ((envVar = getenv("MGMT_CLUSTER_PORT")) != NULL) {
    cluster_port = atoi(envVar);
  }

  if ((envVar = getenv("MGMT_CLUSTER_RS_PORT")) != NULL) {
    cluster_server_port = atoi(envVar);
  }

  if ((envVar = getenv("MGMT_GROUP_ADDR")) != NULL) {
    group_addr = envVar;
  }

  for (int i = 1; i < argc; i++) {      /* Process command line args */

    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "-version") == 0) {
        fprintf(stderr, "%s\n", appVersionInfo.FullVersionInfoStr);
        exit(0);
      } else if (strcmp(argv[i], "-proxyOff") == 0) {
        proxy_on = false;
      } else if (strcmp(argv[i], "-nosyslog") == 0) {
        log_to_syslog = false;
      } else if (strcmp(argv[i], "-snmplog") == 0) {
        i++;
        snmpLogLevel = atoi(argv[i]);
      } else {
        // The rest of the options require an argument in the form of -<Flag> <val>
        if ((i + 1) < argc) {

          if (strcmp(argv[i], "-webPort") == 0) {
            ++i;
            web_port_arg = atoi(argv[i]);
          } else if (strcmp(argv[i], "-aconfPort") == 0) {
            ++i;
            aconf_port_arg = atoi(argv[i]);
          } else if (strcmp(argv[i], "-clusterPort") == 0) {
            ++i;
            cluster_port = atoi(argv[i]);
          } else if (strcmp(argv[i], "-groupAddr") == 0) {
            ++i;
            group_addr = argv[i];
          } else if (strcmp(argv[i], "-clusterRSPort") == 0) {
            ++i;
            cluster_server_port = atoi(argv[i]);
          } else if (strcmp(argv[i], "-debug") == 0) {
            ++i;
            strncpy(debug_tags, argv[i], 1023);
            debug_tags[1023] = '\0';
          } else if (strcmp(argv[i], "-action") == 0) {
            ++i;
            strncpy(action_tags, argv[i], 1023);
            action_tags[1023] = '\0';
          } else if (strcmp(argv[i], "-path") == 0) {
            ++i;
            //bugfixed by YTS Team, yamsat(id-59703)
            if ((strlen(argv[i]) > 600)) {
              fprintf(stderr, "\n   Path exceeded the maximum allowed characters.\n");
              exit(1);
            }

            mgmt_path = argv[i];
            /*
               } else if(strcmp(argv[i], "-lmConf") == 0) {
               ++i;
               lm_conf = argv[i];
             */
          } else if (strcmp(argv[i], "-recordsConf") == 0) {
            ++i;
            recs_conf = argv[i];
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
          } else if (strcmp(argv[i], "-tsArgs") == 0) {
            int size_of_args = 0, j = (++i);
            while (j < argc) {
              size_of_args += 1;
              size_of_args += strlen((argv[j++]));
            }
            ink_assert((tsArgs = (char *) xmalloc(size_of_args + 1)));

            j = 0;
            while (i < argc) {
              ink_snprintf(&tsArgs[j], ((size_of_args + 1) - j), " %s", argv[i]);
              j += strlen(argv[i]) + 1;
              ++i;
            }
          } else if (strcmp(argv[i], "-proxyPort") == 0) {
            ++i;
            proxy_port = atoi(argv[i]);
          } else if (strcmp(argv[i], "-proxyBackDoor") == 0) {
            ++i;
            proxy_backdoor = atoi(argv[i]);
          } else if (strcmp(argv[i], "-vingid") == 0) {
            // smanager/cnp integration, this argument is
            // really just a dummy argument used so that
            // smanager can find all instances of a
            // particular TM process.
            ++i;
          } else if (strcmp(argv[i], "-schema") == 0) {
            // hidden option
            ++i;
            schema_path = argv[i];
            schema_on = true;
          } else if (strcmp(argv[i], "-xml") == 0) {    // hidden option
            ++i;
            xml_path = argv[i];
            xml_on = true;
          } else {
            printUsage();
          }
        } else {
          printUsage();
        }
      }
    }
  }


#ifdef MGMT_USE_SYSLOG
  // Bootstrap with LOG_DAEMON until we've read our configuration
  if (log_to_syslog) {
    openlog("traffic_manager", LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_DAEMON);
    mgmt_use_syslog();
    syslog(LOG_NOTICE, "NOTE: --- Manager Starting ---");
    syslog(LOG_NOTICE, "NOTE: Manager Version: %s", appVersionInfo.FullVersionInfoStr);
  }
#endif /* MGMT_USE_SYSLOG */

  // Bootstrap the Diags facility so that we can use it while starting
  //  up the manager
  diagsConfig = NEW(new DiagsConfig(debug_tags, action_tags, false));
  diags = diagsConfig->diags;
  diags->prefix_str = "Manager ";

/* Disable 12/14/98 (see explanation below)
    icmp_ping = new MgmtPing(); */
  icmp_ping = NULL;

  // Get the config info we need while we are still root
  extractConfigInfo(mgmt_path, recs_conf, userToRunAs);

  runAsUser(userToRunAs);

  setup_coredump();

  check_lockfile();

  ink_snprintf(config_internal_dir, sizeof(config_internal_dir), "%s%sinternal", mgmt_path, DIR_SEP);
  url_init(config_internal_dir);
  mime_init(config_internal_dir);
  http_init(config_internal_dir);

#if defined(MGMT_API)
  // initialize alarm queue
  int ret;

  ret = init_mgmt_alarm_q(mgmt_alarm_event_q);
  if (ret < 0)
    mgmt_alarm_event_q = NULL;

#endif

  RecLocalInit();
  LibRecordsConfigInit();
  lmgmt = new LocalManager(mgmt_path, new LMRecords(mgmt_path, recs_conf, 0), proxy_on);
  RecLocalInitMessage();
  lmgmt->initAlarm();

  if (diags) {
    delete diagsConfig;
    // diagsConfig->reconfigure_diags(); INKqa11968
    /*
       delete diags;
       diags = NEW (new Diags(debug_tags,action_tags));
     */
  }
  // INKqa11968: need to set up callbacks and diags data structures
  // using configuration in records.config
  diagsConfig = NEW(new DiagsConfig(debug_tags, action_tags, true));
  diags = diagsConfig->diags;
  RecSetDiags(diags);
  diags->prefix_str = "Manager ";

  if (is_debug_tag_set("diags"))
    diags->dump();
  diags->cleanup_func = mgmt_cleanup;
  diags_init = 1;

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

#ifdef MGMT_USE_SYSLOG
  if (log_to_syslog) {
    char sys_var[] = "proxy.config.syslog_facility";
    char *facility_str = NULL;
    int facility_int;
    facility_str = REC_readString(sys_var, &found);
    ink_assert(found);

    if (!found) {
      mgmt_elog("Could not read %s.  Defaulting to DAEMON\n", sys_var);
      facility_int = LOG_DAEMON;
    } else {
      facility_int = facility_string_to_int(facility_str);
      xfree(facility_str);
      if (facility_int < 0) {
        mgmt_elog("Bad syslog facility specified.  Defaulting to DAEMON\n");
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
#endif /* MGMT_USE_SYSLOG */

#ifdef USE_SNMP
  ink_thread snmpThrId;
  SNMPStateInit();
  snmp = new SNMP();
  if (snmpLogLevel > 0) {
    snmp->enableLogging(snmpLogLevel);
  }
#endif

    /****************************
     * Register Alarm Callbacks *
     ****************************/
  lmgmt->alarm_keeper->registerCallback(overviewAlarmCallback);

#ifdef USE_SNMP
  lmgmt->alarm_keeper->registerCallback(snmpAlarmCallback);
#endif


  // Find out our hostname so we can use it as part of the initialization
  setHostnameVar();

  // Create the data structure for overview page
  //   Do this before the rest of the set up since it needs
  //   to created to handle any alarms thrown by later
  //   initialization
  overviewGenerator = new overviewPage();

  // Initialize the Config Object bindings before
  //   starting any other threads
  configFiles = new FileManager();
  initializeRegistry();
  configFiles->registerCallback(fileUpdated);

  // RecLocal's 'sync_thr' depends on 'configFiles', so we can't
  // stat the 'sync_thr' until 'configFiles' has been initialized.
  RecLocalStart();

/* Disabled 12/14/98
 *   Goldmine is seeing ping failures for no apparent reason.
 *   My tests show that the ping packets go over the loop back interface
 *   and the driver forwards them to loop back even if the link status
 *   for the interface is down
    if(!icmp_ping->init()) {  // Initialize after the records data object has been created
	delete icmp_ping;
	icmp_ping = NULL;
 	lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR,
 					 "Unable to open raw icmp socket, Virtual IP failover at risk"
 					 " if multiple interfaces are present on this node.");
    }
    */

  /* Update cmd line overrides/environmental overrides/etc */
  if (tsArgs) {                 /* Passed command line args for proxy */
    if (lmgmt->proxy_options) {
      xfree(lmgmt->proxy_options);
    }
    lmgmt->proxy_options = tsArgs;
    mgmt_log(stderr, "[main] Traffic Server Args: '%s'\n", lmgmt->proxy_options);
  }
  // DI Footprint: Only allow the user to override the main proxy
  // server port (proxy_server_port[0]) if we're in standard
  // operation and not in the special DI Footprint listen_mode
  // (difp_listen_mode equals 1 or 2).
  if (lmgmt->difp_listen_mode == 0) {
    if (proxy_port != -1) {
      lmgmt->proxy_server_port[0] = proxy_port;
      mgmt_log(stderr, "[main] Traffic Server Port: '%d'\n", lmgmt->proxy_server_port[0]);
    }
  }

  if (proxy_backdoor != -1) {
    REC_setInteger("proxy.config.process_manager.mgmt_port", proxy_backdoor);
  }

  if (cluster_server_port == -1) {
    cluster_server_port = REC_readInteger("proxy.config.cluster.rsport", &found);
    ink_assert(found);
  }

  if (cluster_port == -1) {
    cluster_port = REC_readInteger("proxy.config.cluster.mcport", &found);
    ink_assert(found);
  }

  if (!group_addr) {
    group_addr = REC_readString("proxy.config.cluster.mc_group_addr", &found);
    ink_assert(found);
  }

  if (schema_on) {
    XMLDom schema;
    schema.LoadFile(schema_path);
    bool validate = validateRecordsConfig(&schema);
    NOWARN_UNUSED(validate);
    // Why is this assert disabled? /leif
    //ink_assert(validate);
  }


  in_addr_t min_ip = inet_network("224.0.0.255");
  in_addr_t max_ip = inet_network("239.255.255.255");
  in_addr_t group_addr_ip = inet_network(group_addr);

  if (!(min_ip < group_addr_ip && group_addr_ip < max_ip)) {
    mgmt_fatal("[TrafficManager] Multi-Cast group addr '%s' is not in the permitted range of %s\n",
               group_addr, "224.0.1.0 - 239.255.255.255");
  }

  lmgmt->initCCom(cluster_port, group_addr, cluster_server_port);       /* Setup cluster communication */

  lmgmt->initMgmtProcessServer();       /* Setup p-to-p process server */


  // Now that we know our cluster ip address, add the
  //   UI record for this machine
  overviewGenerator->addSelfRecord();

  webThrId = ink_thread_create(webIntr_main, NULL);     /* Spin web agent thread */
#ifdef USE_SNMP
  snmpThrId = ink_thread_create(snmpThread, (void *) snmp);     /* Spin snmp agent thread */
#endif


  lmgmt->listenForProxy();


#if defined(HAVE_LIBSSL) && defined(TRAFFIC_NET)
  // Traffic Net Thread
  ink_thread tnThrId;
  tnThrId = ink_thread_create(tn_mgmt_main, NULL);      /* Spin Traffic Net thread */
  mgmt_log("[TrafficManager] Traffic Net thread created\n");
#endif


  /* Check the permissions on vip_config */
  if (lmgmt->virt_map->enabled) {
    char absolute_vipconf_binary[1024];
    struct stat buf;

    ink_snprintf(absolute_vipconf_binary, sizeof(absolute_vipconf_binary), "%s/vip_config", lmgmt->bin_path);
    if (stat(absolute_vipconf_binary, &buf) < 0) {
      mgmt_elog(stderr, "[main] Unable to stat vip_config for proper permissions\n");
    } else if (!((buf.st_mode & S_ISUID) &&
                 (buf.st_mode & S_IRWXU) &&
                 (buf.st_mode & S_IRGRP) &&
                 (buf.st_mode & S_IXGRP) && (buf.st_mode & S_IROTH) && (buf.st_mode & S_IXOTH))) {
      lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR,
                                       "Virtual IP Addressing enabled, but improper permissions on '/inktomi/bin/vip_config'"
                                       "[requires: setuid root and at least a+rx]\n");
    }
  }

  ticker = time(NULL);
  mgmt_log("[TrafficManager] Setup complete\n");

#ifdef USE_STATPRO
  statProcessor = NEW(new StatProcessor());
#endif

  if (xml_on) {
    converterInit();
    TrafficServer_xml(xml_path);
  }
#if defined(OEM) && !defined(OEM_3COM)

  //For OEM releases we check here whether a floppy with the correct config file exists
  //if it does, we first configure the network, then we continue
  char floppyLockFile[256];
  ink_snprintf(floppyLockFile, sizeof(floppyLockFile), "./conf/yts/internal/floppy.dat");
  struct stat lockFileStat;
  FILE *floppy_lock_fd;
  bool floppyRestore = true;

  // Check for the presence of floppy.dat lock file in the conf/yts/internal directory
  // If the lock is found, check if it is more than 120 seconds old.
  // If the lock file is missing or if it is more than 120 seconds old, create a new lock file
  // and restore network settings.
  if (stat(floppyLockFile, &lockFileStat) < 0) {
    perror("floppy.lock: ");
    if ((floppy_lock_fd = fopen(floppyLockFile, "w+")) == NULL) {
      mgmt_log(stderr, "[main] Could not create floppy.lock. Not restoring floppy configurations\n");
      floppyRestore = false;
    } else
      fclose(floppy_lock_fd);
  } else {
    time_t now;
    floppyRestore = false;
    if ((time(&now) - lockFileStat.st_mtime) > 120) {
      unlink(floppyLockFile);
      floppyRestore = true;
      if ((floppy_lock_fd = fopen(floppyLockFile, "w+")) == NULL) {
        mgmt_log(stderr, "[main] Could not create floppy.lock. Not restoring floppy configurations\n");
        floppyRestore = false;
      } else {
        fclose(floppy_lock_fd);
      }
    }
  }

  if (floppyRestore) {
    int oem_status;
    int old_euid = getuid();
    if (seteuid(0))
      perror("Main: Config_FloppyRestore setuid failed: ");
    if (setreuid(0, 0))
      perror("Main: Config_FloppyRestore setreuid failed: ");
    oem_status = Config_FloppyNetRestore();
    setreuid(old_euid, old_euid);
    if (oem_status) {
      Debug("lm", "[Main] Failed to initialize network using floppy configuration file\n");
    }
    //try a to queue a request for a restart
    //lmgmt->mgmt_shutdown_outstanding = true; //This is marked out as we don't need to restart

  }
#endif //OEM - Floppy Configuration

  for (;;) {


    lmgmt->processEventQueue();
    lmgmt->pollMgmtProcessServer();

    // Check for a SIGHUP
    if (sigHupNotifier != 0) {
      if (xml_on)               // convert xml into TS config files
        TrafficServer_xml(xml_path);
      mgmt_log(stderr, "[main] Reading Configuration Files due to SIGHUP\n");
      configFiles->rereadConfig();
      lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, "*");
      sigHupNotifier = 0;
      mgmt_log(stderr, "[main] Reading Configuration Files Reread\n");
    }
    // Check for SIGUSR2
    if (sigUsr2Notifier != 0) {
      xdump();
      sigUsr2Notifier = 0;
    }
//      if(lmgmt->processRunning() && just_started >= 10) { /* Sync the config/process records */
//          lmgmt->record_data->syncRecords();
//      } else {
//          /* Continue to update the db for config, this also syncs the records.config */
//          lmgmt->record_data->syncRecords(false);
//      }

//      snmp->poll();
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
#ifndef USE_STATPRO
    // Aggregate node statistics
    overviewGenerator->doClusterAg();
#endif
    lmgmt->ccom->checkPeers(&ticker);
    overviewGenerator->checkForUpdates();
#ifndef USE_STATPRO
    // Aggregate cluster statistics
    aggregateNodeRecords();
#else
    if (statProcessor) {
      statProcessor->processStat();
    }
#endif

    if (lmgmt->mgmt_shutdown_outstanding == true) {
      lmgmt->mgmtShutdown(0, true);
    }

#ifdef USE_SNMP
    // deal with the SNMP agent daemon, fork/exec/etc.
    if (snmp) {
      snmp->processMgmt(lmgmt);
    }
#endif
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
#ifndef _WIN32
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
#else
      if (lmgmt->proxy_launch_hproc != INVALID_HANDLE_VALUE) {
        TerminateProcess(lmgmt->proxy_launch_hproc, 1);
      }
#endif /* !_WIN32 */
      mgmt_log(stderr, "[main] Proxy launch failed, retrying...\n");
    }

  }

#if defined(STATPRO)
  if (statProcessor) {
    delete(statProcessor);
  }
#endif

#ifndef MGMT_SERVICE
  return 0;
#endif

}                               /* End main */


#ifndef _WIN32
#if (HOST_OS != linux) && (HOST_OS != freebsd)
void
SignalAlrmHandler(int sig, siginfo_t * t, void *c)
#else
void
SignalAlrmHandler(int sig)
#endif
{
  /*
     fprintf(stderr,"[TrafficManager] ==> SIGALRM received\n");
     mgmt_elog(stderr,"[TrafficManager] ==> SIGALRM received\n");
   */
#if (HOST_OS != linux) && (HOST_OS != freebsd)
  if (t) {
    if (t->si_code <= 0) {
      fprintf(stderr, "[TrafficManager] ==> User Alarm from pid: %d uid: %d\n", t->si_pid, t->si_uid);
      mgmt_elog(stderr, "[TrafficManager] ==> User Alarm from pid: %d uid: %d\n", t->si_pid, t->si_uid);
    } else {
      fprintf(stderr, "[TrafficManager] ==> Kernel Alarm Reason: %d\n", t->si_code);
      mgmt_elog(stderr, "[TrafficManager] ==> Kernel Alarm Reason: %d\n", t->si_code);
    }
  }
#endif

  return;
}


#if (HOST_OS != linux) && (HOST_OS != freebsd)
void
SignalHandler(int sig, siginfo_t * t, void *c)
#else
void
SignalHandler(int sig)
#endif
{
  static int clean = 0;
  int status;

#if (HOST_OS != linux) && (HOST_OS != freebsd)
  if (t) {
    if (t->si_code <= 0) {
      fprintf(stderr, "[TrafficManager] ==> User Sig %d from pid: %d uid: %d\n", sig, t->si_pid, t->si_uid);
      mgmt_elog(stderr, "[TrafficManager] ==> User Sig %d from pid: %d uid: %d\n", sig, t->si_pid, t->si_uid);
    } else {
      fprintf(stderr, "[TrafficManager] ==> Kernel Sig %d; Reason: %d\n", sig, t->si_code);
      mgmt_elog(stderr, "[TrafficManager] ==> Kernel Sig %d; Reason: %d\n", sig, t->si_code);
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
  mgmt_elog(stderr, "[TrafficManager] ==> Cleaning up and reissuing signal #%d\n", sig);

  if (lmgmt && !clean) {
    clean = 1;
#ifdef __alpha
    lmgmt->clean_up = true;
#endif
    if (lmgmt->watched_process_pid != -1) {

      if (sig == SIGTERM || sig == SIGINT) {
        kill(lmgmt->watched_process_pid, sig);
        waitpid(lmgmt->watched_process_pid, &status, 0);
#if (HOST_OS == linux) && defined (USE_SNMP)
        // INKqa08918: band-aid: for some reasons if snmpd.cnf is modified
        // the snmp processes are killed and restarted successfully. But
        // when traffic server is shut down, the snmp processes are not killed.
        // NOTE: race condition with the shutdown() call in LocalManager.cc
        if (snmp) {
          snmp->shutdown();
          snmp = NULL;
        }
#endif
      }
    }
    lmgmt->mgmtCleanup();
  }
// INKqa08323 (linux/mgmt: tm process doesn't exit on SIGxxx)
#if (HOST_OS == linux) && defined(_exit)
#undef _exit                    // _exit redefined below to be ink__exit()
#endif

  switch (sig) {
  case SIGQUIT:
  case SIGILL:
  case SIGTRAP:
#if (HOST_OS != linux)
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
    mgmt_elog(stderr, "[TrafficManager] ==> signal #%d\n", sig);
    _exit(sig);
  }
  fprintf(stderr, "[TrafficManager] ==> signal2 #%d\n", sig);
  mgmt_elog(stderr, "[TrafficManager] ==> signal2 #%d\n", sig);
  _exit(sig);

#if (HOST_OS == linux)
#define _exit(val) ink__exit(val)
#endif

}                               /* End SignalHandler */


// void SigChldHandler(int sig)
//
//   An empty handler needed so that we catch SIGCHLD
//    With Solaris 2.6, ignoring sig child changes the behavior
//    of waitpid() so that if there are no unwaited children,
//    waitpid() blocks until all child are transformed into
//    zombies which is bad for us
//
void
SigChldHandler(int sig)
{
}

// void SigHupHandler(int sig,...)
//
//  Records that a sigHup was sent so that we can reread our
//    config files on the next run through the main loop
void
SigHupHandler(int sig, ...)
{
  ink_assert(sig == SIGHUP);
  Debug("lm", "[SigHupHandler] hup caught\n");
  sigHupNotifier = 1;
}                               /* End SigHupHandler */
#endif /* !_WIN32 */


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
  fprintf(stderr, "     -printRecords  [...]   Print flags, default all are off.\n");
  fprintf(stderr, "     -debug         <tags>  Enable the given debug tags\n");
  fprintf(stderr, "     -action        <tags>  Enable the given action tags.\n");
  fprintf(stderr, "     -version               Print version id and exit.\n");
  fprintf(stderr, "     -snmplog       <int>   Turn on SNMP SDK diagnostics. (2147450879 is good...)\n");
  fprintf(stderr, "     -vingid        <id>    Vingid Flag\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "   [...] can be one+ of: [config process node cluster local all]\n");
  fprintf(stderr, "----------------------------------------------------------------------------\n");
  exit(0);
}                               /* End printUsage */

void
fileUpdated(char *fname)
{

  if (strcmp(fname, "cluster.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cluster.cluster_configuration");

  } else if (strcmp(fname, "remap.config") == 0) {
    lmgmt->signalFileChange("proxy.config.url_remap.filename");

  } else if (strcmp(fname, "logs.config") == 0) {
    lmgmt->signalFileChange("proxy.config.log2.config_file");

  } else if (strcmp(fname, "socks.config") == 0) {
    lmgmt->signalFileChange("proxy.config.socks.socks_config_file");

  } else if (strcmp(fname, "records.config") == 0) {
    lmgmt->signalFileChange("records.config");

  } else if (strcmp(fname, "cache.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cache.control.filename");

  } else if (strcmp(fname, "filter.config") == 0) {
    lmgmt->convert_filters();
    lmgmt->signalFileChange("proxy.config.content_filter.filename");

  } else if (strcmp(fname, "parent.config") == 0) {
    lmgmt->signalFileChange("proxy.config.http.parent_proxy.file");

  } else if (strcmp(fname, "mgmt_allow.config") == 0) {
    lmgmt->signalFileChange("proxy.config.admin.ip_allow.filename");
    // signalFileChange does not cause callbacks in the manager
    //  so generate one here by hand
    markMgmtIpAllowChange();
  } else if (strcmp(fname, "ip_allow.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cache.ip_allow.filename");

  } else if (strcmp(fname, "lm.config") == 0) {

    /* Fix INKqa00919.  The lm.config file is for stats only so
     *  we should never need to re-read it and destroy stats
     *  the manager has stored there
     *
     mgmt_log(stderr, "[fileUpdated] lm.config updated\n");
     if(lmgmt->record_data->rereadRecordFile(lmgmt->config_path, lm_conf) < 0) {
     mgmt_elog(stderr, "[FileUpdated] Config update failed for lm.config\n");
     }
     */
  } else if (strcmp(fname, "vaddrs.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] vaddrs.config updated\n");
    lmgmt->virt_map->lt_readAListFile(fname);

  } else if (strcmp(fname, "storage.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] storage.config changed, need restart auto-rebuild mode\n");

  } else if (strcmp(fname, "proxy.pac") == 0) {
    mgmt_log(stderr, "[fileUpdated] proxy.pac file has been modified\n");

  } else if (strcmp(fname, "wpad.dat") == 0) {
    mgmt_log(stderr, "[fileUpdated] wpad.dat file has been modified\n");

  } else if (strcmp(fname, "snmpd.cnf") == 0) {
    lmgmt->signalFileChange("snmpd.cnf");

  } else if (strcmp(fname, "icp.config") == 0) {
    lmgmt->signalFileChange("proxy.config.icp.icp_configuration");

  } else if (strcmp(fname, "update.config") == 0) {
    lmgmt->signalFileChange("proxy.config.update.update_configuration");

  } else if (strcmp(fname, "admin_access.config") == 0) {
    lmgmt->signalFileChange("admin_access.config");

  } else if (strcmp(fname, "partition.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] partition.config changed, need restart\n");

  } else if (strcmp(fname, "hosting.config") == 0) {
    lmgmt->signalFileChange("proxy.config.cache.hosting_filename");

    // INKqa07930: recently added SNMP config files need to be caught here to
  } else if (strcmp(fname, "snmpinfo.dat") == 0) {
    mgmt_log(stderr, "[fileUpdated] snmpinfo.dat file has been modified\n");

  } else if (strcmp(fname, "mgr.cnf") == 0) {
    mgmt_log(stderr, "[fileUpdated] mgr.cnf file has been modified\n");

  } else if (strcmp(fname, "log_hosts.config") == 0) {
    lmgmt->signalFileChange("proxy.config.log2.hosts_config_file");

  } else if (strcmp(fname, "logs_xml.config") == 0) {
    lmgmt->signalFileChange("proxy.config.log2.xml_config_file");


// INKqa10113
  } else if (strcmp(fname, "ldapsrvr.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] ldapsrvr.config file has been modified\n");

  } else if (strcmp(fname, "splitdns.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] splitdns.config file has been modified\n");

  } else if (strcmp(fname, "ftp_remap.config") == 0) {
    lmgmt->signalFileChange("proxy.config.ftp.reverse_ftp_remap_file_name");

  } else if (strcmp(fname, "plugin.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] plugin.config file has been modified\n");

  } else if (strcmp(fname, "ssl_multicert.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] ssl_multicert.config file has been modified\n");

  } else if (strcmp(fname, "ipnat.conf") == 0) {
    mgmt_log(stderr, "[fileUpdated] ipnat.conf file has been modified\n");

// INKqa11910
  } else if (strcmp(fname, "proxy.config.body_factory.template_sets_dir") == 0) {
    lmgmt->signalFileChange("proxy.config.body_factory.template_sets_dir");

  } else if (strcmp(fname, "nntp_config.xml") == 0) {
    lmgmt->signalFileChange("proxy.config.nntp.config_file");
  } else if (strcmp(fname, "stats.config.xml") == 0) {
#if defined(STATPRO)
    if (statProcessor) {
      statProcessor->rereadConfig();
    }
#endif
    mgmt_log(stderr, "[fileUpdated] stats.config.xml file has been modified\n");
  } else if (strcmp(fname, "congestion.config") == 0) {
    lmgmt->signalFileChange("proxy.config.http.congestion_control.filename");
#if defined(OEM)
  } else if (strcmp(fname, "net.config.xml") == 0) {
    mgmt_log(stderr, "[fileUpdated] net.config.xml file has been modified\n");
  } else if (strcmp(fname, "plugins/vscan.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] plugins/vscan.config file has been modified\n");
  } else if (strcmp(fname, "plugins/trusted-host.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] plugins/trusted-host.config file has been modified\n");
  } else if (strcmp(fname, "plugins/extensions.config") == 0) {
    mgmt_log(stderr, "[fileUpdated] plugins/extensions.config file has been modified\n");
#endif
  } else {
    mgmt_elog(stderr, "[fileUpdated] Unknown config file updated '%s'\n", fname);

  }
  return;
}                               /* End fileUpdate */


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
      mgmt_elog(stderr, "[runAsUser] Fatal Error: proxy.config.admin.user_id is not set\n", userName, strerror(errno));
      _exit(1);
    }
// this is behaving weird.  refer to getpwnam(3C) sparc -jcoates
// this looks like the POSIX getpwnam_r

    struct passwd passwdInfo;
    struct passwd *ppasswd = NULL;
    result = NULL;
    int res = getpwnam_r(&userName[0], &passwdInfo, buf, bufSize, &ppasswd);

#if 0
// non-POSIX
    struct passwd *ppasswd = NULL;
    result = NULL;
    int res = (int) getpwnam_r(&userName[0], ppasswd, buf, bufSize);
#endif
    if (!res && ppasswd) {
      result = ppasswd;
    }

    if (result == NULL) {
      mgmt_elog(stderr, "[runAsUser] Fatal Error: Unable to get info about user %s : %s\n", userName, strerror(errno));
      _exit(1);
    }

    if (setegid(result->pw_gid) != 0 || seteuid(result->pw_uid) != 0) {
      mgmt_elog(stderr, "[runAsUser] Fatal Error: Unable to switch to user %s : %s\n", userName, strerror(errno));
      _exit(1);
    }

    uid = getuid();
    euid = geteuid();


    Debug("lm", "[runAsUser] Running with uid: '%d' euid: '%d'\n", uid, euid);

    if (uid != result->pw_uid && euid != result->pw_uid) {
      mgmt_elog(stderr, "[runAsUser] Fatal Error: Failed to switch to user %s\n", userName);
      _exit(1);
    }
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
extractConfigInfo(char *mgmt_path, char *recs_conf, char *userName)
{
  char file[1024];
  bool useridFound = false;

  /* Figure out what user we should run as */
  if (mgmt_path && recs_conf) {
    FILE *fin;

    ink_snprintf(file, sizeof(file), "%s%s.shadow", mgmt_path, recs_conf);
    if (!(fin = fopen(file, "r"))) {

      ink_snprintf(file, sizeof(file), "%s%s", mgmt_path, recs_conf);
      if (!(fin = fopen(file, "r"))) {
        mgmt_elog(stderr, "[extractConfigInfo] Unable to open config file(%s)\n", file);
        _exit(1);
      }
    }

    while ((!useridFound) && fgets(file, 1024, fin)) {

      if (strstr(file, "CONFIG proxy.config.admin.user_id STRING")) {
        //coverity[secure_coding]
        if ((sscanf(file, "CONFIG proxy.config.admin.user_id STRING %1023s\n", userName) == 1) &&
            strcmp(userName, "NULL") != 0) {
          useridFound = true;
        }
      }

    }

    fclose(fin);
  } else {
    mgmt_elog(stderr, "[extractConfigInfo] Fatal Error: unable to access records file\n");
    _exit(1);
  }

  if (useridFound == false) {
    mgmt_elog(stderr, "[extractConfigInfo] Fatal Error: proxy.config.admin.user_id is not set\n");
    _exit(1);
  }

}                               /* End extractConfigInfo() */
