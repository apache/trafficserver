/** @file

  The Local Manager process of the management system.

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

#include "ts/ink_platform.h"
#include "ts/ink_sock.h"
#include "ts/ink_file.h"
#include "MgmtUtils.h"
#include "ts/I_Layout.h"
#include "LocalManager.h"
#include "MgmtSocket.h"
#include "ts/ink_cap.h"
#include "FileManager.h"
#include "ClusterCom.h"
#include "VMap.h"

#if TS_USE_POSIX_CAP
#include <sys/capability.h>
#endif

void
LocalManager::mgmtCleanup()
{
  close_socket(process_server_sockfd);

  // fix me for librecords

  if (virt_map) {
    virt_map->rl_downAddrs(); // We are bailing done need to worry about table
  }
  closelog();
  return;
}

void
LocalManager::mgmtShutdown()
{
  mgmt_log("[LocalManager::mgmtShutdown] Executing shutdown request.\n");
  processShutdown(true);
  // WCCP TBD: Send a shutdown message to routers.

  if (processRunning()) {
    waitpid(watched_process_pid, NULL, 0);
#if defined(linux)
    /* Avert race condition, wait for the thread to complete,
       before getting one more restart process */
    /* Workaround for bugid INKqa10060 */
    mgmt_sleep_msec(1);
#endif
  }
  mgmtCleanup();
}

void
LocalManager::processShutdown(bool mainThread)
{
  mgmt_log("[LocalManager::processShutdown] Executing process shutdown request.\n");
  if (mainThread) {
    sendMgmtMsgToProcesses(MGMT_EVENT_SHUTDOWN, "processShutdown[main]");
  } else {
    signalEvent(MGMT_EVENT_SHUTDOWN, "processShutdown");
  }
  return;
}

void
LocalManager::processRestart()
{
  mgmt_log("[LocalManager::processRestart] Executing process restart request.\n");
  signalEvent(MGMT_EVENT_RESTART, "processRestart");
  return;
}

void
LocalManager::processBounce()
{
  mgmt_log("[LocalManager::processBounce] Executing process bounce request.\n");
  signalEvent(MGMT_EVENT_BOUNCE, "processBounce");
  return;
}

void
LocalManager::rollLogFiles()
{
  mgmt_log("[LocalManager::rollLogFiles] Log files are being rolled.\n");
  signalEvent(MGMT_EVENT_ROLL_LOG_FILES, "rollLogs");
  return;
}

void
LocalManager::clearStats(const char *name)
{
  // Clear our records and then send the signal.  There is a race condition
  //  here where our stats could get re-updated from the proxy
  //  before the proxy clears them, but this should be rare.
  //
  //  Doing things in the opposite order prevents that race
  //   but excerbates the race between the node and cluster
  //   stats getting cleared by progation of clearing the
  //   cluster stats
  //
  if (name) {
    RecResetStatRecord(name);
  } else {
    RecResetStatRecord(RECT_NULL, true);
  }

  // If the proxy is not running, sending the signal does
  //   not do anything.  Remove the stats file to make sure
  //   that operation works even when the proxy is off
  //
  if (this->proxy_running == 0) {
    ats_scoped_str statsPath(RecConfigReadPersistentStatsPath());
    if (unlink(statsPath) < 0) {
      if (errno != ENOENT) {
        mgmt_log(stderr, "[LocalManager::clearStats] Unlink of %s failed : %s\n", (const char *)statsPath, strerror(errno));
      }
    }
  }
}

// bool LocalManager::clusterOk()
//
//   Returns false if the proxy has been up for more than
//     30 seconds but is not reporting that it has clustered
//     with all the nodes in cluster.config
//
//   Otherwise returns true
//
bool
LocalManager::clusterOk()
{
  bool found  = true;
  bool result = true;

  if (processRunning() == true && time(NULL) > (this->proxy_started_at + 30) &&
      this->ccom->alive_peers_count + 1 != REC_readInteger("proxy.process.cluster.nodes", &found)) {
    result = false;
  }

  ink_assert(found);
  return result;
}

bool
LocalManager::processRunning()
{
  if (watched_process_fd != ts::NO_FD && watched_process_pid != -1) {
    return true;
  } else {
    return false;
  }
}

LocalManager::LocalManager(bool proxy_on) : BaseManager(), run_proxy(proxy_on), configFiles(NULL)
{
  bool found;
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  ats_scoped_str bindir(RecConfigReadBinDir());
  ats_scoped_str sysconfdir(RecConfigReadConfigDir());

  syslog_facility = 0;

  ccom                      = NULL;
  proxy_started_at          = -1;
  proxy_launch_count        = 0;
  manager_started_at        = time(NULL);
  proxy_launch_outstanding  = false;
  mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
  proxy_running             = 0;
  RecSetRecordInt("proxy.node.proxy_running", 0, REC_SOURCE_DEFAULT);

  virt_map = NULL;

  RecInt http_enabled = REC_readInteger("proxy.config.http.enabled", &found);
  ink_assert(found);
  if (http_enabled && found) {
    HttpProxyPort::loadConfig(m_proxy_ports);
  }
  HttpProxyPort::loadDefaultIfEmpty(m_proxy_ports);

  // Get the default IP binding values.
  RecHttpLoadIp("proxy.local.incoming_ip_to_bind", m_inbound_ip4, m_inbound_ip6);

  if (access(sysconfdir, R_OK) == -1) {
    mgmt_elog(0, "[LocalManager::LocalManager] unable to access() directory '%s': %d, %s\n", (const char *)sysconfdir, errno,
              strerror(errno));
    mgmt_fatal(0, "[LocalManager::LocalManager] please set the 'TS_ROOT' environment variable\n");
  }

#if TS_HAS_WCCP
  // Bind the WCCP address if present.
  ats_scoped_str wccp_addr_str(REC_readString("proxy.config.wccp.addr", &found));
  if (found && wccp_addr_str && *wccp_addr_str) {
    wccp_cache.setAddr(inet_addr(wccp_addr_str));
    mgmt_log("[LocalManager::LocalManager] WCCP identifying address set to %s.\n", static_cast<char *>(wccp_addr_str));
  }

  ats_scoped_str wccp_config_str(RecConfigReadConfigPath("proxy.config.wccp.services"));
  if (wccp_config_str && strlen(wccp_config_str) > 0) {
    bool located = true;
    if (access(wccp_config_str, R_OK) == -1) {
      located = false;
    }

    if (located) {
      wccp_cache.loadServicesFromFile(wccp_config_str);
    } else { // not located
      mgmt_log("[LocalManager::LocalManager] WCCP service configuration file '%s' was specified but could not be found in the file "
               "system.\n",
               static_cast<char *>(wccp_config_str));
    }
  }
#endif

  process_server_timeout_secs  = REC_readInteger("proxy.config.lm.pserver_timeout_secs", &found);
  process_server_timeout_msecs = REC_readInteger("proxy.config.lm.pserver_timeout_msecs", &found);
  proxy_name                   = REC_readString("proxy.config.proxy_name", &found);
  proxy_binary                 = REC_readString("proxy.config.proxy_binary", &found);
  proxy_options                = REC_readString("proxy.config.proxy_binary_opts", &found);
  env_prep                     = REC_readString("proxy.config.env_prep", &found);

  // Calculate proxy_binary from the absolute bin_path
  absolute_proxy_binary = Layout::relative_to(bindir, proxy_binary);

  // coverity[fs_check_call]
  if (access(absolute_proxy_binary, R_OK | X_OK) == -1) {
    mgmt_elog(0, "[LocalManager::LocalManager] Unable to access() '%s': %d, %s\n", absolute_proxy_binary, errno, strerror(errno));
    mgmt_fatal(0, "[LocalManager::LocalManager] please set bin path 'proxy.config.bin_path' \n");
  }

  watched_process_pid = -1;

  process_server_sockfd = -1;
  watched_process_fd    = -1;
  proxy_launch_pid      = -1;

  return;
}

LocalManager::~LocalManager()
{
  delete alarm_keeper;
  delete virt_map;
  delete ccom;
  ats_free(absolute_proxy_binary);
  ats_free(proxy_name);
  ats_free(proxy_binary);
  ats_free(proxy_options);
  ats_free(env_prep);
}

void
LocalManager::initAlarm()
{
  alarm_keeper = new Alarms();
}

/*
 * initCCom(...)
 *   Function initializes cluster communication structure held by local manager.
 */
void
LocalManager::initCCom(const AppVersionInfo &version, FileManager *configFiles, int mcport, char *addr, int rsport)
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  bool found;
  IpEndpoint cluster_ip;         // ip addr of the cluster interface
  ip_text_buffer clusterAddrStr; // cluster ip addr as a String
  char *intrName;                // Name of the interface we are to use
  char hostname[1024];           // hostname of this machine
  const char envVar[] = "PROXY_CLUSTER_ADDR=";
  char *envBuf;

  if (gethostname(hostname, 1024) < 0) {
    mgmt_fatal(stderr, errno, "[LocalManager::initCCom] gethostname failed\n");
  }
  // Fetch which interface we are using for clustering
  intrName = REC_readString("proxy.config.cluster.ethernet_interface", &found);
  ink_assert(intrName != NULL);

  found = mgmt_getAddrForIntr(intrName, &cluster_ip.sa);
  if (found == false) {
    mgmt_fatal(stderr, 0, "[LocalManager::initCCom] Unable to find network interface %s.  Exiting...\n", intrName);
  } else if (!ats_is_ip4(&cluster_ip)) {
    mgmt_fatal(stderr, 0, "[LocalManager::initCCom] Unable to find IPv4 network interface %s.  Exiting...\n", intrName);
  }

  ats_ip_ntop(&cluster_ip, clusterAddrStr, sizeof(clusterAddrStr));
  Debug("ccom", "Cluster Interconnect is %s : %s\n", intrName, clusterAddrStr);

  // This an awful hack but I could not come up with a better way to
  //  pass the cluster address to the proxy
  //    Set an environment variable so the proxy can find out
  //    what the cluster address is.  The reason we need this awful
  //    hack is that the proxy needs this info immediately at startup
  //    and it is different for every machine in the cluster so using
  //    a config variable will not work.
  // The other options are to pass it on the command line to the proxy
  //    which would require a fair bit of code modification since
  //    what is passed right now is assumed to be static.  The other
  //    is to write it to a separate file but that seems like a
  //    lot of trouble for a 16 character string
  // Set the cluster ip addr variable so that proxy can read it
  //    and flush it to disk
  const size_t envBuf_size = strlen(envVar) + strlen(clusterAddrStr) + 1;
  envBuf                   = (char *)ats_malloc(envBuf_size);
  ink_strlcpy(envBuf, envVar, envBuf_size);
  ink_strlcat(envBuf, clusterAddrStr, envBuf_size);
  ink_release_assert(putenv(envBuf) == 0);

  ccom     = new ClusterCom(ats_ip4_addr_cast(&cluster_ip), hostname, mcport, addr, rsport, rundir);
  virt_map = new VMap(intrName, ats_ip4_addr_cast(&cluster_ip), &lmgmt->ccom->mutex);

  ccom->appVersionInfo = version;
  ccom->configFiles    = configFiles;

  virt_map->appVersionInfo = version;

  virt_map->downAddrs(); // Just to be safe
  ccom->establishChannels();
  ats_free(intrName);

  return;
}

/*
 * initMgmtProcessServer()
 *   sets up the server socket that proxy processes connect to.
 */
void
LocalManager::initMgmtProcessServer()
{
  ats_scoped_str rundir(RecConfigReadRuntimeDir());
  ats_scoped_str sockpath(Layout::relative_to(rundir, LM_CONNECTION_SERVER));
  mode_t oldmask = umask(0);

#if TS_HAS_WCCP
  if (wccp_cache.isConfigured()) {
    if (0 > wccp_cache.open())
      mgmt_log("Failed to open WCCP socket\n");
  }
#endif

  process_server_sockfd = bind_unix_domain_socket(sockpath, 00700);
  if (process_server_sockfd == -1) {
    mgmt_fatal(stderr, errno, "[LocalManager::initMgmtProcessServer] failed to bind socket at %s\n", (const char *)sockpath);
  }

  umask(oldmask);
  RecSetRecordInt("proxy.node.restarts.manager.start_time", manager_started_at, REC_SOURCE_DEFAULT);
}

/*
 * pollMgmtProcessServer()
 * -  Function checks the mgmt process server for new processes
 *    and any requests sent from processes. It handles processes sent.
 */
void
LocalManager::pollMgmtProcessServer()
{
  int num;
  struct timeval timeout;
  fd_set fdlist;
#if TS_HAS_WCCP
  int wccp_fd = wccp_cache.getSocket();
#endif

  while (1) {
    // poll only
    timeout.tv_sec  = process_server_timeout_secs;
    timeout.tv_usec = process_server_timeout_msecs * 1000;
    FD_ZERO(&fdlist);
    FD_SET(process_server_sockfd, &fdlist);
    if (watched_process_fd != ts::NO_FD)
      FD_SET(watched_process_fd, &fdlist);

#if TS_HAS_WCCP
    // Only run WCCP housekeeping while we have a server process.
    // Note: The WCCP socket is opened iff WCCP is configured.
    if (wccp_fd != ts::NO_FD && watched_process_fd != ts::NO_FD) {
      wccp_cache.housekeeping();
      time_t wccp_wait = wccp_cache.waitTime();
      if (wccp_wait < process_server_timeout_secs)
        timeout.tv_sec = wccp_wait;
      FD_SET(wccp_cache.getSocket(), &fdlist);
    }
#endif

    num = mgmt_select(FD_SETSIZE, &fdlist, NULL, NULL, &timeout);
    if (num == 0) { /* Have nothing */
      break;
    } else if (num > 0) { /* Have something */
#if TS_HAS_WCCP
      if (wccp_fd != ts::NO_FD && FD_ISSET(wccp_fd, &fdlist)) {
        wccp_cache.handleMessage();
        --num;
      }
#endif
      if (FD_ISSET(process_server_sockfd, &fdlist)) { /* New connection */
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int new_sockfd      = mgmt_accept(process_server_sockfd, (struct sockaddr *)&clientAddr, &clientLen);

        mgmt_log(stderr, "[LocalManager::pollMgmtProcessServer] New process connecting fd '%d'\n", new_sockfd);

        if (new_sockfd < 0) {
          mgmt_elog(stderr, errno, "[LocalManager::pollMgmtProcessServer] ==> ");
        } else if (!processRunning()) {
          watched_process_fd = new_sockfd;
        } else {
          close_socket(new_sockfd);
        }
        --num;
      }

      if (ts::NO_FD != watched_process_fd && FD_ISSET(watched_process_fd, &fdlist)) {
        int res;
        MgmtMessageHdr mh_hdr;
        MgmtMessageHdr *mh_full;
        char *data_raw;

        // read the message
        if ((res = mgmt_read_pipe(watched_process_fd, (char *)&mh_hdr, sizeof(MgmtMessageHdr))) > 0) {
          mh_full = (MgmtMessageHdr *)alloca(sizeof(MgmtMessageHdr) + mh_hdr.data_len);
          memcpy(mh_full, &mh_hdr, sizeof(MgmtMessageHdr));
          data_raw = (char *)mh_full + sizeof(MgmtMessageHdr);
          if ((res = mgmt_read_pipe(watched_process_fd, data_raw, mh_hdr.data_len)) > 0) {
            handleMgmtMsgFromProcesses(mh_full);
          } else if (res < 0) {
            mgmt_fatal(0, "[LocalManager::pollMgmtProcessServer] Error in read (errno: %d)\n", -res);
          }
        } else if (res < 0) {
          mgmt_fatal(0, "[LocalManager::pollMgmtProcessServer] Error in read (errno: %d)\n", -res);
        }
        // handle EOF
        if (res == 0) {
          int estatus;
          pid_t tmp_pid = watched_process_pid;

          Debug("lm", "[LocalManager::pollMgmtProcessServer] Lost process EOF!\n");

          close_socket(watched_process_fd);

          waitpid(watched_process_pid, &estatus, 0); /* Reap child */
          if (WIFSIGNALED(estatus)) {
            int sig = WTERMSIG(estatus);
            mgmt_elog(stderr, 0, "[LocalManager::pollMgmtProcessServer] "
                                 "Server Process terminated due to Sig %d: %s\n",
                      sig, strsignal(sig));
          }

          if (lmgmt->run_proxy) {
            mgmt_elog(0, "[Alarms::signalAlarm] Server Process was reset\n");
            lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PROCESS_DIED, NULL);
          } else {
            mgmt_log("[TrafficManager] Server process shutdown\n");
          }

          watched_process_fd = watched_process_pid = -1;
          if (tmp_pid != -1) { /* Incremented after a pid: message is sent */
            proxy_running--;
          }
          proxy_started_at = -1;
          RecSetRecordInt("proxy.node.proxy_running", 0, REC_SOURCE_DEFAULT);
        }

        num--;
      }
      ink_assert(num == 0); /* Invariant */

    } else if (num < 0) { /* Error */
      mgmt_elog(stderr, 0, "[LocalManager::pollMgmtProcessServer] select failed or was interrupted (%d)\n", errno);
    }
  }
}

void
LocalManager::handleMgmtMsgFromProcesses(MgmtMessageHdr *mh)
{
  char *data_raw = (char *)mh + sizeof(MgmtMessageHdr);
  switch (mh->msg_id) {
  case MGMT_SIGNAL_PID:
    watched_process_pid = *((pid_t *)data_raw);
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PROCESS_BORN, NULL);
    proxy_running++;
    proxy_launch_pid         = -1;
    proxy_launch_outstanding = false;
    RecSetRecordInt("proxy.node.proxy_running", 1, REC_SOURCE_DEFAULT);
    break;

  case MGMT_SIGNAL_MACHINE_UP:
    /*
       {
       struct in_addr addr;
       addr.s_addr = *((unsigned int*)data_raw);
       alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PEER_BORN, inet_ntoa(addr));
       }
     */
    break;
  case MGMT_SIGNAL_MACHINE_DOWN:
    /*
       {
       struct in_addr addr;
       addr.s_addr = *((unsigned int*)data_raw);
       alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PEER_DIED, inet_ntoa(addr));
       }
     */
    break;

  // FIX: This is very messy need to correlate mgmt signals and
  // alarms better
  case MGMT_SIGNAL_CONFIG_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_CONFIG_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_SYSTEM_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_LOG_SPACE_CRISIS:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_LOG_SPACE_CRISIS, data_raw);
    break;
  case MGMT_SIGNAL_CACHE_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_CACHE_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_CACHE_WARNING:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_CACHE_WARNING, data_raw);
    break;
  case MGMT_SIGNAL_LOGGING_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_LOGGING_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_LOGGING_WARNING:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_LOGGING_WARNING, data_raw);
    break;
  case MGMT_SIGNAL_CONFIG_FILE_READ:
    mgmt_log(stderr, "[LocalManager::handleMgmtMsgFromProcesses] File done '%d'\n", data_raw);
    break;
  case MGMT_SIGNAL_PLUGIN_SET_CONFIG: {
    char var_name[256];
    char var_value[256];
    MgmtType stype;
    // stype is an enum type, so cast to an int* to avoid warnings. /leif
    int tokens = sscanf(data_raw, "%255s %d %255s", var_name, (int *)&stype, var_value);
    if (tokens != 3) {
      stype = MGMT_INVALID;
    }
    switch (stype) {
    case MGMT_INT:
      RecSetRecordInt(var_name, ink_atoi64(var_value), REC_SOURCE_EXPLICIT);
      break;
    case MGMT_COUNTER:
    case MGMT_FLOAT:
    case MGMT_STRING:
    case MGMT_INVALID:
    default:
      mgmt_elog(stderr, 0, "[LocalManager::handleMgmtMsgFromProcesses] "
                           "Invalid plugin set-config msg '%s'\n",
                data_raw);
      break;
    }
  } break;
  case MGMT_SIGNAL_LOG_FILES_ROLLED: {
    Debug("lm", "Rolling logs %s", (char *)data_raw);
    break;
  }
  case MGMT_SIGNAL_LIBRECORDS:
    if (mh->data_len > 0) {
      executeMgmtCallback(MGMT_SIGNAL_LIBRECORDS, data_raw, mh->data_len);
    } else {
      executeMgmtCallback(MGMT_SIGNAL_LIBRECORDS, NULL, 0);
    }
    break;
  // Congestion Control - begin
  case MGMT_SIGNAL_HTTP_CONGESTED_SERVER:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_HTTP_CONGESTED_SERVER, data_raw);
    break;
  case MGMT_SIGNAL_HTTP_ALLEVIATED_SERVER:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_HTTP_ALLEVIATED_SERVER, data_raw);
    break;
  // Congestion Control - end
  case MGMT_SIGNAL_CONFIG_FILE_CHILD: {
    static const MgmtMarshallType fields[] = {MGMT_MARSHALL_STRING, MGMT_MARSHALL_STRING, MGMT_MARSHALL_INT};
    char *parent                           = NULL;
    char *child                            = NULL;
    MgmtMarshallInt options                = 0;
    if (mgmt_message_parse(data_raw, mh->data_len, fields, countof(fields), &parent, &child, &options) != -1) {
      configFiles->configFileChild(parent, child, (unsigned int)options);
    } else {
      mgmt_elog(stderr, 0, "[LocalManager::handleMgmtMsgFromProcesses] "
                           "MGMT_SIGNAL_CONFIG_FILE_CHILD mgmt_message_parse error\n");
    }
    // Output pointers are guaranteed to be NULL or valid.
    ats_free_null(parent);
    ats_free_null(child);
  } break;
  case MGMT_SIGNAL_SAC_SERVER_DOWN:
    alarm_keeper->signalAlarm(MGMT_ALARM_SAC_SERVER_DOWN, data_raw);
    break;

  default:
    break;
  }
}

void
LocalManager::sendMgmtMsgToProcesses(int msg_id, const char *data_str)
{
  sendMgmtMsgToProcesses(msg_id, data_str, strlen(data_str) + 1);
  return;
}

void
LocalManager::sendMgmtMsgToProcesses(int msg_id, const char *data_raw, int data_len)
{
  MgmtMessageHdr *mh;

  mh           = (MgmtMessageHdr *)alloca(sizeof(MgmtMessageHdr) + data_len);
  mh->msg_id   = msg_id;
  mh->data_len = data_len;
  memcpy((char *)mh + sizeof(MgmtMessageHdr), data_raw, data_len);
  sendMgmtMsgToProcesses(mh);
  return;
}

void
LocalManager::sendMgmtMsgToProcesses(MgmtMessageHdr *mh)
{
  switch (mh->msg_id) {
  case MGMT_EVENT_SHUTDOWN: {
    run_proxy = false;
    if (lmgmt->virt_map) {
      lmgmt->virt_map->downAddrs(); /* Down all known addrs to be safe */
    }
    this->closeProxyPorts();
    break;
  }
  case MGMT_EVENT_RESTART:
    run_proxy = true;
    listenForProxy();
    return;
  case MGMT_EVENT_BOUNCE: /* Just bouncing the cluster, have it exit well restart */
    mh->msg_id = MGMT_EVENT_SHUTDOWN;
    break;
  case MGMT_EVENT_ROLL_LOG_FILES:
    mgmt_log("[LocalManager::SendMgmtMsgsToProcesses]Event is being constructed .\n");
    break;
  case MGMT_EVENT_CONFIG_FILE_UPDATE:
  case MGMT_EVENT_CONFIG_FILE_UPDATE_NO_INC_VERSION:
    bool found;
    char *fname;
    Rollback *rb;
    char *data_raw;

    data_raw = (char *)mh + sizeof(MgmtMessageHdr);
    fname    = REC_readString(data_raw, &found);

    RecT rec_type;
    if (RecGetRecordType(data_raw, &rec_type) == REC_ERR_OKAY && rec_type == RECT_CONFIG) {
      RecSetSyncRequired(data_raw);
    } else {
      mgmt_elog(stderr, 0, "[LocalManager:sendMgmtMsgToProcesses] Unknown file change: '%s'\n", data_raw);
    }
    ink_assert(found);
    if (!(configFiles && configFiles->getRollbackObj(fname, &rb)) &&
        (strcmp(data_raw, "proxy.config.cluster.cluster_configuration") != 0) &&
        (strcmp(data_raw, "proxy.config.body_factory.template_sets_dir") != 0)) {
      mgmt_fatal(stderr, 0, "[LocalManager::sendMgmtMsgToProcesses] "
                            "Invalid 'data_raw' for MGMT_EVENT_CONFIG_FILE_UPDATE\n");
    }
    ats_free(fname);
    break;
  }

  if (watched_process_fd != -1) {
    if (mgmt_write_pipe(watched_process_fd, (char *)mh, sizeof(MgmtMessageHdr) + mh->data_len) <= 0) {
      // In case of Linux, sometimes when the TS dies, the connection between TS and TM
      // is not closed properly. the socket does not receive an EOF. So, the TM does
      // not detect that the connection and hence TS has gone down. Hence it still
      // tries to send a message to TS, but encounters an error and enters here
      // Also, ensure that this whole thing is done only once because there will be a
      // deluge of message in the traffic.log otherwise

      static pid_t check_prev_pid    = watched_process_pid;
      static pid_t check_current_pid = watched_process_pid;
      if (check_prev_pid != watched_process_pid) {
        check_prev_pid    = watched_process_pid;
        check_current_pid = watched_process_pid;
      }

      if (check_prev_pid == check_current_pid) {
        check_current_pid = -1;
        int lerrno        = errno;
        mgmt_elog(stderr, errno, "[LocalManager::sendMgmtMsgToProcesses] Error writing message\n");
        if (lerrno == ECONNRESET || lerrno == EPIPE) { // Connection closed by peer or Broken pipe
          if ((kill(watched_process_pid, 0) < 0) && (errno == ESRCH)) {
            // TS is down
            pid_t tmp_pid = watched_process_pid;
            close_socket(watched_process_fd);
            mgmt_elog(stderr, 0, "[LocalManager::pollMgmtProcessServer] "
                                 "Server Process has been terminated\n");
            if (lmgmt->run_proxy) {
              mgmt_elog(0, "[Alarms::signalAlarm] Server Process was reset\n");
              lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PROCESS_DIED, NULL);
            } else {
              mgmt_log("[TrafficManager] Server process shutdown\n");
            }
            watched_process_fd = watched_process_pid = -1;
            if (tmp_pid != -1) { /* Incremented after a pid: message is sent */
              proxy_running--;
            }
            proxy_started_at = -1;
            RecSetRecordInt("proxy.node.proxy_running", 0, REC_SOURCE_DEFAULT);
            // End of TS down
          } else {
            // TS is still up, but the connection is lost
            const char *err_msg = "The TS-TM connection is broken for some reason. Either restart TS and TM or correct this error "
                                  "for TM to display TS statistics correctly";
            lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR, err_msg);
          }

          // check if the TS is down, by checking the pid
          // if TS is down, then,
          //     raise an alarm
          //     set the variables so that TS is restarted later
          // else (TS is still up)
          //     raise an alarm stating the problem
        }
      }
    }
  }
}

void
LocalManager::signalFileChange(const char *var_name, bool incVersion)
{
  if (incVersion) {
    signalEvent(MGMT_EVENT_CONFIG_FILE_UPDATE, var_name);
  } else {
    signalEvent(MGMT_EVENT_CONFIG_FILE_UPDATE_NO_INC_VERSION, var_name);
  }
  return;
}

void
LocalManager::signalEvent(int msg_id, const char *data_str)
{
  signalEvent(msg_id, data_str, strlen(data_str) + 1);
  return;
}

void
LocalManager::signalEvent(int msg_id, const char *data_raw, int data_len)
{
  MgmtMessageHdr *mh;

  mh           = (MgmtMessageHdr *)ats_malloc(sizeof(MgmtMessageHdr) + data_len);
  mh->msg_id   = msg_id;
  mh->data_len = data_len;
  memcpy((char *)mh + sizeof(MgmtMessageHdr), data_raw, data_len);
  ink_assert(enqueue(mgmt_event_queue, mh));

  return;
}

/*
 * processEventQueue()
 *   Function drains and processes the mgmt event queue
 * notifying any registered callback functions and performing
 * any mgmt tasks for each event.
 */
void
LocalManager::processEventQueue()
{
  bool handled_by_mgmt;

  while (!queue_is_empty(mgmt_event_queue)) {
    handled_by_mgmt = false;

    MgmtMessageHdr *mh = (MgmtMessageHdr *)dequeue(mgmt_event_queue);
    char *data_raw     = (char *)mh + sizeof(MgmtMessageHdr);

    // check if we have a local file update
    if (mh->msg_id == MGMT_EVENT_CONFIG_FILE_UPDATE || mh->msg_id == MGMT_EVENT_CONFIG_FILE_UPDATE_NO_INC_VERSION) {
      // records.config
      if (!(strcmp(data_raw, REC_CONFIG_FILE))) {
        bool incVersion = mh->msg_id == MGMT_EVENT_CONFIG_FILE_UPDATE;
        if (RecReadConfigFile(incVersion) != REC_ERR_OKAY) {
          mgmt_elog(stderr, errno, "[fileUpdated] Config update failed for records.config\n");
        }
        handled_by_mgmt = true;
      }
    }

    if (!handled_by_mgmt) {
      if (processRunning() == false) {
        // Fix INKqa04984
        // If traffic server hasn't completely come up yet,
        // we will hold off until next round.
        ink_assert(enqueue(mgmt_event_queue, mh));
        return;
      }
      Debug("lm", "[TrafficManager] ==> Sending signal event '%d' payload=%d\n", mh->msg_id, mh->data_len);
      lmgmt->sendMgmtMsgToProcesses(mh);
    }
    ats_free(mh);
  }
}

/*
 * startProxy()
 *   Function fires up a proxy process.
 */
bool
LocalManager::startProxy()
{
  if (proxy_launch_outstanding) {
    return false;
  }
  mgmt_log(stderr, "[LocalManager::startProxy] Launching ts process\n");

  pid_t pid;

  // Before we do anything lets check for the existence of
  // the traffic server binary along with it's execute permmissions
  if (access(absolute_proxy_binary, F_OK) < 0) {
    // Error can't find traffic_server
    mgmt_elog(stderr, errno, "[LocalManager::startProxy] Unable to find traffic server at %s\n", absolute_proxy_binary);
    return false;
  }
  // traffic server binary exists, check permissions
  else if (access(absolute_proxy_binary, R_OK | X_OK) < 0) {
    // Error don't have proper permissions
    mgmt_elog(stderr, errno, "[LocalManager::startProxy] Unable to access %s due to bad permisssions \n", absolute_proxy_binary);
    return false;
  }

  if (env_prep) {
#ifdef POSIX_THREAD
    if ((pid = fork()) < 0)
#else
    if ((pid = fork1()) < 0)
#endif
    {
      mgmt_elog(stderr, errno, "[LocalManager::startProxy] Unable to fork1 prep process\n");
      return false;
    } else if (pid > 0) {
      int estatus;
      waitpid(pid, &estatus, 0);
    } else {
      int res;

      char env_prep_bin[MAXPATHLEN];
      ats_scoped_str bindir(RecConfigReadBinDir());

      ink_filepath_make(env_prep_bin, sizeof(env_prep_bin), bindir, env_prep);
      res = execl(env_prep_bin, env_prep_bin, (char *)NULL);
      _exit(res);
    }
  }
#ifdef POSIX_THREAD
  if ((pid = fork()) < 0)
#else
  if ((pid = fork1()) < 0)
#endif
  {
    mgmt_elog(stderr, errno, "[LocalManager::startProxy] Unable to fork1 process\n");
    return false;
  } else if (pid > 0) { /* Parent */
    proxy_launch_pid         = pid;
    proxy_launch_outstanding = true;
    proxy_started_at         = time(NULL);
    ++proxy_launch_count;
    RecSetRecordInt("proxy.node.restarts.proxy.start_time", proxy_started_at, REC_SOURCE_DEFAULT);
    RecSetRecordInt("proxy.node.restarts.proxy.restart_count", proxy_launch_count, REC_SOURCE_DEFAULT);
  } else {
    int res, i = 0;
    char *options[32], *last, *tok;
    bool open_ports_p = false;

    Vec<char> real_proxy_options;

    real_proxy_options.append(proxy_options, strlen(proxy_options));

    // Check if we need to pass down port/fd information to
    // traffic_server by seeing if there are any open ports.
    for (int i = 0, limit = m_proxy_ports.length(); !open_ports_p && i < limit; ++i) {
      if (ts::NO_FD != m_proxy_ports[i].m_fd) {
        open_ports_p = true;
      }
    }

    if (open_ports_p) {
      char portbuf[128];
      bool need_comma_p = false;
      real_proxy_options.append(" --httpport ", strlen(" --httpport "));
      for (int i = 0, limit = m_proxy_ports.length(); i < limit; ++i) {
        HttpProxyPort &p = m_proxy_ports[i];
        if (ts::NO_FD != p.m_fd) {
          if (need_comma_p) {
            real_proxy_options.append(',');
          }
          need_comma_p = true;
          p.print(portbuf, sizeof(portbuf));
          real_proxy_options.append((const char *)portbuf, strlen(portbuf));
        }
      }
    }

    // NUL-terminate for the benefit of strtok and printf.
    real_proxy_options.add(0);

    Debug("lm", "[LocalManager::startProxy] Launching %s with options '%s'\n", absolute_proxy_binary, &real_proxy_options[0]);

    ink_zero(options);
    options[0]   = absolute_proxy_binary;
    i            = 1;
    tok          = strtok_r(&real_proxy_options[0], " ", &last);
    options[i++] = tok;
    while (i < 32 && (tok = strtok_r(NULL, " ", &last))) {
      Debug("lm", "opt %d = '%s'\n", i, tok);
      options[i++] = tok;
    }

    if (!strstr(proxy_options, "-M")) { // Make sure we're starting the proxy in mgmt mode
      mgmt_fatal(stderr, 0, "[LocalManager::startProxy] ts options must contain -M");
    }

    EnableDeathSignal(SIGTERM);

    res = execv(absolute_proxy_binary, options);
    mgmt_elog(stderr, errno, "[LocalManager::startProxy] Exec of %s failed\n", absolute_proxy_binary);
    _exit(res);
  }
  return true;
}

/** Close all open ports.
 */
void
LocalManager::closeProxyPorts()
{
  for (int i = 0, n = lmgmt->m_proxy_ports.length(); i < n; ++i) {
    HttpProxyPort &p = lmgmt->m_proxy_ports[i];
    if (ts::NO_FD != p.m_fd) {
      close_socket(p.m_fd);
      p.m_fd = ts::NO_FD;
    }
  }
}
/*
 * listenForProxy()
 *  Function listens on the accept port of the proxy, so users aren't dropped.
 */
void
LocalManager::listenForProxy()
{
  if (!run_proxy)
    return;

  // We are not already bound, bind the port
  for (int i = 0, n = lmgmt->m_proxy_ports.length(); i < n; ++i) {
    HttpProxyPort &p = lmgmt->m_proxy_ports[i];
    if (ts::NO_FD == p.m_fd) {
      this->bindProxyPort(p);
    }

    // read backlong configuration value and overwrite the default value if found
    bool found;
    RecInt backlog = REC_readInteger("proxy.config.net.listen_backlog", &found);
    backlog        = (found && backlog >= 0) ? backlog : ats_tcp_somaxconn();

    if ((listen(p.m_fd, backlog)) < 0) {
      mgmt_fatal(stderr, errno, "[LocalManager::listenForProxy] Unable to listen on port: %d (%s)\n", p.m_port,
                 ats_ip_family_name(p.m_family));
    }
    mgmt_log(stderr, "[LocalManager::listenForProxy] Listening on port: %d (%s)\n", p.m_port, ats_ip_family_name(p.m_family));
  }
  return;
}

/*
 * bindProxyPort()
 *  Function binds the accept port of the proxy
 */
void
LocalManager::bindProxyPort(HttpProxyPort &port)
{
  int one  = 1;
  int priv = (port.m_port < 1024 && 0 != geteuid()) ? ElevateAccess::LOW_PORT_PRIVILEGE : 0;

  ElevateAccess access(priv);

  /* Setup reliable connection, for large config changes */
  if ((port.m_fd = socket(port.m_family, SOCK_STREAM, 0)) < 0) {
    mgmt_elog(stderr, 0, "[bindProxyPort] Unable to create socket : %s\n", strerror(errno));
    _exit(1);
  }

  if (port.m_type == HttpProxyPort::TRANSPORT_DEFAULT) {
    int should_filter_int = 0;
    bool found;
    should_filter_int = REC_readInteger("proxy.config.net.defer_accept", &found);
    if (found && should_filter_int > 0) {
#if defined(SOL_FILTER) && defined(FIL_ATTACH)
      (void)setsockopt(port.m_fd, SOL_FILTER, FIL_ATTACH, "httpfilt", 9);
#endif
    }
  }

  if (port.m_family == AF_INET6) {
    if (setsockopt(port.m_fd, IPPROTO_IPV6, IPV6_V6ONLY, SOCKOPT_ON, sizeof(int)) < 0) {
      mgmt_elog(stderr, 0, "[bindProxyPort] Unable to set socket options: %d : %s\n", port.m_port, strerror(errno));
    }
  }
  if (setsockopt(port.m_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int)) < 0) {
    mgmt_elog(stderr, 0, "[bindProxyPort] Unable to set socket options: %d : %s\n", port.m_port, strerror(errno));
    _exit(1);
  }

  if (port.m_inbound_transparent_p) {
#if TS_USE_TPROXY
    Debug("http_tproxy", "Listen port %d inbound transparency enabled.\n", port.m_port);
    if (setsockopt(port.m_fd, SOL_IP, TS_IP_TRANSPARENT, &one, sizeof(one)) == -1) {
      mgmt_elog(stderr, 0, "[bindProxyPort] Unable to set transparent socket option [%d] %s\n", errno, strerror(errno));
      _exit(1);
    }
#else
    Debug("lm", "[bindProxyPort] Transparency requested but TPROXY not configured\n");
#endif
  }

  IpEndpoint ip;
  if (port.m_inbound_ip.isValid()) {
    ip.assign(port.m_inbound_ip);
  } else if (AF_INET6 == port.m_family) {
    if (m_inbound_ip6.isValid())
      ip.assign(m_inbound_ip6);
    else
      ip.setToAnyAddr(AF_INET6);
  } else if (AF_INET == port.m_family) {
    if (m_inbound_ip4.isValid())
      ip.assign(m_inbound_ip4);
    else
      ip.setToAnyAddr(AF_INET);
  } else {
    mgmt_elog(stderr, 0, "[bindProxyPort] Proxy port with invalid address type %d\n", port.m_family);
    _exit(1);
  }
  ip.port() = htons(port.m_port);
  if (bind(port.m_fd, &ip.sa, ats_ip_size(&ip)) < 0) {
    mgmt_elog(stderr, 0, "[bindProxyPort] Unable to bind socket: %d : %s\n", port.m_port, strerror(errno));
    _exit(1);
  }

  Debug("lm", "[bindProxyPort] Successfully bound proxy port %d\n", port.m_port);
}

void
LocalManager::signalAlarm(int alarm_id, const char *desc, const char *ip)
{
  if (alarm_keeper)
    alarm_keeper->signalAlarm((alarm_t)alarm_id, desc, ip);
}
