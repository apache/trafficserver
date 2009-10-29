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
 * VMap.h
 *   Class definition for virtual ip cluster mgmt. Maintains the virtual
 * map for the cluster and provides support for mapping operations.
 *
 * $Date: 2003-06-01 18:37:37 $
 *
 * 
 */

#ifndef _VMAP_H
#define _VMAP_H

#include <stdlib.h>
#include <stdio.h>

#include "ink_mutex.h"

#ifdef _WIN32
/* Currently, iphlpapi.h is not present in VC98\include;
   However, its present in sdk
*/
#  include <iphlpapi.h>

#define    NULL_IP_ADDR                 0
#define    IP_ADDR_EQUAL(ip1, ip2)      ((ip1) == (ip2))

#define MAX_INTERFACE_LENGTH   20

#define INVALID_NTE_CONTEXT    0xffffffff

typedef struct _vip_info
{
  char interface[MAX_INTERFACE_LENGTH];
} VIPInfo;


typedef struct _realip_info
{
  DWORD ifindex;
  IPAddr subnet_mask;
} RealIPInfo;

typedef struct _vmap_info
{
  bool mapping;
  ULONG nte_context;
} VMAPInfo;


#else // _WIN32

#define MAX_INTERFACE  16
#define MAX_SUB_ID      8


typedef struct _vip_info
{
  char interface[MAX_INTERFACE];
  char sub_interface_id[MAX_SUB_ID];
} VIPInfo;


typedef struct _realip_info
{
  struct in_addr real_ip;
  bool mappings_for_interface;
} RealIPInfo;

#endif


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

  VMap(char *interface, unsigned long ip, ink_mutex * m);
   ~VMap();

  void init();                  /* VIP is ON, initialize it */

#ifdef _WIN32
  bool upAddr(char *virt_ip, ULONG * Context);
  bool downAddr(char *virt_ip, ULONG Context);
#else
  bool upAddr(char *virt_ip);
  bool downAddr(char *virt_ip);
#endif
  void downAddrs();
  void downOurAddrs();
  void rl_downAddrs();

  void lt_runGambit();
  void lt_readAListFile(char *data);
  void lt_constructVMapMessage(char *ip, char *message, int max);

  void rl_rebalance();

  bool rl_remote_map(char *virt_ip, char *real_ip);
  bool rl_remote_unmap(char *virt_ip, char *real_ip);

  bool rl_map(char *virt_ip, char *real_ip = NULL);
  bool rl_unmap(char *virt_ip, char *real_ip = NULL);
  bool rl_remap(char *virt_ip, char *cur_ip, char *dest_ip, int cur_naddr, int dest_naddr);

#ifdef _WIN32
  char *rl_checkConflict(char *virt_ip, VMAPInfo ** virt_hash_value);
#else
  char *rl_checkConflict(char *virt_ip);
#endif
  bool rl_checkGlobConflict(char *virt_ip);
#ifdef _WIN32
  void rl_resolveConflict(char *virt_ip, char *conf_ip, VMAPInfo * virt_ip_hash_value);
#else
  void rl_resolveConflict(char *virt_ip, char *conf_ip);
#endif

  int rl_boundAddr(char *virt_ip);
  unsigned long rl_boundTo(char *virt_ip);
  int rl_clearUnSeen(char *ip);
  void rl_resetSeenFlag(char *ip);

#ifdef _WIN32
  PMIB_IPADDRTABLE GetMyAddrTable(BOOL sort);
  ULONG SearchAddrinMyAddrTable(PMIB_IPADDRTABLE AddrTable, IPAddr item);
#else
  char vip_conf[PATH_MAX];
  char absolute_vipconf_binary[PATH_MAX];
#endif

  int enabled;
  bool enabled_init;            /* have we initialized VIP? Call when VIP is turned on */
  bool turning_off;             /* are we turning off VIP but haven't down'd the addr? */
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
  InkHashTable *our_map;
  InkHashTable *ext_map;
  InkHashTable *id_map;
  InkHashTable *interface_realip_map;

  char *interface;              /* used to passed the interface from VMap::VMap to VMap::init */

private:

};                              /* End class VMap */

#endif /* _VMAP_H */
