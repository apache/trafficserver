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

/*****************************************************************************
 * Filename: APITestCliRemote.cc
 * Purpose: An interactive cli to test remote mgmt API; UNIT TEST for mgmtAPI
 * Created: lant
 *
 ***************************************************************************/

/***************************************************************************
 * Possible Commands:
 ***************************************************************************
 * Control Operations:
 * -------------------
 * state:   returns ON (proxy is on) or OFF (proxy is off)
 * start:<tsArgs>  -   turns Proxy on, the tsArgs is optional;
 *                     it can either be  "hostdb" or "all",
 *                 eg. start, start:hostdb, start:all
 * stop:    turns Proxy off
 * restart: restarts Traffic Manager (Traffic Cop must be running)
 * kill_TC: restarts Traffic Cop (and TM and TS too)
 *
 * File operations:
 * ---------------
 * read_file:  reads filter.config file
 * write_file: write some made-up text to filter.config file
 * proxy.config.xxx (a records.config variable): returns value of that record
 * records: tests get/set/get a record of each different type
 *          (int, float, counter, string)
 * err_recs: stress test record get/set functions by purposely entering
 *              invalid record names and/or values
 * get_mlt: tests INKRecordGetMlt
 * set_mlt: tests INKRecordSetMlt
 *
 * read_url: tests INKReadFromUrl works by retrieving two valid urls
 * test_url: tests robustness of INKReadFromUrl using invalid urls
 *
 * CfgContext operations:
 * ---------------------
 * cfg_get:<config-filename>: prints out the rules in confg-filename
 * cfg:<config-filename>: switches the position of first and last rule of
 *                        <config-filename>
 * cfg_context: calls all the INKCfgCOntext helper function calls using
 *              vaddrs.config
 * cfg_socks: does some basic testing of socks.config (reads in file,
 *            modifies it, eg. add new rules, then commits changes)
 *
 * Event Operations:
 * ----------------
 * active_events: lists the names of all currently active events
 * MGMT_ALARM_xxx (event_name specified in CoreAPIShared.h or Alarms.h):
 *                 resolves the specified event
 * register: registers a generic callback (=eventCallbackFn) which
 *           prints out the event name whenever an event is signalled
 * unregister: unregisters the generic callback function eventCallbackFn
 *
 * Snapshot Operations:
 * --------------------
 * take_snap:<snap_name> - takes the snapshot snap_name
 * restore_snap:<snap_name> restores the snapshot snap_name
 * remove_snap:<snap_name> - removes the snapshot snap_name
 * snapshots - lists all snapshots in etc/trafficserver/snapshot directory
 *
 * Diags
 * ----
 * diags - uses STATUS, NOTE, FATAL, ERROR diags
 *
 * Statistics
 * ----------
 * set_stats - sets dummy values for selected group of NODE, CLUSTER, PROCESS
 *             records
 * print_stats - prints the values for the same selected group of records
 * reset_stats - resets all statistics to default values
 */

#include "ink_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include "ink_string.h"

#include "INKMgmtAPI.h"
#include "CfgContextUtils.h"

// refer to test_records() function
#define TEST_STRING     1
#define TEST_FLOAT      1
#define TEST_INT        1
#define TEST_COUNTER    1
#define TEST_REC_SET    1
#define TEST_REC_GET    0
#define TEST_REC_GET_2  0

// set to 1 if running as part of installation package
// set to 0 if being tested in developer environment
#define INSTALL_TEST    0

/***************************************************************************
 * Printing Helper Functions
 ***************************************************************************/

/* ------------------------------------------------------------------------
 * print_err
 * ------------------------------------------------------------------------
 * used to print the error description associated with the INKError err
 */
void
print_err(const char *module, INKError err)
{
  char *err_msg;

  err_msg = INKGetErrorMessage(err);
  printf("(%s) ERROR: %s\n", module, err_msg);

  if (err_msg)
    INKfree(err_msg);
}


/*--------------------------------------------------------------
 * print_ports
 *--------------------------------------------------------------*/
void
print_ports(INKPortList list)
{
  int i, count;
  INKPortEle *port_ele;

  count = INKPortListLen(list);
  for (i = 0; i < count; i++) {
    port_ele = INKPortListDequeue(list);
    printf(" %d \n", port_ele->port_a);
    if (port_ele->port_b != -1)
      printf(" %d - %d \n", port_ele->port_a, port_ele->port_b);
    INKPortListEnqueue(list, port_ele);
  }

  return;
}

/*-------------------------------------------------------------
 * print_string_list
 *-------------------------------------------------------------*/
void
print_string_list(INKStringList list)
{
  int i, count, buf_pos = 0;
  char *str;
  char buf[1000];

  if (!list)
    return;
  count = INKStringListLen(list);
  for (i = 0; i < count; i++) {
    str = INKStringListDequeue(list);
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s,", str);
    buf_pos = strlen(buf);
    INKStringListEnqueue(list, str);
  }
  printf("%s \n", buf);
}

/*-------------------------------------------------------------
 * print_int_list
 *-------------------------------------------------------------*/
void
print_int_list(INKIntList list)
{
  int i, count, buf_pos = 0;
  int *elem;
  char buf[1000];

  count = INKIntListLen(list);
  for (i = 0; i < count; i++) {
    elem = INKIntListDequeue(list);
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%d:", *elem);
    buf_pos = strlen(buf);
    INKIntListEnqueue(list, elem);
  }
  printf("Int List: %s \n", buf);
}

/*-------------------------------------------------------
 * print_domain_list
 *-------------------------------------------------------*/
void
print_domain_list(INKDomainList list)
{
  int i, count;
  INKDomain *proxy;

  count = INKDomainListLen(list);
  for (i = 0; i < count; i++) {
    proxy = INKDomainListDequeue(list);
    if (proxy->domain_val)
      printf("%s:%d\n", proxy->domain_val, proxy->port);
    INKDomainListEnqueue(list, proxy);
  }
}


void
print_ip_addr_ele(INKIpAddrEle * ele)
{
  if (!ele)
    return;

  if (ele->type == INK_IP_RANGE) {
    if (ele->cidr_a != -1)
      printf("IP_addr: %s/%d - %s/%d\n", ele->ip_a, ele->cidr_a, ele->ip_b, ele->cidr_b);
    else
      printf("IP_addr: %s - %s\n", ele->ip_a, ele->ip_b);
  } else {
    if (ele->cidr_a != -1)
      printf("IP_addr: %s/%d \n", ele->ip_a, ele->cidr_a);
    else
      printf("IP_addr: %s \n", ele->ip_a);
  }
}

/*-------------------------------------------------------
 * print_ip_list
 *-------------------------------------------------------*/
void
print_ip_list(INKIpAddrList list)
{
  int i, count;
  INKIpAddrEle *ele;

  count = INKIpAddrListLen(list);
  for (i = 0; i < count; i++) {
    ele = INKIpAddrListDequeue(list);
    print_ip_addr_ele(ele);

    INKIpAddrListEnqueue(list, ele);
  }
}

/*-------------------------------------------------------
 * print_list_of_ip_list
 *-------------------------------------------------------*/
void
print_list_of_ip_list(INKList list)
{
  int i, count;
  INKIpAddrList ele;

  count = INKIpAddrListLen(list);
  for (i = 0; i < count; i++) {
    ele = INKListDequeue(list);
    printf("\n");
    print_ip_list(ele);
    printf("\n");
    INKListEnqueue(list, ele);
  }

}


/*-------------------------------------------------------
 * print_pd_sspec
 *-------------------------------------------------------*/
void
print_pd_sspec(INKPdSsFormat info)
{
  switch (info.pd_type) {
  case INK_PD_DOMAIN:
    printf("Prime Dest: dest_domain=%s\n", info.pd_val);
    break;
  case INK_PD_HOST:
    printf("Prime Host: dest_host=%s\n", info.pd_val);
    break;
  case INK_PD_IP:
    printf("Prime IP: dest_ip=%s\n", info.pd_val);
    break;
  case INK_PD_URL_REGEX:
    printf("Prime Url regex: url_regex=%s\n", info.pd_val);
    break;
  default:
    break;
  }

  printf("Secondary Specifiers:\n");
  printf("\ttime: %d:%d-%d:%d\n",
         info.sec_spec.time.hour_a, info.sec_spec.time.min_a, info.sec_spec.time.hour_b, info.sec_spec.time.min_b);

  if (info.sec_spec.src_ip)
    printf("\tsrc_ip: %s\n", info.sec_spec.src_ip);

  if (info.sec_spec.prefix)
    printf("\tprefix: %s\n", info.sec_spec.prefix);

  if (info.sec_spec.suffix)
    printf("\tsuffix: %s\n", info.sec_spec.suffix);

  printf("\tport-a: %d\n", info.sec_spec.port->port_a);
  printf("\tport-b: %d\n", info.sec_spec.port->port_b);

  printf("\tmethod: ");
  switch (info.sec_spec.method) {
  case INK_METHOD_NONE:
    printf("NONE");
    break;
  case INK_METHOD_GET:
    printf("GET");
    break;
  case INK_METHOD_POST:
    printf("POST");
    break;
  case INK_METHOD_PUT:
    printf("PUT");
    break;
  case INK_METHOD_TRACE:
    printf("TRACE");
    break;
  case INK_METHOD_UNDEFINED:
    printf("UNDEFINED");
    break;
  default:
    // Handled here:
    // INK_METHOD_PUSH
    break;
  }
  printf("\n");


  printf("\tscheme: ");
  switch (info.sec_spec.scheme) {
  case INK_SCHEME_NONE:
    printf("NONE\n");
    break;
  case INK_SCHEME_HTTP:
    printf("HTTP\n");
    break;
  case INK_SCHEME_HTTPS:
    printf("HTTPS\n");
    break;
  case INK_SCHEME_RTSP:
  case INK_SCHEME_MMS:
    printf("MIXT\n");
    break;
  case INK_SCHEME_UNDEFINED:
    printf("UNDEFINED\n");
    break;
  }

  printf("\tmixt: ");
  switch (info.sec_spec.mixt) {
  case INK_MIXT_RNI:
    printf("RNI\n");
    break;
  case INK_MIXT_QT:
    printf("QT\n");
    break;
  case INK_MIXT_WMT:
    printf("WMT\n");
    break;
  case INK_MIXT_UNDEFINED:
    printf("UNDEFINED\n");
    break;
  }

  return;
}


//
// Ele printing functions
//

void
print_admin_access_ele(INKAdminAccessEle * ele)
{
  if (!ele) {
    fprintf(stderr, "print_admin_access_ele: ele is NULL\n");
    return;
  }

  char accessType;
  switch (ele->access) {
  case INK_ACCESS_NONE:
    accessType = '0';
    break;
  case INK_ACCESS_MONITOR:
    accessType = '1';
    break;
  case INK_ACCESS_MONITOR_VIEW:
    accessType = '2';
    break;
  case INK_ACCESS_MONITOR_CHANGE:
    accessType = '3';
    break;
  default:
    accessType = '?';           /* lv: to make gcc happy and don't brake fprintf */
    // Handled here:
    // INK_ACCESS_UNDEFINED
    break;
  }

  fprintf(stderr, "%s:%s:%c:\n", ele->user, ele->password, accessType);
  return;
}

void
print_arm_security_ele(INKArmSecurityEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  printf("Connection type: %d\n", ele->type_con);
  print_ip_addr_ele(ele->src_ip_addr);
  print_ip_addr_ele(ele->dest_ip_addr);
  print_ports(ele->open_ports);
  print_ports(ele->src_ports);
  print_ports(ele->dest_ports);
}

void
print_bypass_ele(INKBypassEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  char buf[MAX_BUF_SIZE];
  bzero(buf, MAX_BUF_SIZE);
  strncat(buf, "bypass ", sizeof(buf) - strlen(buf) - 1);
  char *str_list;

  if (ele->src_ip_addr) {
    snprintf(buf, sizeof(buf), "src ");
    str_list = ip_addr_list_to_string((IpAddrList *) ele->src_ip_addr, ",");
    strncat(buf, str_list, sizeof(buf) - strlen(buf) - 1);
    xfree(str_list);
  }

  if (ele->dest_ip_addr) {
    if (ele->src_ip_addr) {
      strncat(buf, " AND ", sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, "dst ", sizeof(buf) - strlen(buf) - 1);
    str_list = ip_addr_list_to_string((IpAddrList *) ele->dest_ip_addr, ",");
    strncat(buf, str_list, sizeof(buf) - strlen(buf) - 1);
    xfree(str_list);
  }

  printf("%s\n", buf);
  return;
}

void
print_cache_ele(INKCacheEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  char *pd_str, *time_str;

  char buf[MAX_BUF_SIZE];
  int buf_pos = 0;

  bzero(buf, MAX_BUF_SIZE);

  pd_str = pdest_sspec_to_string(ele->cache_info.pd_type, ele->cache_info.pd_val, &(ele->cache_info.sec_spec));
  if (!pd_str) {
    printf("pd_str is null.\n");
    return;
  }

  snprintf(buf, sizeof(buf), "%s ", pd_str);
  buf_pos = strlen(buf);
  xfree(pd_str);


  // now format the message
  switch (ele->cfg_ele.type) {
  case INK_CACHE_NEVER:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=never-cache");
    break;
  case INK_CACHE_IGNORE_NO_CACHE:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ignore-no-cache");
    break;
  case INK_CACHE_IGNORE_CLIENT_NO_CACHE:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ignore-server-no-cache");
    break;
  case INK_CACHE_IGNORE_SERVER_NO_CACHE:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ignore-client-no-cache");
    break;
  case INK_CACHE_PIN_IN_CACHE:
    time_str = hms_time_to_string(ele->time_period);
    if (!time_str)
      return;
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "pin-in-cache=%s", time_str);
    xfree(time_str);
    break;
  case INK_CACHE_REVALIDATE:
    time_str = hms_time_to_string(ele->time_period);
    if (!time_str)
      return;
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "revalidate=%s", time_str);
    xfree(time_str);
    break;
  default:                     /* invalid action directive */
    printf("hello..\n");
    return;
  }
  printf("%s\n", buf);

  /*
     print_pd_sspec(ele->cache_info);
     printf("Time: %d day, %d hr, %d min, %d sec\n", ele->time_period.d, ele->time_period.h,
     ele->time_period.m, ele->time_period.s);
   */
  return;
}

void
print_filter_ele(INKFilterEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  // now format the message
  switch (ele->cfg_ele.type) {
  case INK_FILTER_ALLOW:
    printf("action=allow\t");
    break;
  case INK_FILTER_DENY:
    printf("action=deny\t");
    break;
  case INK_FILTER_KEEP_HDR:
    printf("keep_hdr=\t");
    break;
  case INK_FILTER_STRIP_HDR:
    printf("strip_hdr=\t");
    break;
  default:                     /* invalid action directive */
    return;
  }

  // Just for keep_hdr or strip_hdr
  switch (ele->cfg_ele.type) {
  case INK_FILTER_KEEP_HDR:
  case INK_FILTER_STRIP_HDR:
    switch (ele->hdr) {
    case INK_HDR_DATE:
      printf("date\n");
      break;
    case INK_HDR_HOST:
      printf("host\n");
      break;
    case INK_HDR_COOKIE:
      printf("cookie\n");
      break;
    case INK_HDR_CLIENT_IP:
      printf("client_ip\n");
      break;
    default:
      return;
    }
  default:
    break;
  }

  print_pd_sspec(ele->filter_info);

  return;
}

void
print_hosting_ele(INKHostingEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  switch (ele->pd_type) {
  case INK_PD_DOMAIN:
    printf("dest_domain=%s\n", ele->pd_val);
    break;
  case INK_PD_HOST:
    printf("dest_host=%s\n", ele->pd_val);
    break;
  case INK_PD_IP:
    printf("ip=%s\n", ele->pd_val);
    break;
  case INK_PD_URL_REGEX:
    printf("url_regex=%s\n", ele->pd_val);
    break;
  default:
    printf("INVALID Prime Dest specifier\n");
    break;
  }

  print_int_list(ele->partitions);
}

void
print_icp_ele(INKIcpEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  int peer_type;

  switch (ele->peer_type) {
  case INK_ICP_PARENT:
    peer_type = 1;
    break;
  case INK_ICP_SIBLING:
    peer_type = 2;
    break;

  default:
    peer_type = 10;
  }

  printf("%s:%s:%d:%d:%d:%d:%s:%d:\n",
         ele->peer_hostname,
         ele->peer_host_ip_addr,
         peer_type,
         ele->peer_proxy_port,
         ele->peer_icp_port,
         (ele->is_multicast ? 1 : 0), ele->mc_ip_addr, (ele->mc_ttl == INK_MC_TTL_SINGLE_SUBNET ? 1 : 2)
    );
}

void
print_ip_allow_ele(INKIpAllowEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  print_ip_addr_ele(ele->src_ip_addr);
}

void
print_ipnat_ele(INKIpFilterEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }


  printf("%s; %s:%d; %s:%d\n", ele->intr, ele->src_ip_addr, ele->src_port, ele->dest_ip_addr, ele->dest_port);
}

void
print_mgmt_allow_ele(INKMgmtAllowEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  print_ip_addr_ele(ele->src_ip_addr);
}

void
print_parent_ele(INKParentProxyEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  printf("parent rule type: %d\n", ele->cfg_ele.type);
  print_pd_sspec(ele->parent_info);
  printf("round robin? %d\n", ele->rr);
  print_domain_list(ele->proxy_list);
  printf("direct? %d\n", ele->direct);

}

void
print_partition_ele(INKPartitionEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  printf("partition #: %d\n", ele->partition_num);
  printf("scheme: %d\n", ele->scheme);
  switch (ele->size_format) {
  case INK_SIZE_FMT_ABSOLUTE:
    printf("partition_size=%d\n", ele->partition_size);
    break;
  case INK_SIZE_FMT_PERCENT:
    printf("partition_size=%% %d\n", ele->partition_size);
    break;
  default:
    // Handled here:
    // INK_SIZE_FMT_UNDEFINED
    break;
  }
}

void
print_plugin_ele(INKPluginEle * ele)
{
  if (!ele) {
    printf("can't print plugin ele\n");
  }

  printf("name: %s\t\t", ele->name);
  if (ele->args) {
    printf("args: ");
    print_string_list(ele->args);
  } else {
    printf("NO ARGS\n");
  }
}

void
print_remap_ele(INKRemapEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  char buf[MAX_BUF_SIZE];
  bzero(buf, MAX_BUF_SIZE);

  switch (ele->cfg_ele.type) {
  case INK_REMAP_MAP:
    strncat(buf, "map", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_REMAP_REVERSE_MAP:
    strncat(buf, "reverse_map", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_REMAP_REDIRECT:
    strncat(buf, "redirect", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_REMAP_REDIRECT_TEMP:
    strncat(buf, "redirect_temporary", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }
  // space delimitor
  strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);

  // from scheme
  switch (ele->from_scheme) {
  case INK_SCHEME_HTTP:
    strncat(buf, "http", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_HTTPS:
    strncat(buf, "https", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_RTSP:
    strncat(buf, "rtsp", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_MMS:
    strncat(buf, "mms", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_SCHEME_NONE, INK_SCHEME_UNDEFINED, INK_SCHEME_NONE,
    // INK_SCHEME_UNDEFINED
    break;
  }
  strncat(buf, "://", sizeof(buf) - strlen(buf) - 1);

  // from host
  if (ele->from_host) {
    strncat(buf, ele->from_host, sizeof(buf) - strlen(buf) - 1);
  }
  // from port
  if (ele->from_port != INK_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, ele->from_port);
  }
  // from host path
  if (ele->from_path_prefix) {
    strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, ele->from_path_prefix, sizeof(buf) - strlen(buf) - 1);
  }
  // space delimitor
  strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);

  // to scheme
  switch (ele->to_scheme) {
  case INK_SCHEME_HTTP:
    strncat(buf, "http", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_HTTPS:
    strncat(buf, "https", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_RTSP:
    strncat(buf, "rtsp", sizeof(buf) - strlen(buf) - 1);
    break;
  case INK_SCHEME_MMS:
    strncat(buf, "mms", sizeof(buf) - strlen(buf) - 1);
    break;
  default:
    // Handled here:
    // INK_SCHEME_NONE, INK_SCHEME_UNDEFINED
    break;
  }
  strncat(buf, "://", sizeof(buf) - strlen(buf) - 1);

  // to host
  if (ele->to_host) {
    strncat(buf, ele->to_host, sizeof(buf) - strlen(buf) - 1);
  }
  // to port
  if (ele->to_port != INK_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, ele->to_port);
  }
  // to host path
  if (ele->to_path_prefix) {
    strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, ele->to_path_prefix, sizeof(buf) - strlen(buf) - 1);
  }

  printf("%s\n", buf);
  return;
}

void
print_socks_ele(INKSocksEle * ele)
{
  printf("\n");
  if (!ele) {
    printf("can't print ele\n");
  } else if (ele->ip_addrs) {
    print_ip_list(ele->ip_addrs);
    printf("\n");
  } else {
    print_ip_addr_ele(ele->dest_ip_addr);
    print_domain_list(ele->socks_servers);
    printf("round_robin=%d\n", ele->rr);
  }
}

void
print_split_dns_ele(INKSplitDnsEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  char *str;
  char buf[MAX_BUF_SIZE];
  bzero(buf, MAX_BUF_SIZE);

  char *pd_name = 0;
  switch (ele->pd_type) {
  case INK_PD_DOMAIN:
    pd_name = xstrdup("dest_domain");
    break;
  case INK_PD_HOST:
    pd_name = xstrdup("dest_host");
    break;
  case INK_PD_URL_REGEX:
    pd_name = xstrdup("url_regex");
    break;
  default:
    pd_name = xstrdup("?????");
    // Handled here:
    // INK_PD_IP, INK_PD_UNDEFINED
    break;
  }

  if (ele->pd_val) {
    strncat(buf, pd_name, sizeof(buf) - strlen(buf) - 1);
    strncat(buf, "=", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, ele->pd_val, sizeof(buf) - strlen(buf) - 1);
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }

  if (ele->dns_servers_addrs) {
    strncat(buf, "named=", sizeof(buf) - strlen(buf) - 1);
    str = ip_addr_list_to_string((LLQ *) ele->dns_servers_addrs, " ");
    strncat(buf, str, sizeof(buf) - strlen(buf) - 1);
    xfree(str);
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }

  if (ele->def_domain) {
    strncat(buf, "dns_server=", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, ele->def_domain, sizeof(buf) - strlen(buf) - 1);
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }

  if (ele->search_list) {
    strncat(buf, "search_list=", sizeof(buf) - strlen(buf) - 1);
    str = domain_list_to_string(ele->search_list, " ");
    strncat(buf, str, sizeof(buf) - strlen(buf) - 1);
    xfree(str);
    strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
  }
  printf("%s\n", buf);

  if (pd_name)
    xfree(pd_name);

  return;
}

void
print_storage_ele(INKStorageEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  if (ele->pathname)
    printf("pathname=%s, size=%d\n", ele->pathname, ele->size);
}

void
print_update_ele(INKUpdateEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  printf("url: %s\n", ele->url);
  printf("headers: ");
  print_string_list(ele->headers);
  printf("%d: %d: %d\n", ele->offset_hour, ele->interval, ele->recursion_depth);
}

void
print_vaddrs_ele(INKVirtIpAddrEle * ele)
{
  if (!ele) {
    printf("can't print ele\n");
  }

  printf("ip=%s, intr=%s, sub_intr=%d\n", ele->ip_addr, ele->intr, ele->sub_intr);
}

//
// print_ele_list
//
// prints a list of Ele's
//

void
print_ele_list(INKFileNameT file, INKCfgContext ctx)
{
  INKFileNameT filename = file;
  INKCfgEle *ele;

  if (!ctx) {
    printf("[print_ele_list] invalid INKCFgContext\n");
    return;
  }

  int count = INKCfgContextGetCount(ctx);
  printf("\n[print_ele_list] %d rules\n", count);
  for (int i = 0; i < count; i++) {
    ele = INKCfgContextGetEleAt(ctx, i);

    switch (filename) {
    case INK_FNAME_ADMIN_ACCESS:
      print_admin_access_ele((INKAdminAccessEle *) ele);
      break;
    case INK_FNAME_CACHE_OBJ:
      print_cache_ele((INKCacheEle *) ele);
      break;
    case INK_FNAME_FILTER:
      print_filter_ele((INKFilterEle *) ele);
      break;
    case INK_FNAME_HOSTING:
      print_hosting_ele((INKHostingEle *) ele);
      break;
    case INK_FNAME_ICP_PEER:
      print_icp_ele((INKIcpEle *) ele);
      break;
    case INK_FNAME_IP_ALLOW:
      print_ip_allow_ele((INKIpAllowEle *) ele);
      break;
    case INK_FNAME_LOGS_XML:
      break;                    /*NOT DONE */
    case INK_FNAME_MGMT_ALLOW:
      print_mgmt_allow_ele((INKMgmtAllowEle *) ele);
      break;
    case INK_FNAME_PARENT_PROXY:
      print_parent_ele((INKParentProxyEle *) ele);
      break;
    case INK_FNAME_PARTITION:
      print_partition_ele((INKPartitionEle *) ele);
      break;
    case INK_FNAME_PLUGIN:
      print_plugin_ele((INKPluginEle *) ele);
      break;
    case INK_FNAME_REMAP:
      print_remap_ele((INKRemapEle *) ele);
      break;
    case INK_FNAME_SOCKS:
      print_socks_ele((INKSocksEle *) ele);
      break;
    case INK_FNAME_SPLIT_DNS:
      print_split_dns_ele((INKSplitDnsEle *) ele);
      break;
    case INK_FNAME_STORAGE:
      print_storage_ele((INKStorageEle *) ele);
      break;
    case INK_FNAME_UPDATE_URL:
      print_update_ele((INKUpdateEle *) ele);
      break;
    case INK_FNAME_VADDRS:
      print_vaddrs_ele((INKVirtIpAddrEle *) ele);
    default:
      printf("[print_ele_list] invalid file type \n");
      return;
    }
  }

  return;
}


/***************************************************************************
 * Control Testing
 ***************************************************************************/
void
print_proxy_state()
{
  INKProxyStateT state = INKProxyStateGet();

  switch (state) {
  case INK_PROXY_ON:
    printf("Proxy State = ON\n");
    break;
  case INK_PROXY_OFF:
    printf("Proxy State = OFF\n");
    break;
  default:
    printf("ERROR: Proxy State Undefined!\n");
    break;
  }
}

// starts Traffic Server (turns proxy on)
void
start_TS(char *tsArgs)
{
  INKError ret;
  INKCacheClearT clear = INK_CACHE_CLEAR_OFF;

  char *args = strtok(tsArgs, ":");
  args = strtok(NULL, ":");
  if (args) {
    if (strcmp(args, "all\n") == 0)
      clear = INK_CACHE_CLEAR_ON;
    else if (strcmp(args, "hostdb\n") == 0)
      clear = INK_CACHE_CLEAR_HOSTDB;
  } else {
    clear = INK_CACHE_CLEAR_OFF;
  }

  printf("STARTING PROXY with cache: %d\n", clear);
  if ((ret = INKProxyStateSet(INK_PROXY_ON, clear)) != INK_ERR_OKAY)
    printf("[INKProxyStateSet] turn on FAILED\n");
  print_err("start_TS", ret);
}

// stops Traffic Server (turns proxy off)
void
stop_TS()
{
  INKError ret;

  printf("STOPPING PROXY\n");
  if ((ret = INKProxyStateSet(INK_PROXY_OFF, INK_CACHE_CLEAR_OFF)) != INK_ERR_OKAY)
    printf("[INKProxyStateSet] turn off FAILED\n");
  print_err("stop_TS", ret);
}

// restarts Traffic Manager cluster wide (Traffic Cop must be running)
void
restart()
{
  INKError ret;

  printf("RESTART - Cluster wide\n");
  if ((ret = INKRestart(true)) != INK_ERR_OKAY)
    printf("[INKRestart] FAILED\n");

  print_err("restart", ret);
}

// rereads all the configuration files
void
reconfigure()
{
  INKError ret;

  printf("RECONFIGURE\n");
  if ((ret = INKReconfigure()) != INK_ERR_OKAY)
    printf("[INKReconfigure] FAILED\n");

  print_err("reconfigure", ret);
}

// currently does nothing
void
hard_restart()
{
  INKError ret;

  printf("[hard_restart]Restart Traffic Cop\n");
  if ((ret = INKHardRestart()) != INK_ERR_OKAY)
    printf("[INKHardRestart] FAILED\n");

  print_err("hard_restart", ret);
}


/* ------------------------------------------------------------------------
 * test_action_need
 * ------------------------------------------------------------------------
 * tests if correct action need is returned when requested record is set
 */
void
test_action_need(void)
{
  INKActionNeedT action;

  // RU_NULL record
  INKRecordSetString("proxy.config.proxy_name", "proxy_dorky", &action);
  printf("[INKRecordSetString] proxy.config.proxy_name \n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_UNDEFINED, action);

  // RU_REREAD record
  INKRecordSetInt("proxy.config.ldap.cache.size", 1000, &action);
  printf("[INKRecordSetInt] proxy.config.ldap.cache.size\n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_RECONFIGURE, action);

  // RU_RESTART_TS record
  INKRecordSetInt("proxy.config.cluster.cluster_port", 6666, &action);
  printf("[INKRecordSetInt] proxy.config.cluster.cluster_port\n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_RESTART, action);

}

/***************************************************************************
 * Record Testing
 ***************************************************************************/

/* ------------------------------------------------------------------------
 * test_error_records
 * ------------------------------------------------------------------------
 * stress test error handling by purposely being dumb; send requests to
 * get invalid record names
 */
void
test_error_records()
{
  INKInt port1, new_port = 8080;
  INKActionNeedT action;
  INKError ret;
  INKFloat flt1;
  INKCounter ctr1;

  printf("\n");
  // test get integer
  fprintf(stderr, "Test invalid record names\n");

  ret = INKRecordGetInt("proy.config.cop.core_signal", &port1);
  if (ret != INK_ERR_OKAY) {
    print_err("INKRecordGetInt", ret);
  } else
    printf("[INKRecordGetInt] proxy.config.cop.core_signal=%lld \n", port1);

  // test set integer
  ret = INKRecordSetInt("proy.config.cop.core_signal", new_port, &action);
  print_err("INKRecordSetInt", ret);


  printf("[INKRecordSetString] proxy.config.ldap.proc.ldap.server.name = invalid name\n");
  ret = INKRecordSetString("proxy.config.ldap.proc.ldap.server.name", "invalid name", &action);
  print_err("INKRecordSetString", ret);

  printf("\n");
  if (INKRecordGetCounter("proxy.press.socks.connections_successful", &ctr1) != INK_ERR_OKAY)
    printf("INKRecordGetCounter FAILED!\n");
  else
    printf("[INKRecordGetCounter]proxy.process.socks.connections_successful=%lld \n", ctr1);

  printf("\n");
  if (INKRecordGetFloat("proxy.conig.http.cache.fuzz.probability", &flt1) != INK_ERR_OKAY)
    printf("INKRecordGetFloat FAILED!\n");
  else
    printf("[INKRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt1);

}

/* ------------------------------------------------------------------------
 * test_records
 * ------------------------------------------------------------------------
 * stress test record functionality by getting and setting different
 * records types; use the #defines defined above to determine which
 * type of tests you'd like turned on/off
 */
void
test_records()
{
  INKActionNeedT action;
  char *rec_value;
  char new_str[] = "new_record_value";
  INKInt port1, port2, new_port = 52432;
  INKFloat flt1, flt2, new_flt = 1.444;
  INKCounter ctr1, ctr2, new_ctr = 6666;
  INKError err;

  /********************* START TEST SECTION *****************/
  printf("\n\n");

#if SET_INT
  // test set integer
  if (INKRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != INK_ERR_OKAY)
    printf("INKRecordSetInt FAILED!\n");
  else
    printf("[INKRecordSetInt] proxy.config.cop.core_signal=%lld \n", new_port);
#endif


#if TEST_REC_GET
  INKRecordEle *rec_ele;
  // retrieve a string value record using generic RecordGet
  rec_ele = INKRecordEleCreate();
  if (INKRecordGet("proxy.config.http.cache.vary_default_other", rec_ele) != INK_ERR_OKAY)
    printf("INKRecordGet FAILED!\n");
  else
    printf("[INKRecordGet] proxy.config.http.cache.vary_default_other=%s\n", rec_ele->string_val);

  INKRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif


#if TEST_REC_GET_2
  // retrieve a string value record using generic RecordGet
  rec_ele = INKRecordEleCreate();
  if (INKRecordGet("proxy.config.proxy_name", rec_ele) != INK_ERR_OKAY)
    printf("INKRecordGet FAILED!\n");
  else
    printf("[INKRecordGet] proxy.config.proxy_name=%s\n", rec_ele->string_val);

  INKRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif

#if TEST_STRING
  // retrieve an string value record using GetString
  err = INKRecordGetString("proxy.config.proxy_name", &rec_value);
  if (err != INK_ERR_OKAY)
    print_err("INKRecordGetString", err);
  else
    printf("[INKRecordGetString] proxy.config.proxy_name=%s\n", rec_value);
  INKfree(rec_value);

  // test RecordSet
  err = INKRecordSetString("proxy.config.proxy_name", (INKString) new_str, &action);
  if (err != INK_ERR_OKAY)
    print_err("INKRecordSetString", err);
  else
    printf("[INKRecordSetString] proxy.config.proxy_name=%s\n", new_str);


  // get
  err = INKRecordGetString("proxy.config.proxy_name", &rec_value);
  if (err != INK_ERR_OKAY)
    print_err("INKRecordGetString", err);
  else
    printf("[INKRecordGetString] proxy.config.proxy_name=%s\n", rec_value);
  printf("\n");
  INKfree(rec_value);
#endif

#if TEST_INT
  printf("\n");
  // test get integer
  if (INKRecordGetInt("proxy.config.cop.core_signal", &port1) != INK_ERR_OKAY)
    printf("INKRecordGetInt FAILED!\n");
  else
    printf("[INKRecordGetInt] proxy.config.cop.core_signal=%lld \n", port1);

  // test set integer
  if (INKRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != INK_ERR_OKAY)
    printf("INKRecordSetInt FAILED!\n");
  else
    printf("[INKRecordSetInt] proxy.config.cop.core_signal=%lld \n", new_port);

  if (INKRecordGetInt("proxy.config.cop.core_signal", &port2) != INK_ERR_OKAY)
    printf("INKRecordGetInt FAILED!\n");
  else
    printf("[INKRecordGetInt] proxy.config.cop.core_signal=%lld \n", port2);
  printf("\n");
#endif

#if TEST_COUNTER
  printf("\n");

  if (INKRecordGetCounter("proxy.process.socks.connections_successful", &ctr1) != INK_ERR_OKAY)
    printf("INKRecordGetCounter FAILED!\n");
  else
    printf("[INKRecordGetCounter]proxy.process.socks.connections_successful=%lld \n", ctr1);

  if (INKRecordSetCounter("proxy.process.socks.connections_successful", new_ctr, &action) != INK_ERR_OKAY)
    printf("INKRecordSetCounter FAILED!\n");
  else
    printf("[INKRecordSetCounter] proxy.process.socks.connections_successful=%lld \n", new_ctr);

  if (INKRecordGetCounter("proxy.process.socks.connections_successful", &ctr2) != INK_ERR_OKAY)
    printf("INKRecordGetCounter FAILED!\n");
  else
    printf("[INKRecordGetCounter]proxy.process.socks.connections_successful=%lld \n", ctr2);
  printf("\n");
#endif

#if TEST_FLOAT
  printf("\n");
  if (INKRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt1) != INK_ERR_OKAY)
    printf("INKRecordGetFloat FAILED!\n");
  else
    printf("[INKRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt1);

  if (INKRecordSetFloat("proxy.config.http.cache.fuzz.probability", new_flt, &action) != INK_ERR_OKAY)
    printf("INKRecordSetFloat FAILED!\n");
  else
    printf("[INKRecordSetFloat] proxy.config.http.cache.fuzz.probability=%f\n", new_flt);

  if (INKRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt2) != INK_ERR_OKAY)
    printf("INKRecordGetFloat FAILED!\n");
  else
    printf("[INKRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt2);
  printf("\n");
#endif

#if TEST_REC_SET
  printf("\n");
  if (INKRecordSet("proxy.config.http.cache.fuzz.probability", "-0.3456", &action) != INK_ERR_OKAY)
    printf("INKRecordSet FAILED!\n");
  else
    printf("[INKRecordSet] proxy.config.http.cache.fuzz.probability=-0.3456\n");

  if (INKRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt2) != INK_ERR_OKAY)
    printf("INKRecordGetFloat FAILED!\n");
  else
    printf("[INKRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt2);
#endif

}

// retrieves the value of the "proxy.config.xxx" record requested at input
void
test_rec_get(char *rec_name)
{
  INKRecordEle *rec_ele;
  INKError ret;
  char *name;

  name = (char *) INKmalloc(sizeof(char) * (strlen(rec_name)));
  strncpy(name, rec_name, strlen(rec_name) - 1);
  name[strlen(rec_name) - 1] = '\0';
  printf("[test_rec_get] Get Record: %s\n", name);

  // retrieve a string value record using generic RecordGet
  rec_ele = INKRecordEleCreate();
  if ((ret = INKRecordGet(name, rec_ele)) != INK_ERR_OKAY)
    printf("INKRecordGet FAILED!\n");
  else {
    switch (rec_ele->rec_type) {
    case INK_REC_INT:
      printf("[INKRecordGet] %s=%lld\n", name, rec_ele->int_val);
      break;
    case INK_REC_COUNTER:
      printf("[INKRecordGet] %s=%lld\n", name, rec_ele->counter_val);
      break;
    case INK_REC_FLOAT:
      printf("[INKRecordGet] %s=%f\n", name, rec_ele->float_val);
      break;
    case INK_REC_STRING:
      printf("[INKRecordGet] %s=%s\n", name, rec_ele->string_val);
      break;
    default:
      // Handled here:
      // INK_REC_UNDEFINED
      break;
    }
  }

  print_err("INKRecordGet", ret);

  INKRecordEleDestroy(rec_ele);
  INKfree(name);
}

/* ------------------------------------------------------------------------
 * test_record_get_mlt
 * ------------------------------------------------------------------------
 * Creates a list of record names to retrieve, and then batch request to
 * get list of records
 */
void
test_record_get_mlt(void)
{
  INKRecordEle *rec_ele;
  INKStringList name_list;
  INKList rec_list;
  int i, num;
  char *v1, *v2, *v3, *v4, *v5, *v6, *v7, *v8;
  INKError ret;

  name_list = INKStringListCreate();
  rec_list = INKListCreate();

  const size_t v1_size = (sizeof(char) * (strlen("proxy.config.proxy_name") + 1));
  v1 = (char *) INKmalloc(v1_size);
  ink_strncpy(v1, "proxy.config.proxy_name", v1_size);
  const size_t v2_size = (sizeof(char) * (strlen("proxy.config.bin_path") + 1));
  v2 = (char *) INKmalloc(v2_size);
  ink_strncpy(v2, "proxy.config.bin_path", v2_size);
  const size_t v3_size = (sizeof(char) * (strlen("proxy.config.manager_binary") + 1));
  v3 = (char *) INKmalloc(v3_size);
  ink_strncpy(v3, "proxy.config.manager_binary", v3_size);
  const size_t v4_size = (sizeof(char) * (strlen("proxy.config.cli_binary") + 1));
  v4 = (char *) INKmalloc(v4_size);
  ink_strncpy(v4, "proxy.config.cli_binary", v4_size);
  const size_t v5_size = (sizeof(char) * (strlen("proxy.config.watch_script") + 1));
  v5 = (char *) INKmalloc(v5_size);
  ink_strncpy(v5, "proxy.config.watch_script", v5_size);
  const size_t v6_size = (sizeof(char) * (strlen("proxy.config.env_prep") + 1));
  v6 = (char *) INKmalloc(v6_size);
  ink_strncpy(v6, "proxy.config.env_prep", v6_size);
  const size_t v7_size = (sizeof(char) * (strlen("proxy.config.cop.core_signal") + 1));
  v7 = (char *) INKmalloc(v7_size);
  ink_strncpy(v7, "proxy.config.cop.core_signal", v7_size);
  const size_t v8_size = (sizeof(char) * (strlen("proxy.config.http.cache.fuzz.probability") + 1));
  v8 = (char *) INKmalloc(v8_size);
  ink_strncpy(v8, "proxy.config.http.cache.fuzz.probability", v8_size);

  // add the names to the get_list
  INKStringListEnqueue(name_list, v1);
  INKStringListEnqueue(name_list, v2);
  INKStringListEnqueue(name_list, v3);
  INKStringListEnqueue(name_list, v4);
  INKStringListEnqueue(name_list, v5);
  INKStringListEnqueue(name_list, v6);
  INKStringListEnqueue(name_list, v7);
  INKStringListEnqueue(name_list, v8);


  num = INKStringListLen(name_list);
  printf("Num Records to Get: %d\n", num);
  ret = INKRecordGetMlt(name_list, rec_list);
  // free the string list
  INKStringListDestroy(name_list);
  if (ret != INK_ERR_OKAY) {
    print_err("INKStringListDestroy", ret);
  }

  for (i = 0; i < num; i++) {
    rec_ele = (INKRecordEle *) INKListDequeue(rec_list);
    if (!rec_ele) {
      printf("ERROR\n");
      break;
    }
    printf("Record: %s = ", rec_ele->rec_name);
    switch (rec_ele->rec_type) {
    case INK_REC_INT:
      printf("%lld\n", rec_ele->int_val);
      break;
    case INK_REC_COUNTER:
      printf("%lld\n", rec_ele->counter_val);
      break;
    case INK_REC_FLOAT:
      printf("%f\n", rec_ele->float_val);
      break;
    case INK_REC_STRING:
      printf("%s\n", rec_ele->string_val);
      break;
    default:
      // Handled here:
      // INK_REC_UNDEFINED
      break;
    }
    INKRecordEleDestroy(rec_ele);
  }

  INKListDestroy(rec_list);     // must dequeue and free each string individually

  return;
}

/* ------------------------------------------------------------------------
 * test_record_set_mlt
 * ------------------------------------------------------------------------
 * Creates a list of INKRecordEle's, and then batch request to set records
 * Also checks to make sure correct action_need type is set.
 */
void
test_record_set_mlt(void)
{
  INKList list;
  INKRecordEle *ele1, *ele2, *ele3, *ele4, *ele5;
  INKActionNeedT action = INK_ACTION_UNDEFINED;
  INKError err;

  list = INKListCreate();

  ele1 = INKRecordEleCreate();  // INK_TYPE_UNDEFINED action
  const size_t ele1_rec_name_size = (sizeof(char) * (strlen("proxy.config.cli_binary") + 1));
  ele1->rec_name = (char *) INKmalloc(ele1_rec_name_size);
  ink_strncpy(ele1->rec_name, "proxy.config.cli_binary", ele1_rec_name_size);
  ele1->rec_type = INK_REC_STRING;
  ele1->string_val = INKstrdup(ele1->rec_name);

  ele2 = INKRecordEleCreate();  // reread action
  const size_t ele2_rec_name_size = (sizeof(char) * (strlen("proxy.config.http.cache.fuzz.probability") + 1));
  ele2->rec_name = (char *) INKmalloc(ele2_rec_name_size);
  ink_strncpy(ele2->rec_name, "proxy.config.http.cache.fuzz.probability", ele2_rec_name_size);
  ele2->rec_type = INK_REC_FLOAT;
  ele2->float_val = 0.1234;

  ele3 = INKRecordEleCreate();  // undefined action
  const size_t ele3_rec_name_size = (sizeof(char) * (strlen("proxy.config.cop.core_signal") + 1));
  ele3->rec_name = (char *) INKmalloc(ele3_rec_name_size);
  ink_strncpy(ele3->rec_name, "proxy.config.cop.core_signal", ele3_rec_name_size);
  ele3->rec_type = INK_REC_INT;
  ele3->int_val = -4;

  ele4 = INKRecordEleCreate();  //restart TM
  const size_t ele4_rec_name_size = (sizeof(char) * (strlen("proxy.local.cluster.type") + 1));
  ele4->rec_name = (char *) INKmalloc(ele4_rec_name_size);
  ink_strncpy(ele4->rec_name, "proxy.local.cluster.type", ele4_rec_name_size);
  ele4->rec_type = INK_REC_INT;
  ele4->int_val = 2;

  ele5 = INKRecordEleCreate();  // reread action
  const size_t ele5_rec_name_size = (sizeof(char) * (strlen("proxy.config.cluster.mc_ttl") + 1));
  ele5->rec_name = (char *) INKmalloc(ele5_rec_name_size);
  ink_strncpy(ele5->rec_name, "proxy.config.cluster.mc_ttl", ele5_rec_name_size);
  ele5->rec_type = INK_REC_INT;
  ele5->int_val = 555;


  INKListEnqueue(list, ele4);
  INKListEnqueue(list, ele1);
  INKListEnqueue(list, ele2);
  INKListEnqueue(list, ele3);
  INKListEnqueue(list, ele5);

  err = INKRecordSetMlt(list, &action);
  print_err("INKRecordSetMlt", err);
  fprintf(stderr, "[INKRecordSetMlt] Action Required: %d\n", action);

  // cleanup: need to iterate through list and delete each ele
  int count = INKListLen(list);
  for (int i = 0; i < count; i++) {
    INKRecordEle *ele = (INKRecordEle *) INKListDequeue(list);
    INKRecordEleDestroy(ele);
  }
  INKListDestroy(list);
}


/***************************************************************************
 * File I/O Testing
 ***************************************************************************/

// if valid==true, then use a valid url to read
void
test_read_url(bool valid)
{
  char *header = NULL;
  int headerSize;
  char *body = NULL;
  int bodySize;
  INKError err;

  if (!valid) {
    // first try

    err = INKReadFromUrlEx("hsdfasdf.com:80/index.html", &header, &headerSize, &body, &bodySize, 50000);
    if (err != INK_ERR_OKAY) {
      print_err("INKReadFromUrlEx", err);
    } else {
      printf("--------------------------------------------------------------\n");
      //  printf("The header...\n%s\n%d\n", *header, *headerSize);
      printf("--------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (body)
      INKfree(body);
    if (header)
      INKfree(header);

    err = INKReadFromUrlEx("http://sadfasdfi.com:80/", &header, &headerSize, &body, &bodySize, 50000);
    if (err != INK_ERR_OKAY) {
      print_err("INKReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header)
      INKfree(header);
    if (body)
      INKfree(body);

  } else {                      // use valid urls
    err = INKReadFromUrlEx("lakota.inktomi.com:80/", &header, &headerSize, &body, &bodySize, 50000);

    if (err != INK_ERR_OKAY) {
      print_err("INKReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header)
      INKfree(header);
    if (body)
      INKfree(body);

    // read second url
    err = INKReadFromUrlEx("http://www.apache.org:80/index.html", &header, &headerSize, &body, &bodySize, 50000);
    if (err != INK_ERR_OKAY) {
      print_err("INKReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header)
      INKfree(header);
    if (body)
      INKfree(body);
  }
}

/* ------------------------------------------------------------------------
 * test_read_file
 * ------------------------------------------------------------------------
 * reads filter.config file and prints it out
 */
void
test_read_file()
{
  char *f_text = NULL;
  int f_size = -1;
  int f_ver = -1;

  printf("\n");
  if (INKConfigFileRead(INK_FNAME_FILTER, &f_text, &f_size, &f_ver) != INK_ERR_OKAY)
    printf("[INKConfigFileRead] FAILED!\n");
  else {
    printf("[INKConfigFileRead]\n\tFile Size=%d, Version=%d\n%s\n", f_size, f_ver, f_text);
    INKfree(f_text);
  }

}

/* ------------------------------------------------------------------------
 * test_write_file
 * ------------------------------------------------------------------------
 * writes filter.config with some garbage text then reads the file and
 * prints the new file to stdout
 */
void
test_write_file()
{
  char *f_text = NULL;
  int f_size = -1;
  int f_ver = -1;
  char new_f_text[] = "blah, blah blah\n I hope this works. please!!!   \n";
  int new_f_size = strlen(new_f_text);

  printf("\n");
  if (INKConfigFileWrite(INK_FNAME_FILTER, new_f_text, new_f_size, -1) != INK_ERR_OKAY)
    printf("[INKConfigFileWrite] FAILED!\n");
  else
    printf("[INKConfigFileWrite] SUCCESS!\n");
  printf("\n");

  // should free f_text???
  if (INKConfigFileRead(INK_FNAME_FILTER, &f_text, &f_size, &f_ver) != INK_ERR_OKAY)
    printf("[INKConfigFileRead] FAILED!\n");
  else {
    printf("[INKConfigFileRead]\n\tFile Size=%d, Version=%d\n%s\n", f_size, f_ver, f_text);
    INKfree(f_text);
  }
}

/***************************************************************************
 * INKCfgContext Testing
 ***************************************************************************/
// tests the INKCfgContextMoveEleUp/Down functions (which end up calling
// the new "copy" utility functions in CfgContextUtils.cc

// uses INKCfgContextGet to read in a file and print out all the rules
void
test_cfg_context_get(char *args)
{
  INKCfgContext ctx;
  INKFileNameT file;

  char *filename = strtok(args, ":");
  filename = strtok(NULL, ":");
  fprintf(stderr, "modify file: %s\n", filename);
  char *name = (char *) INKmalloc(sizeof(char) * (strlen(filename)));
  ink_strncpy(name, filename, strlen(filename));
  name[strlen(filename) - 1] = '\0';

  // convert file name to INKFileNameT
  if (strcmp(name, "admin_access.config") == 0) {
    file = INK_FNAME_ADMIN_ACCESS;
  } else if (strcmp(name, "cache.config") == 0) {
    file = INK_FNAME_CACHE_OBJ;
  } else if (strcmp(name, "congestion.config") == 0) {
    file = INK_FNAME_CONGESTION;
  } else if (strcmp(name, "filter.config") == 0) {
    file = INK_FNAME_FILTER;
  } else if (strcmp(name, "hosting.config") == 0) {
    file = INK_FNAME_HOSTING;
  } else if (strcmp(name, "icp.config") == 0) {
    file = INK_FNAME_ICP_PEER;
  } else if (strcmp(name, "ip_allow.config") == 0) {
    file = INK_FNAME_IP_ALLOW;
  } else if (strcmp(name, "logs_xml.config") == 0) {
    file = INK_FNAME_LOGS_XML;
  } else if (strcmp(name, "mgmt_allow.config") == 0) {
    file = INK_FNAME_MGMT_ALLOW;
  } else if (strcmp(name, "parent.config") == 0) {
    file = INK_FNAME_PARENT_PROXY;
  } else if (strcmp(name, "partition.config") == 0) {
    file = INK_FNAME_PARTITION;
  } else if (strcmp(name, "plugin.config") == 0) {
    file = INK_FNAME_PLUGIN;
  } else if (strcmp(name, "remap.config") == 0) {
    file = INK_FNAME_REMAP;
  } else if (strcmp(name, "socks.config") == 0) {
    file = INK_FNAME_SOCKS;
  } else if (strcmp(name, "storage.config") == 0) {
    file = INK_FNAME_STORAGE;
  } else if (strcmp(name, "splitdns.config") == 0) {
    file = INK_FNAME_SPLIT_DNS;
  } else if (strcmp(name, "update.config") == 0) {
    file = INK_FNAME_UPDATE_URL;
  } else if (strcmp(name, "vaddrs.config") == 0) {
    file = INK_FNAME_VADDRS;
  } else {
    INKfree(name);
    return;
  }

  ctx = INKCfgContextCreate(file);
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY)
    printf("ERROR READING FILE\n");

  int count = INKCfgContextGetCount(ctx);
  printf("%d rules in file: %s\n", count, name);

  print_ele_list(file, ctx);

  INKCfgContextDestroy(ctx);
  INKfree(name);
  return;
}

//
// tests the INKCfgContextMoveEleUp/Down functions (which end up calling
// the new "copy" utility functions in CfgContextUtils.cc
// depending on the file specified, this will move the top rule to the bottom,
// and the second to the last rule to the top
// essentially, the original top and bottom rules switch places!!
//
void
test_cfg_context_move(char *args)
{
  INKCfgContext ctx;
  INKFileNameT file;
  int i;
  INKError err;

  char *filename = strtok(args, ":");
  filename = strtok(NULL, ":");
  fprintf(stderr, "modify file: %s\n", filename);
  char *name = (char *) INKmalloc(sizeof(char) * (strlen(filename)));
  ink_strncpy(name, filename, strlen(filename));
  name[strlen(filename) - 1] = '\0';

  // convert file name to INKFileNameT
  if (strcmp(name, "admin_access.config") == 0) {
    file = INK_FNAME_ADMIN_ACCESS;
  } else if (strcmp(name, "cache.config") == 0) {
    file = INK_FNAME_CACHE_OBJ;
  } else if (strcmp(name, "congestion.config") == 0) {
    file = INK_FNAME_CONGESTION;
  } else if (strcmp(name, "filter.config") == 0) {
    file = INK_FNAME_FILTER;
  } else if (strcmp(name, "hosting.config") == 0) {
    file = INK_FNAME_HOSTING;
  } else if (strcmp(name, "icp.config") == 0) {
    file = INK_FNAME_ICP_PEER;
  } else if (strcmp(name, "ip_allow.config") == 0) {
    file = INK_FNAME_IP_ALLOW;
  } else if (strcmp(name, "logs_xml.config") == 0) {
    file = INK_FNAME_LOGS_XML;
  } else if (strcmp(name, "mgmt_allow.config") == 0) {
    file = INK_FNAME_MGMT_ALLOW;
  } else if (strcmp(name, "parent.config") == 0) {
    file = INK_FNAME_PARENT_PROXY;
  } else if (strcmp(name, "partition.config") == 0) {
    file = INK_FNAME_PARTITION;
  } else if (strcmp(name, "remap.config") == 0) {
    file = INK_FNAME_REMAP;
  } else if (strcmp(name, "socks.config") == 0) {
    file = INK_FNAME_SOCKS;
  } else if (strcmp(name, "storage.config") == 0) {
    file = INK_FNAME_STORAGE;
  } else if (strcmp(name, "splitdns.config") == 0) {
    file = INK_FNAME_SPLIT_DNS;
  } else if (strcmp(name, "update.config") == 0) {
    file = INK_FNAME_UPDATE_URL;
  } else if (strcmp(name, "vaddrs.config") == 0) {
    file = INK_FNAME_VADDRS;
  } else {
    INKfree(name);
    return;
  }

  ctx = INKCfgContextCreate(file);
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY)
    printf("ERROR READING FILE\n");

  int count = INKCfgContextGetCount(ctx);
  printf("%d rules in file: %s\n", count, name);

  // shift all the ele's up so that the top ele is now the bottom ele
  printf("\nShift all ele's up so that top ele is now bottom ele\n");
  for (i = 1; i < count; i++) {
    err = INKCfgContextMoveEleUp(ctx, i);
    if (err != INK_ERR_OKAY) {
      printf("ERROR moving ele at index %d up \n", i);
      goto END;
    }
  }

  // shift all the ele's down so that the next to bottom ele is now top ele
  // move all ele's above the last ele down; bottom ele becomes top ele
  printf("\nShift all Ele's above second to last ele down; bottom ele becomes top ele\n");
  for (i = count - 3; i >= 0; i--) {
    err = INKCfgContextMoveEleDown(ctx, i);
    if (err != INK_ERR_OKAY) {
      printf("ERROR: moving ele down at index %d\n", i);
      goto END;
    }
  }

  // clean up; commit change
  INKCfgContextCommit(ctx, NULL, NULL);

END:
  INKCfgContextDestroy(ctx);
  INKfree(name);
  return;
}

void
test_cfg_context_ops()
{
  // Not used here.
  //INKCfgIterState iter_state;
  //INKCfgEle *cfg_ele;
  INKError err;
  INKCfgContext ctx;
  INKVirtIpAddrEle *ele;
  int rm_index = 0, i;
  int insert_at;

  ctx = INKCfgContextCreate(INK_FNAME_VADDRS);

  if (INKCfgContextGet(ctx) != INK_ERR_OKAY)
    printf("ERROR READING FILE\n");

  printf("\nBEFORE CHANGE:\n");
  //  print_VirtIpAddr_ele_list(ctx);

  int count = INKCfgContextGetCount(ctx);
  printf("# ele's = %d\n", count);

  printf("\nShifted all Ele's < %d up\n", rm_index);
  // move all ele's below rm_index up one; this shifts the rm_index ele to
  // bottom of INKCfgContext
  for (i = (rm_index + 1); i < count; i++) {
    err = INKCfgContextMoveEleUp(ctx, i);
    if (err != INK_ERR_OKAY) {
      printf("ERROR moving ele at index %d up \n", i);
      goto END;
    }
  }
  //print_VirtIpAddr_ele_list(ctx);

  printf("\nREMOVE LAST ELE (originally the first ele)\n");
  // remove the last ele (which was originally the first ele)
  err = INKCfgContextRemoveEleAt(ctx, (count - 1));
  if (err != INK_ERR_OKAY) {
    printf("ERROR: removing ele at index %d\n", count - 1);
    goto END;
  }

  printf("\nRemoving second to last Ele \n");
  err = INKCfgContextRemoveEleAt(ctx, (count - 2));
  if (err != INK_ERR_OKAY) {
    printf("ERROR: removing ele at index %d\n", count - 2);
    goto END;
  }

  // append a new ele
  printf("\nappend new ele\n");
  ele = INKVirtIpAddrEleCreate();
  if (ele) {
    const size_t ip_addr_size = (sizeof(char) * (strlen("201.201.201.201") + 1));
    ele->ip_addr = (char *) INKmalloc(ip_addr_size);
    ink_strncpy(ele->ip_addr, "201.201.201.201", ip_addr_size);
    const size_t intr_size = (sizeof(char) * (strlen("appended") + 1));
    ele->intr = (char *) INKmalloc(intr_size);
    ink_strncpy(ele->intr, "appended", intr_size);
    ele->sub_intr = 201;
    err = INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);
    if (err != INK_ERR_OKAY) {
      printf("ERROR: append ele\n");
      INKVirtIpAddrEleDestroy(ele);
      goto END;
    }
  } else {
    printf("Can't create VirtIpAddrEle\n");
  }
  //print_VirtIpAddr_ele_list(ctx);

  insert_at = 1;
  // insert a new ele in insert_at index
  printf("\nINSERT NEW ELE at %d index\n", insert_at);
  ele = INKVirtIpAddrEleCreate();
  if (ele) {
    const size_t ip_addr_size = (sizeof(char) * (strlen("101.101.101.101") + 1));
    ele->ip_addr = (char *) INKmalloc(ip_addr_size);
    ink_strncpy(ele->ip_addr, "101.101.101.101", ip_addr_size);
    const size_t intr_size = (sizeof(char) * (strlen("inserted") + 1));
    ele->intr = (char *) INKmalloc(intr_size);
    ink_strncpy(ele->intr, "inserted", intr_size);
    ele->sub_intr = 100;
    err = INKCfgContextInsertEleAt(ctx, (INKCfgEle *) ele, insert_at);
    if (err != INK_ERR_OKAY) {
      printf("ERROR: insert ele  at index %d\n", insert_at);
      INKVirtIpAddrEleDestroy(ele);
      goto END;
    }
  } else {
    printf("Can't create VirtIpAddrEle\n");
  }
  //print_VirtIpAddr_ele_list(ctx);


  printf("\nMove ele at index %d to botoom of list\n", insert_at);
  for (i = insert_at; i < INKCfgContextGetCount(ctx); i++) {
    err = INKCfgContextMoveEleDown(ctx, i);
    if (err != INK_ERR_OKAY) {
      printf("ERROR: moving ele down at index %d\n", i);
      goto END;
    }
  }
  //print_VirtIpAddr_ele_list(ctx);

  printf("\nShift all Ele's above last ele down; bottom ele becomes top ele\n");
  count = INKCfgContextGetCount(ctx);
  for (i = count - 2; i >= 0; i--) {
    err = INKCfgContextMoveEleDown(ctx, i);
    if (err != INK_ERR_OKAY) {
      printf("ERROR: moving ele down at index %d\n", i);
      goto END;
    }
  }
  //print_VirtIpAddr_ele_list(ctx);


  // commit change
  INKCfgContextCommit(ctx, NULL, NULL);

  printf("\nAFTER CHANGE:\n");
  //print_VirtIpAddr_ele_list(ctx);

END:
  INKCfgContextDestroy(ctx);

}

/* ------------------------------------------------------------------------
 * test_cfg_plugin
 * ------------------------------------------------------------------------
 * Gets all the Ele's from plugin.config, modifies them, and commits the changes
 * to file
 */
void
test_cfg_plugin()
{
  // Not used here.
  //INKCfgIterState iter_state;
  INKCfgContext ctx;
  INKCfgEle *cfg_ele;
  INKPluginEle *ele;

  ctx = INKCfgContextCreate(INK_FNAME_PLUGIN);
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY)
    printf("ERROR READING FILE\n");

  // retrieve and modify ele
  printf("test_cfg_plugin: modifying the first ele...\n");
  cfg_ele = INKCfgContextGetEleAt(ctx, 0);
  ele = (INKPluginEle *) cfg_ele;
  if (ele) {
    //free(ele->name);
    ele->name = xstrdup("change-plugin.so");
  }

  // remove the second ele
  printf("test_cfg_plugin: removing the second ele...\n");
  INKCfgContextRemoveEleAt(ctx, 1);

  // create and add new ele
  printf("test_socks_set: appending a new ele...\n");
  ele = INKPluginEleCreate();

  ele->name = xstrdup("new-plugin.so");

  ele->args = INKStringListCreate();
  INKStringListEnqueue(ele->args, xstrdup("arg1"));
  INKStringListEnqueue(ele->args, xstrdup("arg2"));
  INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);

  // commit change
  INKCfgContextCommit(ctx, NULL, NULL);

  INKCfgContextDestroy(ctx);

}

/* ------------------------------------------------------------------------
 * test_cfg_socks
 * ------------------------------------------------------------------------
 * Gets all the Ele's from socks, modifies them, and commits the changes
 * to file
 */
void
test_cfg_socks()
{
  // Not used here.
  //INKCfgIterState iter_state;
  INKCfgContext ctx;
  INKCfgEle *cfg_ele;
  INKSocksEle *ele;

  ctx = INKCfgContextCreate(INK_FNAME_SOCKS);
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY)
    printf("ERROR READING FILE\n");

  // retrieving an ele
  printf("test_socks_set: modifying the fourth ele...\n");
  cfg_ele = INKCfgContextGetEleAt(ctx, 3);
  ele = (INKSocksEle *) cfg_ele;
  if (ele) {
    if (ele->rr != INK_RR_NONE)
      ele->rr = INK_RR_FALSE;
  }

  // remove the second ele
  printf("test_socks_set: removing the second ele...\n");
  INKCfgContextRemoveEleAt(ctx, 1);

  // create new structs for new rules
  INKIpAddrEle *ip1 = INKIpAddrEleCreate();
  ip1->type = INK_IP_SINGLE;
  ip1->ip_a = INKstrdup("1.1.1.1");

  INKDomainList dlist = INKDomainListCreate();
  INKDomain *dom1 = INKDomainCreate();
  dom1->domain_val = INKstrdup("www.mucky.com");
  dom1->port = 8888;
  INKDomainListEnqueue(dlist, dom1);

  INKDomain *dom2 = INKDomainCreate();
  dom2->domain_val = INKstrdup("freakazoid.com");
  dom2->port = 2222;
  INKDomainListEnqueue(dlist, dom2);

  INKDomain *dom3 = INKDomainCreate();
  dom3->domain_val = INKstrdup("hong.kong.com");
  dom3->port = 3333;
  INKDomainListEnqueue(dlist, dom3);

  // create and add new ele
  printf("test_socks_set: appending a new ele...\n");
  ele = INKSocksEleCreate(INK_TYPE_UNDEFINED);
  if (ele) {
    ele->cfg_ele.type = INK_SOCKS_MULTIPLE;
    ele->dest_ip_addr = ip1;
    ele->socks_servers = dlist;
    ele->rr = INK_RR_STRICT;

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);
  } else {
    printf("Can't create SocksEle\n");
  }

  // commit change
  INKCfgContextCommit(ctx, NULL, NULL);

  INKCfgContextDestroy(ctx);
}



/***************************************************************************
 * Events Testing
 ***************************************************************************/
/* ------------------------------------------------------------------------
 * print_active_events
 * ------------------------------------------------------------------------
 * retrieves a list of all active events and prints out each event name,
 * one event per line
 */
void
print_active_events()
{
  INKList events;
  INKError ret;
  int count, i;
  char *name;

  printf("[print_active_events]\n");

  events = INKListCreate();
  ret = INKActiveEventGetMlt(events);
  if (ret != INK_ERR_OKAY) {
    print_err("INKActiveEventGetMlt", ret);
    goto END;
  } else {                      // successful get
    count = INKListLen(events);
    for (i = 0; i < count; i++) {
      name = (char *) INKListDequeue(events);
      printf("\t%s\n", name);
      INKfree(name);
    }
  }

END:
  INKListDestroy(events);
  return;
}

/* ------------------------------------------------------------------------
 * check_active
 * ------------------------------------------------------------------------
 * returns true if the event named event_name is currently active (unresolved)
 * returns false otherwise
 */
bool
check_active(char *event_name)
{
  bool active;
  INKError ret;

  ret = INKEventIsActive(event_name, &active);
  print_err("INKEventIsActive", ret);

  if (active)
    printf("%s is ACTIVE\n", event_name);
  else
    printf("%s is NOT-ACTIVE\n", event_name);

  return active;
}

/* ------------------------------------------------------------------------
 * try_resolve
 * ------------------------------------------------------------------------
 * checks if the event_name is still unresolved; if it is, it then
 * resolves it, and checks the status of the event again to make sure
 * the event was actually resolved
 *
 * NOTE: all the special string manipulation is needed because the CLI
 * appends extra newline character to end of the user input; normally
 * do not have to do all this special string manipulation
 */
void
try_resolve(char *event_name)
{
  INKError ret;
  char *name;

  name = (char *) INKmalloc(sizeof(char) * (strlen(event_name)));
  strncpy(name, event_name, strlen(event_name) - 1);
  name[strlen(event_name) - 1] = '\0';
  printf("[try_resolve] Resolving event: %s\n", name);

  if (check_active(name)) {     // resolve events
    ret = INKEventResolve(name);
    print_err("INKEventResolve", ret);
    check_active(name);         // should be non-active now
  }

  INKfree(name);
}

/* ------------------------------------------------------------------------
 * eventCallbackFn
 * ------------------------------------------------------------------------
 * the callback function; when called, it just prints out the name
 * of the event that was signalled
 */
void
eventCallbackFn(char *name, char *msg, int pri, void *data)
{
  printf("[eventCallbackFn] EVENT: %s, %s\n", name, msg);
  return;
}

/* ------------------------------------------------------------------------
 * register_event_callback
 * ------------------------------------------------------------------------
 * registers the eventCallbackFn above for all events; this just means
 * that for any event that's signalled, the callback fn will also be called
 */
void
register_event_callback(void)
{
  INKError err;

  printf("\n[register_event_callback] \n");
  err = INKEventSignalCbRegister(NULL, eventCallbackFn, NULL);
  print_err("INKEventSignalCbRegister", err);
}

/* ------------------------------------------------------------------------
 * unregister_event_callback
 * ------------------------------------------------------------------------
 * unregisters the eventCallbackFn above for all events; this just means
 * that it will remove this eventCallbackFn entirely so that for any
 * event called, the eventCallbackFn will NOT be called
 */
void
unregister_event_callback(void)
{
  INKError err;

  printf("\n[unregister_event_callback]\n");
  err = INKEventSignalCbUnregister(NULL, eventCallbackFn);
  print_err("INKEventSignalCbUnregister", err);
}

/***************************************************************************
 * Snapshots Testing
 ***************************************************************************/

void
print_snapshots()
{
  INKStringList list;
  INKError err;
  char *name;

  list = INKStringListCreate();
  err = INKSnapshotGetMlt(list);
  print_err("INKSnapshotGetMlt", err);

  printf("All Snapshots:\n");
  if (err == INK_ERR_OKAY) {
    int num = INKStringListLen(list);
    for (int i = 0; i < num; i++) {
      name = INKStringListDequeue(list);
      if (name)
        printf("%s\n", name);
      INKfree(name);
    }
  }

  INKStringListDestroy(list);
  return;
}

void
add_snapshot(char *args)
{
  char *snap_name = strtok(args, ":");
  snap_name = strtok(NULL, ":");
  fprintf(stderr, "add snapshot: %s\n", snap_name);
  char *name = (char *) INKmalloc(sizeof(char) * (strlen(snap_name)));
  strncpy(name, snap_name, strlen(snap_name) - 1);
  name[strlen(snap_name) - 1] = '\0';

  INKError err = INKSnapshotTake(name);
  print_err("INKSnapshotTake", err);

  INKfree(name);
}

void
remove_snapshot(char *args)
{
  char *snap_name = strtok(args, ":");
  snap_name = strtok(NULL, ":");
  fprintf(stderr, "remove snapshot: %s\n", snap_name);
  char *name = (char *) INKmalloc(sizeof(char) * (strlen(snap_name)));
  strncpy(name, snap_name, strlen(snap_name) - 1);
  name[strlen(snap_name) - 1] = '\0';

  INKError err = INKSnapshotRemove(name);
  print_err("INKSnapshotRemove", err);

  INKfree(name);
}

void
restore_snapshot(char *args)
{
  char *snap_name = strtok(args, ":");
  snap_name = strtok(NULL, ":");
  fprintf(stderr, "resotre snapshot: %s\n", snap_name);
  char *name = (char *) INKmalloc(sizeof(char) * (strlen(snap_name)));
  strncpy(name, snap_name, strlen(snap_name) - 1);
  name[strlen(snap_name) - 1] = '\0';

  INKError err = INKSnapshotRestore(name);
  print_err("INKSnapshotRestore", err);

  INKfree(name);
}

/***************************************************************************
 * Diags Testing
 ***************************************************************************/

void
test_diags()
{
  // status
  for (int i = 0; i < 5; i++) {
    INKDiags(INK_DIAG_STATUS, "[remote]status diag %d", i);
  }

  // warning
  INKDiags(INK_DIAG_WARNING, "[remote]warning msg %s %s", "I am", "a fiue");

  // fatal
  INKDiags(INK_DIAG_FATAL, "[remote]FATAL, FATAL: Nuclear meltdown in %d seconds", 10);

  // error
  INKDiags(INK_DIAG_ERROR, "[remote]error msg shouldn't have printed this %s", "argument");

  // debug
  INKDiags(INK_DIAG_DEBUG, "[remote]debug ... wish I was good at it");

}

/***************************************************************************
 * Statistics
 ***************************************************************************/

// generate dummy values for statistics
void
set_stats()
{
  INKActionNeedT action;

  fprintf(stderr, "[set_stats] Set Dummy Stat Values\n");


  INKRecordSetInt("proxy.process.http.user_agent_response_document_total_size", 100, &action);
  INKRecordSetInt("proxy.process.http.user_agent_response_header_total_size", 100, &action);
  INKRecordSetInt("proxy.process.http.current_client_connections", 100, &action);
  INKRecordSetInt("proxy.process.http.current_client_transactions", 100, &action);
  INKRecordSetInt("proxy.process.http.origin_server_response_document_total_size", 100, &action);
  INKRecordSetInt("proxy.process.http.origin_server_response_header_total_size", 100, &action);
  INKRecordSetInt("proxy.process.http.current_server_connections", 100, &action);
  INKRecordSetInt("proxy.process.http.current_server_transactions", 100, &action);


  INKRecordSetFloat("proxy.node.http.cache_hit_ratio", 110.0, &action);
  INKRecordSetFloat("proxy.node.http.bandwidth_hit_ratio", 110.0, &action);
  INKRecordSetFloat("proxy.node.bandwidth_hit_ratio", 110, &action);
  INKRecordSetFloat("proxy.node.hostdb.hit_ratio", 110, &action);
  INKRecordSetFloat("proxy.node.cache.percent_free", 110, &action);
  INKRecordSetFloat("proxy.node.cache_hit_ratio", 110, &action);
  INKRecordSetFloat("proxy.node.bandwidth_hit_ratio_avg_10s", 110, &action);
  INKRecordSetFloat("proxy.node.http.cache_hit_fresh_avg_10s", 110, &action);
  INKRecordSetFloat("proxy.node.http.cache_hit_revalidated_avg_10s", 110, &action);
  INKRecordSetFloat("proxy.node.http.cache_hit_ims_avg_10s", 100, &action);
  INKRecordSetFloat("proxy.node.client_throughput_out", 110, &action);

  INKRecordSetInt("proxy.node.http.cache_hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.node.http.bandwidth_hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.node.bandwidth_hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.node.hostdb.hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.node.proxy_running", 110, &action);
  INKRecordSetInt("proxy.node.hostdb.hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.node.proxy_running", 110, &action);
  INKRecordSetInt("proxy.node.cache_hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.node.current_client_connections", 110, &action);
  INKRecordSetInt("proxy.node.current_cache_connections", 110, &action);

  INKRecordSetFloat("proxy.cluster.user_agent_total_bytes_avg_10s", 110, &action);
  INKRecordSetFloat("proxy.cluster.origin_server_total_bytes_avg_10s", 110, &action);
  INKRecordSetFloat("proxy.cluster.bandwidth_hit_ratio", 110, &action);
  INKRecordSetFloat("proxy.cluster.bandwidth_hit_ratio_avg_10s", 110, &action);
  INKRecordSetFloat("proxy.cluster.http.cache_hit_ratio", 110, &action);
  INKRecordSetFloat("proxy.cluster.http.bandwidth_hit_ratio", 110, &action);

  INKRecordSetInt("proxy.cluster.http.cache_hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.cluster.bandwidth_hit_ratio_int_pct", 110, &action);
  INKRecordSetInt("proxy.cluster.http.cache_total_hits", 110, &action);
  INKRecordSetInt("proxy.cluster.http.cache_total_misses", 110, &action);
  INKRecordSetInt("proxy.cluster.http.throughput", 110, &action);
}

void
print_stats()
{
  INKFloat f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11;
  INKInt i1, i2, i3, i4, i5, i6, i7, i8, i9, i10;

  fprintf(stderr, "[print_stats]\n");


  INKRecordGetInt("proxy.process.http.user_agent_response_document_total_size", &i1);
  INKRecordGetInt("proxy.process.http.user_agent_response_header_total_size", &i2);
  INKRecordGetInt("proxy.process.http.current_client_connections", &i3);
  INKRecordGetInt("proxy.process.http.current_client_transactions", &i4);
  INKRecordGetInt("proxy.process.http.origin_server_response_document_total_size", &i5);
  INKRecordGetInt("proxy.process.http.origin_server_response_header_total_size", &i6);
  INKRecordGetInt("proxy.process.http.current_server_connections", &i7);
  INKRecordGetInt("proxy.process.http.current_server_transactions", &i8);

  fprintf(stderr, "%lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld\n", i1, i2, i3, i4, i5, i6, i7, i8);

  INKRecordGetFloat("proxy.node.http.cache_hit_ratio", &f1);
  INKRecordGetFloat("proxy.node.http.bandwidth_hit_ratio", &f2);
  INKRecordGetFloat("proxy.node.bandwidth_hit_ratio", &f3);
  INKRecordGetFloat("proxy.node.hostdb.hit_ratio", &f4);
  INKRecordGetFloat("proxy.node.cache.percent_free", &f5);
  INKRecordGetFloat("proxy.node.cache_hit_ratio", &f6);
  INKRecordGetFloat("proxy.node.bandwidth_hit_ratio_avg_10s", &f7);
  INKRecordGetFloat("proxy.node.http.cache_hit_fresh_avg_10s", &f8);
  INKRecordGetFloat("proxy.node.http.cache_hit_revalidated_avg_10s", &f9);
  INKRecordGetFloat("proxy.node.http.cache_hit_ims_avg_10s", &f10);
  INKRecordGetFloat("proxy.node.client_throughput_out", &f11);

  fprintf(stderr, "NODE stats: \n%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f\n",
          f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11);

  INKRecordGetInt("proxy.node.http.cache_hit_ratio_int_pct", &i1);
  INKRecordGetInt("proxy.node.http.bandwidth_hit_ratio_int_pct", &i2);
  INKRecordGetInt("proxy.node.bandwidth_hit_ratio_int_pct", &i3);
  INKRecordGetInt("proxy.node.hostdb.hit_ratio_int_pct", &i4);
  INKRecordGetInt("proxy.node.proxy_running", &i5);
  INKRecordGetInt("proxy.node.hostdb.hit_ratio_int_pct", &i6);
  INKRecordGetInt("proxy.node.proxy_running", &i7);
  INKRecordGetInt("proxy.node.cache_hit_ratio_int_pct", &i8);
  INKRecordGetInt("proxy.node.current_client_connections", &i9);
  INKRecordGetInt("proxy.node.current_cache_connections", &i10);

  fprintf(stderr, "%lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld, %lld\n",
          i1, i2, i3, i4, i5, i6, i7, i8, i9, i10);

  INKRecordGetFloat("proxy.cluster.user_agent_total_bytes_avg_10s", &f1);
  INKRecordGetFloat("proxy.cluster.origin_server_total_bytes_avg_10s", &f2);
  INKRecordGetFloat("proxy.cluster.bandwidth_hit_ratio", &f3);
  INKRecordGetFloat("proxy.cluster.bandwidth_hit_ratio_avg_10s", &f4);
  INKRecordGetFloat("proxy.cluster.http.cache_hit_ratio", &f5);
  INKRecordGetFloat("proxy.cluster.http.bandwidth_hit_ratio", &f6);

  INKRecordGetInt("proxy.cluster.http.cache_hit_ratio_int_pct", &i1);
  INKRecordGetInt("proxy.cluster.bandwidth_hit_ratio_int_pct", &i2);
  INKRecordGetInt("proxy.cluster.http.cache_total_hits", &i3);
  INKRecordGetInt("proxy.cluster.http.cache_total_misses", &i4);
  INKRecordGetInt("proxy.cluster.http.throughput", &i5);

  fprintf(stderr, "CLUSTER stats: \n");
  fprintf(stderr, "%f, %f, %f, %f, %f, %f\n", f1, f2, f3, f4, f5, f6);
  fprintf(stderr, "%lld, %lld, %lld, %lld, %lld\n", i1, i2, i3, i4, i5);

  fprintf(stderr, "PROCESS stats: \n");
  fprintf(stderr, "%f, %f\n", f1, f2);
  fprintf(stderr, "%lld, %lld, %lld, %lld\n", i1, i2, i3, i4);

}

void
reset_stats()
{
  INKError err = INKStatsReset();
  print_err("INKStatsReset", err);
  return;
}

void
sync_test()
{
  INKActionNeedT action;

  INKRecordSetString("proxy.config.proxy_name", "dorkface", &action);
  printf("[INKRecordSetString] proxy.config.proxy_name \n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_UNDEFINED, action);

  INKRecordSetInt("proxy.config.ldap.cache.size", 3333, &action);
  printf("[INKRecordSetInt] proxy.config.ldap.cache.size\n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_RECONFIGURE, action);

  INKRecordSetInt("proxy.config.cluster.cluster_port", 3333, &action);
  printf("[INKRecordSetInt] proxy.config.cluster.cluster_port\n\tAction Should: [%d]\n\tAction is    : [%d]\n",
         INK_ACTION_RESTART, action);

  if (INKRecordSet("proxy.config.http.cache.fuzz.probability", "-0.3333", &action) != INK_ERR_OKAY)
    printf("INKRecordSet FAILED!\n");
  else
    printf("[INKRecordSet] proxy.config.http.cache.fuzz.probability=-0.3333\n");

  INKError ret;
  if ((ret = INKProxyStateSet(INK_PROXY_OFF, INK_CACHE_CLEAR_OFF)) != INK_ERR_OKAY)
    printf("[INKProxyStateSet] turn off FAILED\n");
  print_err("stop_TS", ret);
}

void
test_encrypt_password(char *pwd)
{
  if (INKEncryptToFile(pwd, "/export/workareas/lant/tsunami/traffic/sun_dbg/etc/trafficserver/LAN_pwd") != INK_ERR_OKAY)
    printf("[INKEncryptToFile] could not encrypt %s", pwd);
}


/* ########################################################################*/
/* ------------------------------------------------------------------------
 * runInteractive
 * ------------------------------------------------------------------------
 * the loop that processes the commands inputted by user
 */
static void
runInteractive()
{
  char buf[512];                // holds request from interactive prompt

  // process input from command line
  while (1) {
    // Display a prompt
    printf("api_cli-> ");

    // get input from command line
    NOWARN_UNUSED_RETURN(fgets(buf, 512, stdin));

    // check status of 'stdin' after reading
    if (feof(stdin) != 0) {
      printf("EXIT api_cli_remote\n");
      return;
    } else if (ferror(stdin) != 0) {
      printf("EXIT api_cli_remote\n");
      return;
    }
    // continue on newline
    if (strcmp(buf, "\n") == 0) {
      continue;
    }
    // exiting/quitting?
    if (strcasecmp("quit\n", buf) == 0 || strcasecmp("exit\n", buf) == 0) {
      // Don't wait for response LM
      //exit(0);
      return;
    }
    // check what operation user typed in
    if (strstr(buf, "state")) {
      print_proxy_state();
    } else if (strncmp(buf, "start", 5) == 0) {
      start_TS(buf);
    } else if (strstr(buf, "stop")) {
      stop_TS();
    } else if (strstr(buf, "restart")) {
      restart();
    } else if (strstr(buf, "reconfig")) {
      reconfigure();
    } else if (strstr(buf, "kill_TC")) {
      hard_restart();
    } else if (strstr(buf, "records")) {
      test_records();
    } else if (strstr(buf, "err_recs")) {
      test_error_records();
    } else if (strstr(buf, "get_mlt")) {
      test_record_get_mlt();
    } else if (strstr(buf, "set_mlt")) {
      test_record_set_mlt();
    } else if (strstr(buf, "read_file")) {
      test_read_file();
    } else if (strstr(buf, "write_file")) {
      test_write_file();
    } else if (strstr(buf, "proxy.")) {
      test_rec_get(buf);
    } else if (strstr(buf, "active_events")) {
      print_active_events();
    } else if (strstr(buf, "MGMT_ALARM_")) {
      try_resolve(buf);
    } else if (strncmp(buf, "register", 8) == 0) {
      register_event_callback();
    } else if (strstr(buf, "unregister")) {
      unregister_event_callback();
    } else if (strstr(buf, "snapshots")) {
      print_snapshots();
    } else if (strstr(buf, "take_snap")) {
      add_snapshot(buf);
    } else if (strstr(buf, "remove_snap")) {
      remove_snapshot(buf);
    } else if (strstr(buf, "restore_snap")) {
      restore_snapshot(buf);
    } else if (strstr(buf, "diags")) {
      test_diags();
    } else if (strstr(buf, "read_url")) {
      test_read_url(true);
    } else if (strstr(buf, "test_url")) {
      test_read_url(false);
    } else if (strstr(buf, "cfg_get:")) {
      test_cfg_context_get(buf);
    } else if (strstr(buf, "cfg:")) {
      test_cfg_context_move(buf);
    } else if (strstr(buf, "cfg_context")) {
      test_cfg_context_ops();
    } else if (strstr(buf, "cfg_socks")) {
      test_cfg_socks();
    } else if (strstr(buf, "cfg_plugin")) {
      test_cfg_plugin();
    } else if (strstr(buf, "reset_stats")) {
      reset_stats();
    } else if (strstr(buf, "set_stats")) {
      set_stats();
    } else if (strstr(buf, "print_stats")) {
      print_stats();
    } else if (strstr(buf, "encrypt:")) {
      test_encrypt_password(buf);
    } else {
      sync_test();
    }




  }                             // end while(1)

}                               // end runInteractive


/* ------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------
 * Main entry point which connects the client to the API, does any
 * clean up on exit, and gets the interactive command-line running
 */
int
main(int argc, char **argv)
{
  INKError ret;

  // initialize
#if INSTALL_TEST
  if ((ret = INKInit("../etc/trafficserver/")) != INK_ERR_OKAY)
#else
  if ((ret = INKInit("../../../../etc/trafficserver/")) != INK_ERR_OKAY)
#endif
  {
    print_err("main", ret);

    //return -1;
  }
  // Interactive mode
  runInteractive();

  // clean-up
  INKTerminate();               //ERROR:Causes infinite!!
  printf("END REMOTE API TEST\n");

  return 0;
}                               // end main()
