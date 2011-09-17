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

/************************************** */
/*
 * VMap.c
 *   Function defs for the virtual mapping mgmt stuff.
 *
 *
 */

#include "ink_platform.h"
#include "ink_unused.h"        /* MAGIC_EDITING_TAG */

#include "Main.h"
#include "LocalManager.h"
#include "VMap.h"
#include "MgmtUtils.h"
#include "P_RecLocal.h"

// for linux and freebsd
#ifndef C_ISUID
#define C_ISUID S_ISUID
#endif

int
vmapEnableHandler(const char *tok, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(data_type);
  NOWARN_UNUSED(cookie);
  bool before = true;
  ink_assert(!tok);
  if (!lmgmt->virt_map->enabled)
    before = false;
  lmgmt->virt_map->enabled = (RecInt) data.rec_int;
  if (!lmgmt->virt_map->enabled && before) {
    lmgmt->virt_map->turning_off = true;        // turing VIP from off to on
    lmgmt->virt_map->downAddrs();
  }
  return 0;
}                               /* End vmapEnableHandler */


/*
 * init()
 *   The code is used to be part of the VMap::VMap().
 *   Initialize the location of vaddrs.config.
 */
void
VMap::init()
{
//    int id;
//    bool found;
//    RecordType type;

  if (enabled) {
    ink_strlcpy(vip_conf, "vip_config", sizeof(vip_conf));
    snprintf(absolute_vipconf_binary, sizeof(absolute_vipconf_binary), "%s/%s", lmgmt->bin_path, vip_conf);

    /* Make sure vip_config is setuid root */
    int vip_config_fd = open(absolute_vipconf_binary, O_RDONLY);
    char msg_buffer[1024];

    if (vip_config_fd < 0) {
      snprintf(msg_buffer, sizeof(msg_buffer), "unable to open %s", absolute_vipconf_binary);
      lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR, msg_buffer);
    } else {
      struct stat stat_buf;

      int err = fstat(vip_config_fd, &stat_buf);
      if (err < 0) {
        snprintf(msg_buffer, sizeof(msg_buffer), "[VMap::VMap] fstat of %s failed, see syslog for more info.",
                 absolute_vipconf_binary);
        mgmt_elog(msg_buffer);
        snprintf(msg_buffer, sizeof(msg_buffer), "fstat of %s failed, see syslog for more info.",
                 absolute_vipconf_binary);
        lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR, msg_buffer);
      } else if (stat_buf.st_uid != 0 || !(stat_buf.st_mode & C_ISUID)) {
        snprintf(msg_buffer, sizeof(msg_buffer),
                 "[VMap::VMap] %s is not setuid root, manager will be unable to enable virtual ip addresses.",
                 absolute_vipconf_binary);
        mgmt_elog(msg_buffer);
        snprintf(msg_buffer, sizeof(msg_buffer),
                 "%s is not setuid root, traffic manager will be unable to enable virtual ip addresses.",
                 absolute_vipconf_binary);
        lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR, msg_buffer);
      }
      close(vip_config_fd);
    }
    enabled_init = true;
  }
}


VMap::VMap(char *interface, unsigned long ip, ink_mutex * m)
{
  bool found;

  while (!m) {
    mgmt_sleep_sec(1);
  }                             // Wait until mutex pointer is initialized
  mutex = m;

  our_ip = ip;
  num_interfaces = 0;
  id_map = NULL;
  interface_realip_map = ink_hash_table_create(InkHashTableKeyType_String);
  our_map = ink_hash_table_create(InkHashTableKeyType_String);
  ext_map = ink_hash_table_create(InkHashTableKeyType_String);
  addr_list = NULL;
  num_addrs = 0;
  num_nics = 0;


  this->interface = ats_strdup(interface);
  enabled_init = false;         // don't whether enabled, but definitely no init'd
  turning_off = false;          // we are not turning off VIP

  enabled = REC_readInteger("proxy.config.vmap.enabled", &found);

  init();                       // Initialize VIP

  /*
   * Perpetuating a hack for the cluster interface. Here I want to loop
   * at startup(before any virtual ips have been brought up) and get the real
   * IP address for each interface.
   *
   * Later this will be used to ping the interfaces that have virtual IP addrs
   * associated with them to detect if the interface is down.
   */
  {                             /* First Add the cluster interface */
    RealIPInfo *tmp_realip_info;
    struct in_addr tmp_addr;

    tmp_addr.s_addr = ip;

    tmp_realip_info = (RealIPInfo *)ats_malloc(sizeof(RealIPInfo));
    tmp_realip_info->real_ip = tmp_addr;
    tmp_realip_info->mappings_for_interface = true;

    num_nics++;
    ink_hash_table_insert(interface_realip_map, interface, (void *) tmp_realip_info);
    if (enabled) {
      mgmt_log("[VMap::Vmap] Added cluster interface '%s' real ip: '%s' to known interfaces\n",
               interface, inet_ntoa(tmp_realip_info->real_ip));
    }
  }
  {
    int tmp_socket;
    struct sockaddr_in *tmp;    // a tmp ptr for addresses
    struct ifconf ifc;          // ifconf information
    char *ifbuf;                // ifconf buffer
    struct ifreq *ifr, *ifend;  // pointer to individual inferface info
    int lastlen;
    int len;

    if ((tmp_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      mgmt_fatal(stderr, "[VMap::VMap] Unable to create socket for interface ioctls\n");
    }
    // INKqa06739
    // Fetch the list of network interfaces
    // . from Stevens, Unix Network Prog., pg 434-435
    ifbuf = 0;
    lastlen = 0;
    len = 128 * sizeof(struct ifreq);   // initial buffer size guess
    for (;;) {
      ifbuf = (char *)ats_malloc(len);
      memset(ifbuf, 0, len);    // prevent UMRs
      ifc.ifc_len = len;
      ifc.ifc_buf = ifbuf;
      if (ioctl(tmp_socket, SIOCGIFCONF, &ifc) < 0) {
        if (errno != EINVAL || lastlen != 0) {
          mgmt_fatal(stderr, "[VMap::VMap] Unable to read network interface configuration\n");
        }
      } else {
        if (ifc.ifc_len == lastlen) {
          break;
        }
        lastlen = ifc.ifc_len;
      }
      len *= 2;
      ats_free(ifbuf);
    }

    ifr = ifc.ifc_req;
    ifend = (struct ifreq *) (ifc.ifc_buf + ifc.ifc_len);
    // Loop through the list of interfaces
    for (ifr = ifc.ifc_req; ifr < ifend;) {
      if (ifr->ifr_addr.sa_family == AF_INET && strcmp(ifr->ifr_name, "lo0") != 0 && !strstr(ifr->ifr_name, ":")) {     // Don't count the loopback interface

        // Get the address of the interface
        if (ioctl(tmp_socket, SIOCGIFADDR, (char *) ifr) < 0) {
          mgmt_log("[VMap::VMap] Unable obtain address for network interface %s, presuming unused\n", ifr->ifr_name);
        } else {
          InkHashTableValue hash_value;

          // Only look at the address if it an internet address
          if (ifr->ifr_ifru.ifru_addr.sa_family == AF_INET) {
            RealIPInfo *tmp_realip_info;

            tmp = (struct sockaddr_in *) &ifr->ifr_ifru.ifru_addr;

            tmp_realip_info = (RealIPInfo *)ats_malloc(sizeof(RealIPInfo));
            tmp_realip_info->real_ip = tmp->sin_addr;
            tmp_realip_info->mappings_for_interface = false;

            if (ink_hash_table_lookup(interface_realip_map, ifr->ifr_name, &hash_value) != 0) {
              if (enabled) {
                mgmt_log("[VMap::VMap] Already added interface '%s'. Not adding for"
                         " real IP '%s'\n", ifr->ifr_name, inet_ntoa(tmp_realip_info->real_ip));
              }
              ats_free(tmp_realip_info);
            } else {
              ink_hash_table_insert(interface_realip_map, ifr->ifr_name, (void *) tmp_realip_info);
              num_nics++;
              if (enabled) {
                mgmt_log("[VMap::Vmap] Added interface '%s' real ip: '%s' to known interfaces\n",
                         ifr->ifr_name, inet_ntoa(tmp_realip_info->real_ip));
              }
            }
          } else {
            if (enabled) {
              mgmt_log(stderr, "[VMap::VMap] Interface %s is not configured for IP.\n", ifr->ifr_name);
            }
          }
        }
      }
#if defined(freebsd) || defined(darwin)
      ifr = (struct ifreq *) ((char *) &ifr->ifr_addr + ifr->ifr_addr.sa_len);
#else
      ifr = (struct ifreq *) (((char *) ifr) + sizeof(*ifr));
#endif
    }
    ats_free(ifbuf);
    close(tmp_socket);
  }


  RecRegisterConfigUpdateCb("proxy.config.vmap.enabled", vmapEnableHandler, NULL);

  down_up_timeout = REC_readInteger("proxy.config.vmap.down_up_timeout", &found);
  addr_list_fname = REC_readString("proxy.config.vmap.addr_file", &found);
  lt_readAListFile(addr_list_fname);

  map_change_thresh = 10;
  last_map_change = time(NULL);

  return;

}                               /* End VMap::VMap */


VMap::~VMap()
{
  if (id_map)
    ink_hash_table_destroy_and_xfree_values(id_map);

  ink_hash_table_destroy_and_xfree_values(interface_realip_map);
  ink_hash_table_destroy(our_map);
  ink_hash_table_destroy(ext_map);
  ats_free(this->interface);
  ats_free(addr_list);
}                               /* End VMap::~VMap */


/*
 * lt_runGambit()
 *   Function basically runs the virtual ip assignment gambit. If you are
 * the current cluster master you will check for rebalancing or un-assigned
 * interfaces and distribute them. Both cluster master and other nodes will
 * check for conflicts between their virtual interfaces and other nodes in the
 * cluster.
 */
void
VMap::lt_runGambit()
{
  int i, no = 0;
  char vaddr[80], raddr[80], *conf_addr = NULL;
  bool init = false;
  struct in_addr virtual_addr, real_addr;


  if (!enabled) {
    return;
  }
  if (!((time(NULL) - lmgmt->ccom->startup_time) > lmgmt->ccom->startup_timeout)) {
    return;
  }
  if (num_addrs == 0) {
    return;
  }
  // Have we initialized yet? If not, VIP is turned from OFF to ON since last restart
  // Let's initialize it
  if (!enabled_init) {
    this->init();
  }

  ink_mutex_acquire(mutex);
  if (lmgmt->ccom->isMaster()) {        /* Are we the cluster master? */

    for (i = 0; i < num_addrs; i++) {   /* See if there is an unbound interface */
      virtual_addr.s_addr = addr_list[i];
      ink_strlcpy(vaddr, inet_ntoa(virtual_addr), sizeof(vaddr));
      if (rl_boundAddr(vaddr) == 0) {
        mgmt_log(stderr, "[VMap::lt_runGambit] Unmapped vaddr: '%s'\n", vaddr);
        break;
      }
    }

    if (i == num_addrs) {       /* So, all addrs are mapped. Check if rebalance is needed */
      rl_rebalance();
    } else {                    /* Got one to map, find a candidate and map it */

      real_addr.s_addr = lmgmt->ccom->lowestPeer(&no);
      if (no != -1) {           /* Make sure we have peers to map interfaces to */
        init = true;
      }

      if (init && ((no < num_interfaces) || (no == num_interfaces && real_addr.s_addr < our_ip))) {
        ink_strlcpy(raddr, inet_ntoa(real_addr), sizeof(raddr));
        rl_remote_map(vaddr, raddr);
      } else if (!rl_map(vaddr)) {      /* We are the winner, map it to us */
        mgmt_elog(stderr, "[VMap::lt_runGambit] Map failed for vaddr: %s\n", vaddr);
      } else {
        mgmt_log(stderr, "[VMap::lt_runGambit] Map succeeded for vaddr: %s\n", vaddr);
      }
    }
  }


  for (i = 0; i < num_addrs; i++) {     /* Check for conflicts with your interfaces */
    virtual_addr.s_addr = addr_list[i];
    ink_strlcpy(vaddr, inet_ntoa(virtual_addr), sizeof(vaddr));

    if ((conf_addr = rl_checkConflict(vaddr))) {
      mgmt_log(stderr, "[VMap::lt_runGambit] Conflict w/addr: '%s'\n", vaddr);
      break;
    }
  }

  if (conf_addr) {              /* If there was a conflict, resolve it */
    rl_resolveConflict(vaddr, conf_addr);
    ats_free(conf_addr);
  }

  ink_mutex_release(mutex);
  return;
}                               /* End VMap::lt_runGambit */


/*
 * lt_readAListFile(...)
 *   Function reads in the virtual ip list, basically a parsing routine for the
 * vaddr file.
 */
void
VMap::lt_readAListFile(char *data)
{
  int tmp_num_addrs = 0;
  char buf[1024], fname[1024];
  char tmp_addr[1024], tmp_interface[1024];
  FILE *fin;
  char tmp_id[1024];

  if (!data)
    return;
  snprintf(fname, sizeof(fname), "%s/%s", lmgmt->config_path, data);
  if (!(fin = fopen(fname, "r"))) {
    mgmt_log(stderr, "[VMap::lt_readAListFile] Unable to open file: %s, addr list unchanged\n", fname);
    return;
  }

  ink_mutex_acquire(mutex);
  rl_downAddrs();               /* Down everything before we re-init */
  if (id_map) {
    ink_hash_table_destroy_and_xfree_values(id_map);
  }

  id_map = ink_hash_table_create(InkHashTableKeyType_String);
  while (fgets(buf, 1024, fin)) {
    // since each of the tmp_addr, tmp_interface, tmp_id has length 1024 which is not less than buf
    // so here we don't need to worry about overflow, disable coverity check for this line
    // coverity[secure_coding]
    if (buf[0] != '#' && isascii(buf[0]) && isdigit(buf[0]) &&
        (sscanf(buf, "%s %s %s\n", tmp_addr, tmp_interface, tmp_id) == 3)) {
      tmp_num_addrs++;
    }
  }

  num_addrs = tmp_num_addrs;
  if (num_addrs) {
    addr_list = (unsigned long *)ats_malloc(sizeof(unsigned long) * num_addrs);
  } else {                      /* Handle the case where there are no addrs in the file */
    addr_list = NULL;
    fclose(fin);
    ink_mutex_release(mutex);
    return;
  }

  tmp_num_addrs = 0;
  rewind(fin);
  while (fgets(buf, 1024, fin)) {
    VIPInfo *tmp_val;
    InkHashTableValue hash_value;

    /* Make sure we have a valid line and its not commented */
    // since each of the tmp_addr, tmp_interface, tmp_id has length 1024 which is not less than buf
    // so here we don't need to worry about overflow, disable coverity check for this line
    // coverity[secure_coding]
    if (!isascii(buf[0]) || !isdigit(buf[0]) || (sscanf(buf, "%s %s %s\n", tmp_addr, tmp_interface, tmp_id) != 3)) {
      continue;
    }
    mgmt_log("[VMap::lt_readAListFile] Adding virtual address '%s' interface: '%s'"
             " sub-interface-id '%s'\n", tmp_addr, tmp_interface, tmp_id);

    addr_list[tmp_num_addrs++] = inet_addr(tmp_addr);

    tmp_val = (VIPInfo *)ats_malloc(sizeof(VIPInfo));

    ink_strlcpy(tmp_val->interface, tmp_interface, sizeof(tmp_val->interface));
    ink_strlcpy(tmp_val->sub_interface_id, tmp_id, sizeof(tmp_val->sub_interface_id));
    ink_hash_table_insert(id_map, tmp_addr, (void *) tmp_val);

    // we don't need to do mgmt ping stuff on NT the way its done on UNIX
    if (ink_hash_table_lookup(interface_realip_map, tmp_interface, &hash_value) != 0) {

      if (!((RealIPInfo *) hash_value)->mappings_for_interface) {
        ((RealIPInfo *) hash_value)->mappings_for_interface = true;
        mgmt_log(stderr, "[VMap::lt_readAListFile] Interface '%s' marked as having potential"
                 " virtual ips\n", tmp_interface);
      }
    } else {
      mgmt_elog(stderr, "[VMap::lt_readAListFile] VIP in config file but no interface"
                " '%s' present on node.\n", tmp_interface);
    }
  }
  fclose(fin);

  ink_mutex_release(mutex);
  return;
}                               /* End VMap::lt_readAListFile */


/*
 * rl_resetSeenFlag(...)
 *   Function resets the "seen" flag for a given peer's mapped addrs.
 */
void
VMap::rl_resetSeenFlag(char *ip)
{
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  for (entry = ink_hash_table_iterator_first(ext_map, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(ext_map, &iterator_state)) {

    char *key = (char *) ink_hash_table_entry_key(ext_map, entry);
    bool *tmp = (bool *) ink_hash_table_entry_value(ext_map, entry);

    if (strstr(key, ip)) {
      *tmp = false;
    }
  }
  return;
}                               /* End VMap::rl_resetSeenFlag */


/*
 * rl_clearUnSeen(...)
 *   This function is a sweeper function to clean up the map.
 */
int
VMap::rl_clearUnSeen(char *ip)
{
  int numAddrs = 0;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  for (entry = ink_hash_table_iterator_first(ext_map, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(ext_map, &iterator_state)) {

    char *key = (char *) ink_hash_table_entry_key(ext_map, entry);
    bool *tmp = (bool *) ink_hash_table_entry_value(ext_map, entry);

    if (strstr(key, ip)) {
      if (!*tmp) {
        ink_hash_table_delete(ext_map, key);    /* Safe in iterator? */
        ats_free(tmp);
      } else {
        numAddrs++;
      }
    }
  }
  return numAddrs;
}                               /* End VMap::rl_clearUnSeen */


/*
 * rl_remote_map(...)
 *   Function sends the up interface command to a remote node.
 */
bool
VMap::rl_remote_map(char *virt_ip, char *real_ip)
{
  char buf[1024], reply[4096];

  snprintf((char *) buf, sizeof(buf), "map: %s", virt_ip);
  if (!(lmgmt->ccom->sendReliableMessage(inet_addr(real_ip), buf, strlen(buf), reply, 4096, false))) {
    mgmt_elog(stderr, "[VMap::rl_remote_map] Reliable send failed\n");
    return false;
  } else if (strcmp(reply, "map: failed") == 0) {
    mgmt_log(stderr, "[VMap::rl_remote_map] Mapping failed\n");
    return false;
  }
  rl_map(virt_ip, real_ip);
  return true;
}                               /* End VMap::rl_remote_map */


/*
 * rl_remote_unmap(...)
 *   Function sends the up interface command to a remote node.
 */
bool
VMap::rl_remote_unmap(char *virt_ip, char *real_ip)
{
  char buf[1024], reply[4096];

  snprintf((char *) buf, sizeof(buf), "unmap: %s", virt_ip);
  if (!(lmgmt->ccom->sendReliableMessage(inet_addr(real_ip), buf, strlen(buf), reply, 4096, false))) {
    mgmt_elog(stderr, "[VMap::rl_remote_unmap] Reliable send failed\n");
    return false;
  } else if (strcmp((char *) reply, "unmap: failed") == 0) {
    mgmt_log(stderr, "[VMap::rl_remote_unmap] Mapping failed\n");
    return false;
  }
  return true;
}                               /* End VMap::rl_remote_unmap */


/*
 * rl_map(...)
 *   Function maps a virt_ip to a real_ip, if real_ip is NULL it maps it
 * to the local node itself(actually bringing the interface up as well).
 */
bool
VMap::rl_map(char *virt_ip, char *real_ip)
{
  char buf[80];
  bool *entry;
  InkHashTable *tmp;
  InkHashTableValue hash_value;

  if (real_ip) {
    tmp = ext_map;
    snprintf(buf, sizeof(buf), "%s %s", virt_ip, real_ip);
  } else {
    tmp = our_map;
    ink_strlcpy(buf, virt_ip, sizeof(buf));
  }

  if (ink_hash_table_lookup(tmp, buf, &hash_value) != 0) {
    *((bool *) hash_value) = true;
    return false;
  }

  entry = (bool *)ats_malloc(sizeof(bool));
  *entry = true;

  if (!real_ip) {
    last_map_change = time(NULL);

    if (!upAddr(virt_ip)) {
      mgmt_elog(stderr, "[VMap::rl_map] upAddr failed\n");
      ats_free(entry);
      return false;
    }
  }
  ink_hash_table_insert(tmp, buf, (void *) entry);
  return true;
}                               /* End VMap::rl_map */


bool
VMap::rl_unmap(char *virt_ip, char *real_ip)
{
  char buf[80];
  InkHashTable *tmp;
  InkHashTableValue hash_value;

  if (real_ip) {
    tmp = ext_map;
    snprintf(buf, sizeof(buf), "%s %s", virt_ip, real_ip);
  } else {
    tmp = our_map;
    ink_strlcpy(buf, virt_ip, sizeof(buf));
  }

  if (ink_hash_table_lookup(tmp, buf, &hash_value) == 0) {
    return false;
  }

  if (!real_ip) {
    last_map_change = time(NULL);
    if (!(downAddr(virt_ip))) {
      mgmt_elog(stderr, "[VMap::rl_unmap] downAddr failed\n");
      return false;
    }
  }
  ink_hash_table_delete(tmp, buf);
  ats_free(hash_value);
  return true;
}                               /* End VMap::rl_unmap */


/*
 * rl_checkConflict(...)
 *   This function checks for virt conflicts between the local node and
 * any peers. It returns NULL on no conflict or the real ip of the peer
 * the local node is in contention with.
 */
char *
VMap::rl_checkConflict(char *virt_ip)
{
  char *key = NULL;
  bool in_our_map = false, in_ext_map = false;
  InkHashTableValue hash_value;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  if ((time(NULL) - last_map_change) < map_change_thresh) {
    return NULL;
  }

  for (entry = ink_hash_table_iterator_first(ext_map, &iterator_state);
       (entry != NULL && !in_ext_map); entry = ink_hash_table_iterator_next(ext_map, &iterator_state)) {

    key = (char *) ink_hash_table_entry_key(ext_map, entry);
    if (strstr(key, virt_ip)) {
      in_ext_map = true;
    }
  }

  if (ink_hash_table_lookup(our_map, virt_ip, &hash_value) != 0) {
    in_our_map = true;
  }

  if (in_our_map && in_ext_map) {
    char *buf, buf2[80];

    if ((buf = strstr(key, " ")) != NULL) {
      buf++;
      ink_strlcpy(buf2, buf, sizeof(buf2));
    } else {
      mgmt_fatal(stderr, "[VMap::rl_checkConflict] Corrupt VMap entry('%s'), bailing\n", key);
    }
    return ats_strdup(buf2);
  }
  return NULL;
}                               /* End VMap::rl_checkConflict */


/*
 * checkGlobConflict(...)
 *   This function checks for conflict in the local map as well as the
 * global map. It returns false on no conflict and true on conflict.
 */
bool
VMap::rl_checkGlobConflict(char *virt_ip)
{
  bool in_ext_map = false, ret = false;
  InkHashTableValue hash_value;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  for (entry = ink_hash_table_iterator_first(ext_map, &iterator_state);
       (entry != NULL && !in_ext_map); entry = ink_hash_table_iterator_next(ext_map, &iterator_state)) {

    char *key = (char *) ink_hash_table_entry_key(ext_map, entry);
    if (strstr(key, virt_ip)) {
      if (!in_ext_map) {
        in_ext_map = true;
      }
    }
  }

  if (in_ext_map) {
    ret = true;
  }

  if (ret) {
    return ret;
  }

  if (ink_hash_table_lookup(our_map, virt_ip, &hash_value) != 0) {
    if (in_ext_map) {
      ret = true;
    } else {
      ret = false;
    }
  }

  return ret;
}                               /* End VMap::rl_checkGlobConflict */


/*
 * resolveConflict(...)
 *   This function is to be called after a conflict has been detected between
 * the local node and some peer. It will down the interface, pause for a bit,
 * determine who wins in the conflict and potentially attempt to bring the
 * interface up again.
 */
void
VMap::rl_resolveConflict(char *virt_ip, char *conf_ip)
{

  if (inet_addr(conf_ip) < our_ip) {    /* They win, bow out gracelfully */
    rl_unmap(virt_ip);
  } else {                      /* We win(don't gloat), down, timeout, up */
    downAddr(virt_ip);          /* downAddr, to avoid possible re-assignment */
    mgmt_sleep_sec(down_up_timeout);    /* FIX: SHOULD this be changed to a wait till no-ping? */
    upAddr(virt_ip);
  }
  return;
}                               /* End VMap::rl_resolveConflict */


/*
 * remap(...)
 *   Function attempts to remap virt_ip. If dest is NULL, test to see if local
 * node should grab the addr.
 */
bool
VMap::rl_remap(char *virt_ip, char *cur_ip, char *dest_ip, int cur_naddr, int dest_naddr)
{
  char buf[1024];
  InkHashTable *tmp;
  InkHashTableValue hash_value;

  if (inet_addr(cur_ip) != our_ip) {
    snprintf(buf, sizeof(buf), "%s %s", cur_ip, dest_ip);
    tmp = ext_map;
  } else {
    struct in_addr addr;
    addr.s_addr = our_ip;

    ink_strlcpy(buf, inet_ntoa(addr), sizeof(buf));
    tmp = our_map;
  }

  /* Verify the map is correct and has not changed */
  if (ink_hash_table_lookup(tmp, buf, &hash_value) != 0) {
    mgmt_log(stderr, "[VMap::rl_remap] Map has changed for ip: '%s' virt: '%s'\n", cur_ip, virt_ip);
    return false;
  } else if (cur_naddr <= dest_naddr) {
    return false;
  }

  /* Down interface on cur_ip, wait for completion */
  if (inet_addr(cur_ip) == our_ip) {
    if (!rl_unmap(virt_ip)) {
      return false;
    }
  } else {
    if (!rl_remote_unmap(virt_ip, cur_ip)) {
      return false;
    }
  }

  /* Up interface on dest_ip, again wait for completion */
  if (inet_addr(dest_ip) == our_ip) {
    if (!rl_map(virt_ip)) {
      return false;
    }
  } else {
    if (!rl_remote_map(virt_ip, dest_ip)) {
      return false;
    }
  }
  return true;
}                               /* End VMap::rl_remap */


/*
 * boundAddr(...)
 *   Function tests whether or not the addr is bound. Returns 0(not bound),
 * 1(bound locally), 2(bound in cluster).
 */
int
VMap::rl_boundAddr(char *virt_ip)
{
  InkHashTableValue hash_value;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  if (ink_hash_table_lookup(our_map, virt_ip, &hash_value) != 0) {
    return 1;
  }

  for (entry = ink_hash_table_iterator_first(ext_map, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(ext_map, &iterator_state)) {
    char *key = (char *) ink_hash_table_entry_key(ext_map, entry);

    if (strstr(key, virt_ip)) {
      return 2;
    }
  }
  return 0;
}                               /* End VMap::rl_boundAddr */


/*
 * boundTo(...)
 *   Function returns ip addr(string form) of the node that the virt address
 * is bound to. Returning NULL if it is unbound.
 */
unsigned long
VMap::rl_boundTo(char *virt_ip)
{
  InkHashTableValue hash_value;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  if (ink_hash_table_lookup(our_map, virt_ip, &hash_value) != 0) {
    return our_ip;
  }

  for (entry = ink_hash_table_iterator_first(ext_map, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(ext_map, &iterator_state)) {
    char *key = (char *) ink_hash_table_entry_key(ext_map, entry);

    if (strstr(key, virt_ip)) {
      char *buf, buf2[80];
      if ((buf = strstr(key, " ")) != NULL) {
        buf++;
        ink_strlcpy(buf2, buf, sizeof(buf2));
      } else {
        mgmt_fatal(stderr, "[VMap::rl_boundTo] Corrupt VMap entry('%s'), bailing\n", key);
      }
      return (inet_addr(buf2));
    }
  }
  return 0;
}                               /* End VMap::rl_boundTo */


/*
 * constructVMapMessage(...)
 *   Constructs the broadcast message of the local nodes virtual ip map.
 */
void
VMap::lt_constructVMapMessage(char *ip, char *message, int max)
{
  int n = 0, bsum = 0;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  if (!ip) {
    return;
  }
  // Insert the standard mcast packet header
  n = ClusterCom::constructSharedPacketHeader(message, ip, max);

  if (!((n + (int) strlen("type: vmap\n")) < max)) {
    if (max >= 1) {
      message[0] = '\0';
    }
    return;
  }

  ink_strlcpy(&message[n], "type: vmap\n", max - n);
  n += strlen("type: vmap\n");
  bsum = n;

  ink_mutex_acquire(mutex);
  for (entry = ink_hash_table_iterator_first(our_map, &iterator_state);
       (entry != NULL && n < max); entry = ink_hash_table_iterator_next(our_map, &iterator_state)) {

    char buf[1024];
    char *key = (char *) ink_hash_table_entry_key(our_map, entry);

    snprintf(buf, sizeof(buf), "virt: %s\n", key);
    if (!((n + (int) strlen(buf)) < max)) {
      break;
    }
    ink_strlcpy(&message[n], buf, max - n);
    n += strlen(buf);
  }
  ink_mutex_release(mutex);

  if (n == bsum) {              /* No alarms */
    if (!((n + (int) strlen("virt: none\n")) < max)) {
      if (max >= 1) {
        message[0] = '\0';
      }
      return;
    }
    ink_strlcpy(&message[n], "virt: none\n", max - n);
    n += strlen("virt: none\n");
  }
  return;
}                               /* End VMap::constructVMapMessage */


/*
 * rebalance()
 *   Advanced feature that initiates a re-balancing effort(if determined to be
 * necessary) of the addresses throughout the cluster.
 */
void
VMap::rl_rebalance()
{
  int naddr_low, naddr_high;
  char low_ip[80], high_ip[80];
  unsigned long low, high;
  struct in_addr tmp_addr;

  low = lmgmt->ccom->lowestPeer(&naddr_low);
  high = lmgmt->ccom->highestPeer(&naddr_high);
  tmp_addr.s_addr = high;
  ink_strlcpy(high_ip, inet_ntoa(tmp_addr), sizeof(high_ip));
  tmp_addr.s_addr = low;
  ink_strlcpy(low_ip, inet_ntoa(tmp_addr), sizeof(low_ip));

  if (naddr_low == -1 || naddr_high == -1) {
    return;
  }

  if (naddr_low > num_interfaces || (naddr_low == num_interfaces && our_ip < low)) {
    naddr_low = num_interfaces;
    tmp_addr.s_addr = our_ip;
    low = our_ip;
    ink_strlcpy(low_ip, inet_ntoa(tmp_addr), sizeof(low_ip));
  } else if (naddr_high<num_interfaces || (naddr_high == num_interfaces && our_ip> high)) {
    naddr_high = num_interfaces;
    tmp_addr.s_addr = our_ip;
    high = our_ip;
    ink_strlcpy(high_ip, inet_ntoa(tmp_addr), sizeof(high_ip));
  }
#ifdef DEBUG_VMAP
  Debug("vmap",
        "[VMap::rl_rebalance] Checking balance of virtual map low: %s, %d high: %s, %d\n",
        low_ip, naddr_low, high_ip, naddr_high);
#endif /* DEBUG_VMAP */

  if (naddr_low < naddr_high && high != low && (naddr_high - naddr_low) != 1) {
    InkHashTableEntry *entry;
    InkHashTableIteratorState iterator_state;

    mgmt_log(stderr, "[VMap::rl_rebalance] Attempting to rebalance virtual map\n");
    if (high == our_ip) {
      char *key, tmp_key[80];

      entry = ink_hash_table_iterator_first(our_map, &iterator_state);
      key = (char *) ink_hash_table_entry_key(ext_map, entry);
      ink_strlcpy(tmp_key, key, sizeof(tmp_key));

      mgmt_log(stderr, "[VMap::rl_rebalance] Remapping vaddr: '%s' from: '%s' to: '%s'\n", key, high_ip, low_ip);
      if (!rl_remap(key, high_ip, low_ip, naddr_high, naddr_low)) {
        mgmt_elog(stderr, "[VMap::rl_rebalance] Remap failed vaddr: '%s' from: '%s' to: '%s'\n", key, high_ip, low_ip);
      }
    } else {
      for (entry = ink_hash_table_iterator_first(ext_map, &iterator_state);
           entry != NULL; entry = ink_hash_table_iterator_next(ext_map, &iterator_state)) {
        char *key = (char *) ink_hash_table_entry_key(ext_map, entry);

        if (strstr(key, high_ip)) {
          char vip[80], buf[80];
          //coverity[secure_coding]
          if (sscanf(key, "%79s %79s", vip, buf) != 2) {
            mgmt_fatal("[VMap::rl_rebalance] Corrupt VMap entry('%s'), bailing\n", key);
          }

          mgmt_log(stderr, "[VMap::rl_rebalance] Remapping vaddr: '%s' from: '%s' to: '%s'\n", vip, high_ip, low_ip);
          if (!rl_remap(vip, high_ip, low_ip, naddr_high, naddr_low)) {
            mgmt_elog(stderr, "[VMap::lt_rebalance] Failed vaddr: '%s' from: '%s' to: '%s'\n", vip, high_ip, low_ip);
          }
          break;
        }
      }
    }
  }
  return;
}                               /* End VMap::rl_rebalance */


bool
VMap::upAddr(char *virt_ip)
{
  int status;
  pid_t pid;
  InkHashTableValue hash_value;

  if (!enabled) {
    mgmt_elog(stderr, "[VMap::upAddr] Called for '%s' though virtual addressing disabled\n", virt_ip);
    return false;
  }
  mgmt_log(stderr, "[VMap::upAddr] Bringing up addr: '%s'\n", virt_ip);

  if (ink_hash_table_lookup(id_map, (InkHashTableKey) virt_ip, &hash_value) == 0) {
    mgmt_elog(stderr, "[VMap::upAddr] Called for '%s' which is not in our vaddr.config\n", virt_ip);
    return false;
  }
#ifdef POSIX_THREAD
  if ((pid = fork()) < 0)
#else
  if ((pid = fork1()) < 0)
#endif
  {
    mgmt_elog(stderr, "[VMap::upAddr] Unable to fork1 process\n");
    return false;
  } else if (pid > 0) {         /* Parent */
    waitpid(pid, &status, 0);

    if (status != 0) {
      return false;
    }
    num_interfaces++;
    return true;
  } else {
    int res = 1;
    char *interface, *sub_id;

    interface = ((VIPInfo *) hash_value)->interface;
    sub_id = ((VIPInfo *) hash_value)->sub_interface_id;

#if defined(linux) || defined(freebsd) || defined(solaris) || defined(darwin)
    res = execl(absolute_vipconf_binary, vip_conf, "up", virt_ip, "/sbin/ifconfig", interface, sub_id, (char*) NULL);
#else
    res = execl(absolute_vipconf_binary, vip_conf, "up", virt_ip, "/usr/sbin/ifconfig", interface, sub_id, NULL);
#endif
    _exit(res);
  }


  num_interfaces++;
  ink_assert(num_interfaces > 0 && num_interfaces <= num_addrs);

  return true;
}                               /* End VMap::upAddr */


bool
VMap::downAddr(char *virt_ip)
{
  if (!enabled && !turning_off) {
    mgmt_elog(stderr, "[VMap::downAddr] Called for '%s' though virtual addressing disabled\n", virt_ip);
    return false;
  }
  mgmt_log(stderr, "[VMap::downAddr] Bringing down addr: '%s'\n", virt_ip);


  int status;
  pid_t pid;
  InkHashTableValue hash_value;

  if (ink_hash_table_lookup(id_map, (InkHashTableKey) virt_ip, &hash_value) == 0) {
    mgmt_elog(stderr, "[VMap::downAddr] Called for '%s' which is not in our vaddr.config\n", virt_ip);
    return false;
  }
#ifdef POSIX_THREAD
  if ((pid = fork()) < 0)
#else
  if ((pid = fork1()) < 0)
#endif
  {
    mgmt_elog(stderr, "[VMap::downAddr] Unable to fork1 process\n");
    return false;
  } else if (pid > 0) {         /* Parent */
    waitpid(pid, &status, 0);
    if (status != 0) {
      return false;
    }
    num_interfaces--;
    return true;
  } else {
    int res = 1;
    char *interface, *sub_id;

    interface = ((VIPInfo *) hash_value)->interface;
    sub_id = ((VIPInfo *) hash_value)->sub_interface_id;

#if defined(linux)|| defined(freebsd) || defined(solaris) || defined(darwin)
    res = execl(absolute_vipconf_binary, vip_conf, "down", virt_ip, "/sbin/ifconfig", interface, sub_id, (char*)NULL);
#else
    res = execl(absolute_vipconf_binary, vip_conf, "down", virt_ip, "/usr/sbin/ifconfig", interface, sub_id, NULL);
#endif
    _exit(res);
  }

  num_interfaces--;

  // whenever the manager starts up, it tries to remove all vip's
  // and at that time num_interfaces was 0
  ink_assert(num_interfaces >= 0 && num_interfaces < num_addrs);

  return true;
}                               /* End VMap::downAddr */


void
VMap::rl_downAddrs()
{

  for (int i = 0; i < num_addrs; i++) {
    char str_addr[1024];
    struct in_addr address;
    address.s_addr = addr_list[i];
    ink_strlcpy(str_addr, inet_ntoa(address), sizeof(str_addr));
    rl_unmap(str_addr);
  }
  return;
}                               /* End VMap::rl_downAddrs */

void
VMap::downAddrs()
{


  // Now for WIN32, we have to find the nte_context for the virtual ip address
  // if its still bound to the machine
  // BUGBUG:
  // The way we are doing it is totally undocumented and can change...

  ink_mutex_acquire(mutex);
  for (int i = 0; i < num_addrs; i++) {
    char str_addr[1024];
    struct in_addr address;
    address.s_addr = addr_list[i];
    ink_strlcpy(str_addr, inet_ntoa(address), sizeof(str_addr));
    downAddr(str_addr);
    ink_hash_table_delete(our_map, str_addr);   /* Make sure removed */
  }

  // Reset On->Off flag
  turning_off = false;

  num_interfaces = 0;
  ink_mutex_release(mutex);
  return;
}                               /* End VMap::downAddrs */


void
VMap::downOurAddrs()
{
  bool some_address_mapped = false;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  ink_mutex_acquire(mutex);
  for (entry = ink_hash_table_iterator_first(our_map, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(our_map, &iterator_state)) {
    some_address_mapped = true;
  }

  /*
   * If any address was mapped, take no chance and down
   * everything we know about
   */
  if (some_address_mapped) {


    for (int i = 0; i < num_addrs; i++) {
      char str_addr[1024];
      struct in_addr address;
      address.s_addr = addr_list[i];
      ink_strlcpy(str_addr, inet_ntoa(address), sizeof(str_addr));
      downAddr(str_addr);
      ink_hash_table_delete(our_map, str_addr); /* Make sure removed */
    }
  }
  num_interfaces = 0;
  ink_mutex_release(mutex);
}                               /* End VMap::downOurAddrs */
