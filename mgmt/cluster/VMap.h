/** @file

  Class definition for virtual ip cluster mgmt. Maintains the virtual
  map for the cluster and provides support for mapping operations.

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
#ifndef _VMAP_H
#define _VMAP_H

#include "ts/ink_platform.h"
#include "ts/I_Version.h"
#include "ts/ink_string.h"

#define MAX_INTERFACE 16
#define MAX_SUB_ID 8

typedef struct _vip_info {
  char interface[MAX_INTERFACE];
  char sub_interface_id[MAX_SUB_ID];
} VIPInfo;

typedef struct _realip_info {
  struct in_addr real_ip;
  bool mappings_for_interface;
} RealIPInfo;

/*
 * class VMap
 *   Class implements the protocol and support functions for mapping the
 * clusters virtual addresses. Member functions names are important here,
 * since this class shares a lock with the ClusterCom class:
 *
 *      lt_   "Lock Taken"   -- release the lock prior to invoking
 *      rl_   "Require Lock" -- acquire the lock prior to invoking
 *
 *   Care should also be taken when accessing any of the member variables,
 * a lock is generally required before modification should be made to them.
 *
 */
class VMap
{
public:
  VMap(char *interface, unsigned long ip, ink_mutex *m);
  ~VMap();

  void downAddrs();
  void downOurAddrs();
  void rl_downAddrs();
  void removeAddressMapping(int i);
  void lt_runGambit();
  void lt_readAListFile(const char *data);
  void lt_constructVMapMessage(char *ip, char *message, int max);

  bool rl_remote_map(char *virt_ip, char *real_ip);
  bool rl_remote_unmap(char *virt_ip, char *real_ip);

  bool rl_map(char *virt_ip, char *real_ip = NULL);
  bool rl_unmap(char *virt_ip, char *real_ip = NULL);
  bool rl_remap(char *virt_ip, char *cur_ip, char *dest_ip, int cur_naddr, int dest_naddr);

  char *rl_checkConflict(char *virt_ip);
  bool rl_checkGlobConflict(char *virt_ip);

  int rl_boundAddr(char *virt_ip);
  unsigned long rl_boundTo(char *virt_ip);
  int rl_clearUnSeen(char *ip);
  void rl_resetSeenFlag(char *ip);

  char vip_conf[PATH_NAME_MAX];
  char absolute_vipconf_binary[PATH_NAME_MAX];

  AppVersionInfo appVersionInfo;

  int enabled;
  bool turning_off; /* are we turning off VIP but haven't down'd the addr? */
  /* map_init has never been used, remove it to avoid coverity complain */
  int map_change_thresh;
  time_t last_map_change;
  time_t down_up_timeout;

  char *addr_list_fname;

  int num_addrs;
  int num_nics;
  unsigned long *addr_list;

  int num_interfaces;
  unsigned long our_ip;

  ink_mutex *mutex;
  // Map of virtual ip addresses assigned to the local node
  InkHashTable *our_map;
  // Map of virtual ip addresses assigned to other nodes; as indicated through multicast messages; used
  // to detect conflicts
  InkHashTable *ext_map;
  InkHashTable *id_map;
  InkHashTable *interface_realip_map;

  char *interface; /* used to passed the interface from VMap::VMap to VMap::init */

private:
}; /* End class VMap */

#endif /* _VMAP_H */
