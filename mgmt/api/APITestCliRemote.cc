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
 *
 * File operations:
 * ---------------
 * read_file:  reads hosting.config file
 * write_file: write some made-up text to hosting.config file
 * proxy.config.xxx (a records.config variable): returns value of that record
 * records: tests get/set/get a record of each different type
 *          (int, float, counter, string)
 * err_recs: stress test record get/set functions by purposely entering
 *              invalid record names and/or values
 * get_mlt: tests TSRecordGetMlt
 * set_mlt: tests TSRecordSetMlt
 *
 * read_url: tests TSReadFromUrl works by retrieving two valid urls
 * test_url: tests robustness of TSReadFromUrl using invalid urls
 *
 * CfgContext operations:
 * ---------------------
 * cfg_get:<config-filename>: prints out the rules in confg-filename
 * cfg:<config-filename>: switches the position of first and last rule of
 *                        <config-filename>
 * cfg_context: calls all the TSCfgCOntext helper function calls using
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

#include "ts/ink_config.h"
#include "ts/ink_defs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include "ts/ink_string.h"

#include "mgmtapi.h"
#include "CfgContextUtils.h"

// refer to test_records() function
#define TEST_STRING 1
#define TEST_FLOAT 1
#define TEST_INT 1
#define TEST_COUNTER 1
#define TEST_REC_SET 1
#define TEST_REC_GET 0
#define TEST_REC_GET_2 0

#define SET_INT 0

// set to 1 if running as part of installation package
// set to 0 if being tested in developer environment
#define INSTALL_TEST 0

/***************************************************************************
 * Printing Helper Functions
 ***************************************************************************/

/* ------------------------------------------------------------------------
 * print_err
 * ------------------------------------------------------------------------
 * used to print the error description associated with the TSMgmtError err
 */
void
print_err(const char *module, TSMgmtError err)
{
  char *err_msg;

  err_msg = TSGetErrorMessage(err);
  printf("(%s) ERROR: %s\n", module, err_msg);

  if (err_msg)
    TSfree(err_msg);
}

/*--------------------------------------------------------------
 * print_ports
 *--------------------------------------------------------------*/
void
print_ports(TSPortList list)
{
  int i, count;
  TSPortEle *port_ele;

  count = TSPortListLen(list);
  for (i = 0; i < count; i++) {
    port_ele = TSPortListDequeue(list);
    printf(" %d \n", port_ele->port_a);
    if (port_ele->port_b != -1)
      printf(" %d - %d \n", port_ele->port_a, port_ele->port_b);
    TSPortListEnqueue(list, port_ele);
  }

  return;
}

/*-------------------------------------------------------------
 * print_string_list
 *-------------------------------------------------------------*/
void
print_string_list(TSStringList list)
{
  int i, count, buf_pos = 0;
  char *str;
  char buf[1000];

  if (!list)
    return;
  count = TSStringListLen(list);
  for (i = 0; i < count; i++) {
    str = TSStringListDequeue(list);
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%s,", str);
    buf_pos = strlen(buf);
    TSStringListEnqueue(list, str);
  }
  printf("%s \n", buf);
}

/*-------------------------------------------------------------
 * print_int_list
 *-------------------------------------------------------------*/
void
print_int_list(TSIntList list)
{
  int i, count, buf_pos = 0;
  int *elem;
  char buf[1000];

  count = TSIntListLen(list);
  for (i = 0; i < count; i++) {
    elem = TSIntListDequeue(list);
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "%d:", *elem);
    buf_pos = strlen(buf);
    TSIntListEnqueue(list, elem);
  }
  printf("Int List: %s \n", buf);
}

/*-------------------------------------------------------
 * print_domain_list
 *-------------------------------------------------------*/
void
print_domain_list(TSDomainList list)
{
  int i, count;
  TSDomain *proxy;

  count = TSDomainListLen(list);
  for (i = 0; i < count; i++) {
    proxy = TSDomainListDequeue(list);
    if (proxy->domain_val)
      printf("%s:%d\n", proxy->domain_val, proxy->port);
    TSDomainListEnqueue(list, proxy);
  }
}

void
print_ip_addr_ele(TSIpAddrEle *ele)
{
  if (!ele)
    return;

  if (ele->type == TS_IP_RANGE) {
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
print_ip_list(TSIpAddrList list)
{
  int i, count;
  TSIpAddrEle *ele;

  count = TSIpAddrListLen(list);
  for (i = 0; i < count; i++) {
    ele = TSIpAddrListDequeue(list);
    print_ip_addr_ele(ele);

    TSIpAddrListEnqueue(list, ele);
  }
}

/*-------------------------------------------------------
 * print_list_of_ip_list
 *-------------------------------------------------------*/
void
print_list_of_ip_list(TSList list)
{
  int i, count;
  TSIpAddrList ele;

  count = TSIpAddrListLen(list);
  for (i = 0; i < count; i++) {
    ele = TSListDequeue(list);
    printf("\n");
    print_ip_list(ele);
    printf("\n");
    TSListEnqueue(list, ele);
  }
}

/*-------------------------------------------------------
 * print_pd_sspec
 *-------------------------------------------------------*/
void
print_pd_sspec(TSPdSsFormat info)
{
  switch (info.pd_type) {
  case TS_PD_DOMAIN:
    printf("Prime Dest: dest_domain=%s\n", info.pd_val);
    break;
  case TS_PD_HOST:
    printf("Prime Host: dest_host=%s\n", info.pd_val);
    break;
  case TS_PD_IP:
    printf("Prime IP: dest_ip=%s\n", info.pd_val);
    break;
  case TS_PD_URL_REGEX:
    printf("Prime Url regex: url_regex=%s\n", info.pd_val);
    break;
  case TS_PD_URL:
    printf("Prime Url: url=%s\n", info.pd_val);
    break;
  default:
    break;
  }

  printf("Secondary Specifiers:\n");
  printf("\ttime: %d:%d-%d:%d\n", info.sec_spec.time.hour_a, info.sec_spec.time.min_a, info.sec_spec.time.hour_b,
         info.sec_spec.time.min_b);

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
  case TS_METHOD_NONE:
    printf("NONE");
    break;
  case TS_METHOD_GET:
    printf("GET");
    break;
  case TS_METHOD_POST:
    printf("POST");
    break;
  case TS_METHOD_PUT:
    printf("PUT");
    break;
  case TS_METHOD_TRACE:
    printf("TRACE");
    break;
  case TS_METHOD_UNDEFINED:
    printf("UNDEFINED");
    break;
  default:
    // Handled here:
    // TS_METHOD_PUSH
    break;
  }
  printf("\n");

  printf("\tscheme: ");
  switch (info.sec_spec.scheme) {
  case TS_SCHEME_NONE:
    printf("NONE\n");
    break;
  case TS_SCHEME_HTTP:
    printf("HTTP\n");
    break;
  case TS_SCHEME_HTTPS:
    printf("HTTPS\n");
    break;
  case TS_SCHEME_UNDEFINED:
    printf("UNDEFINED\n");
    break;
  }

  return;
}

void
print_cache_ele(TSCacheEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
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
  ats_free(pd_str);

  // now format the message
  switch (ele->cfg_ele.type) {
  case TS_CACHE_NEVER:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=never-cache");
    break;
  case TS_CACHE_IGNORE_NO_CACHE:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ignore-no-cache");
    break;
  case TS_CACHE_CLUSTER_CACHE_LOCAL:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=cluster-cache-local");
    break;
  case TS_CACHE_IGNORE_CLIENT_NO_CACHE:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ignore-server-no-cache");
    break;
  case TS_CACHE_IGNORE_SERVER_NO_CACHE:
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "action=ignore-client-no-cache");
    break;
  case TS_CACHE_PIN_IN_CACHE:
    time_str = hms_time_to_string(ele->time_period);
    if (!time_str)
      return;
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "pin-in-cache=%s", time_str);
    ats_free(time_str);
    break;
  case TS_CACHE_REVALIDATE:
    time_str = hms_time_to_string(ele->time_period);
    if (!time_str)
      return;
    snprintf(buf + buf_pos, sizeof(buf) - buf_pos, "revalidate=%s", time_str);
    ats_free(time_str);
    break;
  default: /* invalid action directive */
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
print_hosting_ele(TSHostingEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  switch (ele->pd_type) {
  case TS_PD_DOMAIN:
    printf("dest_domain=%s\n", ele->pd_val);
    break;
  case TS_PD_HOST:
    printf("dest_host=%s\n", ele->pd_val);
    break;
  case TS_PD_IP:
    printf("ip=%s\n", ele->pd_val);
    break;
  case TS_PD_URL_REGEX:
    printf("url_regex=%s\n", ele->pd_val);
    break;
  case TS_PD_URL:
    printf("url=%s\n", ele->pd_val);
    break;
  default:
    printf("INVALID Prime Dest specifier\n");
    break;
  }

  print_int_list(ele->volumes);
}

void
print_icp_ele(TSIcpEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  int peer_type;

  switch (ele->peer_type) {
  case TS_ICP_PARENT:
    peer_type = 1;
    break;
  case TS_ICP_SIBLING:
    peer_type = 2;
    break;

  default:
    peer_type = 10;
  }

  printf("%s:%s:%d:%d:%d:%d:%s:%d:\n", ele->peer_hostname, ele->peer_host_ip_addr, peer_type, ele->peer_proxy_port,
         ele->peer_icp_port, (ele->is_multicast ? 1 : 0), ele->mc_ip_addr, (ele->mc_ttl == TS_MC_TTL_SINGLE_SUBNET ? 1 : 2));
}

void
print_ip_allow_ele(TSIpAllowEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  print_ip_addr_ele(ele->src_ip_addr);
}

void
print_parent_ele(TSParentProxyEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  printf("parent rule type: %d\n", ele->cfg_ele.type);
  print_pd_sspec(ele->parent_info);
  printf("round robin? %d\n", ele->rr);
  print_domain_list(ele->proxy_list);
  printf("direct? %d\n", ele->direct);
}

void
print_volume_ele(TSVolumeEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  printf("volume #: %d\n", ele->volume_num);
  printf("scheme: %d\n", ele->scheme);
  switch (ele->size_format) {
  case TS_SIZE_FMT_ABSOLUTE:
    printf("volume_size=%d\n", ele->volume_size);
    break;
  case TS_SIZE_FMT_PERCENT:
    printf("volume_size=%% %d\n", ele->volume_size);
    break;
  default:
    // Handled here:
    // TS_SIZE_FMT_UNDEFINED
    break;
  }
}

void
print_plugin_ele(TSPluginEle *ele)
{
  if (!ele) {
    printf("can't print plugin ele\n");
    return;
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
print_remap_ele(TSRemapEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  char buf[MAX_BUF_SIZE];
  bzero(buf, MAX_BUF_SIZE);

  switch (ele->cfg_ele.type) {
  case TS_REMAP_MAP:
    ink_strlcat(buf, "map", sizeof(buf));
    break;
  case TS_REMAP_REVERSE_MAP:
    ink_strlcat(buf, "reverse_map", sizeof(buf));
    break;
  case TS_REMAP_REDIRECT:
    ink_strlcat(buf, "redirect", sizeof(buf));
    break;
  case TS_REMAP_REDIRECT_TEMP:
    ink_strlcat(buf, "redirect_temporary", sizeof(buf));
    break;
  default:
    // Handled here:
    // Lots of cases...
    break;
  }
  // space delimitor
  ink_strlcat(buf, " ", sizeof(buf));

  // from scheme
  switch (ele->from_scheme) {
  case TS_SCHEME_HTTP:
    ink_strlcat(buf, "http", sizeof(buf));
    break;
  case TS_SCHEME_HTTPS:
    ink_strlcat(buf, "https", sizeof(buf));
    break;
  default:
    // Handled here:
    // TS_SCHEME_NONE, TS_SCHEME_UNDEFINED, TS_SCHEME_NONE,
    // TS_SCHEME_UNDEFINED
    break;
  }
  ink_strlcat(buf, "://", sizeof(buf));

  // from host
  if (ele->from_host) {
    ink_strlcat(buf, ele->from_host, sizeof(buf));
  }
  // from port
  if (ele->from_port != TS_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, ele->from_port);
  }
  // from host path
  if (ele->from_path_prefix) {
    ink_strlcat(buf, "/", sizeof(buf));
    ink_strlcat(buf, ele->from_path_prefix, sizeof(buf));
  }
  // space delimitor
  ink_strlcat(buf, " ", sizeof(buf));

  // to scheme
  switch (ele->to_scheme) {
  case TS_SCHEME_HTTP:
    ink_strlcat(buf, "http", sizeof(buf));
    break;
  case TS_SCHEME_HTTPS:
    ink_strlcat(buf, "https", sizeof(buf));
    break;
  default:
    // Handled here:
    // TS_SCHEME_NONE, TS_SCHEME_UNDEFINED
    break;
  }
  ink_strlcat(buf, "://", sizeof(buf));

  // to host
  if (ele->to_host) {
    ink_strlcat(buf, ele->to_host, sizeof(buf));
  }
  // to port
  if (ele->to_port != TS_INVALID_PORT) {
    snprintf(buf, sizeof(buf), "%s:%d", buf, ele->to_port);
  }
  // to host path
  if (ele->to_path_prefix) {
    ink_strlcat(buf, "/", sizeof(buf));
    ink_strlcat(buf, ele->to_path_prefix, sizeof(buf));
  }

  printf("%s\n", buf);
  return;
}

void
print_socks_ele(TSSocksEle *ele)
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
print_split_dns_ele(TSSplitDnsEle *ele)
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
  case TS_PD_DOMAIN:
    pd_name = ats_strdup("dest_domain");
    break;
  case TS_PD_HOST:
    pd_name = ats_strdup("dest_host");
    break;
  case TS_PD_URL_REGEX:
    pd_name = ats_strdup("url_regex");
    break;
  case TS_PD_URL:
    pd_name = ats_strdup("url");
    break;
  default:
    pd_name = ats_strdup("?????");
    // Handled here:
    // TS_PD_IP, TS_PD_UNDEFINED
    break;
  }

  if (ele->pd_val) {
    ink_strlcat(buf, pd_name, sizeof(buf));
    ink_strlcat(buf, "=", sizeof(buf));
    ink_strlcat(buf, ele->pd_val, sizeof(buf));
    ink_strlcat(buf, " ", sizeof(buf));
  }

  if (ele->dns_servers_addrs) {
    ink_strlcat(buf, "named=", sizeof(buf));
    str = ip_addr_list_to_string((LLQ *)ele->dns_servers_addrs, " ");
    ink_strlcat(buf, str, sizeof(buf));
    ats_free(str);
    ink_strlcat(buf, " ", sizeof(buf));
  }

  if (ele->def_domain) {
    ink_strlcat(buf, "dns_server=", sizeof(buf));
    ink_strlcat(buf, ele->def_domain, sizeof(buf));
    ink_strlcat(buf, " ", sizeof(buf));
  }

  if (ele->search_list) {
    ink_strlcat(buf, "search_list=", sizeof(buf));
    str = domain_list_to_string(ele->search_list, " ");
    ink_strlcat(buf, str, sizeof(buf));
    ats_free(str);
    ink_strlcat(buf, " ", sizeof(buf));
  }
  printf("%s\n", buf);
  ats_free(pd_name);

  return;
}

void
print_storage_ele(TSStorageEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  if (ele->pathname)
    printf("pathname=%s, size=%d\n", ele->pathname, ele->size);
}

void
print_vaddrs_ele(TSVirtIpAddrEle *ele)
{
  if (!ele) {
    printf("can't print ele\n");
    return;
  }

  printf("ip=%s, intr=%s, sub_intr=%d\n", ele->ip_addr, ele->intr, ele->sub_intr);
}

//
// print_ele_list
//
// prints a list of Ele's
//

void
print_ele_list(TSFileNameT file, TSCfgContext ctx)
{
  TSFileNameT filename = file;
  TSCfgEle *ele;

  if (!ctx) {
    printf("[print_ele_list] invalid TSCFgContext\n");
    return;
  }

  int count = TSCfgContextGetCount(ctx);
  printf("\n[print_ele_list] %d rules\n", count);
  for (int i = 0; i < count; i++) {
    ele = TSCfgContextGetEleAt(ctx, i);

    switch (filename) {
    case TS_FNAME_CACHE_OBJ:
      print_cache_ele((TSCacheEle *)ele);
      break;
    case TS_FNAME_HOSTING:
      print_hosting_ele((TSHostingEle *)ele);
      break;
    case TS_FNAME_ICP_PEER:
      print_icp_ele((TSIcpEle *)ele);
      break;
    case TS_FNAME_IP_ALLOW:
      print_ip_allow_ele((TSIpAllowEle *)ele);
      break;
    case TS_FNAME_LOGS_XML:
      break; /*NOT DONE */
    case TS_FNAME_PARENT_PROXY:
      print_parent_ele((TSParentProxyEle *)ele);
      break;
    case TS_FNAME_VOLUME:
      print_volume_ele((TSVolumeEle *)ele);
      break;
    case TS_FNAME_PLUGIN:
      print_plugin_ele((TSPluginEle *)ele);
      break;
    case TS_FNAME_REMAP:
      print_remap_ele((TSRemapEle *)ele);
      break;
    case TS_FNAME_SOCKS:
      print_socks_ele((TSSocksEle *)ele);
      break;
    case TS_FNAME_SPLIT_DNS:
      print_split_dns_ele((TSSplitDnsEle *)ele);
      break;
    case TS_FNAME_STORAGE:
      print_storage_ele((TSStorageEle *)ele);
      break;
    case TS_FNAME_VADDRS:
      print_vaddrs_ele((TSVirtIpAddrEle *)ele);
      break;
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
  TSProxyStateT state = TSProxyStateGet();

  switch (state) {
  case TS_PROXY_ON:
    printf("Proxy State = ON\n");
    break;
  case TS_PROXY_OFF:
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
  TSMgmtError ret;
  TSCacheClearT clear = TS_CACHE_CLEAR_OFF;
  char *args;

  strtok(tsArgs, ":");
  args = strtok(NULL, ":");
  if (args) {
    if (strcmp(args, "all\n") == 0)
      clear = TS_CACHE_CLEAR_ON;
    else if (strcmp(args, "hostdb\n") == 0)
      clear = TS_CACHE_CLEAR_HOSTDB;
  } else {
    clear = TS_CACHE_CLEAR_OFF;
  }

  printf("STARTING PROXY with cache: %d\n", clear);
  if ((ret = TSProxyStateSet(TS_PROXY_ON, clear)) != TS_ERR_OKAY)
    printf("[TSProxyStateSet] turn on FAILED\n");
  print_err("start_TS", ret);
}

// stops Traffic Server (turns proxy off)
void
stop_TS()
{
  TSMgmtError ret;

  printf("STOPPING PROXY\n");
  if ((ret = TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_OFF)) != TS_ERR_OKAY)
    printf("[TSProxyStateSet] turn off FAILED\n");
  print_err("stop_TS", ret);
}

// restarts Traffic Manager cluster wide (Traffic Cop must be running)
void
restart()
{
  TSMgmtError ret;

  printf("RESTART - Cluster wide\n");
  if ((ret = TSRestart(true)) != TS_ERR_OKAY)
    printf("[TSRestart] FAILED\n");

  print_err("restart", ret);
}

// rereads all the configuration files
void
reconfigure()
{
  TSMgmtError ret;

  printf("RECONFIGURE\n");
  if ((ret = TSReconfigure()) != TS_ERR_OKAY)
    printf("[TSReconfigure] FAILED\n");

  print_err("reconfigure", ret);
}

/* ------------------------------------------------------------------------
 * test_action_need
 * ------------------------------------------------------------------------
 * tests if correct action need is returned when requested record is set
 */
void
test_action_need(void)
{
  TSActionNeedT action;

  // RU_NULL record
  TSRecordSetString("proxy.config.proxy_name", "proxy_dorky", &action);
  printf("[TSRecordSetString] proxy.config.proxy_name \n\tAction Should: [%d]\n\tAction is    : [%d]\n", TS_ACTION_UNDEFINED,
         action);

  // RU_RESTART_TS record
  TSRecordSetInt("proxy.config.cluster.cluster_port", 6666, &action);
  printf("[TSRecordSetInt] proxy.config.cluster.cluster_port\n\tAction Should: [%d]\n\tAction is    : [%d]\n", TS_ACTION_RESTART,
         action);
}

/* Bouncer the traffic_server process(es) */
void
bounce()
{
  TSMgmtError ret;

  printf("BOUNCER - Cluster wide\n");
  if ((ret = TSBounce(true)) != TS_ERR_OKAY)
    printf("[TSBounce] FAILED\n");

  print_err("bounce", ret);
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
  TSInt port1, new_port = 8080;
  TSActionNeedT action;
  TSMgmtError ret;
  TSFloat flt1;
  TSCounter ctr1;

  printf("\n");
  // test get integer
  fprintf(stderr, "Test invalid record names\n");

  ret = TSRecordGetInt("proy.config.cop.core_signal", &port1);
  if (ret != TS_ERR_OKAY) {
    print_err("TSRecordGetInt", ret);
  } else
    printf("[TSRecordGetInt] proxy.config.cop.core_signal=%" PRId64 " \n", port1);

  // test set integer
  ret = TSRecordSetInt("proy.config.cop.core_signal", new_port, &action);
  print_err("TSRecordSetInt", ret);

  printf("\n");
  if (TSRecordGetCounter("proxy.press.socks.connections_successful", &ctr1) != TS_ERR_OKAY)
    printf("TSRecordGetCounter FAILED!\n");
  else
    printf("[TSRecordGetCounter]proxy.process.socks.connections_successful=%" PRId64 " \n", ctr1);

  printf("\n");
  if (TSRecordGetFloat("proxy.conig.http.cache.fuzz.probability", &flt1) != TS_ERR_OKAY)
    printf("TSRecordGetFloat FAILED!\n");
  else
    printf("[TSRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt1);
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
  TSActionNeedT action;
  char *rec_value;
  char new_str[] = "new_record_value";
  TSInt port1, port2, new_port  = 52432;
  TSFloat flt1, flt2, new_flt   = 1.444;
  TSCounter ctr1, ctr2, new_ctr = 6666;
  TSMgmtError err;

  /********************* START TEST SECTION *****************/
  printf("\n\n");

#if SET_INT
  // test set integer
  if (TSRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != TS_ERR_OKAY)
    printf("TSRecordSetInt FAILED!\n");
  else
    printf("[TSRecordSetInt] proxy.config.cop.core_signal=%" PRId64 " \n", new_port);
#endif

#if TEST_REC_GET
  TSRecordEle *rec_ele;
  // retrieve a string value record using generic RecordGet
  rec_ele = TSRecordEleCreate();
  if (TSRecordGet("proxy.config.http.cache.vary_default_other", rec_ele) != TS_ERR_OKAY)
    printf("TSRecordGet FAILED!\n");
  else
    printf("[TSRecordGet] proxy.config.http.cache.vary_default_other=%s\n", rec_ele->string_val);

  TSRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif

#if TEST_REC_GET_2
  // retrieve a string value record using generic RecordGet
  rec_ele = TSRecordEleCreate();
  if (TSRecordGet("proxy.config.proxy_name", rec_ele) != TS_ERR_OKAY)
    printf("TSRecordGet FAILED!\n");
  else
    printf("[TSRecordGet] proxy.config.proxy_name=%s\n", rec_ele->string_val);

  TSRecordEleDestroy(rec_ele);
  printf("\n\n");
#endif

#if TEST_STRING
  // retrieve an string value record using GetString
  err = TSRecordGetString("proxy.config.proxy_name", &rec_value);
  if (err != TS_ERR_OKAY)
    print_err("TSRecordGetString", err);
  else
    printf("[TSRecordGetString] proxy.config.proxy_name=%s\n", rec_value);
  TSfree(rec_value);
  rec_value = NULL;

  // test RecordSet
  err = TSRecordSetString("proxy.config.proxy_name", (TSString)new_str, &action);
  if (err != TS_ERR_OKAY)
    print_err("TSRecordSetString", err);
  else
    printf("[TSRecordSetString] proxy.config.proxy_name=%s\n", new_str);

  // get
  err = TSRecordGetString("proxy.config.proxy_name", &rec_value);
  if (err != TS_ERR_OKAY)
    print_err("TSRecordGetString", err);
  else
    printf("[TSRecordGetString] proxy.config.proxy_name=%s\n", rec_value);
  printf("\n");
  TSfree(rec_value);
#endif

#if TEST_INT
  printf("\n");
  // test get integer
  if (TSRecordGetInt("proxy.config.cop.core_signal", &port1) != TS_ERR_OKAY)
    printf("TSRecordGetInt FAILED!\n");
  else
    printf("[TSRecordGetInt] proxy.config.cop.core_signal=%" PRId64 " \n", port1);

  // test set integer
  if (TSRecordSetInt("proxy.config.cop.core_signal", new_port, &action) != TS_ERR_OKAY)
    printf("TSRecordSetInt FAILED!\n");
  else
    printf("[TSRecordSetInt] proxy.config.cop.core_signal=%" PRId64 " \n", new_port);

  if (TSRecordGetInt("proxy.config.cop.core_signal", &port2) != TS_ERR_OKAY)
    printf("TSRecordGetInt FAILED!\n");
  else
    printf("[TSRecordGetInt] proxy.config.cop.core_signal=%" PRId64 " \n", port2);
  printf("\n");
#endif

#if TEST_COUNTER
  printf("\n");

  if (TSRecordGetCounter("proxy.process.socks.connections_successful", &ctr1) != TS_ERR_OKAY)
    printf("TSRecordGetCounter FAILED!\n");
  else
    printf("[TSRecordGetCounter]proxy.process.socks.connections_successful=%" PRId64 " \n", ctr1);

  if (TSRecordSetCounter("proxy.process.socks.connections_successful", new_ctr, &action) != TS_ERR_OKAY)
    printf("TSRecordSetCounter FAILED!\n");
  else
    printf("[TSRecordSetCounter] proxy.process.socks.connections_successful=%" PRId64 " \n", new_ctr);

  if (TSRecordGetCounter("proxy.process.socks.connections_successful", &ctr2) != TS_ERR_OKAY)
    printf("TSRecordGetCounter FAILED!\n");
  else
    printf("[TSRecordGetCounter]proxy.process.socks.connections_successful=%" PRId64 " \n", ctr2);
  printf("\n");
#endif

#if TEST_FLOAT
  printf("\n");
  if (TSRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt1) != TS_ERR_OKAY)
    printf("TSRecordGetFloat FAILED!\n");
  else
    printf("[TSRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt1);

  if (TSRecordSetFloat("proxy.config.http.cache.fuzz.probability", new_flt, &action) != TS_ERR_OKAY)
    printf("TSRecordSetFloat FAILED!\n");
  else
    printf("[TSRecordSetFloat] proxy.config.http.cache.fuzz.probability=%f\n", new_flt);

  if (TSRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt2) != TS_ERR_OKAY)
    printf("TSRecordGetFloat FAILED!\n");
  else
    printf("[TSRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt2);
  printf("\n");
#endif

#if TEST_REC_SET
  printf("\n");
  if (TSRecordSet("proxy.config.http.cache.fuzz.probability", "-0.3456", &action) != TS_ERR_OKAY)
    printf("TSRecordSet FAILED!\n");
  else
    printf("[TSRecordSet] proxy.config.http.cache.fuzz.probability=-0.3456\n");

  if (TSRecordGetFloat("proxy.config.http.cache.fuzz.probability", &flt2) != TS_ERR_OKAY)
    printf("TSRecordGetFloat FAILED!\n");
  else
    printf("[TSRecordGetFloat] proxy.config.http.cache.fuzz.probability=%f\n", flt2);
#endif
}

// retrieves the value of the "proxy.config.xxx" record requested at input
void
test_rec_get(char *rec_name)
{
  TSRecordEle *rec_ele;
  TSMgmtError ret;
  char *name;

  name = ats_strdup(rec_name);
  printf("[test_rec_get] Get Record: %s\n", name);

  // retrieve a string value record using generic RecordGet
  rec_ele = TSRecordEleCreate();
  if ((ret = TSRecordGet(name, rec_ele)) != TS_ERR_OKAY)
    printf("TSRecordGet FAILED!\n");
  else {
    switch (rec_ele->rec_type) {
    case TS_REC_INT:
      printf("[TSRecordGet] %s=%" PRId64 "\n", name, rec_ele->valueT.int_val);
      break;
    case TS_REC_COUNTER:
      printf("[TSRecordGet] %s=%" PRId64 "\n", name, rec_ele->valueT.counter_val);
      break;
    case TS_REC_FLOAT:
      printf("[TSRecordGet] %s=%f\n", name, rec_ele->valueT.float_val);
      break;
    case TS_REC_STRING:
      printf("[TSRecordGet] %s=%s\n", name, rec_ele->valueT.string_val);
      break;
    default:
      // Handled here:
      // TS_REC_UNDEFINED
      break;
    }
  }

  print_err("TSRecordGet", ret);

  TSRecordEleDestroy(rec_ele);
  TSfree(name);
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
  TSRecordEle *rec_ele;
  TSStringList name_list;
  TSList rec_list;
  int i, num;
  char *v1, *v2, *v3, *v6, *v7, *v8;
  TSMgmtError ret;

  name_list = TSStringListCreate();
  rec_list  = TSListCreate();

  const size_t v1_size = (sizeof(char) * (strlen("proxy.config.proxy_name") + 1));
  v1                   = (char *)TSmalloc(v1_size);
  ink_strlcpy(v1, "proxy.config.proxy_name", v1_size);
  const size_t v2_size = (sizeof(char) * (strlen("proxy.config.bin_path") + 1));
  v2                   = (char *)TSmalloc(v2_size);
  ink_strlcpy(v2, "proxy.config.bin_path", v2_size);
  const size_t v3_size = (sizeof(char) * (strlen("proxy.config.manager_binary") + 1));
  v3                   = (char *)TSmalloc(v3_size);
  ink_strlcpy(v3, "proxy.config.manager_binary", v3_size);
  const size_t v6_size = (sizeof(char) * (strlen("proxy.config.env_prep") + 1));
  v6                   = (char *)TSmalloc(v6_size);
  ink_strlcpy(v6, "proxy.config.env_prep", v6_size);
  const size_t v7_size = (sizeof(char) * (strlen("proxy.config.cop.core_signal") + 1));
  v7                   = (char *)TSmalloc(v7_size);
  ink_strlcpy(v7, "proxy.config.cop.core_signal", v7_size);
  const size_t v8_size = (sizeof(char) * (strlen("proxy.config.http.cache.fuzz.probability") + 1));
  v8                   = (char *)TSmalloc(v8_size);
  ink_strlcpy(v8, "proxy.config.http.cache.fuzz.probability", v8_size);

  // add the names to the get_list
  TSStringListEnqueue(name_list, v1);
  TSStringListEnqueue(name_list, v2);
  TSStringListEnqueue(name_list, v3);
  TSStringListEnqueue(name_list, v6);
  TSStringListEnqueue(name_list, v7);
  TSStringListEnqueue(name_list, v8);

  num = TSStringListLen(name_list);
  printf("Num Records to Get: %d\n", num);
  ret = TSRecordGetMlt(name_list, rec_list);
  // free the string list
  TSStringListDestroy(name_list);
  if (ret != TS_ERR_OKAY) {
    print_err("TSStringListDestroy", ret);
  }

  for (i = 0; i < num; i++) {
    rec_ele = (TSRecordEle *)TSListDequeue(rec_list);
    if (!rec_ele) {
      printf("ERROR\n");
      break;
    }
    printf("Record: %s = ", rec_ele->rec_name);
    switch (rec_ele->rec_type) {
    case TS_REC_INT:
      printf("%" PRId64 "\n", rec_ele->valueT.int_val);
      break;
    case TS_REC_COUNTER:
      printf("%" PRId64 "\n", rec_ele->valueT.counter_val);
      break;
    case TS_REC_FLOAT:
      printf("%f\n", rec_ele->valueT.float_val);
      break;
    case TS_REC_STRING:
      printf("%s\n", rec_ele->valueT.string_val);
      break;
    default:
      // Handled here:
      // TS_REC_UNDEFINED
      break;
    }
    TSRecordEleDestroy(rec_ele);
  }

  TSListDestroy(rec_list); // must dequeue and free each string individually

  return;
}

/* ------------------------------------------------------------------------
 * test_record_set_mlt
 * ------------------------------------------------------------------------
 * Creates a list of TSRecordEle's, and then batch request to set records
 * Also checks to make sure correct action_need type is set.
 */
void
test_record_set_mlt(void)
{
  TSList list;
  TSRecordEle *ele1, *ele2, *ele3, *ele4, *ele5;
  TSActionNeedT action = TS_ACTION_UNDEFINED;
  TSMgmtError err;

  list = TSListCreate();

  ele1                    = TSRecordEleCreate(); // TS_TYPE_UNDEFINED action
  ele1->rec_name          = TSstrdup("proxy.config.cli_binary");
  ele1->rec_type          = TS_REC_STRING;
  ele1->valueT.string_val = TSstrdup(ele1->rec_name);

  ele2                   = TSRecordEleCreate(); // reread action
  ele2->rec_name         = TSstrdup("proxy.config.http.cache.fuzz.probability");
  ele2->rec_type         = TS_REC_FLOAT;
  ele2->valueT.float_val = 0.1234;

  ele3                 = TSRecordEleCreate(); // undefined action
  ele3->rec_name       = TSstrdup("proxy.config.cop.core_signal");
  ele3->rec_type       = TS_REC_INT;
  ele3->valueT.int_val = -4;

  ele4                 = TSRecordEleCreate(); // restart TM
  ele4->rec_name       = (char *)TSstrdup("proxy.local.cluster.type");
  ele4->rec_type       = TS_REC_INT;
  ele4->valueT.int_val = 2;

  ele5                 = TSRecordEleCreate(); // reread action
  ele5->rec_name       = (char *)TSstrdup("proxy.config.cluster.mc_ttl");
  ele5->rec_type       = TS_REC_INT;
  ele5->valueT.int_val = 555;

  TSListEnqueue(list, ele4);
  TSListEnqueue(list, ele1);
  TSListEnqueue(list, ele2);
  TSListEnqueue(list, ele3);
  TSListEnqueue(list, ele5);

  err = TSRecordSetMlt(list, &action);
  print_err("TSRecordSetMlt", err);
  fprintf(stderr, "[TSRecordSetMlt] Action Required: %d\n", action);

  // cleanup: need to iterate through list and delete each ele
  int count = TSListLen(list);
  for (int i = 0; i < count; i++) {
    TSRecordEle *ele = (TSRecordEle *)TSListDequeue(list);
    TSRecordEleDestroy(ele);
  }
  TSListDestroy(list);
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
  TSMgmtError err;

  if (!valid) {
    // first try

    err = TSReadFromUrlEx("hsdfasdf.com:80/index.html", &header, &headerSize, &body, &bodySize, 50000);
    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("--------------------------------------------------------------\n");
      //  printf("The header...\n%s\n%d\n", *header, *headerSize);
      printf("--------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (body)
      TSfree(body);
    if (header)
      TSfree(header);

    err = TSReadFromUrlEx("http://sadfasdfi.com:80/", &header, &headerSize, &body, &bodySize, 50000);
    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header)
      TSfree(header);
    if (body)
      TSfree(body);

  } else { // use valid urls
    err = TSReadFromUrlEx("lakota.example.com:80/", &header, &headerSize, &body, &bodySize, 50000);

    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header)
      TSfree(header);
    if (body)
      TSfree(body);

    // read second url
    err = TSReadFromUrlEx("http://www.apache.org:80/index.html", &header, &headerSize, &body, &bodySize, 50000);
    if (err != TS_ERR_OKAY) {
      print_err("TSReadFromUrlEx", err);
    } else {
      printf("---------------------------------------------------------------\n");
      printf("The header...\n%s\n%d\n", header, headerSize);
      printf("-------------------------------------------------------------\n");
      printf("The body...\n%s\n%d\n", body, bodySize);
    }
    if (header)
      TSfree(header);
    if (body)
      TSfree(body);
  }
}

/* ------------------------------------------------------------------------
 * test_read_file
 * ------------------------------------------------------------------------
 * reads hosting.config file and prints it out
 */
void
test_read_file()
{
  char *f_text = NULL;
  int f_size   = -1;
  int f_ver    = -1;

  printf("\n");
  if (TSConfigFileRead(TS_FNAME_HOSTING, &f_text, &f_size, &f_ver) != TS_ERR_OKAY)
    printf("[TSConfigFileRead] FAILED!\n");
  else {
    printf("[TSConfigFileRead]\n\tFile Size=%d, Version=%d\n%s\n", f_size, f_ver, f_text);
    TSfree(f_text);
  }
}

/* ------------------------------------------------------------------------
 * test_write_file
 * ------------------------------------------------------------------------
 * writes hosting.config with some garbage text then reads the file and
 * prints the new file to stdout
 */
void
test_write_file()
{
  char *f_text      = NULL;
  int f_size        = -1;
  int f_ver         = -1;
  char new_f_text[] = "blah, blah blah\n I hope this works. please!!!   \n";
  int new_f_size    = strlen(new_f_text);

  printf("\n");
  if (TSConfigFileWrite(TS_FNAME_HOSTING, new_f_text, new_f_size, -1) != TS_ERR_OKAY)
    printf("[TSConfigFileWrite] FAILED!\n");
  else
    printf("[TSConfigFileWrite] SUCCESS!\n");
  printf("\n");

  // should free f_text???
  if (TSConfigFileRead(TS_FNAME_HOSTING, &f_text, &f_size, &f_ver) != TS_ERR_OKAY)
    printf("[TSConfigFileRead] FAILED!\n");
  else {
    printf("[TSConfigFileRead]\n\tFile Size=%d, Version=%d\n%s\n", f_size, f_ver, f_text);
    TSfree(f_text);
  }
}

/***************************************************************************
 * TSCfgContext Testing
 ***************************************************************************/
// tests the TSCfgContextMoveEleUp/Down functions (which end up calling
// the new "copy" utility functions in CfgContextUtils.cc

// uses TSCfgContextGet to read in a file and print out all the rules
void
test_cfg_context_get(char *args)
{
  TSCfgContext ctx;
  TSFileNameT file;
  char *filename;

  strtok(args, ":");
  filename = strtok(NULL, ":");
  fprintf(stderr, "modify file: %s\n", filename);
  char *name = TSstrdup(filename);

  // convert file name to TSFileNameT
  if (strcmp(name, "cache.config") == 0) {
    file = TS_FNAME_CACHE_OBJ;
  } else if (strcmp(name, "congestion.config") == 0) {
    file = TS_FNAME_CONGESTION;
  } else if (strcmp(name, "hosting.config") == 0) {
    file = TS_FNAME_HOSTING;
  } else if (strcmp(name, "icp.config") == 0) {
    file = TS_FNAME_ICP_PEER;
  } else if (strcmp(name, "ip_allow.config") == 0) {
    file = TS_FNAME_IP_ALLOW;
  } else if (strcmp(name, "logs_xml.config") == 0) {
    file = TS_FNAME_LOGS_XML;
  } else if (strcmp(name, "parent.config") == 0) {
    file = TS_FNAME_PARENT_PROXY;
  } else if (strcmp(name, "volume.config") == 0) {
    file = TS_FNAME_VOLUME;
  } else if (strcmp(name, "plugin.config") == 0) {
    file = TS_FNAME_PLUGIN;
  } else if (strcmp(name, "remap.config") == 0) {
    file = TS_FNAME_REMAP;
  } else if (strcmp(name, "socks.config") == 0) {
    file = TS_FNAME_SOCKS;
  } else if (strcmp(name, "storage.config") == 0) {
    file = TS_FNAME_STORAGE;
  } else if (strcmp(name, "splitdns.config") == 0) {
    file = TS_FNAME_SPLIT_DNS;
  } else if (strcmp(name, "vaddrs.config") == 0) {
    file = TS_FNAME_VADDRS;
  } else {
    TSfree(name);
    return;
  }

  ctx = TSCfgContextCreate(file);
  if (TSCfgContextGet(ctx) != TS_ERR_OKAY)
    printf("ERROR READING FILE\n");

  int count = TSCfgContextGetCount(ctx);
  printf("%d rules in file: %s\n", count, name);

  print_ele_list(file, ctx);

  TSCfgContextDestroy(ctx);
  TSfree(name);
  return;
}

//
// tests the TSCfgContextMoveEleUp/Down functions (which end up calling
// the new "copy" utility functions in CfgContextUtils.cc
// depending on the file specified, this will move the top rule to the bottom,
// and the second to the last rule to the top
// essentially, the original top and bottom rules switch places!!
//
void
test_cfg_context_move(char *args)
{
  TSCfgContext ctx;
  TSFileNameT file;
  int i;
  TSMgmtError err;
  char *filename;

  strtok(args, ":");
  filename = strtok(NULL, ":");
  fprintf(stderr, "modify file: %s\n", filename);
  char *name = TSstrdup(filename);

  // convert file name to TSFileNameT
  if (strcmp(name, "cache.config") == 0) {
    file = TS_FNAME_CACHE_OBJ;
  } else if (strcmp(name, "congestion.config") == 0) {
    file = TS_FNAME_CONGESTION;
  } else if (strcmp(name, "hosting.config") == 0) {
    file = TS_FNAME_HOSTING;
  } else if (strcmp(name, "icp.config") == 0) {
    file = TS_FNAME_ICP_PEER;
  } else if (strcmp(name, "ip_allow.config") == 0) {
    file = TS_FNAME_IP_ALLOW;
  } else if (strcmp(name, "logs_xml.config") == 0) {
    file = TS_FNAME_LOGS_XML;
  } else if (strcmp(name, "parent.config") == 0) {
    file = TS_FNAME_PARENT_PROXY;
  } else if (strcmp(name, "volume.config") == 0) {
    file = TS_FNAME_VOLUME;
  } else if (strcmp(name, "remap.config") == 0) {
    file = TS_FNAME_REMAP;
  } else if (strcmp(name, "socks.config") == 0) {
    file = TS_FNAME_SOCKS;
  } else if (strcmp(name, "storage.config") == 0) {
    file = TS_FNAME_STORAGE;
  } else if (strcmp(name, "splitdns.config") == 0) {
    file = TS_FNAME_SPLIT_DNS;
  } else if (strcmp(name, "vaddrs.config") == 0) {
    file = TS_FNAME_VADDRS;
  } else {
    TSfree(name);
    return;
  }

  ctx = TSCfgContextCreate(file);
  if (TSCfgContextGet(ctx) != TS_ERR_OKAY)
    printf("ERROR READING FILE\n");

  int count = TSCfgContextGetCount(ctx);
  printf("%d rules in file: %s\n", count, name);

  // shift all the ele's up so that the top ele is now the bottom ele
  printf("\nShift all ele's up so that top ele is now bottom ele\n");
  for (i = 1; i < count; i++) {
    err = TSCfgContextMoveEleUp(ctx, i);
    if (err != TS_ERR_OKAY) {
      printf("ERROR moving ele at index %d up \n", i);
      goto END;
    }
  }

  // shift all the ele's down so that the next to bottom ele is now top ele
  // move all ele's above the last ele down; bottom ele becomes top ele
  printf("\nShift all Ele's above second to last ele down; bottom ele becomes top ele\n");
  for (i = count - 3; i >= 0; i--) {
    err = TSCfgContextMoveEleDown(ctx, i);
    if (err != TS_ERR_OKAY) {
      printf("ERROR: moving ele down at index %d\n", i);
      goto END;
    }
  }

  // clean up; commit change
  TSCfgContextCommit(ctx, NULL, NULL);

END:
  TSCfgContextDestroy(ctx);
  TSfree(name);
  return;
}

void
test_cfg_context_ops()
{
  // Not used here.
  // TSCfgIterState iter_state;
  // TSCfgEle *cfg_ele;
  TSMgmtError err;
  TSCfgContext ctx;
  TSVirtIpAddrEle *ele;
  int rm_index = 0, i;
  int insert_at;

  ctx = TSCfgContextCreate(TS_FNAME_VADDRS);

  if (TSCfgContextGet(ctx) != TS_ERR_OKAY)
    printf("ERROR READING FILE\n");

  printf("\nBEFORE CHANGE:\n");
  //  print_VirtIpAddr_ele_list(ctx);

  int count = TSCfgContextGetCount(ctx);
  printf("# ele's = %d\n", count);

  printf("\nShifted all Ele's < %d up\n", rm_index);
  // move all ele's below rm_index up one; this shifts the rm_index ele to
  // bottom of TSCfgContext
  for (i = (rm_index + 1); i < count; i++) {
    err = TSCfgContextMoveEleUp(ctx, i);
    if (err != TS_ERR_OKAY) {
      printf("ERROR moving ele at index %d up \n", i);
      goto END;
    }
  }
  // print_VirtIpAddr_ele_list(ctx);

  printf("\nREMOVE LAST ELE (originally the first ele)\n");
  // remove the last ele (which was originally the first ele)
  err = TSCfgContextRemoveEleAt(ctx, (count - 1));
  if (err != TS_ERR_OKAY) {
    printf("ERROR: removing ele at index %d\n", count - 1);
    goto END;
  }

  printf("\nRemoving second to last Ele \n");
  err = TSCfgContextRemoveEleAt(ctx, (count - 2));
  if (err != TS_ERR_OKAY) {
    printf("ERROR: removing ele at index %d\n", count - 2);
    goto END;
  }

  // append a new ele
  printf("\nappend new ele\n");
  ele = TSVirtIpAddrEleCreate();
  if (ele) {
    ele->ip_addr  = TSstrdup("201.201.201.201");
    ele->intr     = TSstrdup("appended");
    ele->sub_intr = 201;
    err           = TSCfgContextAppendEle(ctx, (TSCfgEle *)ele);
    if (err != TS_ERR_OKAY) {
      printf("ERROR: append ele\n");
      TSVirtIpAddrEleDestroy(ele);
      goto END;
    }
  } else {
    printf("Can't create VirtIpAddrEle\n");
  }
  // print_VirtIpAddr_ele_list(ctx);

  insert_at = 1;
  // insert a new ele in insert_at index
  printf("\nINSERT NEW ELE at %d index\n", insert_at);
  ele = TSVirtIpAddrEleCreate();
  if (ele) {
    ele->ip_addr  = TSstrdup("101.101.101.101");
    ele->intr     = (char *)TSstrdup("inserted");
    ele->sub_intr = 100;
    err           = TSCfgContextInsertEleAt(ctx, (TSCfgEle *)ele, insert_at);
    if (err != TS_ERR_OKAY) {
      printf("ERROR: insert ele  at index %d\n", insert_at);
      TSVirtIpAddrEleDestroy(ele);
      goto END;
    }
  } else {
    printf("Can't create VirtIpAddrEle\n");
  }
  // print_VirtIpAddr_ele_list(ctx);

  printf("\nMove ele at index %d to botoom of list\n", insert_at);
  for (i = insert_at; i < TSCfgContextGetCount(ctx); i++) {
    err = TSCfgContextMoveEleDown(ctx, i);
    if (err != TS_ERR_OKAY) {
      printf("ERROR: moving ele down at index %d\n", i);
      goto END;
    }
  }
  // print_VirtIpAddr_ele_list(ctx);

  printf("\nShift all Ele's above last ele down; bottom ele becomes top ele\n");
  count = TSCfgContextGetCount(ctx);
  for (i = count - 2; i >= 0; i--) {
    err = TSCfgContextMoveEleDown(ctx, i);
    if (err != TS_ERR_OKAY) {
      printf("ERROR: moving ele down at index %d\n", i);
      goto END;
    }
  }
  // print_VirtIpAddr_ele_list(ctx);

  // commit change
  TSCfgContextCommit(ctx, NULL, NULL);

  printf("\nAFTER CHANGE:\n");
// print_VirtIpAddr_ele_list(ctx);

END:
  TSCfgContextDestroy(ctx);
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
  // TSCfgIterState iter_state;
  TSCfgContext ctx;
  TSCfgEle *cfg_ele;
  TSPluginEle *ele;

  ctx = TSCfgContextCreate(TS_FNAME_PLUGIN);
  if (TSCfgContextGet(ctx) != TS_ERR_OKAY)
    printf("ERROR READING FILE\n");

  // retrieve and modify ele
  printf("test_cfg_plugin: modifying the first ele...\n");
  cfg_ele = TSCfgContextGetEleAt(ctx, 0);
  ele     = (TSPluginEle *)cfg_ele;
  if (ele) {
    // free(ele->name);
    ele->name = ats_strdup("change-plugin.so");
  }

  // remove the second ele
  printf("test_cfg_plugin: removing the second ele...\n");
  TSCfgContextRemoveEleAt(ctx, 1);

  // create and add new ele
  printf("test_socks_set: appending a new ele...\n");
  ele = TSPluginEleCreate();

  ele->name = ats_strdup("new-plugin.so");

  ele->args = TSStringListCreate();
  TSStringListEnqueue(ele->args, ats_strdup("arg1"));
  TSStringListEnqueue(ele->args, ats_strdup("arg2"));
  TSCfgContextAppendEle(ctx, (TSCfgEle *)ele);

  // commit change
  TSCfgContextCommit(ctx, NULL, NULL);

  TSCfgContextDestroy(ctx);
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
  // TSCfgIterState iter_state;
  TSCfgContext ctx;
  TSCfgEle *cfg_ele;
  TSSocksEle *ele;

  ctx = TSCfgContextCreate(TS_FNAME_SOCKS);
  if (TSCfgContextGet(ctx) != TS_ERR_OKAY)
    printf("ERROR READING FILE\n");

  // retrieving an ele
  printf("test_socks_set: modifying the fourth ele...\n");
  cfg_ele = TSCfgContextGetEleAt(ctx, 3);
  ele     = (TSSocksEle *)cfg_ele;
  if (ele) {
    if (ele->rr != TS_RR_NONE)
      ele->rr = TS_RR_FALSE;
  }

  // remove the second ele
  printf("test_socks_set: removing the second ele...\n");
  TSCfgContextRemoveEleAt(ctx, 1);

  // create new structs for new rules
  TSIpAddrEle *ip1 = TSIpAddrEleCreate();
  ip1->type        = TS_IP_SINGLE;
  ip1->ip_a        = TSstrdup("1.1.1.1");

  TSDomainList dlist = TSDomainListCreate();
  TSDomain *dom1     = TSDomainCreate();
  dom1->domain_val   = TSstrdup("www.mucky.com");
  dom1->port         = 8888;
  TSDomainListEnqueue(dlist, dom1);

  TSDomain *dom2   = TSDomainCreate();
  dom2->domain_val = TSstrdup("freakazoid.com");
  dom2->port       = 2222;
  TSDomainListEnqueue(dlist, dom2);

  TSDomain *dom3   = TSDomainCreate();
  dom3->domain_val = TSstrdup("hong.kong.com");
  dom3->port       = 3333;
  TSDomainListEnqueue(dlist, dom3);

  // create and add new ele
  printf("test_socks_set: appending a new ele...\n");
  ele = TSSocksEleCreate(TS_TYPE_UNDEFINED);
  if (ele) {
    ele->cfg_ele.type  = TS_SOCKS_MULTIPLE;
    ele->dest_ip_addr  = ip1;
    ele->socks_servers = dlist;
    ele->rr            = TS_RR_STRICT;

    TSCfgContextAppendEle(ctx, (TSCfgEle *)ele);
  } else {
    printf("Can't create SocksEle\n");
  }

  // commit change
  TSCfgContextCommit(ctx, NULL, NULL);

  TSCfgContextDestroy(ctx);
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
  TSList events;
  TSMgmtError ret;
  int count, i;
  char *name;

  printf("[print_active_events]\n");

  events = TSListCreate();
  ret    = TSActiveEventGetMlt(events);
  if (ret != TS_ERR_OKAY) {
    print_err("TSActiveEventGetMlt", ret);
    goto END;
  } else { // successful get
    count = TSListLen(events);
    for (i = 0; i < count; i++) {
      name = (char *)TSListDequeue(events);
      printf("\t%s\n", name);
      TSfree(name);
    }
  }

END:
  TSListDestroy(events);
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
  TSMgmtError ret;

  ret = TSEventIsActive(event_name, &active);
  print_err("TSEventIsActive", ret);

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
  TSMgmtError ret;
  char *name;

  name = TSstrdup(event_name);
  printf("[try_resolve] Resolving event: %s\n", name);

  if (check_active(name)) { // resolve events
    ret = TSEventResolve(name);
    print_err("TSEventResolve", ret);
    check_active(name); // should be non-active now
  }

  TSfree(name);
}

/* ------------------------------------------------------------------------
 * eventCallbackFn
 * ------------------------------------------------------------------------
 * the callback function; when called, it just prints out the name
 * of the event that was signalled
 */
void
eventCallbackFn(char *name, char *msg, int /* pri ATS_UNUSED */, void * /* data ATS_UNUSED */)
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
  TSMgmtError err;

  printf("\n[register_event_callback] \n");
  err = TSEventSignalCbRegister(NULL, eventCallbackFn, NULL);
  print_err("TSEventSignalCbRegister", err);
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
  TSMgmtError err;

  printf("\n[unregister_event_callback]\n");
  err = TSEventSignalCbUnregister(NULL, eventCallbackFn);
  print_err("TSEventSignalCbUnregister", err);
}

/***************************************************************************
 * Snapshots Testing
 ***************************************************************************/

void
print_snapshots()
{
  TSStringList list;
  TSMgmtError err;
  char *name;

  list = TSStringListCreate();
  err  = TSSnapshotGetMlt(list);
  print_err("TSSnapshotGetMlt", err);

  printf("All Snapshots:\n");
  if (err == TS_ERR_OKAY) {
    int num = TSStringListLen(list);
    for (int i = 0; i < num; i++) {
      name = TSStringListDequeue(list);
      if (name)
        printf("%s\n", name);
      TSfree(name);
    }
  }

  TSStringListDestroy(list);
  return;
}

void
add_snapshot(char *args)
{
  char *snap_name;

  strtok(args, ":");
  snap_name = strtok(NULL, ":");
  fprintf(stderr, "add snapshot: %s\n", snap_name);
  char *name = TSstrdup(snap_name);

  TSMgmtError err = TSSnapshotTake(name);
  print_err("TSSnapshotTake", err);

  TSfree(name);
}

void
remove_snapshot(char *args)
{
  char *snap_name;

  strtok(args, ":");
  snap_name = strtok(NULL, ":");
  fprintf(stderr, "remove snapshot: %s\n", snap_name);
  char *name = TSstrdup(snap_name);

  TSMgmtError err = TSSnapshotRemove(name);
  print_err("TSSnapshotRemove", err);

  TSfree(name);
}

void
restore_snapshot(char *args)
{
  char *snap_name;

  strtok(args, ":");
  snap_name = strtok(NULL, ":");
  fprintf(stderr, "resotre snapshot: %s\n", snap_name);
  char *name = TSstrdup(snap_name);

  TSMgmtError err = TSSnapshotRestore(name);
  print_err("TSSnapshotRestore", err);

  TSfree(name);
}

/***************************************************************************
 * Diags Testing
 ***************************************************************************/

void
test_diags()
{
  // status
  for (int i = 0; i < 5; i++) {
    TSDiags(TS_DIAG_STATUS, "[remote]status diag %d", i);
  }

  // warning
  TSDiags(TS_DIAG_WARNING, "[remote]warning msg %s %s", "I am", "a fiue");

  // fatal
  TSDiags(TS_DIAG_FATAL, "[remote]FATAL, FATAL: Nuclear meltdown in %d seconds", 10);

  // error
  TSDiags(TS_DIAG_ERROR, "[remote]error msg shouldn't have printed this %s", "argument");

  // debug
  TSDiags(TS_DIAG_DEBUG, "[remote]debug ... wish I was good at it");
}

/***************************************************************************
 * Statistics
 ***************************************************************************/

// generate dummy values for statistics
void
set_stats()
{
  TSActionNeedT action;

  fprintf(stderr, "[set_stats] Set Dummy Stat Values\n");

  TSRecordSetInt("proxy.process.http.user_agent_response_document_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.user_agent_response_header_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.current_client_connections", 100, &action);
  TSRecordSetInt("proxy.process.http.current_client_transactions", 100, &action);
  TSRecordSetInt("proxy.process.http.origin_server_response_document_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.origin_server_response_header_total_size", 100, &action);
  TSRecordSetInt("proxy.process.http.current_server_connections", 100, &action);
  TSRecordSetInt("proxy.process.http.current_server_transactions", 100, &action);

  TSRecordSetFloat("proxy.node.bandwidth_hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.node.hostdb.hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.node.cache.percent_free", 110, &action);
  TSRecordSetFloat("proxy.node.cache_hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.node.cache_hit_mem_ratio", 110, &action);
  TSRecordSetFloat("proxy.node.bandwidth_hit_ratio_avg_10s", 110, &action);
  TSRecordSetFloat("proxy.node.http.cache_hit_fresh_avg_10s", 110, &action);
  TSRecordSetFloat("proxy.node.http.cache_hit_mem_fresh_avg_10s", 110, &action);
  TSRecordSetFloat("proxy.node.http.cache_hit_revalidated_avg_10s", 110, &action);
  TSRecordSetFloat("proxy.node.http.cache_hit_ims_avg_10s", 100, &action);
  TSRecordSetFloat("proxy.node.client_throughput_out", 110, &action);

  TSRecordSetInt("proxy.node.cache_hit_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.node.cache_hit_mem_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.node.bandwidth_hit_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.node.hostdb.hit_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.node.proxy_running", 110, &action);
  TSRecordSetInt("proxy.node.hostdb.hit_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.node.proxy_running", 110, &action);
  TSRecordSetInt("proxy.node.current_client_connections", 110, &action);
  TSRecordSetInt("proxy.node.current_cache_connections", 110, &action);

  TSRecordSetFloat("proxy.cluster.user_agent_total_bytes_avg_10s", 110, &action);
  TSRecordSetFloat("proxy.cluster.origin_server_total_bytes_avg_10s", 110, &action);
  TSRecordSetFloat("proxy.cluster.bandwidth_hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.cluster.bandwidth_hit_ratio_avg_10s", 110, &action);
  TSRecordSetFloat("proxy.cluster.cache_hit_ratio", 110, &action);
  TSRecordSetFloat("proxy.cluster.cache_hit_mem_ratio", 110, &action);

  TSRecordSetInt("proxy.cluster.cache_hit_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.cluster.cache_hit_mem_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.cluster.bandwidth_hit_ratio_int_pct", 110, &action);
  TSRecordSetInt("proxy.cluster.cache_total_hits", 110, &action);
  TSRecordSetInt("proxy.cluster.cache_total_hits_mem", 110, &action);
  TSRecordSetInt("proxy.cluster.cache_total_misses", 110, &action);
  TSRecordSetInt("proxy.cluster.http.throughput", 110, &action);
}

void
print_stats()
{
  TSFloat f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11;
  TSInt i1, i2, i3, i4, i5, i6, i7, i8, i9;

  fprintf(stderr, "[print_stats]\n");

  TSRecordGetInt("proxy.process.http.user_agent_response_document_total_size", &i1);
  TSRecordGetInt("proxy.process.http.user_agent_response_header_total_size", &i2);
  TSRecordGetInt("proxy.process.http.current_client_connections", &i3);
  TSRecordGetInt("proxy.process.http.current_client_transactions", &i4);
  TSRecordGetInt("proxy.process.http.origin_server_response_document_total_size", &i5);
  TSRecordGetInt("proxy.process.http.origin_server_response_header_total_size", &i6);
  TSRecordGetInt("proxy.process.http.current_server_connections", &i7);
  TSRecordGetInt("proxy.process.http.current_server_transactions", &i8);

  fprintf(stderr, "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "\n", i1,
          i2, i3, i4, i5, i6, i7, i8);

  TSRecordGetFloat("proxy.node.bandwidth_hit_ratio", &f1);
  TSRecordGetFloat("proxy.node.hostdb.hit_ratio", &f2);
  TSRecordGetFloat("proxy.node.cache.percent_free", &f3);
  TSRecordGetFloat("proxy.node.cache_hit_ratio", &f4);
  TSRecordGetFloat("proxy.node.cache_hit_mem_ratio", &f10);
  TSRecordGetFloat("proxy.node.bandwidth_hit_ratio_avg_10s", &f5);
  TSRecordGetFloat("proxy.node.http.cache_hit_fresh_avg_10s", &f6);
  TSRecordGetFloat("proxy.node.http.cache_hit_mem_fresh_avg_10s", &f11);
  TSRecordGetFloat("proxy.node.http.cache_hit_revalidated_avg_10s", &f7);
  TSRecordGetFloat("proxy.node.http.cache_hit_ims_avg_10s", &f8);
  TSRecordGetFloat("proxy.node.client_throughput_out", &f9);

  fprintf(stderr, "NODE stats: \n%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f\n", f1, f2, f3, f4, f10, f5, f6, f11, f7, f8, f9);

  TSRecordGetInt("proxy.node.cache_hit_ratio_int_pct", &i1);
  TSRecordGetInt("proxy.node.cache_hit_mem_ratio_int_pct", &i7);
  TSRecordGetInt("proxy.node.bandwidth_hit_ratio_int_pct", &i2);
  TSRecordGetInt("proxy.node.hostdb.hit_ratio_int_pct", &i3);
  TSRecordGetInt("proxy.node.proxy_running", &i4);
  TSRecordGetInt("proxy.node.hostdb.hit_ratio_int_pct", &i5);
  TSRecordGetInt("proxy.node.proxy_running", &i6);
  TSRecordGetInt("proxy.node.current_client_connections", &i8);
  TSRecordGetInt("proxy.node.current_cache_connections", &i9);

  fprintf(stderr,
          "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "\n",
          i1, i7, i2, i3, i4, i5, i6, i8, i9);

  TSRecordGetFloat("proxy.cluster.user_agent_total_bytes_avg_10s", &f1);
  TSRecordGetFloat("proxy.cluster.origin_server_total_bytes_avg_10s", &f2);
  TSRecordGetFloat("proxy.cluster.bandwidth_hit_ratio", &f3);
  TSRecordGetFloat("proxy.cluster.bandwidth_hit_ratio_avg_10s", &f4);
  TSRecordGetFloat("proxy.cluster.cache_hit_ratio", &f5);
  TSRecordGetFloat("proxy.cluster.cache_hit_mem_ratio", &f6);

  TSRecordGetInt("proxy.cluster.cache_hit_ratio_int_pct", &i1);
  TSRecordGetInt("proxy.cluster.cache_hit_mem_ratio_int_pct", &i6);
  TSRecordGetInt("proxy.cluster.bandwidth_hit_ratio_int_pct", &i2);
  TSRecordGetInt("proxy.cluster.cache_total_hits", &i3);
  TSRecordGetInt("proxy.cluster.cache_total_hits_mem", &i7);
  TSRecordGetInt("proxy.cluster.cache_total_misses", &i4);
  TSRecordGetInt("proxy.cluster.http.throughput", &i5);

  fprintf(stderr, "CLUSTER stats: \n");
  fprintf(stderr, "%f, %f, %f, %f, %f, %f\n", f1, f2, f3, f4, f5, f6);
  fprintf(stderr, "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "\n", i1, i6, i2, i3, i7,
          i4, i5);

  fprintf(stderr, "PROCESS stats: \n");
  fprintf(stderr, "%f, %f\n", f1, f2);
  fprintf(stderr, "%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "\n", i1, i2, i3, i4);
}

void
reset_stats()
{
  TSMgmtError err = TSStatsReset(false, NULL);
  print_err("TSStatsReset", err);
  return;
}

void
sync_test()
{
  TSActionNeedT action;

  TSRecordSetString("proxy.config.proxy_name", "dorkface", &action);
  printf("[TSRecordSetString] proxy.config.proxy_name \n\tAction Should: [%d]\n\tAction is    : [%d]\n", TS_ACTION_UNDEFINED,
         action);

  TSRecordSetInt("proxy.config.cluster.cluster_port", 3333, &action);
  printf("[TSRecordSetInt] proxy.config.cluster.cluster_port\n\tAction Should: [%d]\n\tAction is    : [%d]\n", TS_ACTION_RESTART,
         action);

  if (TSRecordSet("proxy.config.http.cache.fuzz.probability", "-0.3333", &action) != TS_ERR_OKAY)
    printf("TSRecordSet FAILED!\n");
  else
    printf("[TSRecordSet] proxy.config.http.cache.fuzz.probability=-0.3333\n");

  TSMgmtError ret;
  if ((ret = TSProxyStateSet(TS_PROXY_OFF, TS_CACHE_CLEAR_OFF)) != TS_ERR_OKAY)
    printf("[TSProxyStateSet] turn off FAILED\n");
  print_err("stop_TS", ret);
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
  char buf[512]; // holds request from interactive prompt

  // process input from command line
  while (1) {
    // Display a prompt
    printf("api_cli-> ");

    // get input from command line
    ATS_UNUSED_RETURN(fgets(buf, 512, stdin));

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
      // exit(0);
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
    } else {
      sync_test();
    }

  } // end while(1)

} // end runInteractive

/* ------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------
 * Main entry point which connects the client to the API, does any
 * clean up on exit, and gets the interactive command-line running
 */
int
main(int /* argc ATS_UNUSED */, char ** /* argv ATS_UNUSED */)
{
  TSMgmtError ret;

  if ((ret = TSInit(NULL, TS_MGMT_OPT_DEFAULTS)) == TS_ERR_OKAY) {
    runInteractive();
    TSTerminate();
    printf("END REMOTE API TEST\n");
  } else {
    print_err("main", ret);
  }

  return 0;
} // end main()
