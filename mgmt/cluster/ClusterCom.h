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
 * ClusterCom.h
 *   Cluster communications class. Wrapper around communication
 * channel used. This class is basically another agent in that it
 * synthezises out bound data and collects incoming(user in the case
 * of other agents, other nodes in this case) data.
 *
 * A thread is spun up at runtime to drain the incoming channel, and
 * integrate it with other node data. Out bound is done by the main
 * mgmt thread. This scheme should insure that the read buffer is always
 * drained.
 *
 * $Date: 2007-10-05 16:56:45 $
 *
 *
 */

#ifndef _CLUSTER_COM_H
#define _CLUSTER_COM_H

#include "ts/ink_platform.h"
#include "P_RecDefs.h"
#include "ts/I_Version.h"
#include "Rollback.h"

class FileManager;

#define CLUSTER_MSG_SHUTDOWN_MANAGER 1000
#define CLUSTER_MSG_SHUTDOWN_PROCESS 1001
#define CLUSTER_MSG_RESTART_PROCESS 1002
#define CLUSTER_MSG_BOUNCE_PROCESS 1003
#define CLUSTER_MSG_CLEAR_STATS 1004

#define MAX_MC_GROUP_LEN 20
#define MAX_NODE_SYSINFO_STRING 32

#define CLUSTER_CONFIG_FILE_BLURB                                                                                                  \
  "# Cluster Configuration file\n#\n# This file is machine generated and machine parsed.\n# Please do not change this file by "    \
  "hand.\n#\n# This file designates the machines which make up the cluster\n# proper.  Data and load are distributed among these " \
  "machines.\n#\n############################################################################\n# Number\n# IP:Port \n# "           \
  "...\n############################################################################\n# Number = { 0, 1 ... } where 0 is a "       \
  "stand-alone proxy\n# IP:Port = IP address: cluster accept port number\n#\n# Example 1: stand-alone proxy\n# 0\n#\n# Example "   \
  "2: 3 machines\n# 3\n# 127.1.2.3:83\n# 127.1.2.4:83\n# 127.1.2.5:83\n#\n"

enum MgmtClusterType {
  CLUSTER_INVALID = 0,
  FULL_CLUSTER,
  MGMT_CLUSTER,
  NO_CLUSTER,
};

enum ClusterMismatch {
  TS_NAME_MISMATCH,
  TS_VER_MISMATCH,
};

typedef struct _cluster_peer_info {
  unsigned long inet_address; /* IP addr of node */
  int port;                   /* Cluster port */
  int ccom_port;              /* CCom reliable port */
  time_t idle_ticks;          /* Number of ticks since last heard */
  time_t manager_idle_ticks;  /* Time last manager heartbeat received */
  int manager_alive;

  long last_time_recorded;
  long delta;

  int num_virt_addrs;

  RecRecords node_rec_data;

} ClusterPeerInfo;

class ClusterCom
{
public:
  ClusterCom(unsigned long oip, char *hname, int mcport, char *group, int rsport, char *p);
  ~ClusterCom(){};

  void checkPeers(time_t *ticker);
  void generateClusterDelta(void);
  void handleMultiCastMessage(char *message);
  void handleMultiCastFilePacket(char *last, char *from_ip);
  void handleMultiCastStatPacket(char *last, ClusterPeerInfo *peer);
  void handleMultiCastAlarmPacket(char *last, char *from_ip);
  void handleMultiCastVMapPacket(char *last, char *from_ip);

  void establishChannels();
  void establishBroadcastChannel();
  int establishReceiveChannel(int fatal_on_error = 1);

  bool sendSharedData(bool send_proxy_heart_beat = true);
  void constructSharedGenericPacket(char *message, int max, RecT packet_type);
  void constructSharedStatPacket(char *message, int max);
  void constructSharedFilePacket(char *message, int max);

  static int constructSharedPacketHeader(const AppVersionInfo &version, char *message, char *ip, int max);

  bool sendClusterMessage(int msg_type, const char *args = NULL);
  bool sendOutgoingMessage(char *buf, int len);
  bool sendReliableMessage(unsigned long addr, char *buf, int len);

  bool rl_sendReliableMessage(unsigned long addr, const char *buf, int len);
  bool sendReliableMessage(unsigned long addr, char *buf, int len, char *reply, int len2, bool take_lock);
  bool sendReliableMessageReadTillClose(unsigned long addr, char *buf, int len, textBuffer *reply);

  int receiveIncomingMessage(char *buf, int max);

  bool isMaster();
  unsigned long lowestPeer(int *no);
  unsigned long highestPeer(int *no);

  unsigned long
  getIP()
  {
    return our_ip;
  }

  void logClusterMismatch(const char *ip, ClusterMismatch type, char *data);

  InkHashTable *mismatchLog; // drainer thread use only

  bool init;

  MgmtClusterType cluster_type;

  unsigned long our_ip;
  char our_host[1024];

  AppVersionInfo appVersionInfo;
  char sys_name[MAX_NODE_SYSINFO_STRING];
  char sys_release[MAX_NODE_SYSINFO_STRING];

  time_t delta_thresh;
  time_t peer_timeout;
  time_t mc_send_interval;
  time_t mc_poll_timeout;
  time_t our_wall_clock;
  time_t startup_timeout;
  time_t startup_time;
  time_t last_shared_send;

  int log_bogus_mc_msgs;

  ink_mutex mutex;
  InkHashTable *peers;
  volatile int alive_peers_count;

  char cluster_conf[1024];
  Rollback *cluster_file_rb;
  FileManager *configFiles;

  int cluster_port;
  int reliable_server_port;
  int reliable_server_fd;
  int broadcast_fd;
  int receive_fd;

  int mc_port;
  int mc_ttl;
  char mc_group[MAX_MC_GROUP_LEN];

  struct sockaddr_in broadcast_addr;
  struct sockaddr_in receive_addr;

}; /* End class ClusterCom */

struct in_addr *mgmt_sortipaddrs(int num, struct in_addr **list);

#endif /* _CLUSTER_COM_H */
