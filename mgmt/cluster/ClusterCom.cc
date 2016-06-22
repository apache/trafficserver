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

/**************************************
 *
 * ClusterCom.cc
 *   Member function definitions for Cluster communication class.
 *
 * $Date: 2008-05-20 17:26:19 $
 *
 *
 */

#include "ts/ink_platform.h"
#include "ts/ink_sock.h"
#include "ts/ink_file.h"

#include "ts/I_Version.h"

#include "ts/TextBuffer.h"
#include "MgmtSocket.h"

#include "LocalManager.h"
#include "ClusterCom.h"
#include "VMap.h"
#include "MgmtUtils.h"
#include "WebMgmtUtils.h"
#include "MgmtHashTable.h"
#include "FileManager.h"

static bool checkBackDoor(int req_fd, char *message);

int MultiCastMessages = 0;
long LastHighestDelta = -1L;

static void *
drainIncomingChannel_broadcast(void *arg)
{
  char message[61440];
  ClusterCom *ccom = (ClusterCom *)arg;

  time_t t;
  time_t last_multicast_receive_time = time(NULL);

  /* Avert race condition, thread spun during constructor */
  while (lmgmt->ccom != ccom || !lmgmt->ccom->init) {
    mgmt_sleep_sec(1);
  }

  for (;;) { /* Loop draining mgmt network channels */
    int nevents = 0;

    // It's not clear whether this can happen, but historically, this code was written as if it
    // could. A hacky little sleep here will prevent this thread spinning on the read timeout.
    if (ccom->cluster_type == NO_CLUSTER || ccom->receive_fd == ts::NO_FD) {
      mgmt_sleep_sec(1);
    }

    ink_zero(message);

    if (ccom->cluster_type != NO_CLUSTER) {
      nevents = mgmt_read_timeout(ccom->receive_fd, ccom->mc_poll_timeout /* secs */, 0 /* usecs */);
      if (nevents > 0) {
        last_multicast_receive_time = time(NULL); // valid multicast msg
      } else {
        t = time(NULL);
        if ((t - last_multicast_receive_time) > ccom->mc_poll_timeout) {
          // Timeout on multicast receive channel, reset channel.
          if (ccom->receive_fd > 0) {
            close(ccom->receive_fd);
          }
          ccom->receive_fd = -1;
          Debug("ccom", "Timeout, resetting multicast receive channel");
          if (lmgmt->ccom->establishReceiveChannel(0)) {
            Debug("ccom", "establishReceiveChannel failed");
            ccom->receive_fd = -1;
          }
          last_multicast_receive_time = t; // next action at next interval
        }
      }
    }

    /* Broadcast message */
    if (ccom->cluster_type != NO_CLUSTER && ccom->receive_fd > 0 && nevents > 0 &&
        (ccom->receiveIncomingMessage(message, sizeof(message)) > 0)) {
      ccom->handleMultiCastMessage(message);
    }
  }

  return NULL;
} /* End drainIncomingChannel */

/*
 * drainIncomingChannel
 *   This function is blocking, it never returns. It is meant to allow for
 * continuous draining of the network. It drains and handles requests made on
 * the reliable and multicast channels between all the peers.
 */
static void *
drainIncomingChannel(void *arg)
{
  char message[61440];
  ClusterCom *ccom = (ClusterCom *)arg;
  struct sockaddr_in cli_addr;

  // Fix for INKqa07688: There was a problem at Genuity where if you
  // pulled out the cable on the cluster interface (or just ifconfig'd
  // down/up the cluster interface), the fd associated with that
  // interface would somehow get into a bad state... and the multicast
  // packets from other nodes wouldn't be received anymore.
  //
  // The fix for the problem was to close() and re-open the multicast
  // socket if we detected that no activity has occurred for 30
  // seconds.  30 seconds was based on the default peer_timeout
  // (proxy.config.cluster.peer_timeout) value.  davey showed that
  // this value worked out well experimentally (though more testing
  // and experimentation would be beneficial).
  //
  // traffic_manager running w/ no cop: In this case, our select()
  // call will hang if the fd gets into the bad state.  The solution
  // is to timeout select if nothing comes in off the network for
  // sometime.. wake up, and close/open the multicast channel.
  //
  // traffic_manager running w/ cop: In this case, our select() will
  // never timeout (since cop will be heartbeating us).  Some
  // additional logic was added to keep track of the last successful
  // mulitcast receive.
  //
  // after closing the channel, some addition logic was put into place
  // to reopen the channel (e.g. opening the socket would fail if the
  // interface was down).  In this case, the ccom->receive_fd is set
  // to '-1' and the open is retried until it succeeds.

  /* Avert race condition, thread spun during constructor */
  while (lmgmt->ccom != ccom || !lmgmt->ccom->init) {
    mgmt_sleep_sec(1);
  }

  for (;;) { /* Loop draining mgmt network channels */
    ink_zero(message);

    // It's not clear whether this can happen, but historically, this code was written as if it
    // could. A hacky little sleep here will prevent this thread spinning on the read timeout.
    if (ccom->cluster_type == NO_CLUSTER || ccom->reliable_server_fd == ts::NO_FD) {
      mgmt_sleep_sec(1);
    }

    if (mgmt_read_timeout(ccom->reliable_server_fd, ccom->mc_poll_timeout /* secs */, 0 /* usecs */) > 0) {
      /* Reliable(TCP) request */
      socklen_t clilen = sizeof(cli_addr);
      int req_fd       = mgmt_accept(ccom->reliable_server_fd, (struct sockaddr *)&cli_addr, &clilen);
      if (req_fd < 0) {
        mgmt_elog(stderr, errno, "[drainIncomingChannel] error accepting "
                                 "reliable connection\n");
        continue;
      }
      if (fcntl(req_fd, F_SETFD, 1) < 0) {
        mgmt_elog(stderr, errno, "[drainIncomingChannel] Unable to set close "
                                 "on exec flag\n");
        close(req_fd);
        continue;
      }

      // In no cluster mode, the rsport should not be listening.
      ink_release_assert(ccom->cluster_type != NO_CLUSTER);

      /* Handle Request */
      if (mgmt_readline(req_fd, message, 61440) > 0) {
        if (strstr(message, "aresolv: ")) {
          /* Peer is Resolving our alarm */
          alarm_t a;

          // coverity[secure_coding]
          if (sscanf(message, "aresolv: %d", &a) != 1) {
            close_socket(req_fd);
            continue;
          }
          lmgmt->alarm_keeper->resolveAlarm(a);
        } else if (strstr(message, "unmap: ")) {
          /*
           * Explicit virtual ip unmap request. Note order unmap then
           * map for strstr.
           */
          char msg_ip[80];
          const char *msg;
          if (sscanf(message, "unmap: %79s", msg_ip) != 1) {
            close_socket(req_fd);
            continue;
          }

          mgmt_log("[drainIncomingChannel] Got unmap request: '%s'\n", message);

          ink_mutex_acquire(&(ccom->mutex));       /* Grab the lock */
          if (lmgmt->virt_map->rl_unmap(msg_ip)) { /* Requires lock */
            msg = "unmap: done";
          } else {
            msg = "unmap: failed";
          }
          ink_mutex_release(&(ccom->mutex)); /* Release the lock */

          mgmt_writeline(req_fd, msg, strlen(msg));

          /* Wait for peer to read status */
          if (mgmt_readline(req_fd, message, 61440) != 0) {
            mgmt_elog(0, "[drainIncomingChannel] Connection not closed\n");
          }
        } else if (strstr(message, "map: ")) {
          /* Explicit virtual ip map request */
          char msg_ip[80];
          const char *msg;
          if (sscanf(message, "map: %79s", msg_ip) != 1) {
            close_socket(req_fd);
            continue;
          }

          mgmt_log("[drainIncomingChannel] Got map request: '%s'\n", message);

          if (lmgmt->run_proxy) {
            ink_mutex_acquire(&(ccom->mutex));     /* Grab the lock */
            if (lmgmt->virt_map->rl_map(msg_ip)) { /* Requires the lock */
              msg = "map: done";
            } else {
              msg = "map: failed";
            }
            ink_mutex_release(&(ccom->mutex)); /* Release the lock */
          } else {
            msg = "map: failed";
          }

          mgmt_writeline(req_fd, msg, strlen(msg));

          /* Wait for peer to read status */
          if (mgmt_readline(req_fd, message, 61440) != 0) {
            mgmt_elog(0, "[drainIncomingChannel] Connection not closed\n");
          }

        } else if (strstr(message, "file: ")) {
          /* Requesting a config file from us */
          bool stat = false;
          char fname[1024];
          version_t ver;
          textBuffer *buff = NULL;
          Rollback *rb;

          /* Get the file and blast it back */
          if (sscanf(message, "file: %1023s %d", fname, &ver) != 2) {
            close_socket(req_fd);
            continue;
          }

          if (ccom->configFiles->getRollbackObj(fname, &rb) && (rb->getCurrentVersion() == ver) &&
              (rb->getVersion(ver, &buff) == OK_ROLLBACK)) {
            size_t bytes_written = 0;
            stat                 = true;
            bytes_written        = write_socket(req_fd, buff->bufPtr(), strlen(buff->bufPtr()));
            if (bytes_written != strlen(buff->bufPtr())) {
              stat = false;
              mgmt_log(stderr, "[drainIncomingChannel] Failed file req: %s v: %d\n", fname, ver);
            } else {
              Debug("ccom", "[drainIncomingChannel] file req: %s v: %d bytes: %d\n", fname, ver, (int)strlen(buff->bufPtr()));
            }
          } else {
            mgmt_elog(0, "[drainIncomingChannel] Error file req: %s ver: %d\n", fname, ver);
          }

          if (!stat) {
            const char *msg = "file: failed";
            mgmt_writeline(req_fd, msg, strlen(msg));
          }
          if (buff)
            delete buff;
        } else if (strstr(message, "cmd: shutdown_manager")) {
          mgmt_log("[ClusterCom::drainIncomingChannel] Received manager shutdown request\n");
          lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_RESTART;
        } else if (strstr(message, "cmd: shutdown_process")) {
          mgmt_log("[ClusterCom::drainIncomingChannel] Received process shutdown request\n");
          lmgmt->processShutdown();
        } else if (strstr(message, "cmd: restart_process")) {
          mgmt_log("[ClusterCom::drainIncomingChannel] Received restart process request\n");
          lmgmt->processRestart();
        } else if (strstr(message, "cmd: bounce_process")) {
          mgmt_log("[ClusterCom::drainIncomingChannel] Received bounce process request\n");
          lmgmt->processBounce();
        } else if (strstr(message, "cmd: clear_stats")) {
          char sname[1024];
          mgmt_log("[ClusterCom::drainIncomingChannel] Received clear stats request\n");
          if (sscanf(message, "cmd: clear_stats %1023s", sname) != 1) {
            lmgmt->clearStats(sname);
          } else {
            lmgmt->clearStats();
          }
        } else if (!checkBackDoor(req_fd, message)) { /* Heh... */
          mgmt_log("[ClusterCom::drainIncomingChannel] Unexpected message on cluster"
                   " port.  Possibly an attack\n");
          Debug("ccom", "Unknown message to rsport received: %s", message);
          close_socket(req_fd);
          continue;
        }
      }
      close_socket(req_fd);
    }
  }

  return NULL;
} /* End drainIncomingChannel */

/*
 * cluster_com_port_watcher(...)
 *   This function watches updates and changes that are made to the
 * cluster port. Reconfiguring it if need be.
 *
 * Note: the cluster port here is the cluster port for the proxy not
 * the manager.
 */
int
cluster_com_port_watcher(const char *name, RecDataT /* data_type ATS_UNUSED */, RecData data, void * /* cookie ATS_UNUSED */)
{
  ink_assert(!name);

  ink_mutex_acquire(&(lmgmt->ccom->mutex)); /* Grab cluster lock */
  lmgmt->ccom->cluster_port = (RecInt)data.rec_int;
  ink_mutex_release(&(lmgmt->ccom->mutex)); /* Release cluster lock */

  return 0;
} /* End cluster_com_port_watcher */

ClusterCom::ClusterCom(unsigned long oip, char *host, int mcport, char *group, int rsport, char *p)
  : our_wall_clock(0), alive_peers_count(0), reliable_server_fd(0), broadcast_fd(0), receive_fd(0)
{
  int rec_err;
  bool found = false;

  init = false;
  if (strlen(host) >= 1024) {
    mgmt_fatal(stderr, 0, "[ClusterCom::ClusterCom] Hostname too large: %s\n", host);
  }
  // the constructor does a memset() on broadcast_addr and receive_addr, initializing them
  // coverity[uninit_member]
  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  memset(&receive_addr, 0, sizeof(receive_addr));

  ink_strlcpy(our_host, host, sizeof(our_host));
  our_ip = oip;

  /* Get the cluster type */
  cluster_type = CLUSTER_INVALID;
  RecInt rec_int;

  rec_err      = RecGetRecordInt("proxy.local.cluster.type", &rec_int);
  cluster_type = (MgmtClusterType)rec_int;
  found        = (rec_err == REC_ERR_OKAY);
  ink_assert(found);

  switch (cluster_type) {
  case FULL_CLUSTER:
  case MGMT_CLUSTER:
  case NO_CLUSTER:
    break;
  case CLUSTER_INVALID:
  default:
    mgmt_log(stderr, "[ClusterCom::ClusterCom] Invalid cluster type.  "
                     "Defaulting to full clustering\n");
    cluster_type = FULL_CLUSTER;
    break;
  }
  /* Get the cluster config file name + path */
  RecString cluster_file;

  rec_err = RecGetRecordString_Xmalloc("proxy.config.cluster.cluster_configuration", &cluster_file);
  found   = (rec_err == REC_ERR_OKAY);

  if (!found) {
    mgmt_fatal(stderr, 0, "[ClusterCom::ClusterCom] no cluster_configuration filename configured\n");
  }

  if (strlen(p) + strlen(cluster_file) >= 1024) {
    mgmt_fatal(stderr, 0, "[ClusterCom::ClusterCom] path + filename too large\n");
  }
  // XXX: This allows to have absolute config cluster_configuration directive.
  //      If that file must be inside config directory (p) use
  //      ink_filepath_make
  ink_filepath_merge(cluster_conf, sizeof(cluster_conf), p, cluster_file, INK_FILEPATH_TRUENAME);
  // XXX: Shouldn't we pass the cluster_conf to the Rollback ???
  //
  Debug("ccom", "[ClusterCom::ClusterCom] Using cluster file: %s", cluster_file);
  Debug("ccom", "[ClusterCom::ClusterCom] Using cluster conf: %s", cluster_conf);
  cluster_file_rb = new Rollback(cluster_file, false);

  ats_free(cluster_file);

  if (ink_sys_name_release(sys_name, sizeof(sys_name), sys_release, sizeof(sys_release)) >= 0) {
    mgmt_log("[ClusterCom::ClusterCom] Node running on OS: '%s' Release: '%s'\n", sys_name, sys_release);
  } else {
    sys_name[0] = sys_release[0] = '\0';
    mgmt_elog(errno, "[ClusterCom::ClusterCom] Unable to determime OS and release info\n");
  }

  /* Grab the proxy cluster port */
  cluster_port = REC_readInteger("proxy.config.cluster.cluster_port", &found);
  RecRegisterConfigUpdateCb("proxy.config.cluster.cluster_port", cluster_com_port_watcher, NULL);

  if (!(strlen(group) < (MAX_MC_GROUP_LEN - 1))) {
    mgmt_fatal(stderr, 0, "[ClusterCom::ClusterCom] mc group length too large!\n");
  }

  ink_strlcpy(mc_group, group, sizeof(mc_group));
  mc_port              = mcport;
  reliable_server_port = rsport;

  mc_ttl = REC_readInteger("proxy.config.cluster.mc_ttl", &found);
  ink_assert(found);

  log_bogus_mc_msgs = REC_readInteger("proxy.config.cluster.log_bogus_mc_msgs", &found);
  ink_assert(found);

  /* Timeout between config changes, basically a clock noise filter */
  delta_thresh = REC_readInteger("proxy.config.cluster.delta_thresh", &found);
  ink_assert(found);

  /* The timeout before marking a peer as dead */
  peer_timeout = REC_readInteger("proxy.config.cluster.peer_timeout", &found);
  ink_assert(found);

  mc_send_interval = REC_readInteger("proxy.config.cluster.mc_send_interval", &found);
  ink_assert(found);

  mc_poll_timeout = REC_readInteger("proxy.config.cluster.mc_poll_timeout", &found);
  ink_assert(found);

  /* Launch time */
  startup_time = time(NULL);

  /* Timeout before broadcasting virtual ip information */
  startup_timeout = REC_readInteger("proxy.config.cluster.startup_timeout", &found);
  ink_assert(found);

  last_shared_send = 0;

  ink_mutex_init(&mutex, "ccom-mutex");
  peers       = ink_hash_table_create(InkHashTableKeyType_String);
  mismatchLog = ink_hash_table_create(InkHashTableKeyType_String);

  if (cluster_type != NO_CLUSTER) {
    ink_thread_create(drainIncomingChannel_broadcast, this); /* Spin drainer thread */
    ink_thread_create(drainIncomingChannel, this);           /* Spin drainer thread */
  }
  return;
} /* End ClusterCom::ClusterCom */

/*
 * checkPeers(...)
 *   Function checks on our peers by racing through the peer list(ht) and
 * marking nodes as idle/dead if we have not heard from them in awhile.
 */
void
ClusterCom::checkPeers(time_t *ticker)
{
  static int number_of_nodes = -1;
  bool signal_alarm          = false;
  time_t t                   = time(NULL);
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  // Hack in the file manager in case the rollback needs to send a notification. This is definitely
  // a hack, but it helps break the dependency on global FileManager in traffic_manager.
  cluster_file_rb->configFiles = configFiles;

  if (cluster_type == NO_CLUSTER)
    return;

  if ((t - *ticker) > 5) {
    int num_peers = 0;
    long idle_since;
    textBuffer *buff;

    Debug("ccom", "MultiCast Messages received: %d", MultiCastMessages);

    /*
     * Need the lock here so that someone doesn't change the peer hash
     * table out from underneath you.
     */
    ink_mutex_acquire(&(mutex)); /* Grab cluster lock */
    for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
         entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
      ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);
      if ((idle_since = t - tmp->idle_ticks) > peer_timeout) {
        char cip[80];
        struct in_addr addr;
        addr.s_addr = tmp->inet_address;

        ink_strlcpy(cip, inet_ntoa(addr), sizeof(cip));

        Debug("ccom", "[ClusterCom::checkPeers] DEAD! %s idle since: %ld naddrs: %d\n", cip, idle_since, tmp->num_virt_addrs);

        if ((idle_since = t - tmp->manager_idle_ticks) > peer_timeout) {
          if (tmp->manager_alive > 0) {
            Note("marking manager on node %s as down", cip);
          }
          tmp->manager_alive = -1;
          Debug("ccom", "[ClusterCom::checkPeers] Manager DEAD! %s idle since: %ld\n", cip, idle_since);
        }

        if (tmp->num_virt_addrs >= 0) {
          Note("marking server on node %s as down", cip);
        }

        tmp->num_virt_addrs = -1; /* This is basically the I'm dead flag */
        if (tmp->num_virt_addrs) {
          lmgmt->virt_map->rl_resetSeenFlag(cip);
          lmgmt->virt_map->rl_clearUnSeen(cip);
        }
      }
    }

    /* Create the base for the cluster file(inserting header comment) */
    buff = new textBuffer(strlen(CLUSTER_CONFIG_FILE_BLURB) + 1024);
    buff->copyFrom(CLUSTER_CONFIG_FILE_BLURB, strlen(CLUSTER_CONFIG_FILE_BLURB));

    if (cluster_type == FULL_CLUSTER) {
      /*
       * Two pass loop. First loop over the peers hash table and count
       * the number of peers. Second construct the new cluster config
       * file for the proxy.
       */
      for (int c = 0; c <= 1; c++) {
        bool flag = false; /* Used to mark first loop on second pass */

        for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
             entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
          char str_number[80];
          char *key            = (char *)ink_hash_table_entry_key(peers, entry);
          ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);

          if (!c) {                          /* First pass */
            if (tmp->num_virt_addrs != -1) { /* Count if not dead */
              num_peers++;
            }
          } else if (!flag) {
            /*
             * First time through second pass, cluster config file needs
             * to start with the number of nodes in the cluster, so we
             * stick this in the file
             */
            snprintf(str_number, sizeof(str_number), "%d\n", num_peers);
            buff->copyFrom(str_number, strlen(str_number));
            flag = true;
          } else if (num_peers == number_of_nodes) {
            /*
             * If the number of peers in the hash_table is the same as the
             * last time we checked, then we have emitted this file before.
             *
             * FIX: there is potentially a case where a node comes in and
             *      a node leaves(for good) and we don't notice the change
             *      of guard.
             */
            break;
          }

          if (c && tmp->num_virt_addrs != -1) {
            /* Second pass, add entry to the file "ip:port" */
            buff->copyFrom(key, strlen(key));
            snprintf(str_number, sizeof(str_number), ":%d\n", tmp->port);
            buff->copyFrom(str_number, strlen(str_number));
          }
        }

        if (num_peers == number_of_nodes) {
          /*
           * If the number of peers in the hash_table is the same as the
           * last time we checked, then we have emitted this file before.
           *
           * FIX: there is potentially a case where a node comes in and
           *      a node leaves(for good) and we don't notice the change
           *      of guard.
           */
          break;
        } else if (num_peers == 0 && !c) {
          /*
           *Handle the standalone case, you are on the second pass and
           * there are no peers. Proxy expects 0 to be in the file to
           * signify standalone case.
           */
          buff->copyFrom("0\n", strlen("0\n"));
          break;
        }
      }
    } else {
      // We are not doing full clustering so we also tell the
      //   proxy there are zero nodes in the cluster
      buff->copyFrom("0\n", strlen("0\n"));
    }
    /*
     * The number of peers have changed, output the new file, this will
     * trigger an update callback which will eveutually signal the proxy.
     */
    if (num_peers != number_of_nodes) {
      if (cluster_file_rb->forceUpdate(buff) != OK_ROLLBACK) {
        mgmt_elog(0, "[ClusterCom::checkPeers] Failed update: cluster.config\n");
        signal_alarm = true; /* Throw the alarm after releasing the lock */
      } else {
        number_of_nodes   = num_peers; /* Update the static count */
        alive_peers_count = num_peers;
      }
    }
    delete buff;
    ink_mutex_release(&(mutex)); /* Release cluster lock */
    if (signal_alarm) {
      /*
      lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR,
                                       "[TrafficManager] Unable to write cluster.config, membership unchanged");
      */
      mgmt_elog(0, "[TrafficManager] Unable to write cluster.config, membership unchanged");
    }
    *ticker = t;
  }

  /*
   * Here we aggregate cluster stats from the stored peers stats and our
   * node stats.
   */
  // fix me -- what does aggregated_node_data do?

  ink_mutex_acquire(&(mutex)); /* Grab the cluster lock */
  for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
    ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);

    /* Skip nodes we've flagged as dead */
    if (tmp->num_virt_addrs == -1) {
      continue;
    }
    // fix me -- what does aggregated_node_data do?
  }
  ink_mutex_release(&(mutex)); /* Release the cluster lock */

  return;
} /* End ClusterCom::checkPeers */

void
ClusterCom::generateClusterDelta(void)
{
  long highest_delta = 0L;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  if (cluster_type == NO_CLUSTER)
    return;

  ink_mutex_acquire(&(mutex));
  for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
    ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);

    // is the node alive?
    if (tmp->num_virt_addrs != -1) {
      highest_delta = max(highest_delta, tmp->delta);
    }
  }
  ink_mutex_release(&(mutex));

  // only transmit if different
  if (highest_delta != LastHighestDelta) {
    LastHighestDelta = highest_delta;
    char highest_delta_str[32];
    snprintf(highest_delta_str, 31, "%ld", highest_delta);
    lmgmt->signalEvent(MGMT_EVENT_HTTP_CLUSTER_DELTA, highest_delta_str);
  }

} /* End ClusterCom::generateClusterDelta */

/*
 * handleMultCastMessage(...)
 *   Function is called to handle(parse) messages received from the broadcast
 * channel.
 */
void
ClusterCom::handleMultiCastMessage(char *message)
{
  int peer_cluster_port, ccom_port;
  char *last, *line, ip[1024], hostname[1024];
  char tsver[128]         = "Before 2.X";
  char cluster_name[1024] = "UNKNOWN";
  RecT type;
  ClusterPeerInfo *p;
  time_t peer_wall_clock, t;
  InkHashTableValue hash_value;

  ++MultiCastMessages;

  t              = time(NULL); /* Get current time for determining most recent changes */
  our_wall_clock = t;

  /* Grab the ip address, we need to know this so that we only complain
     once about a cluster name or traffic server version mismatch */
  if ((line = strtok_r(message, "\n", &last)) == NULL)
    goto Lbogus; /* IP of sender */

  // coverity[secure_coding]
  if (strlen(line) >= sizeof(ip) || sscanf(line, "ip: %s", ip) != 1)
    goto Lbogus;

  // FIX THIS: elam 02/23/1999
  //   Loopback disable is currently not working on NT.
  //   We will ignore our own multicast messages.
  if (inet_addr(ip) == our_ip) {
    return;
  }

  /* Make sure this is a message for the cluster we belong to */
  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus; /* ClusterName of sender */

  // coverity[secure_coding]
  if (strlen(line) >= sizeof(cluster_name) || sscanf(line, "cluster: %s", cluster_name) != 1)
    goto Lbogus;

  if (strcmp(cluster_name, lmgmt->proxy_name) != 0) {
    logClusterMismatch(ip, TS_NAME_MISMATCH, cluster_name);
    return;
  }

  /* Make sure this a message from a Traffic Server of the same version */
  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus; /* TS version of sender */

  // coverity[secure_coding]
  if (strlen(line) >= sizeof(tsver) || sscanf(line, "tsver: %s", tsver) != 1 || strcmp(line + 7, appVersionInfo.VersionStr) != 0) {
    logClusterMismatch(ip, TS_VER_MISMATCH, tsver);
    return;
  }

  /* Figure out what type of message this is */
  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus;
  if (strcmp("type: files", line) == 0) { /* Config Files report */
    handleMultiCastFilePacket(last, ip);
    return;
  } else if (strcmp("type: stat", line) == 0) { /* Statistics report */
    type = RECT_CLUSTER;
  } else if (strcmp("type: alarm", line) == 0) { /* Alarm report */
    handleMultiCastAlarmPacket(last, ip);
    return;
  } else if (strcmp("type: vmap", line) == 0) { /* Virtual Map report */
    handleMultiCastVMapPacket(last, ip);
    return;
  } else {
    mgmt_elog(0, "[ClusterCom::handleMultiCastMessage] Invalid type msg: '%s'\n", line);
    return;
  }

  /* Check OS and version info */
  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus; /* OS of sender */
  if (!strstr(line, "os: ") || !strstr(line, sys_name)) {
    /*
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR,
                                     "Received Multicast message from peer running mis-match"
                                     " Operating system, please investigate");
    */
    Debug("ccom", "[ClusterCom::handleMultiCastMessage] Received message from peer "
                  "running different os/release '%s'(ours os: '%s' rel: '%s'\n",
          line, sys_name, sys_release);
  }

  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus; /* OS-Version of sender */
  if (!strstr(line, "rel: ") || !strstr(line, sys_release)) {
    /*
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR,
                                     "Received Multicast message from peer running mis-match"
                                     " Operating system release, please investigate");
    */
    Debug("ccom", "[ClusterCom::handleMultiCastMessage] Received message from peer "
                  "running different os/release '%s'(ours os: '%s' rel: '%s'\n",
          line, sys_name, sys_release);
  }

  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus; /* Hostname of sender */
  if (strlen(line) >= sizeof(hostname) || sscanf(line, "hostname: %s", hostname) != 1) {
    mgmt_elog(0, "[ClusterCom::handleMultiCastMessage] Invalid message-line(%d) '%s'\n", __LINE__, line);
    return;
  }

  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus; /* mc_port of sender */
  if (sscanf(line, "port: %d", &peer_cluster_port) != 1) {
    mgmt_elog(0, "[ClusterCom::handleMultiCastMessage] Invalid message-line(%d) '%s'\n", __LINE__, line);
    return;
  }

  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus; /* rs_port of sender */
  if (sscanf(line, "ccomport: %d", &ccom_port) != 1) {
    mgmt_elog(0, "[ClusterCom::handleMultiCastMessage] Invalid message-line(%d) '%s'\n", __LINE__, line);
    return;
  }

  /* Their wall clock time and last config change time */
  if ((line = strtok_r(NULL, "\n", &last)) == NULL)
    goto Lbogus;
  int64_t tt;
  if (sscanf(line, "time: %" PRId64 "", &tt) != 1) {
    mgmt_elog(0, "[ClusterCom::handleMultiCastMessage] Invalid message-line(%d) '%s'\n", __LINE__, line);
    return;
  }
  peer_wall_clock = (time_t)tt;

  /* Have we see this guy before? */
  ink_mutex_acquire(&(mutex)); /* Grab cluster lock to access hash table */
  if (ink_hash_table_lookup(peers, (InkHashTableKey)ip, &hash_value) == 0) {
    p                 = (ClusterPeerInfo *)ats_malloc(sizeof(ClusterPeerInfo));
    p->inet_address   = inet_addr(ip);
    p->num_virt_addrs = 0;

    // Safe since these are completely static
    // TODO: This might no longer be completely optimal, since we don't keep track of
    // how many RECT_NODE stats there are. I'm hoping it's negligible though, but worst
    // case we can reoptimize this later (and more efficiently).
    int cnt               = 0;
    p->node_rec_data.recs = (RecRecord *)ats_malloc(sizeof(RecRecord) * g_num_records);
    for (int j = 0; j < g_num_records; j++) {
      RecRecord *rec = &(g_records[j]);

      /*
       * The following code make sence only when RECT_NODE records
       * defined in mgmt/RecordsConfig.cc are placed continuously.
       */
      if (rec->rec_type == RECT_NODE) {
        p->node_rec_data.recs[cnt].rec_type  = rec->rec_type;
        p->node_rec_data.recs[cnt].name      = rec->name;
        p->node_rec_data.recs[cnt].data_type = rec->data_type;
        memset(&p->node_rec_data.recs[cnt].data, 0, sizeof(rec->data));
        memset(&p->node_rec_data.recs[cnt].data_default, 0, sizeof(rec->data_default));
        p->node_rec_data.recs[cnt].lock          = rec->lock;
        p->node_rec_data.recs[cnt].sync_required = rec->sync_required;
        ++cnt;
      }
    }
    p->node_rec_data.num_recs = cnt;

    ink_hash_table_insert(peers, (InkHashTableKey)ip, (p));
    ink_hash_table_delete(mismatchLog, ip);

    Note("adding node %s to the cluster", ip);
  } else {
    p = (ClusterPeerInfo *)hash_value;
    if (p->manager_alive < 0) {
      Note("marking manager on node %s as up", ip);
    }
    if (our_wall_clock - p->idle_ticks > peer_timeout) {
      Note("marking server on node %s as up", ip);
    }
  }
  p->port       = peer_cluster_port;
  p->ccom_port  = ccom_port;
  p->idle_ticks = p->manager_idle_ticks = our_wall_clock;
  p->last_time_recorded                 = peer_wall_clock;
  p->delta                              = peer_wall_clock - our_wall_clock;
  p->manager_alive                      = 1;

  ink_assert(type == RECT_CLUSTER);
  handleMultiCastStatPacket(last, p);
  ink_mutex_release(&(mutex)); /* Release cluster lock */

  return;

Lbogus:
  if (log_bogus_mc_msgs) {
    mgmt_elog(0, "[ClusterCom::handleMultiCastMessage] Bogus mc message-line\n");
    if (line) {
      Debug("ccom", "[ClusterCom::handleMultiCastMessage] Bogus mc message-line %s\n", line);
    }
    return;
  }
} /* End ClusterCom::handleMultiCastMessage */

/*
 * handleMultiCastStatPacket(...)
 *   Function groks the stat packets received on the mc channel and updates
 * our local copy of our peers stats.
 */
void
ClusterCom::handleMultiCastStatPacket(char *last, ClusterPeerInfo *peer)
{
  char *line;
  RecRecords *rec_ptr = NULL;
  int tmp_id          = -1;
  RecDataT tmp_type   = RECD_NULL;
  RecRecord *rec      = NULL;

  /* Loop over records, updating peer copy(summed later) */
  rec_ptr = &(peer->node_rec_data);
  for (int i = 0; (line = strtok_r(NULL, "\n", &last)) && i < rec_ptr->num_recs; i++) {
    tmp_id   = -1;
    tmp_type = RECD_NULL;
    rec      = &(rec_ptr->recs[i]);
    //    rec      = &(g_records[g_type_records[RECT_NODE][i]]);

    switch (rec->data_type) {
    case RECD_INT:
    case RECD_COUNTER: {
      RecInt tmp_msg_val = -1;
      tmp_id             = ink_atoi(line);
      char *v2 = strchr(line, ':'), *v3 = NULL;
      if (v2) {
        tmp_type = (RecDataT)ink_atoi(v2 + 1);
        v3       = strchr(v2 + 1, ':');
        if (v3)
          tmp_msg_val = ink_atoi64(v3 + 1);
      }
      if (!v2 || !v3) {
        mgmt_elog(0, "[ClusterCom::handleMultiCastStatPacket] Invalid message-line(%d) '%s'\n", __LINE__, line);
        return;
      }
      ink_assert(i == tmp_id && rec->data_type == tmp_type);
      ink_release_assert(i == tmp_id && rec->data_type == tmp_type);
      if (!(i == tmp_id && rec->data_type == tmp_type)) {
        return;
      }

      if (rec->data_type == RECD_INT) {
        rec->data.rec_int = tmp_msg_val;
      } else {
        rec->data.rec_counter = tmp_msg_val;
      }
      break;
    }
    case RECD_FLOAT: {
      MgmtFloat tmp_msg_val = -1.0;
      // the types specified are all have a defined constant size
      // coverity[secure_coding]
      if (sscanf(line, "%d:%d: %f", &tmp_id, (int *)&tmp_type, &tmp_msg_val) != 3) {
        mgmt_elog(0, "[ClusterCom::handleMultiCastStatPacket] Invalid message-line(%d) '%s'\n", __LINE__, line);
        return;
      }
      ink_assert(i == tmp_id && rec->data_type == tmp_type);
      ink_release_assert(i == tmp_id && rec->data_type == tmp_type);
      if (!(i == tmp_id && rec->data_type == tmp_type)) {
        return;
      }

      rec->data.rec_float = tmp_msg_val;
      break;
    }
    case RECD_STRING: { /* String stats not supported for cluster passing */
      int ccons;
      char *tmp_msg_val = NULL;

      // the types specified are all have a defined constant size
      // coverity[secure_coding]
      if (sscanf(line, "%d:%d: %n", &tmp_id, (int *)&tmp_type, &ccons) != 2) {
        mgmt_elog(0, "[ClusterCom::handleMultiCastStatPacket] Invalid message-line(%d) '%s'\n", __LINE__, line);
        return;
      }
      tmp_msg_val = &line[ccons];
      ink_assert(i == tmp_id && rec->data_type == tmp_type);
      ink_release_assert(i == tmp_id && rec->data_type == tmp_type);
      if (!(i == tmp_id && rec->data_type == tmp_type)) {
        return;
      }

      if (strcmp(tmp_msg_val, "NULL") == 0 && rec->data.rec_string) {
        ats_free(rec->data.rec_string);
        rec->data.rec_string = NULL;
      } else if (!(strcmp(tmp_msg_val, "NULL") == 0)) {
        ats_free(rec->data.rec_string);
        rec->data.rec_string = (RecString)ats_strdup(tmp_msg_val);
      }
      break;
    }
    default:
      ink_assert(false);
      break;
    }
  }
  return;
} /* End ClusterCom::handleMultiCastStatPacket */

//-------------------------------------------------------------------------
// INKqa08381 - These functions are called by
// ClusterCom::handleMultiCastFilePacket(); required so that we only
// sync records.config CONFIG values (not LOCAL values) across a
// records.config cluster syncronize operation.
//-------------------------------------------------------------------------
bool
scan_and_terminate(char *&p, char a, char b)
{
  bool eob = false; // 'eob' is end-of-buffer
  while ((*p != a) && (*p != b) && (*p != '\0'))
    p++;
  if (*p == '\0') {
    eob = true;
  } else {
    *(p++) = '\0';
    while ((*p == a) || ((*p == b) && (*p != '\0')))
      p++;
    if (*p == '\0')
      eob = true;
  }
  return eob;
}

void
extract_locals(MgmtHashTable *local_ht, char *record_buffer)
{
  char *p, *q, *line, *line_cp, *name;
  bool eof;
  p = record_buffer;
  for (eof = false; !eof;) {
    line = q = p;
    eof      = scan_and_terminate(p, '\r', '\n');
    Debug("ccom_rec", "[extract_locals] %s\n", line);
    while ((*q == ' ') || (*q == '\t'))
      q++;
    // is this line a LOCAL?
    if (strncmp(q, "LOCAL", strlen("LOCAL")) == 0) {
      line_cp = ats_strdup(line);
      q += strlen("LOCAL");
      while ((*q == ' ') || (*q == '\t'))
        q++;
      name = q;
      if (scan_and_terminate(q, ' ', '\t')) {
        Debug("ccom_rec", "[extract_locals] malformed line: %s\n", name);
        ats_free(line_cp);
        continue;
      }
      local_ht->mgmt_hash_table_insert(name, line_cp);
    }
  }
}

bool
insert_locals(textBuffer *rec_cfg_new, textBuffer *rec_cfg, MgmtHashTable *local_ht)
{
  char *p, *q, *line, *name;
  bool eof;
  InkHashTableEntry *hte;
  InkHashTableIteratorState htis;
  MgmtHashTable *local_access_ht = new MgmtHashTable("local_access_ht", false, InkHashTableKeyType_String);
  p                              = rec_cfg->bufPtr();
  for (eof = false; !eof;) {
    line = q = p;
    eof      = scan_and_terminate(p, '\r', '\n');
    Debug("ccom_rec", "[insert_locals] %s\n", line);
    while ((*q == ' ') || (*q == '\t'))
      q++;
    // is this line a local?
    if (strncmp(q, "LOCAL", strlen("LOCAL")) == 0) {
      q += strlen("LOCAL");
      while ((*q == ' ') || (*q == '\t'))
        q++;
      name = q;
      if (scan_and_terminate(q, ' ', '\t')) {
        Debug("ccom_rec", "[insert_locals] malformed line: %s\n", name);
        continue;
      }
      if (local_ht->mgmt_hash_table_lookup(name, (void **)&line)) {
        // LOCAL found in hash-table; 'line' points to our LOCAL
        // value; keep track that we accessed this LOCAL already;
        // later, we need to iterate through all of our un-accessed
        // LOCALs and add them to the bottom of the remote config
        local_access_ht->mgmt_hash_table_insert(name, 0);
      } else {
        // LOCAL didn't exist in our config, don't merge into the
        // remote config
        continue;
      }
    }
    // copy the whole line over
    rec_cfg_new->copyFrom(line, strlen(line));
    rec_cfg_new->copyFrom("\n", strlen("\n"));
  }
  // remove any of our accessed LOCALs from local_ht
  for (hte = local_access_ht->mgmt_hash_table_iterator_first(&htis); hte != NULL;
       hte = local_access_ht->mgmt_hash_table_iterator_next(&htis)) {
    name = (char *)local_access_ht->mgmt_hash_table_entry_key(hte);
    local_ht->mgmt_hash_table_delete(name);
  }
  // add our un-accessed LOCALs to the remote config
  for (hte = local_ht->mgmt_hash_table_iterator_first(&htis); hte != NULL; hte = local_ht->mgmt_hash_table_iterator_next(&htis)) {
    line = (char *)local_ht->mgmt_hash_table_entry_value(hte);
    rec_cfg_new->copyFrom(line, strlen(line));
    rec_cfg_new->copyFrom("\n", strlen("\n"));
  }
  // clean-up and return
  delete local_access_ht;
  return true;
}

/*
 * handleMultiCastFilePacket(...)
 *   Functions handles file packets that come over the mc channel. Its
 * basic job is to determine whether or not the timestamps/version
 * numbers that its peers are reporting are newer than the timestamps
 * and versions of the local config files. If there is a mis-match and
 * their files are newer then we initiate a request to get the newer file
 * and then roll that into our own config files.
 */
void
ClusterCom::handleMultiCastFilePacket(char *last, char *ip)
{
  char *line, file[1024];
  version_t ver, our_ver;
  int64_t tt;
  InkHashTableValue hash_value;
  bool file_update_failure;

  while ((line = strtok_r(NULL, "\n", &last))) {
    Rollback *rb;

    file_update_failure = false;
    // coverity[secure_coding]
    if (sscanf(line, "%1023s %d %" PRId64 "\n", file, &ver, &tt) != 3) {
      mgmt_elog(0, "[ClusterCom::handleMultiCastFilePacket] Invalid message-line(%d) '%s'\n", __LINE__, line);
      return;
    }

    if (configFiles->getRollbackObj(file, &rb)) {
      our_ver = rb->getCurrentVersion();
      if (ver > our_ver) { /* Their version is newer */
                           /*
                            * FIX: we have the timestamp from them as well, should we also
                            * figure that into this? or are version numbers sufficient?
                            *
                            * (mod > rb->versionTimeStamp(our_ver)
                            *
                            * When fixing this, watch out for the workaround put in place
                            * for INKqa08567.  File timestamps aren't sent around the
                            * cluster anymore.
                            */
        char message[1024];
        textBuffer *reply = new textBuffer(2048); /* Start with 2k file size */
        snprintf(message, sizeof(message), "file: %s %d", file, ver);

        /* Send request, read response, write new file. */
        if (!(sendReliableMessageReadTillClose(inet_addr(ip), message, strlen(message), reply)) || (reply->spaceUsed() <= 0)) {
          delete reply;
          return;
        }

        if (strstr(reply->bufPtr(), "file: failed")) {
          file_update_failure = true;
        }
        // INKqa08381: Special case for records.config; we only want
        // to syncronize CONFIG records from remote machine, not LOCAL
        // records; store our LOCAL records in a hash-table, and then
        // merge our LOCALs into the newly acquired remote config.
        if (!file_update_failure && (strcmp(file, "records.config") == 0)) {
          textBuffer *our_rec_cfg;
          char *our_rec_cfg_cp;
          textBuffer *reply_new;
          MgmtHashTable *our_locals_ht;

          if (rb->getVersion(our_ver, &our_rec_cfg) != OK_ROLLBACK) {
            file_update_failure = true;
          } else {
            our_locals_ht  = new MgmtHashTable("our_locals_ht", true, InkHashTableKeyType_String);
            our_rec_cfg_cp = ats_strdup(our_rec_cfg->bufPtr());
            extract_locals(our_locals_ht, our_rec_cfg_cp);
            reply_new = new textBuffer(reply->spaceUsed());
            if (!insert_locals(reply_new, reply, our_locals_ht)) {
              file_update_failure = true;
              delete reply_new;
              reply_new = 0;
            } else {
              delete reply;
              reply = reply_new;
            }
            ats_free(our_rec_cfg_cp);
            delete our_rec_cfg;
            delete our_locals_ht;
          }
        }

        if (!file_update_failure && (rb->updateVersion(reply, our_ver, ver, true, false) != OK_ROLLBACK)) {
          file_update_failure = true;
        }

        if (file_update_failure) {
          mgmt_elog(0, "[ClusterCom::handleMultiCastFilePacket] Update failed\n");
        } else {
          mgmt_log(stderr, "[ClusterCom::handleMultiCastFilePacket] "
                           "Updated '%s' o: %d n: %d\n",
                   file, our_ver, ver);
        }

        delete reply;
      }
    } else {
      mgmt_elog(0, "[ClusterCom::handleMultiCastFilePacket] Unknown file seen: '%s'\n", file);
    }
  }

  ink_mutex_acquire(&(mutex)); /* Grab cluster lock to access hash table */
  if (ink_hash_table_lookup(peers, (InkHashTableKey)ip, &hash_value) != 0) {
    ((ClusterPeerInfo *)hash_value)->manager_idle_ticks = time(NULL);
    if (((ClusterPeerInfo *)hash_value)->manager_alive < 0) {
      Note("marking manager on node %s as up", ip);
    }
    ((ClusterPeerInfo *)hash_value)->manager_alive = 1;
  }
  ink_mutex_release(&(mutex)); /* Release cluster lock */

  return;
} /* End ClusterCom::handleMultiCastFilePacket */

/*
 * handleMultiCastAlarmPacket(...)
 *   Function receives incoming alarm messages and updates the alarms class.
 */
void
ClusterCom::handleMultiCastAlarmPacket(char *last, char *ip)
{
  char *line;

  /* Allows us to expire stale alarms */
  lmgmt->alarm_keeper->resetSeenFlag(ip);
  while ((line = strtok_r(NULL, "\n", &last))) {
    int ccons;
    alarm_t a;
    char *desc;

    if (strcmp(line, "alarm: none") == 0) {
      break;
    }
    // both types have a finite size
    // coverity[secure_coding]
    if (sscanf(line, "alarm: %d %n", &a, &ccons) != 1) {
      mgmt_elog(0, "[ClusterCom::handleMultiCastAlarmPacket] Invalid message-line(%d) '%s'\n", __LINE__, line);
      return;
    }

    desc = &line[ccons];

    /* Signalling will only re-issue if new */
    lmgmt->alarm_keeper->signalAlarm(a, desc, ip);
    Debug("ccom", "[ClusterCom::handleMultiCastAlarmPacket] Alarm: ip: '%s' '%s'\n", ip, line);
  }
  lmgmt->alarm_keeper->clearUnSeen(ip); /* Purge expired alarms */
  return;
} /* End ClusterCom::handleMultiCastAlarmPacket */

/*
 * handleMultiCastVMapPacket(...)
 *   Handles incoming reports from peers about which virtual interfaces
 * they are servicing. This then updates the VMap class to indicate who
 * is holding what.
 */
void
ClusterCom::handleMultiCastVMapPacket(char *last, char *ip)
{
  char *line;
  InkHashTableValue hash_value;

  ink_mutex_acquire(&(mutex));           /* VMap class uses cluster mutex */
  lmgmt->virt_map->rl_resetSeenFlag(ip); /* Ala alarms */
  ink_mutex_release(&(mutex));

  while ((line = strtok_r(NULL, "\n", &last))) {
    char vaddr[80];

    if (strcmp(line, "virt: none") == 0) {
      break;
    }
    // coverity[secure_coding]
    if (sscanf(line, "virt: %79s", vaddr) != 1) {
      mgmt_elog(0, "[ClusterCom::handleMultiCastVMapPacket] Invalid message-line(%d) '%s'\n", __LINE__, line);
      return;
    }

    ink_mutex_acquire(&(mutex));
    lmgmt->virt_map->rl_map(vaddr, ip); /* Record this nodes map */
    ink_mutex_release(&(mutex));
  }

  ink_mutex_acquire(&(mutex));
  if (ink_hash_table_lookup(peers, ip, &hash_value) != 0) {
    ((ClusterPeerInfo *)hash_value)->num_virt_addrs = lmgmt->virt_map->rl_clearUnSeen(ip);
  }
  ink_mutex_release(&(mutex));
  return;
} /* End ClusterCom::handleMultiCastVMapPacket */

/*
 * sendSharedData
 *   Function serves as aggregator of NODE data to be shared with the
 * cluster. It collects the data, formats the message, and finally broadcasts
 * the message.
 */
bool
ClusterCom::sendSharedData(bool send_proxy_heart_beat)
{
  char message[61440], addr[80];
  struct in_addr resolved_addr;
  time_t now;

  if (cluster_type == NO_CLUSTER) {
    return true;
  }

  now = time(NULL);
  if (now == -1) {
    // The time call failed
    last_shared_send = 0;
  } else {
    int time_since_last_send = now - last_shared_send;
    if (last_shared_send != 0 && time_since_last_send > peer_timeout) {
      Warning("multicast send timeout exceeded.  %d seconds since"
              " last send.",
              time_since_last_send);
    } else if (last_shared_send != 0 && time_since_last_send < mc_send_interval) {
      return true;
    }
    last_shared_send = now;
  }

  /* Config Files Message */
  memset(message, 0, 61440);
  constructSharedFilePacket(message, 61440);
  sendOutgoingMessage(message, strlen(message));

  /* Alarm Message */
  memset(message, 0, 61440);
  resolved_addr.s_addr = our_ip;
  ink_strlcpy(addr, inet_ntoa(resolved_addr), sizeof(addr));
  lmgmt->alarm_keeper->constructAlarmMessage(appVersionInfo, addr, message, 61440);
  sendOutgoingMessage(message, strlen(message));

  /*
   * Send alarms and file config information always, if we are not running
   * a proxy we should not send a stat packet(no stats to report since no
   * proxy is running). The stat packet is used to hearbeat that the node
   * is alive, since this packet is not sent the master peer will not assign
   * us ip addresses and if we are the master peer someone will take over our
   * duties.
   */
  if (!send_proxy_heart_beat) {
    return true;
  }

  /* Stat Message */
  memset(message, 0, 61440);
  constructSharedStatPacket(message, 61440);
  sendOutgoingMessage(message, strlen(message));

  /* VMap Message */
  memset(message, 0, 61440);
  lmgmt->virt_map->lt_constructVMapMessage(addr, message, 61440);
  sendOutgoingMessage(message, strlen(message));

  return true;
} /* End ClusterCom::sendSharedData */

/*
 * constructSharedGenericPacket(...)
 *   A generic packet builder that can construct config or stat
 * broadcast packets. Basically the smarts to read the records values.
 */
void
ClusterCom::constructSharedGenericPacket(char *message, int max, RecT packet_type)
{
  int running_sum = 0; /* Make sure we never go over max */
  char tmp[1024];
  struct in_addr resolved_addr;

  /* Insert the standard packet header */
  resolved_addr.s_addr = our_ip;
  running_sum          = constructSharedPacketHeader(appVersionInfo, message, inet_ntoa(resolved_addr), max);

  if (packet_type == RECT_NODE) {
    ink_strlcpy(&message[running_sum], "type: stat\n", (max - running_sum));
    running_sum += strlen("type: stat\n");
    ink_release_assert(running_sum < max);
  } else {
    mgmt_elog(0, "[ClusterCom::constructSharedGenericPacket] Illegal type seen '%d'\n", packet_type);
    return;
  }

  if (sys_name[0]) {
    snprintf(tmp, sizeof(tmp), "os: %s\n", sys_name);
  } else {
    snprintf(tmp, sizeof(tmp), "os: unknown\n");
  }
  ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
  running_sum += strlen(tmp);
  ink_release_assert(running_sum < max);

  if (sys_release[0]) {
    snprintf(tmp, sizeof(tmp), "rel: %s\n", sys_release);
  } else {
    snprintf(tmp, sizeof(tmp), "rel: unknown\n");
  }
  ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
  running_sum += strlen(tmp);
  ink_release_assert(running_sum < max);

  snprintf(tmp, sizeof(tmp), "hostname: %s\n", our_host);
  ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
  running_sum += strlen(tmp);
  ink_release_assert(running_sum < max);

  snprintf(tmp, sizeof(tmp), "port: %d\n", cluster_port);
  ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
  running_sum += strlen(tmp);
  ink_release_assert(running_sum < max);

  snprintf(tmp, sizeof(tmp), "ccomport: %d\n", reliable_server_port);
  ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
  running_sum += strlen(tmp);
  ink_release_assert(running_sum < max);

  /* Current time stamp, for xntp like synching */
  if (time(NULL) > 0) {
    snprintf(tmp, sizeof(tmp), "time: %" PRId64 "\n", (int64_t)time(NULL));
    ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
    running_sum += strlen(tmp);
  } else {
    mgmt_elog(stderr, errno, "[ClusterCom::constructSharedPacket] time failed\n");
  }
  ink_release_assert(running_sum < max);

  int cnt = 0;
  for (int j = 0; j < g_num_records; j++) {
    RecRecord *rec = &(g_records[j]);

    if (rec->rec_type == RECT_NODE) {
      switch (rec->data_type) {
      case RECD_COUNTER:
        snprintf(tmp, sizeof(tmp), "%d:%d: %" PRId64 "\n", cnt, rec->data_type, rec->data.rec_counter);
        ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
        running_sum += strlen(tmp);
        break;
      case RECD_INT:
        snprintf(tmp, sizeof(tmp), "%d:%d: %" PRId64 "\n", cnt, rec->data_type, rec->data.rec_int);
        ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
        running_sum += strlen(tmp);
        break;
      case RECD_FLOAT:
        snprintf(tmp, sizeof(tmp), "%d:%d: %f\n", cnt, rec->data_type, rec->data.rec_float);
        ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
        running_sum += strlen(tmp);
        break;
      case RECD_STRING:
        if (rec->data.rec_string) {
          snprintf(tmp, sizeof(tmp), "%d:%d: %s\n", cnt, rec->data_type, rec->data.rec_string);
        } else {
          snprintf(tmp, sizeof(tmp), "%d:%d: NULL\n", cnt, rec->data_type);
        }
        ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
        running_sum += strlen(tmp);
        break;
      default:
        break;
      }
      ++cnt;
    }
    ink_release_assert(running_sum < max);
  }

  return;
} /* End ClusterCom::constructSharedGenericPacket */

void
ClusterCom::constructSharedStatPacket(char *message, int max)
{
  constructSharedGenericPacket(message, max, RECT_NODE);
  return;
} /* End ClusterCom::constructSharedStatPacket */

/* static int constructSharedPacketHeader(...)
 *   Each multicast packet needs to have the following
 *   header info.  Ip, Cluster Name, TS Version.  This function
 *   Inserts that information.  Returns the nubmer of bytes inserted
 */
int
ClusterCom::constructSharedPacketHeader(const AppVersionInfo &version, char *message, char *ip, int max)
{
  int running_sum = 0;

  /* Insert the IP Address of this node */
  /* Insert the name of this cluster for cluster-identification of mc packets */
  /* Insert the Traffic Server verison */
  snprintf(message, max, "ip: %s\ncluster: %s\ntsver: %s\n", ip, lmgmt->proxy_name, version.VersionStr);
  running_sum = strlen(message);
  ink_release_assert(running_sum < max);

  return running_sum;
} /* End ClusterCom::constructSharedPacketHeader */

/*
 * constructSharedFilePacket(...)
 *   Foreach of the config files we are holding build a packet that
 * can be used to share the current version and time stamp of the
 * files, so others can tell if ours our newer.
 */
void
ClusterCom::constructSharedFilePacket(char *message, int max)
{
  int running_sum = 0;
  char tmp[1024], *files, *line, *last;
  struct in_addr resolved_addr;
  textBuffer *buff;

  /* Insert the standard packet header */
  resolved_addr.s_addr = our_ip;
  running_sum          = constructSharedPacketHeader(appVersionInfo, message, inet_ntoa(resolved_addr), max);

  ink_strlcpy(&message[running_sum], "type: files\n", (max - running_sum));
  running_sum += strlen("type: files\n");
  ink_release_assert(running_sum < max);

  buff  = configFiles->filesManaged();
  files = buff->bufPtr();
  line  = strtok_r(files, "\n", &last);
  if (line == NULL) {
    delete buff;
    return;
  }

  /* Loop over the known files building the packet */
  do {
    Rollback *rb;

    /* Some files are local only */
    if (strcmp(line, "storage.config") == 0) {
      continue;
    }

    if (configFiles->getRollbackObj(line, &rb)) {
      version_t ver = rb->getCurrentVersion();

      // Workaround INKqa08567: Calling versionTimeStamp here causes
      // traffic_manager to periodically switch to be a root user (as
      // it needs to stat() some snmp related configuration files).
      // This caused a race-condition to occur if some configuration
      // were being written to disk (files would be written as owned
      // by root instead of being owned by inktomi... thus causing
      // general permissions badness everywhere).  Because the
      // timestamp isn't actually used by the peer cluster nodes to
      // determine which config files are newer, the workaround is to
      // remove the unnecessary call to versionTimeStamp().

      // time_t mod = rb->versionTimeStamp(ver);
      time_t mod = 0;

      snprintf(tmp, sizeof(tmp), "%s %d %" PRId64 "\n", line, ver, (int64_t)mod);
      ink_strlcpy(&message[running_sum], tmp, (max - running_sum));
      running_sum += strlen(tmp);
      ink_release_assert(running_sum < max);
    } else {
      mgmt_elog(0, "[ClusterCom::constructSharedFilePacket] Invalid base name? '%s'\n", line);
    }
  } while ((line = strtok_r(NULL, "\n", &last)));

  delete buff;
  return;
} /* End ClusterCom::constructSharedFilePacket */

/*
 * estabilishChannels(...)
 *   Sets up the multi-cast and reliable tcp channels for cluster
 * communication. But only if clustering is enabled in some shape.
 */
void
ClusterCom::establishChannels()
{
  int one = 1;
  struct sockaddr_in serv_addr;

  if (cluster_type != NO_CLUSTER) {
    establishBroadcastChannel();
    establishReceiveChannel();

    if (reliable_server_port > 0) {
      /* Setup reliable connection, for large config changes */
      if ((reliable_server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        mgmt_fatal(errno, "[ClusterCom::establishChannels] Unable to create socket\n");
      }
      if (fcntl(reliable_server_fd, F_SETFD, 1) < 0) {
        mgmt_fatal(errno, "[ClusterCom::establishChannels] Unable to set close-on-exec.\n");
      }

      if (setsockopt(reliable_server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int)) < 0) {
        mgmt_fatal(errno, "[ClusterCom::establishChannels] Unable to set socket options.\n");
      }

      memset(&serv_addr, 0, sizeof(serv_addr));
      serv_addr.sin_family      = AF_INET;
      serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      serv_addr.sin_port        = htons(reliable_server_port);

      if ((bind(reliable_server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0) {
        mgmt_fatal(errno, "[ClusterCom::establishChannels] Unable to bind socket (port:%d)\n", reliable_server_port);
      }

      if ((listen(reliable_server_fd, 10)) < 0) {
        mgmt_fatal(errno, "[ClusterCom::establishChannels] Unable to listen on socket\n");
      }
    }
  }

  Debug("ccom", "[ClusterCom::establishChannels] Channels setup\n");
  init = true;
  return;
}

/*
 * establishBroadcastChannel()
 *   Setup our multicast channel for broadcasting.
 */
void
ClusterCom::establishBroadcastChannel(void)
{
  if ((broadcast_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    mgmt_fatal(errno, "[ClusterCom::establishBroadcastChannel] Unable to open socket.\n");
  }

  if (fcntl(broadcast_fd, F_SETFD, 1) < 0) {
    mgmt_fatal(errno, "[ClusterCom::establishBroadcastChannel] Unable to set close-on-exec.\n");
  }

  int one = 1;
  if (setsockopt(broadcast_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one)) < 0) {
    mgmt_fatal(errno, "[ClusterCom::establishBroadcastChannel] Unable to set socket options.\n");
  }

  memset(&broadcast_addr, 0, sizeof(broadcast_addr));
  broadcast_addr.sin_family      = AF_INET;
  broadcast_addr.sin_addr.s_addr = inet_addr(mc_group);
  broadcast_addr.sin_port        = htons(mc_port);

  u_char ttl = mc_ttl, loop = 0;

  /* Set ttl(max forwards), 1 should be default(same subnetwork). */
  if (setsockopt(broadcast_fd, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) < 0) {
    mgmt_fatal(errno, "[ClusterCom::establishBroadcastChannel] Unable to setsocketopt, ttl\n");
  }

  /* Disable broadcast loopback, that is broadcasting to self */
  if (setsockopt(broadcast_fd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof(loop)) < 0) {
    mgmt_fatal(errno, "[ClusterCom::establishBroadcastChannel] Unable to disable loopback\n");
  }

  return;
} /* End ClusterCom::establishBroadcastChannel */

/*
 * establishReceiveChannel()
 *   Setup our multicast channel for receiving incoming broadcasts
 * from other peers.
 */

int
ClusterCom::establishReceiveChannel(int fatal_on_error)
{
  if ((receive_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    if (!fatal_on_error) {
      Debug("ccom", "establishReceiveChannel: Unable to open socket");
      return 1;
    }
    mgmt_fatal(errno, "[ClusterCom::establishReceiveChannel] Unable to open socket\n");
  }

  if (fcntl(receive_fd, F_SETFD, 1) < 0) {
    if (!fatal_on_error) {
      close(receive_fd);
      receive_fd = -1;
      Debug("ccom", "establishReceiveChannel: Unable to set close-on-exec");
      return 1;
    }
    mgmt_fatal(errno, "[ClusterCom::establishReceiveChannel] Unable to set close-on-exec.\n");
  }

  int one = 1;
  if (setsockopt(receive_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int)) < 0) {
    if (!fatal_on_error) {
      close(receive_fd);
      receive_fd = -1;
      Debug("ccom", "establishReceiveChannel: Unable to set socket to reuse addr");
      return 1;
    }
    mgmt_fatal(errno, "[ClusterCom::establishReceiveChannel] Unable to set socket to reuse addr.\n");
  }

  memset(&receive_addr, 0, sizeof(receive_addr));
  receive_addr.sin_family      = AF_INET;
  receive_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  receive_addr.sin_port        = htons(mc_port);

  if (bind(receive_fd, (struct sockaddr *)&receive_addr, sizeof(receive_addr)) < 0) {
    if (!fatal_on_error) {
      close(receive_fd);
      receive_fd = -1;
      Debug("ccom", "establishReceiveChannel: Unable to bind to socket, port %d", mc_port);
      return 1;
    }
    mgmt_fatal(errno, "[ClusterCom::establishReceiveChannel] Unable to bind to socket, port %d\n", mc_port);
  }
  /* Add ourselves to the group */
  struct ip_mreq mc_request;
  mc_request.imr_multiaddr.s_addr = inet_addr(mc_group);
  mc_request.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(receive_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mc_request, sizeof(mc_request)) < 0) {
    if (!fatal_on_error) {
      close(receive_fd);
      receive_fd = -1;
      Debug("ccom", "establishReceiveChannel: Can't add ourselves to multicast group %s", mc_group);

      return 1;
    }
    mgmt_fatal(errno, "[ClusterCom::establishReceiveChannel] Can't add ourselves to multicast group %s\n", mc_group);
  }

  return 0;
} /* End ClusterCom::establishReceiveChannel */

/*
 * sendOutgoingMessage
 *   Function basically writes a message to the broadcast_fd, it is blocking,
 * but since the drainer thread is constantly draining the network for all
 * local managers it should not block for very long.
 */
bool
ClusterCom::sendOutgoingMessage(char *buf, int len)
{
  if (mgmt_sendto(broadcast_fd, buf, len, 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr)) < 0) {
    mgmt_elog(errno, "[ClusterCom::sendOutgoingMessage] Message send failed\n");
    return false;
  }
  return true;
} /* End ClusterCom::sendOutgoingMessage */

bool
ClusterCom::sendClusterMessage(int msg_type, const char *args)
{
  bool ret       = true, tmp_ret;
  char msg[1124] = {0};
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  switch (msg_type) {
  case CLUSTER_MSG_SHUTDOWN_MANAGER:
    ink_strlcpy(msg, "cmd: shutdown_manager", sizeof(msg));
    break;
  case CLUSTER_MSG_SHUTDOWN_PROCESS:
    ink_strlcpy(msg, "cmd: shutdown_process", sizeof(msg));
    break;
  case CLUSTER_MSG_RESTART_PROCESS:
    ink_strlcpy(msg, "cmd: restart_process", sizeof(msg));
    break;
  case CLUSTER_MSG_BOUNCE_PROCESS:
    ink_strlcpy(msg, "cmd: bounce_process", sizeof(msg));
    break;
  case CLUSTER_MSG_CLEAR_STATS:
    if (args) {
      snprintf(msg, sizeof(msg), "cmd: clear_stats %1023s\n", args);
    } else {
      ink_strlcpy(msg, "cmd: clear_stats", sizeof(msg));
    }
    break;
  default:
    mgmt_log(stderr, "[ClusterCom::sendClusterMessage] Invalid message type '%d'\n", msg_type);
    return false;
  }

  ink_mutex_acquire(&(mutex)); /* Grab cluster lock */
  for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
    ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);

    tmp_ret = rl_sendReliableMessage(tmp->inet_address, msg, strlen(msg));
    if (tmp->num_virt_addrs != -1) {
      /* Only change return val if he is not dead, if dead manager could be up. */
      ret = tmp_ret;
    }
  }
  ink_mutex_release(&(mutex));

  switch (msg_type) {
  case CLUSTER_MSG_SHUTDOWN_MANAGER:
    lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_RESTART;
    break;
  case CLUSTER_MSG_SHUTDOWN_PROCESS:
    lmgmt->processShutdown();
    break;
  case CLUSTER_MSG_RESTART_PROCESS:
    lmgmt->processRestart();
    break;
  case CLUSTER_MSG_BOUNCE_PROCESS:
    lmgmt->processBounce();
    break;
  case CLUSTER_MSG_CLEAR_STATS:
    lmgmt->clearStats(args);
    break;
  }

  return ret;
} /* End ClusterCom::sendClusterMessage */

bool
ClusterCom::sendReliableMessage(unsigned long addr, char *buf, int len)
{
  bool ret = false;
  ink_mutex_acquire(&mutex);
  ret = rl_sendReliableMessage(addr, buf, len);
  ink_mutex_release(&mutex);
  return ret;
} /* End ClusterCom::sendReliableMessage */

/*
 * rl_sendReliableMessage(...)
 *   Used to send a string across the reliable fd.
 */
bool
ClusterCom::rl_sendReliableMessage(unsigned long addr, const char *buf, int len)
{
  int fd, cport;
  char string_addr[80];
  struct sockaddr_in serv_addr;
  struct in_addr address;
  InkHashTableValue hash_value;

  address.s_addr = addr;

  ink_strlcpy(string_addr, inet_ntoa(address), sizeof(string_addr));
  if (ink_hash_table_lookup(peers, string_addr, &hash_value) == 0) {
    return false;
  }
  cport = ((ClusterPeerInfo *)hash_value)->ccom_port;

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_addr.s_addr = addr;
  serv_addr.sin_port        = htons(cport);

  if ((fd = mgmt_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    mgmt_elog(errno, "[ClusterCom::rl_sendReliableMessage] Unable to create socket\n");
    return false;
  }
  if (fcntl(fd, F_SETFD, 1) < 0) {
    mgmt_log("[ClusterCom::rl_sendReliableMessage] Unable to set close-on-exec.\n");
    close(fd);
    return false;
  }

  if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    mgmt_elog(errno, "[ClusterCom::rl_sendReliableMessage] Unable to connect to peer\n");
    close_socket(fd);
    return false;
  }

  if (mgmt_writeline(fd, buf, len) != 0) {
    mgmt_elog(stderr, errno, "[ClusterCom::rl_sendReliableMessage] Write failed\n");
    close_socket(fd);
    return false;
  }
  close_socket(fd);
  return true;
} /* End ClusterCom::rl_sendReliableMessage */

/*
 * sendReliableMessage(...)
 *   Used to send a string across the reliable fd.
 */
bool
ClusterCom::sendReliableMessage(unsigned long addr, char *buf, int len, char *reply, int len2, bool take_lock)
{
  int fd, cport;
  char string_addr[80];
  struct sockaddr_in serv_addr;
  struct in_addr address;
  InkHashTableValue hash_value;

  address.s_addr = addr;
  if (take_lock) {
    ink_mutex_acquire(&mutex);
  }
  ink_strlcpy(string_addr, inet_ntoa(address), sizeof(string_addr));
  if (ink_hash_table_lookup(peers, string_addr, &hash_value) == 0) {
    if (take_lock) {
      ink_mutex_release(&mutex);
    }
    return false;
  }
  cport = ((ClusterPeerInfo *)hash_value)->ccom_port;

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_addr.s_addr = addr;
  serv_addr.sin_port        = htons(cport);

  if ((fd = mgmt_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessage] Unable to create socket\n");
    if (take_lock) {
      ink_mutex_release(&mutex);
    }
    return false;
  }
  if (fcntl(fd, F_SETFD, 1) < 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessage] Unable to set close-on-exec.\n");
    if (take_lock) {
      ink_mutex_release(&mutex);
    }
    close(fd);
    return false;
  }

  if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessage] Unable to connect to peer\n");
    if (take_lock) {
      ink_mutex_release(&mutex);
    }
    close_socket(fd);
    return false;
  }

  if (mgmt_writeline(fd, buf, len) != 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessage] Write failed\n");
    if (take_lock) {
      ink_mutex_release(&mutex);
    }
    close_socket(fd);
    return false;
  }

  if (mgmt_readline(fd, reply, len2) == -1) {
    mgmt_elog(stderr, errno, "[ClusterCom::sendReliableMessage] Read failed\n");
    perror("ClusterCom::sendReliableMessage");
    reply[0] = '\0';
    if (take_lock) {
      ink_mutex_release(&mutex);
    }
    close_socket(fd);
    return false;
  }

  close_socket(fd);
  if (take_lock) {
    ink_mutex_release(&mutex);
  }
  return true;
} /* End ClusterCom::sendReliableMessage */

/*
 * sendReliableMessage(...)
 *   Used to send a string across the reliable fd.
 */
bool
ClusterCom::sendReliableMessageReadTillClose(unsigned long addr, char *buf, int len, textBuffer *reply)
{
  int fd, cport, res;
  char string_addr[80], tmp_reply[1024];
  struct sockaddr_in serv_addr;
  struct in_addr address;
  InkHashTableValue hash_value;

  address.s_addr = addr;
  ink_mutex_acquire(&mutex);
  ink_strlcpy(string_addr, inet_ntoa(address), sizeof(string_addr));
  if (ink_hash_table_lookup(peers, string_addr, &hash_value) == 0) {
    ink_mutex_release(&mutex);
    return false;
  }
  cport = ((ClusterPeerInfo *)hash_value)->ccom_port;

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family      = AF_INET;
  serv_addr.sin_addr.s_addr = addr;
  serv_addr.sin_port        = htons(cport);

  if ((fd = mgmt_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessageReadTillClose] Unable create sock\n");
    ink_mutex_release(&mutex);
    return false;
  }
  if (fcntl(fd, F_SETFD, 1) < 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessageReadTillClose] Unable to set close-on-exec.\n");
    ink_mutex_release(&mutex);
    close(fd);
    return false;
  }

  if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessageReadTillClose] Unable to connect\n");
    ink_mutex_release(&mutex);
    close_socket(fd);
    return false;
  }

  if (mgmt_writeline(fd, buf, len) != 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessageReadTillClose] Write failed\n");
    ink_mutex_release(&mutex);
    close_socket(fd);
    return false;
  } else {
    Debug("ccom", "[ClusterCom::sendReliableMessageREadTillClose] Sent '%s' len: %d on fd: %d\n", buf, len, fd);
  }

  memset(tmp_reply, 0, 1024);
  while ((res = read_socket(fd, tmp_reply, 1022)) > 0) {
    if (tmp_reply[0] == (char)EOF) {
      break;
    }
    reply->copyFrom(tmp_reply, strlen(tmp_reply));
    memset(tmp_reply, 0, 1024);
  }

  if (res < 0) {
    mgmt_elog(errno, "[ClusterCom::sendReliableMessageReadTillClose] Read failed\n");
    perror("ClusterCom::sendReliableMessageReadTillClose");
    ink_mutex_release(&mutex);
    close_socket(fd);
    return false;
  }

  close_socket(fd);
  ink_mutex_release(&mutex);
  return true;
} /* End ClusterCom::sendReliableMessageReadTillClose */

/*
 * receiveIncomingMessage
 *   This function reads from the incoming channel. It is blocking,
 * this is ok since the channel is drained by an independent thread.
 */
int
ClusterCom::receiveIncomingMessage(char *buf, int max)
{
  int nbytes = 0, addr_len = sizeof(receive_addr);

  if ((nbytes = recvfrom(receive_fd, buf, max, 0, (struct sockaddr *)&receive_addr, (socklen_t *)&addr_len)) < 0) {
    mgmt_elog(stderr, errno, "[ClusterCom::receiveIncomingMessage] Receive failed\n");
  }
  return nbytes;
} /* End ClusterCom::processIncomingMessages */

/*
 * isMaster()
 *   Function checks known hosts and decides whether this local manager is
 * the current cluster master.
 */
bool
ClusterCom::isMaster()
{
  bool init_flag    = false;
  unsigned long min = 0;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
    ClusterPeerInfo *pinfo = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);

    if (!pinfo || pinfo->num_virt_addrs == -1) {
      continue;
    } else if (!init_flag) {
      init_flag = true;
      min       = pinfo->inet_address;
    } else if (min > pinfo->inet_address) {
      min = pinfo->inet_address;
    }
  }

  if (our_ip == min || !init_flag || our_ip < min) {
    return true;
  }
  return false;
} /* End ClusterCom::isMaster */

/*
 * lowestPeer()
 *   Function finds the peer with the lowest number of current virtual
 * interfaces. It returns the ip and sets num to the no. of connections.
 */
unsigned long
ClusterCom::lowestPeer(int *no)
{
  bool flag            = true;
  int naddrs           = -1;
  unsigned long min_ip = 0;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
    ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);
    if (tmp->num_virt_addrs == -1) {
      continue;
    } else if (flag) {
      flag   = false;
      min_ip = tmp->inet_address;
      naddrs = tmp->num_virt_addrs;
    } else if (naddrs > tmp->num_virt_addrs) {
      min_ip = tmp->inet_address;
      naddrs = tmp->num_virt_addrs;
    } else if (naddrs == tmp->num_virt_addrs && min_ip > tmp->inet_address) {
      min_ip = tmp->inet_address;
      naddrs = tmp->num_virt_addrs;
    }
  }
  *no = naddrs;
  return min_ip;
} /* End ClusterCom::lowestPeer */

void
ClusterCom::logClusterMismatch(const char *ip, ClusterMismatch type, char *data)
{
  void *value;
  ClusterMismatch stored_type;

  // Check to see if we have have already logged a message of time type
  //   for this node
  if (ink_hash_table_lookup(mismatchLog, ip, &value)) {
    stored_type = (ClusterMismatch)(long)value;

    if (type == stored_type) {
      return;
    } else {
      // The message logged is of a different type so delete the entry
      ink_hash_table_delete(mismatchLog, ip);
    }
  }
  // Log the message and store the that we've logged it in
  //   our hash table
  switch (type) {
  case TS_NAME_MISMATCH:
    mgmt_log(stderr, "[ClusterCom::logClusterMismatch] Found node with ip %s.  Ignoring"
                     " since it is part of cluster %s\n",
             ip, data);
    break;
  case TS_VER_MISMATCH:
    mgmt_log(stderr, "[ClusterCom::logClusterMismatch] Found node with ip %s.  Ignoring"
                     " since it is version %s (our version: %s)\n",
             ip, data, appVersionInfo.VersionStr);
    break;
  default:
    ink_assert(0);
  }

  ink_hash_table_insert(mismatchLog, ip, (void *)type);
}

/*
 * highestPeer()
 *   Function finds the peer with the highest number of current virtual
 * interfaces. It returns the ip and sets num to the no. of connections.
 */
unsigned long
ClusterCom::highestPeer(int *no)
{
  bool flag            = true;
  int naddrs           = -1;
  unsigned long max_ip = 0;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  for (entry = ink_hash_table_iterator_first(peers, &iterator_state); entry != NULL;
       entry = ink_hash_table_iterator_next(peers, &iterator_state)) {
    ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(peers, entry);
    if (tmp->num_virt_addrs == -1) {
      continue;
    } else if (flag) {
      flag   = false;
      max_ip = tmp->inet_address;
      naddrs = tmp->num_virt_addrs;
    } else if (naddrs < tmp->num_virt_addrs) {
      max_ip = tmp->inet_address;
      naddrs = tmp->num_virt_addrs;
    } else if (naddrs == tmp->num_virt_addrs && max_ip > tmp->inet_address) {
      max_ip = tmp->inet_address;
      naddrs = tmp->num_virt_addrs;
    }
  }
  *no = naddrs;

  return max_ip;
} /* End ClusterCom::highestPeer */

/*
 * checkBackDoor(...)
 *   Function checks for "backdoor" commands on the cluster port.
 */
bool
checkBackDoor(int req_fd, char *message)
{
  char reply[4096];

  if (strstr(message, "show_map")) {
    const char *tmp_msg;
    bool map_empty = true;
    InkHashTableEntry *entry;
    InkHashTableIteratorState iterator_state;

    ink_mutex_acquire(&(lmgmt->ccom->mutex));
    tmp_msg = "\nLocal Map (virtual-ip):\n-----------------------\n";
    mgmt_writeline(req_fd, tmp_msg, strlen(tmp_msg));
    for (entry = ink_hash_table_iterator_first(lmgmt->virt_map->our_map, &iterator_state); entry != NULL;
         entry = ink_hash_table_iterator_next(lmgmt->virt_map->our_map, &iterator_state)) {
      char *tmp = (char *)ink_hash_table_entry_key(lmgmt->virt_map->our_map, entry);
      mgmt_writeline(req_fd, tmp, strlen(tmp));
      map_empty = false;
    }

    if (map_empty) {
      tmp_msg = "(No interfaces mapped)";
      mgmt_writeline(req_fd, tmp_msg, strlen(tmp_msg));
    }

    map_empty = true;
    tmp_msg   = "\nPeer Map (virtual-ip real-ip):\n------------------------------\n";
    mgmt_writeline(req_fd, tmp_msg, strlen(tmp_msg));
    for (entry = ink_hash_table_iterator_first(lmgmt->virt_map->ext_map, &iterator_state); entry != NULL;
         entry = ink_hash_table_iterator_next(lmgmt->virt_map->ext_map, &iterator_state)) {
      char *tmp = (char *)ink_hash_table_entry_key(lmgmt->virt_map->ext_map, entry);
      mgmt_writeline(req_fd, tmp, strlen(tmp));
      map_empty = false;
    }

    if (map_empty) {
      tmp_msg = "(No interfaces mapped)\n";
      mgmt_writeline(req_fd, tmp_msg, strlen(tmp_msg));
    } else {
      tmp_msg = "\n\n";
      mgmt_writeline(req_fd, tmp_msg, strlen(tmp_msg));
    }
    ink_mutex_release(&(lmgmt->ccom->mutex));
    return true;
  } else if (strstr(message, "read ")) {
    char variable[1024];

    // coverity[secure_coding]
    if (sscanf(message, "read %s\n", variable) != 1) {
      mgmt_elog(0, "[ClusterCom::CBD] Invalid message-line(%d) '%s'\n", __LINE__, message);
      return false;
    }

    RecDataT stype = RECD_NULL;
    if (RecGetRecordDataType(variable, &stype) == REC_ERR_OKAY) {
      bool found  = false;
      int rep_len = 0;

      switch (stype) {
      case RECD_COUNTER:
      case RECD_INT: {
        int64_t val = (stype == RECD_COUNTER ? REC_readCounter(variable, &found) : REC_readInteger(variable, &found));
        if (found) {
          rep_len = snprintf(reply, sizeof(reply), "\nRecord '%s' Val: '%" PRId64 "'\n", variable, val);
        }
        break;
      }
      case RECD_FLOAT: {
        RecFloat val = REC_readFloat(variable, &found);
        if (found) {
          rep_len = snprintf(reply, sizeof(reply), "\nRecord '%s' Val: '%f'\n", variable, val);
        }
        break;
      }
      case RECD_STRING: {
        char *val = REC_readString(variable, &found);
        if (found) {
          rep_len = snprintf(reply, sizeof(reply), "\nRecord '%s' Val: '%s'\n", variable, val);
          ats_free(val);
        }
        break;
      }
      default:
        break;
      }
      if (found) {
        mgmt_writeline(req_fd, reply, rep_len);
      } else {
        mgmt_elog(0, "[checkBackDoor] record not found '%s'\n", variable);
      }
    } else {
      mgmt_elog(0, "[checkBackDoor] Unknown variable requested '%s'\n", variable);
    }
    return true;
  } else if (strstr(message, "write ")) {
    char variable[1024], value[1024];

    if (sscanf(message, "write %s %s", variable, value) != 2) {
      mgmt_elog(0, "[ClusterCom::CBD] Invalid message-line(%d) '%s'\n", __LINE__, message);
      return false;
    }
    // TODO: I think this is correct, it used to do lmgmt->record_data-> ...
    if (RecSetRecordConvert(variable, value, REC_SOURCE_EXPLICIT, true, false) == REC_ERR_OKAY) {
      ink_strlcpy(reply, "\nRecord Updated\n\n", sizeof(reply));
      mgmt_writeline(req_fd, reply, strlen(reply));
    } else {
      mgmt_elog(0, "[checkBackDoor] Assignment to unknown variable requested '%s'\n", variable);
    }
    return true;
  } else if (strstr(message, "peers")) {
    InkHashTableEntry *entry;
    InkHashTableIteratorState iterator_state;

    ink_mutex_acquire(&(lmgmt->ccom->mutex));

    for (entry = ink_hash_table_iterator_first(lmgmt->ccom->peers, &iterator_state); entry != NULL;
         entry = ink_hash_table_iterator_next(lmgmt->ccom->peers, &iterator_state)) {
      const char *tmp_msg;
      char ip_addr[80];
      struct in_addr addr;

      ClusterPeerInfo *tmp = (ClusterPeerInfo *)ink_hash_table_entry_value(lmgmt->ccom->peers, entry);

      tmp_msg = "---------------------------";
      mgmt_writeline(req_fd, tmp_msg, strlen(tmp_msg));

      addr.s_addr = tmp->inet_address;
      ink_strlcpy(ip_addr, inet_ntoa(addr), sizeof(ip_addr));
      snprintf(reply, sizeof(reply), "Peer: %s   naddrs: %d", ip_addr, tmp->num_virt_addrs);
      mgmt_writeline(req_fd, reply, strlen(reply));

      snprintf(reply, sizeof(reply), "Cluster Port(proxy): %d  RS Port(mgmt): %d", tmp->port, tmp->ccom_port);
      mgmt_writeline(req_fd, reply, strlen(reply));

      snprintf(reply, sizeof(reply),
               "Idle-Our-WC: %" PRId64 "   Peer-WC-Last-Time: %ld  Delta: %ld Mgmt-Idle: %" PRId64 " M-Alive: %d",
               (int64_t)tmp->idle_ticks, tmp->last_time_recorded, tmp->delta, (int64_t)tmp->manager_idle_ticks, tmp->manager_alive);
      mgmt_writeline(req_fd, reply, strlen(reply));

      tmp_msg = "---------------------------\n";
      mgmt_writeline(req_fd, tmp_msg, strlen(tmp_msg));
    }
    ink_mutex_release(&(lmgmt->ccom->mutex));
    return true;
  } else if (strstr(message, "dump: lm")) {
    ink_strlcpy(reply, "---------------------------", sizeof(reply));
    mgmt_writeline(req_fd, reply, strlen(reply));
    ink_strlcpy(reply, "Local Manager:\n", sizeof(reply));
    mgmt_writeline(req_fd, reply, strlen(reply));

    snprintf(reply, sizeof(reply), "\tproxy_running: %s", (lmgmt->proxy_running ? "true" : "false"));
    mgmt_writeline(req_fd, reply, strlen(reply));

    snprintf(reply, sizeof(reply), "\tproxy_started_at: %" PRId64 "", (int64_t)lmgmt->proxy_started_at);
    mgmt_writeline(req_fd, reply, strlen(reply));

    snprintf(reply, sizeof(reply), "\trun_proxy: %s", (lmgmt->run_proxy ? "true" : "false"));
    mgmt_writeline(req_fd, reply, strlen(reply));

    snprintf(reply, sizeof(reply), "\tproxy_launch_oustanding: %s", (lmgmt->proxy_launch_outstanding ? "true" : "false"));
    mgmt_writeline(req_fd, reply, strlen(reply));

    snprintf(reply, sizeof(reply), "\tmgmt_shutdown_outstanding: %s\n", (lmgmt->mgmt_shutdown_outstanding ? "true" : "false"));
    mgmt_writeline(req_fd, reply, strlen(reply));

// XXX: Again multiple code caused by misssing PID_T_FMT
// TODO: Was #if defined(solaris) && (!defined(_FILE_OFFSET_BITS) || _FILE_OFFSET_BITS != 64)
#if defined(solaris)
    snprintf(reply, sizeof(reply), "\twatched_process_fd: %d  watched_process_pid: %ld\n", lmgmt->watched_process_fd,
             (long int)lmgmt->watched_process_pid);
#else
    snprintf(reply, sizeof(reply), "\twatched_process_fd: %d  watched_process_pid: %d\n", lmgmt->watched_process_fd,
             lmgmt->watched_process_pid);
#endif
    mgmt_writeline(req_fd, reply, strlen(reply));

    ink_strlcpy(reply, "---------------------------\n", sizeof(reply));
    mgmt_writeline(req_fd, reply, strlen(reply));
    return true;
  } else if (strstr(message, "cluster: ")) {
    int msg_type;
    char *args = NULL;

    if (strstr(message, "cluster: shutdown_manager")) {
      msg_type = CLUSTER_MSG_SHUTDOWN_MANAGER;
    } else if (strstr(message, "cluster: shutdown_process")) {
      msg_type = CLUSTER_MSG_SHUTDOWN_PROCESS;
    } else if (strstr(message, "cluster: restart_process")) {
      msg_type = CLUSTER_MSG_RESTART_PROCESS;
    } else if (strstr(message, "cluster: bounce_process")) {
      msg_type = CLUSTER_MSG_BOUNCE_PROCESS;
    } else if (strstr(message, "cluster: clear_stats")) {
      if (strlen(message) > sizeof("cluster: clear_stats") + 1) {
        args = message + sizeof("cluster: clear_stats") + 1;
      }
      msg_type = CLUSTER_MSG_CLEAR_STATS;
    } else {
      return false;
    }
    lmgmt->ccom->sendClusterMessage(msg_type, args);
    return true;
  }
  return false;
} /* End checkBackDoor */
