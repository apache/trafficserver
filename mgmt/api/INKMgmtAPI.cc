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
 * Filename: InkMgmtAPI.cc
 * Purpose: This file implements all traffic server management functions.
 * Created: 9/11/00
 * Created by: Lan Tran
 *
 *
 ***************************************************************************/
#include "ts/ink_platform.h"
#include "ts/ink_code.h"
#include "ts/ParseRules.h"
#include <limits.h>
#include "ts/I_Layout.h"

#include "mgmtapi.h"
#include "CfgContextManager.h"
#include "CfgContextImpl.h"
#include "CfgContextUtils.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"

#include "ts/TextBuffer.h"

// forward declarations
void init_pdss_format(TSPdSsFormat &info);

/***************************************************************************
 * API Memory Management
 ***************************************************************************/
void *
_TSmalloc(unsigned int size, const char * /* path ATS_UNUSED */)
{
  return ats_malloc(size);
}

void *
_TSrealloc(void *ptr, unsigned int size, const char * /* path ATS_UNUSED */)
{
  return ats_realloc(ptr, size);
}

char *
_TSstrdup(const char *str, int length, const char * /* path ATS_UNUSED */)
{
  return ats_strndup(str, length);
}

void
_TSfree(void *ptr)
{
  ats_free(ptr);
}

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- TSList operations -------------------------------------------------*/
tsapi TSList
TSListCreate(void)
{
  return (void *)create_queue();
}

/* NOTE: The List must be EMPTY */
tsapi void
TSListDestroy(TSList l)
{
  if (!l)
    return;

  delete_queue((LLQ *)l);
  return;
}

tsapi TSMgmtError
TSListEnqueue(TSList l, void *data)
{
  int ret;

  ink_assert(l && data);
  if (!l || !data)
    return TS_ERR_PARAMS;

  ret = enqueue((LLQ *)l, data); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi void *
TSListDequeue(TSList l)
{
  ink_assert(l);
  if (!l || queue_is_empty((LLQ *)l))
    return NULL;

  return dequeue((LLQ *)l);
}

tsapi bool
TSListIsEmpty(TSList l)
{
  ink_assert(l);
  if (!l)
    return true; // list doesn't exist, so it's empty

  return queue_is_empty((LLQ *)l);
}

tsapi int
TSListLen(TSList l)
{
  ink_assert(l);
  if (!l)
    return -1;

  return queue_len((LLQ *)l);
}

tsapi bool
TSListIsValid(TSList l)
{
  int i, len;
  void *ele;

  if (!l)
    return false;

  len = queue_len((LLQ *)l);
  for (i = 0; i < len; i++) {
    ele = (void *)dequeue((LLQ *)l);
    if (!ele)
      return false;
    enqueue((LLQ *)l, ele);
  }
  return true;
}

/*--- TSIpAddrList operations -------------------------------------------------*/
tsapi TSIpAddrList
TSIpAddrListCreate(void)
{
  return (void *)create_queue(); /* this queue will be a list of IpAddrEle* */
}

tsapi void
TSIpAddrListDestroy(TSIpAddrList ip_addrl)
{
  TSIpAddrEle *ipaddr_ele;

  if (!ip_addrl) {
    return;
  }

  /* dequeue each element and free it;
     currently, an element can only be an TSIpAddrEle
     or it can be an TSIpAddr ?? */
  while (!queue_is_empty((LLQ *)ip_addrl)) {
    ipaddr_ele = (TSIpAddrEle *)dequeue((LLQ *)ip_addrl);

    if (!ipaddr_ele)
      continue;

    TSIpAddrEleDestroy(ipaddr_ele);
  }

  /* we have removed everything on the list so free list */
  delete_queue((LLQ *)ip_addrl);
  return;
}

tsapi TSMgmtError
TSIpAddrListEnqueue(TSIpAddrList ip_addrl, TSIpAddrEle *ip_addr)
{
  int ret;

  ink_assert(ip_addrl && ip_addr);
  if (!ip_addrl || !ip_addr)
    return TS_ERR_PARAMS;

  ret = enqueue((LLQ *)ip_addrl, ip_addr);
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

/* The the TSIpAddrEle returned is actually removed from the end of list */
tsapi TSIpAddrEle *
TSIpAddrListDequeue(TSIpAddrList ip_addrl)
{
  ink_assert(ip_addrl);
  if (!ip_addrl || queue_is_empty((LLQ *)ip_addrl))
    return NULL;

  return (TSIpAddrEle *)dequeue((LLQ *)ip_addrl);
}

tsapi int
TSIpAddrListLen(TSIpAddrList ip_addrl)
{
  ink_assert(ip_addrl);
  if (!ip_addrl)
    return -1;

  return queue_len((LLQ *)ip_addrl);
}

tsapi bool
TSIpAddrListIsEmpty(TSIpAddrList ip_addrl)
{
  ink_assert(ip_addrl);
  if (!ip_addrl)
    return true;

  return queue_is_empty((LLQ *)ip_addrl);
}

// returns false if any of the IpAddrEle is not an valid IP address by making
// use of ccu_checkIpAddrEle; if return false, the ip's may be reordered
// from the original list
tsapi bool
TSIpAddrListIsValid(TSIpAddrList ip_addrl)
{
  int i, len;
  TSIpAddrEle *ele;

  if (!ip_addrl)
    return false;

  len = queue_len((LLQ *)ip_addrl);
  for (i = 0; i < len; i++) {
    ele = (TSIpAddrEle *)dequeue((LLQ *)ip_addrl);
    if (!ccu_checkIpAddrEle(ele)) {
      enqueue((LLQ *)ip_addrl, ele);
      return false;
    }
    enqueue((LLQ *)ip_addrl, ele);
  }
  return true;
}

/*--- TSPortList operations ----------------------------------------------*/
tsapi TSPortList
TSPortListCreate()
{
  return (void *)create_queue(); /* this queue will be a list of TSPortEle* */
}

tsapi void
TSPortListDestroy(TSPortList portl)
{
  TSPortEle *port_ele;

  if (!portl) {
    return;
  }
  // dequeue each element and free it
  while (!queue_is_empty((LLQ *)portl)) {
    port_ele = (TSPortEle *)dequeue((LLQ *)portl);

    if (!port_ele)
      continue;

    TSPortEleDestroy(port_ele);
  }

  /* we have removed everything on the list so free list */
  delete_queue((LLQ *)portl);
  return;
}

tsapi TSMgmtError
TSPortListEnqueue(TSPortList portl, TSPortEle *port)
{
  int ret;

  ink_assert(portl && port);
  if (!portl || !port)
    return TS_ERR_PARAMS;

  ret = enqueue((LLQ *)portl, port); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi TSPortEle *
TSPortListDequeue(TSPortList portl)
{
  ink_assert(portl);
  if (!portl || queue_is_empty((LLQ *)portl))
    return NULL;

  return (TSPortEle *)dequeue((LLQ *)portl);
}

tsapi int
TSPortListLen(TSPortList portl)
{
  ink_assert(portl);
  if (!portl)
    return -1;

  return queue_len((LLQ *)portl);
}

tsapi bool
TSPortListIsEmpty(TSPortList portl)
{
  ink_assert(portl);
  if (!portl)
    return true;

  return queue_is_empty((LLQ *)portl);
}

// returns false if any of the PortEle's has a port_a <= 0;
// if returns false, then will return the entire port list
// intact, although the ports may not be ordered in the same way
tsapi bool
TSPortListIsValid(TSPortList portl)
{
  int i, len;
  TSPortEle *ele;

  if (!portl)
    return false;

  len = queue_len((LLQ *)portl);
  for (i = 0; i < len; i++) {
    ele = (TSPortEle *)dequeue((LLQ *)portl);
    if (!ccu_checkPortEle(ele)) {
      enqueue((LLQ *)portl, ele);
      return false;
    }
    enqueue((LLQ *)portl, ele);
  }
  return true;
}

/*--- TSDomainList operations -----------------------------------------*/
tsapi TSDomainList
TSDomainListCreate()
{
  return (void *)create_queue(); /* this queue will be a list of char* */
}

tsapi void
TSDomainListDestroy(TSDomainList domainl)
{
  TSDomain *domain;

  if (!domainl) {
    return;
  }

  /* dequeue each element and free it */
  while (!queue_is_empty((LLQ *)domainl)) {
    domain = (TSDomain *)dequeue((LLQ *)domainl);

    if (!domain)
      continue;

    TSDomainDestroy(domain);
  }

  delete_queue((LLQ *)domainl);
}

tsapi TSMgmtError
TSDomainListEnqueue(TSDomainList domainl, TSDomain *domain)
{
  int ret;

  ink_assert(domainl && domain);
  if (!domainl || !domain)
    return TS_ERR_PARAMS;

  ret = enqueue((LLQ *)domainl, domain); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi TSDomain *
TSDomainListDequeue(TSDomainList domainl)
{
  ink_assert(domainl);
  if (!domainl || queue_is_empty((LLQ *)domainl))
    return NULL;

  return (TSDomain *)dequeue((LLQ *)domainl);
}

tsapi bool
TSDomainListIsEmpty(TSDomainList domainl)
{
  ink_assert(domainl);
  if (!domainl)
    return true;

  return queue_is_empty((LLQ *)domainl);
}

tsapi int
TSDomainListLen(TSDomainList domainl)
{
  ink_assert(domainl);
  if (!domainl)
    return -1;

  return queue_len((LLQ *)domainl);
}

// returns false if encounter a NULL hostname and ip
tsapi bool
TSDomainListIsValid(TSDomainList domainl)
{
  int i, len;
  TSDomain *dom;

  if (!domainl)
    return false;

  len = queue_len((LLQ *)domainl);
  for (i = 0; i < len; i++) {
    dom = (TSDomain *)dequeue((LLQ *)domainl);
    if (!dom) {
      return false;
    }
    if (!dom->domain_val) {
      return false;
    }
    enqueue((LLQ *)domainl, dom);
  }
  return true;
}

/*--- TSStringList operations --------------------------------------*/
tsapi TSStringList
TSStringListCreate()
{
  return (void *)create_queue(); /* this queue will be a list of char* */
}

/* usually, must be an empty list before destroying*/
tsapi void
TSStringListDestroy(TSStringList strl)
{
  char *str;

  if (!strl) {
    return;
  }

  /* dequeue each element and free it */
  while (!queue_is_empty((LLQ *)strl)) {
    str = (char *)dequeue((LLQ *)strl);
    ats_free(str);
  }

  delete_queue((LLQ *)strl);
}

tsapi TSMgmtError
TSStringListEnqueue(TSStringList strl, char *str)
{
  int ret;

  ink_assert(strl && str);
  if (!strl || !str)
    return TS_ERR_PARAMS;

  ret = enqueue((LLQ *)strl, str); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi char *
TSStringListDequeue(TSStringList strl)
{
  ink_assert(strl);
  if (!strl || queue_is_empty((LLQ *)strl))
    return NULL;

  return (char *)dequeue((LLQ *)strl);
}

tsapi bool
TSStringListIsEmpty(TSStringList strl)
{
  ink_assert(strl);
  if (!strl)
    return true;

  return queue_is_empty((LLQ *)strl);
}

tsapi int
TSStringListLen(TSStringList strl)
{
  ink_assert(strl);
  if (!strl)
    return -1;

  return queue_len((LLQ *)strl);
}

// returns false if any element is NULL string
tsapi bool
TSStringListIsValid(TSStringList strl)
{
  int i, len;
  char *str;

  if (!strl)
    return false;

  len = queue_len((LLQ *)strl);
  for (i = 0; i < len; i++) {
    str = (char *)dequeue((LLQ *)strl);
    if (!str)
      return false;
    enqueue((LLQ *)strl, str);
  }
  return true;
}

/*--- TSIntList operations --------------------------------------*/
tsapi TSIntList
TSIntListCreate()
{
  return (void *)create_queue(); /* this queue will be a list of int* */
}

/* usually, must be an empty list before destroying*/
tsapi void
TSIntListDestroy(TSIntList intl)
{
  int *iPtr;

  if (!intl)
    return;

  /* dequeue each element and free it */
  while (!queue_is_empty((LLQ *)intl)) {
    iPtr = (int *)dequeue((LLQ *)intl);
    ats_free(iPtr);
  }

  delete_queue((LLQ *)intl);
  return;
}

tsapi TSMgmtError
TSIntListEnqueue(TSIntList intl, int *elem)
{
  int ret;

  ink_assert(intl && elem);
  if (!intl || !elem)
    return TS_ERR_PARAMS;

  ret = enqueue((LLQ *)intl, elem); /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return TS_ERR_FAIL;
  } else {
    return TS_ERR_OKAY;
  }
}

tsapi int *
TSIntListDequeue(TSIntList intl)
{
  ink_assert(intl);
  if (!intl || queue_is_empty((LLQ *)intl))
    return NULL;

  return (int *)dequeue((LLQ *)intl);
}

tsapi bool
TSIntListIsEmpty(TSIntList intl)
{
  ink_assert(intl);
  if (!intl)
    return true;

  return queue_is_empty((LLQ *)intl);
}

tsapi int
TSIntListLen(TSIntList intl)
{
  ink_assert(intl);
  if (!intl)
    return -1;

  return queue_len((LLQ *)intl);
}

tsapi bool
TSIntListIsValid(TSIntList intl, int min, int max)
{
  if (!intl)
    return false;

  for (unsigned long i = 0; i < queue_len((LLQ *)intl); i++) {
    int *item = (int *)dequeue((LLQ *)intl);
    if (*item < min) {
      return false;
    }
    if (*item > max) {
      return false;
    }
    enqueue((LLQ *)intl, item);
  }
  return true;
}

// helper fn that sets default values for the info passed in
void
init_pdss_format(TSPdSsFormat &info)
{
  info.pd_type              = TS_PD_UNDEFINED;
  info.pd_val               = NULL;
  info.sec_spec.active      = 0;
  info.sec_spec.time.hour_a = 0;
  info.sec_spec.time.min_a  = 0;
  info.sec_spec.time.hour_b = 0;
  info.sec_spec.time.min_b  = 0;
  info.sec_spec.src_ip      = TS_INVALID_IP_ADDR;
  info.sec_spec.prefix      = NULL;
  info.sec_spec.suffix      = NULL;
  info.sec_spec.port        = TS_INVALID_PORT;
  info.sec_spec.method      = TS_METHOD_UNDEFINED;
  info.sec_spec.scheme      = TS_SCHEME_UNDEFINED;
}

/*--- allocate/deallocate operations --------------------------------------*/
tsapi TSMgmtEvent *
TSEventCreate(void)
{
  TSMgmtEvent *event = (TSMgmtEvent *)ats_malloc(sizeof(TSMgmtEvent));

  event->id          = -1;
  event->name        = NULL;
  event->description = NULL;
  event->priority    = TS_EVENT_PRIORITY_UNDEFINED;

  return event;
}

tsapi void
TSEventDestroy(TSMgmtEvent *event)
{
  if (event) {
    ats_free(event->name);
    ats_free(event->description);
    ats_free(event);
  }
  return;
}

tsapi TSRecordEle *
TSRecordEleCreate(void)
{
  TSRecordEle *ele = (TSRecordEle *)ats_malloc(sizeof(TSRecordEle));

  ele->rec_name = NULL;
  ele->rec_type = TS_REC_UNDEFINED;

  return ele;
}

tsapi void
TSRecordEleDestroy(TSRecordEle *ele)
{
  if (ele) {
    ats_free(ele->rec_name);
    if (ele->rec_type == TS_REC_STRING && ele->valueT.string_val)
      ats_free(ele->valueT.string_val);
    ats_free(ele);
  }
  return;
}

tsapi TSIpAddrEle *
TSIpAddrEleCreate(void)
{
  TSIpAddrEle *ele = (TSIpAddrEle *)ats_malloc(sizeof(TSIpAddrEle));

  /* set default values */
  ele->type   = TS_IP_UNDEFINED;
  ele->ip_a   = TS_INVALID_IP_ADDR;
  ele->cidr_a = TS_INVALID_IP_CIDR;
  ele->port_a = TS_INVALID_PORT;
  ele->ip_b   = TS_INVALID_IP_ADDR;
  ele->cidr_b = TS_INVALID_IP_CIDR;
  ele->port_b = TS_INVALID_PORT;
  return ele;
}

tsapi void
TSIpAddrEleDestroy(TSIpAddrEle *ele)
{
  if (ele) {
    ats_free(ele->ip_a);
    ats_free(ele->ip_b);
    ats_free(ele);
  }

  return;
}

tsapi TSPortEle *
TSPortEleCreate(void)
{
  TSPortEle *ele = (TSPortEle *)ats_malloc(sizeof(TSPortEle));

  ele->port_a = TS_INVALID_PORT;
  ele->port_b = TS_INVALID_PORT;

  return ele;
}

tsapi void
TSPortEleDestroy(TSPortEle *ele)
{
  ats_free(ele);
  return;
}

tsapi TSDomain *
TSDomainCreate()
{
  TSDomain *ele = (TSDomain *)ats_malloc(sizeof(TSDomain));

  ele->domain_val = NULL;
  ele->port       = TS_INVALID_PORT;

  return ele;
}

tsapi void
TSDomainDestroy(TSDomain *ele)
{
  if (ele) {
    ats_free(ele->domain_val);
    ats_free(ele);
  }
}

tsapi TSSspec *
TSSspecCreate(void)
{
  TSSspec *sec_spec = (TSSspec *)ats_malloc(sizeof(TSSspec));

  /* set defaults */
  sec_spec->active        = 0;
  (sec_spec->time).hour_a = 0;
  (sec_spec->time).min_a  = 0;
  (sec_spec->time).hour_b = 0;
  (sec_spec->time).min_b  = 0;
  sec_spec->src_ip        = TS_INVALID_IP_ADDR;
  sec_spec->prefix        = NULL;
  sec_spec->suffix        = NULL;
  sec_spec->port          = NULL;
  sec_spec->method        = TS_METHOD_UNDEFINED;
  sec_spec->scheme        = TS_SCHEME_UNDEFINED;
  return sec_spec;
}

tsapi void
TSSspecDestroy(TSSspec *ele)
{
  if (ele) {
    ats_free(ele->prefix);
    ats_free(ele->suffix);
    if (ele->port)
      TSPortEleDestroy(ele->port);
    ats_free(ele);
  }
  return;
}

tsapi TSPdSsFormat *
TSPdSsFormatCreate(void)
{
  TSPdSsFormat *ele = (TSPdSsFormat *)ats_malloc(sizeof(TSPdSsFormat));

  /* should set default values here */
  ele->pd_type = TS_PD_UNDEFINED;
  ele->pd_val  = NULL;

  ele->sec_spec.active        = 0;
  (ele->sec_spec.time).hour_a = -1;
  (ele->sec_spec.time).min_a  = -1;
  (ele->sec_spec.time).hour_b = -1;
  (ele->sec_spec.time).min_b  = -1;
  ele->sec_spec.src_ip        = TS_INVALID_IP_ADDR;
  ele->sec_spec.prefix        = NULL;
  ele->sec_spec.suffix        = NULL;
  ele->sec_spec.port          = NULL;
  ele->sec_spec.method        = TS_METHOD_UNDEFINED;
  ele->sec_spec.scheme        = TS_SCHEME_UNDEFINED;

  return ele;
}

tsapi void
TSPdSsFormatDestroy(TSPdSsFormat &ele)
{
  ats_free(ele.pd_val);
  ats_free(ele.sec_spec.src_ip);
  ats_free(ele.sec_spec.prefix);
  ats_free(ele.sec_spec.suffix);
  if (ele.sec_spec.port)
    TSPortEleDestroy(ele.sec_spec.port);
}

/*-------------------------------------------------------------
 * CacheObj
 *-------------------------------------------------------------*/
tsapi TSCacheEle *
TSCacheEleCreate(TSRuleTypeT type)
{
  TSCacheEle *ele;

  if (type != TS_CACHE_NEVER && type != TS_CACHE_IGNORE_NO_CACHE && type != TS_CACHE_CLUSTER_CACHE_LOCAL &&
      type != TS_CACHE_IGNORE_CLIENT_NO_CACHE && type != TS_CACHE_IGNORE_SERVER_NO_CACHE && type != TS_CACHE_PIN_IN_CACHE &&
      type != TS_CACHE_REVALIDATE && type != TS_CACHE_TTL_IN_CACHE && type != TS_CACHE_AUTH_CONTENT && type != TS_TYPE_UNDEFINED)
    return NULL; // invalid type

  ele = (TSCacheEle *)ats_malloc(sizeof(TSCacheEle));

  /* set defaults */
  ele->cfg_ele.type  = type;
  ele->cfg_ele.error = TS_ERR_OKAY;
  init_pdss_format(ele->cache_info);
  ele->time_period.d = 0;
  ele->time_period.h = 0;
  ele->time_period.m = 0;
  ele->time_period.s = 0;

  return ele;
}

tsapi void
TSCacheEleDestroy(TSCacheEle *ele)
{
  if (ele) {
    TSPdSsFormatDestroy(ele->cache_info);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * CongestionObj
 *-------------------------------------------------------------*/
// FIXME: for now use defaults specified in feature spec; the
// defaults though are configurable as records, so should use
// records values instead
tsapi TSCongestionEle *
TSCongestionEleCreate()
{
  TSCongestionEle *ele = (TSCongestionEle *)ats_malloc(sizeof(TSCongestionEle));

  /* set defaults */
  ele->cfg_ele.type  = TS_CONGESTION;
  ele->cfg_ele.error = TS_ERR_OKAY;
  // init_pdss_format(ele->congestion_info);
  ele->pd_type                 = TS_PD_UNDEFINED;
  ele->pd_val                  = NULL;
  ele->prefix                  = NULL;
  ele->port                    = TS_INVALID_PORT;
  ele->scheme                  = TS_HTTP_CONGEST_PER_IP;
  ele->max_connection_failures = 5;
  ele->fail_window             = 120;
  ele->proxy_retry_interval    = 10;
  ele->client_wait_interval    = 300;
  ele->wait_interval_alpha     = 30;
  ele->live_os_conn_timeout    = 60;
  ele->live_os_conn_retries    = 2;
  ele->dead_os_conn_timeout    = 15;
  ele->dead_os_conn_retries    = 1;
  ele->max_connection          = -1;
  ele->error_page_uri          = ats_strdup("congestion#retryAfter");

  return ele;
}

tsapi void
TSCongestionEleDestroy(TSCongestionEle *ele)
{
  if (ele) {
    ats_free(ele->pd_val);
    ats_free(ele->prefix);
    ats_free(ele->error_page_uri);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * HostingObj
 *-------------------------------------------------------------*/
tsapi TSHostingEle *
TSHostingEleCreate()
{
  TSHostingEle *ele = (TSHostingEle *)ats_malloc(sizeof(TSHostingEle));

  ele->cfg_ele.type  = TS_HOSTING;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->pd_type       = TS_PD_UNDEFINED;
  ele->pd_val        = NULL;
  ele->volumes       = TS_INVALID_LIST;

  return ele;
}

tsapi void
TSHostingEleDestroy(TSHostingEle *ele)
{
  if (ele) {
    ats_free(ele->pd_val);
    if (ele->volumes)
      TSIntListDestroy(ele->volumes);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * IcpObject
 *-------------------------------------------------------------*/
tsapi TSIcpEle *
TSIcpEleCreate()
{
  TSIcpEle *ele = (TSIcpEle *)ats_malloc(sizeof(TSIcpEle));

  /* set defaults */
  ele->cfg_ele.type      = TS_ICP;
  ele->cfg_ele.error     = TS_ERR_OKAY;
  ele->peer_hostname     = NULL;
  ele->peer_host_ip_addr = TS_INVALID_IP_ADDR;
  ele->peer_type         = TS_ICP_UNDEFINED;
  ele->peer_proxy_port   = TS_INVALID_PORT;
  ele->peer_icp_port     = TS_INVALID_PORT;
  ele->is_multicast      = false;
  ele->mc_ip_addr        = TS_INVALID_IP_ADDR;
  ele->mc_ttl            = TS_MC_TTL_SINGLE_SUBNET; // default value

  return ele;
}

tsapi void
TSIcpEleDestroy(TSIcpEle *ele)
{
  if (ele) {
    ats_free(ele->peer_hostname);
    ats_free(ele->peer_host_ip_addr);
    ats_free(ele->mc_ip_addr);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSIpAllowEle
 *-------------------------------------------------------------*/
tsapi TSIpAllowEle *
TSIpAllowEleCreate()
{
  TSIpAllowEle *ele = (TSIpAllowEle *)ats_malloc(sizeof(TSIpAllowEle));

  ele->cfg_ele.type  = TS_IP_ALLOW;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->src_ip_addr   = TS_INVALID_IP_ADDR;
  ele->action        = TS_IP_ALLOW_UNDEFINED;

  return ele;
}

tsapi void
TSIpAllowEleDestroy(TSIpAllowEle *ele)
{
  if (ele) {
    if (ele->src_ip_addr)
      TSIpAddrEleDestroy(ele->src_ip_addr);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSLogFilterEle
 *-------------------------------------------------------------*/
tsapi TSLogFilterEle *
TSLogFilterEleCreate()
{
  TSLogFilterEle *ele = (TSLogFilterEle *)ats_malloc(sizeof(TSLogFilterEle));

  ele->cfg_ele.type  = TS_LOG_FILTER;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->action        = TS_LOG_FILT_UNDEFINED;
  ele->filter_name   = NULL;
  ele->log_field     = NULL;
  ele->compare_op    = TS_LOG_COND_UNDEFINED;
  ele->compare_str   = NULL;
  ele->compare_int   = -1;
  return ele;
}

tsapi void
TSLogFilterEleDestroy(TSLogFilterEle *ele)
{
  if (ele) {
    ats_free(ele->filter_name);
    ats_free(ele->log_field);
    ats_free(ele->compare_str);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSLogFormatEle
 *-------------------------------------------------------------*/
tsapi TSLogFormatEle *
TSLogFormatEleCreate()
{
  TSLogFormatEle *ele = (TSLogFormatEle *)ats_malloc(sizeof(TSLogFormatEle));

  ele->cfg_ele.type            = TS_LOG_FORMAT;
  ele->cfg_ele.error           = TS_ERR_OKAY;
  ele->name                    = NULL;
  ele->format                  = NULL;
  ele->aggregate_interval_secs = 0;

  return ele;
}

tsapi void
TSLogFormatEleDestroy(TSLogFormatEle *ele)
{
  if (ele) {
    ats_free(ele->name);
    ats_free(ele->format);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSLogObjectEle
 *-------------------------------------------------------------*/
tsapi TSLogObjectEle *
TSLogObjectEleCreate()
{
  TSLogObjectEle *ele = (TSLogObjectEle *)ats_malloc(sizeof(TSLogObjectEle));

  ele->cfg_ele.type    = TS_LOG_OBJECT;
  ele->cfg_ele.error   = TS_ERR_OKAY;
  ele->format_name     = NULL;
  ele->file_name       = NULL;
  ele->log_mode        = TS_LOG_MODE_UNDEFINED;
  ele->collation_hosts = TS_INVALID_LIST;
  ele->filters         = TS_INVALID_LIST;
  ele->protocols       = TS_INVALID_LIST;
  ele->server_hosts    = TS_INVALID_LIST;

  return ele;
}

tsapi void
TSLogObjectEleDestroy(TSLogObjectEle *ele)
{
  if (ele) {
    ats_free(ele->format_name);
    ats_free(ele->file_name);
    if (ele->collation_hosts)
      TSDomainListDestroy(ele->collation_hosts);
    if (ele->filters)
      TSStringListDestroy(ele->filters);
    if (ele->protocols)
      TSStringListDestroy(ele->protocols);
    if (ele->server_hosts)
      TSStringListDestroy(ele->server_hosts);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSParentProxyEleCreate
 *-------------------------------------------------------------*/
tsapi TSParentProxyEle *
TSParentProxyEleCreate(TSRuleTypeT type)
{
  TSParentProxyEle *ele;

  if (type != TS_PP_PARENT && type != TS_PP_GO_DIRECT && type != TS_TYPE_UNDEFINED)
    return NULL;

  ele = (TSParentProxyEle *)ats_malloc(sizeof(TSParentProxyEle));

  ele->cfg_ele.type  = type;
  ele->cfg_ele.error = TS_ERR_OKAY;
  init_pdss_format(ele->parent_info);
  ele->rr         = TS_RR_NONE;
  ele->proxy_list = TS_INVALID_LIST;
  ele->direct     = false;

  return ele;
}

tsapi void
TSParentProxyEleDestroy(TSParentProxyEle *ele)
{
  if (ele) {
    TSPdSsFormatDestroy(ele->parent_info);
    if (ele->proxy_list)
      TSDomainListDestroy(ele->proxy_list);
    ats_free(ele);
  }

  return;
}

/*-------------------------------------------------------------
 * TSVolumeEle
 *-------------------------------------------------------------*/
tsapi TSVolumeEle *
TSVolumeEleCreate()
{
  TSVolumeEle *ele = (TSVolumeEle *)ats_malloc(sizeof(TSVolumeEle));

  ele->cfg_ele.type  = TS_VOLUME;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->volume_num    = 0;
  ele->scheme        = TS_VOLUME_UNDEFINED;
  ele->volume_size   = 0;
  ele->size_format   = TS_SIZE_FMT_UNDEFINED;

  return ele;
}

tsapi void
TSVolumeEleDestroy(TSVolumeEle *ele)
{
  ats_free(ele);
  return;
}

/*-------------------------------------------------------------
 * TSPluginEle
 *-------------------------------------------------------------*/
tsapi TSPluginEle *
TSPluginEleCreate()
{
  TSPluginEle *ele = (TSPluginEle *)ats_malloc(sizeof(TSPluginEle));

  ele->cfg_ele.type  = TS_PLUGIN;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->name          = NULL;
  ele->args          = TS_INVALID_LIST;

  return ele;
}

tsapi void
TSPluginEleDestroy(TSPluginEle *ele)
{
  if (ele) {
    ats_free(ele->name);
    if (ele->args)
      TSStringListDestroy(ele->args);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSRemapEle
 *-------------------------------------------------------------*/
TSRemapEle *
TSRemapEleCreate(TSRuleTypeT type)
{
  TSRemapEle *ele;

  if (type != TS_REMAP_MAP && type != TS_REMAP_REVERSE_MAP && type != TS_REMAP_REDIRECT && type != TS_REMAP_REDIRECT_TEMP &&
      type != TS_TYPE_UNDEFINED)
    return NULL;

  ele                   = (TSRemapEle *)ats_malloc(sizeof(TSRemapEle));
  ele->cfg_ele.type     = type;
  ele->cfg_ele.error    = TS_ERR_OKAY;
  ele->map              = true;
  ele->from_scheme      = TS_SCHEME_UNDEFINED;
  ele->from_host        = NULL;
  ele->from_port        = TS_INVALID_PORT;
  ele->from_path_prefix = NULL;
  ele->to_scheme        = TS_SCHEME_UNDEFINED;
  ele->to_host          = NULL;
  ele->to_port          = TS_INVALID_PORT;
  ele->to_path_prefix   = NULL;

  return ele;
}

void
TSRemapEleDestroy(TSRemapEle *ele)
{
  if (ele) {
    ats_free(ele->from_host);
    ats_free(ele->from_path_prefix);
    ats_free(ele->to_host);
    ats_free(ele->to_path_prefix);
    ats_free(ele);
  }
}

/*-------------------------------------------------------------
 * TSSocksEle
 *-------------------------------------------------------------*/
TSSocksEle *
TSSocksEleCreate(TSRuleTypeT type)
{
  TSSocksEle *ele = (TSSocksEle *)ats_malloc(sizeof(TSSocksEle));

  ele->cfg_ele.type  = type;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->ip_addrs      = TS_INVALID_LIST;
  ele->dest_ip_addr  = TS_INVALID_IP_ADDR;
  ele->socks_servers = TS_INVALID_LIST;
  ele->rr            = TS_RR_NONE;
  ele->username      = NULL;
  ele->password      = NULL;

  return ele;
}

void
TSSocksEleDestroy(TSSocksEle *ele)
{
  if (ele) {
    if (ele->ip_addrs)
      TSIpAddrListDestroy(ele->ip_addrs);
    if (ele->dest_ip_addr)
      TSIpAddrEleDestroy(ele->dest_ip_addr);
    if (ele->socks_servers)
      TSDomainListDestroy(ele->socks_servers);
    ats_free(ele->username);
    ats_free(ele->password);
    ats_free(ele);
  }
}

/*-------------------------------------------------------------
 * TSSplitDnsEle
 *-------------------------------------------------------------*/
TSSplitDnsEle *
TSSplitDnsEleCreate()
{
  TSSplitDnsEle *ele = (TSSplitDnsEle *)ats_malloc(sizeof(TSSplitDnsEle));

  ele->cfg_ele.type      = TS_SPLIT_DNS;
  ele->cfg_ele.error     = TS_ERR_OKAY;
  ele->pd_type           = TS_PD_UNDEFINED;
  ele->pd_val            = NULL;
  ele->dns_servers_addrs = TS_INVALID_LIST;
  ele->def_domain        = NULL;
  ele->search_list       = TS_INVALID_LIST;

  return ele;
}

void
TSSplitDnsEleDestroy(TSSplitDnsEle *ele)
{
  if (ele) {
    ats_free(ele->pd_val);
    if (ele->dns_servers_addrs)
      TSDomainListDestroy(ele->dns_servers_addrs);
    ats_free(ele->def_domain);
    if (ele->search_list)
      TSDomainListDestroy(ele->search_list);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSStorageEle
 *-------------------------------------------------------------*/
TSStorageEle *
TSStorageEleCreate()
{
  TSStorageEle *ele = (TSStorageEle *)ats_malloc(sizeof(TSStorageEle));

  ele->cfg_ele.type  = TS_STORAGE;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->pathname      = NULL;
  ele->size          = -1;

  return ele;
}

void
TSStorageEleDestroy(TSStorageEle *ele)
{
  if (ele) {
    ats_free(ele->pathname);
    ats_free(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * TSVirtIpAddrEle
 *-------------------------------------------------------------*/
TSVirtIpAddrEle *
TSVirtIpAddrEleCreate()
{
  TSVirtIpAddrEle *ele = (TSVirtIpAddrEle *)ats_malloc(sizeof(TSVirtIpAddrEle));

  ele->cfg_ele.type  = TS_VADDRS;
  ele->cfg_ele.error = TS_ERR_OKAY;
  ele->intr          = NULL;
  ele->sub_intr      = -1;
  ele->ip_addr       = TS_INVALID_IP_ADDR;

  return ele;
}

void
TSVirtIpAddrEleDestroy(TSVirtIpAddrEle *ele)
{
  if (ele) {
    ats_free(ele->intr);
    ats_free(ele->ip_addr);
    ats_free(ele);
  }
}

/***************************************************************************
 * API Core
 ***************************************************************************/

/*--- statistics operations ----------------------------------------------- */
tsapi TSMgmtError
TSStatsReset(bool cluster, const char *name)
{
  return StatsReset(cluster, name);
}

/*--- variable operations ------------------------------------------------- */
/* Call the CfgFileIO variable operations */

tsapi TSMgmtError
TSRecordGet(const char *rec_name, TSRecordEle *rec_val)
{
  return MgmtRecordGet(rec_name, rec_val);
}

TSMgmtError
TSRecordGetInt(const char *rec_name, TSInt *int_val)
{
  TSMgmtError ret = TS_ERR_OKAY;

  TSRecordEle *ele = TSRecordEleCreate();
  ret              = MgmtRecordGet(rec_name, ele);
  if (ret != TS_ERR_OKAY)
    goto END;

  *int_val = ele->valueT.int_val;

END:
  TSRecordEleDestroy(ele);
  return ret;
}

TSMgmtError
TSRecordGetCounter(const char *rec_name, TSCounter *counter_val)
{
  TSMgmtError ret;

  TSRecordEle *ele = TSRecordEleCreate();
  ret              = MgmtRecordGet(rec_name, ele);
  if (ret != TS_ERR_OKAY)
    goto END;
  *counter_val = ele->valueT.counter_val;

END:
  TSRecordEleDestroy(ele);
  return ret;
}

TSMgmtError
TSRecordGetFloat(const char *rec_name, TSFloat *float_val)
{
  TSMgmtError ret;

  TSRecordEle *ele = TSRecordEleCreate();
  ret              = MgmtRecordGet(rec_name, ele);
  if (ret != TS_ERR_OKAY)
    goto END;
  *float_val = ele->valueT.float_val;

END:
  TSRecordEleDestroy(ele);
  return ret;
}

TSMgmtError
TSRecordGetString(const char *rec_name, TSString *string_val)
{
  TSMgmtError ret;

  TSRecordEle *ele = TSRecordEleCreate();
  ret              = MgmtRecordGet(rec_name, ele);
  if (ret != TS_ERR_OKAY)
    goto END;

  *string_val = ats_strdup(ele->valueT.string_val);

END:
  TSRecordEleDestroy(ele);
  return ret;
}

/*-------------------------------------------------------------------------
 * TSRecordGetMlt
 *-------------------------------------------------------------------------
 * Purpose: Retrieves list of record values specified in the rec_names list
 * Input: rec_names - list of record names to retrieve
 *        rec_vals  - queue of TSRecordEle* that correspons to rec_names
 * Output: If at any point, while retrieving one of the records there's a
 *         a failure then the entire process is aborted, all the allocated
 *         TSRecordEle's are deallocated and TS_ERR_FAIL is returned.
 * Note: rec_names is not freed; if function is successful, the rec_names
 *       list is unchanged!
 *
 * IS THIS FUNCTION AN ATOMIC TRANSACTION? Technically, all the variables
 * requested should refer to the same config file. But a lock is only
 * put on each variable it is looked up. Need to be able to lock
 * a file while retrieving all the requested records!
 */

tsapi TSMgmtError
TSRecordGetMlt(TSStringList rec_names, TSList rec_vals)
{
  TSRecordEle *ele;
  char *rec_name;
  int num_recs, i, j;
  TSMgmtError ret;

  if (!rec_names || !rec_vals)
    return TS_ERR_PARAMS;

  num_recs = queue_len((LLQ *)rec_names);
  for (i = 0; i < num_recs; i++) {
    rec_name = (char *)dequeue((LLQ *)rec_names); // remove name from list
    if (!rec_name)
      return TS_ERR_PARAMS; // NULL is invalid record name

    ele = TSRecordEleCreate();

    ret = MgmtRecordGet(rec_name, ele);
    enqueue((LLQ *)rec_names, rec_name); // return name to list

    if (ret != TS_ERR_OKAY) { // RecordGet failed
      // need to free all the ele's allocated by MgmtRecordGet so far
      TSRecordEleDestroy(ele);
      for (j = 0; j < i; j++) {
        ele = (TSRecordEle *)dequeue((LLQ *)rec_vals);
        if (ele)
          TSRecordEleDestroy(ele);
      }
      return ret;
    }
    enqueue((LLQ *)rec_vals, ele); // all is good; add ele to end of list
  }

  return TS_ERR_OKAY;
}

tsapi TSMgmtError
TSRecordGetMatchMlt(const char *regex, TSList rec_vals)
{
  if (!regex || !rec_vals) {
    return TS_ERR_PARAMS;
  }

  return MgmtRecordGetMatching(regex, rec_vals);
}

tsapi TSMgmtError
TSRecordSet(const char *rec_name, const char *val, TSActionNeedT *action_need)
{
  return MgmtRecordSet(rec_name, val, action_need);
}

tsapi TSMgmtError
TSRecordSetInt(const char *rec_name, TSInt int_val, TSActionNeedT *action_need)
{
  return MgmtRecordSetInt(rec_name, int_val, action_need);
}

tsapi TSMgmtError
TSRecordSetCounter(const char *rec_name, TSCounter counter_val, TSActionNeedT *action_need)
{
  return MgmtRecordSetCounter(rec_name, counter_val, action_need);
}

tsapi TSMgmtError
TSRecordSetFloat(const char *rec_name, TSFloat float_val, TSActionNeedT *action_need)
{
  return MgmtRecordSetFloat(rec_name, float_val, action_need);
}

tsapi TSMgmtError
TSRecordSetString(const char *rec_name, const char *str_val, TSActionNeedT *action_need)
{
  return MgmtRecordSetString(rec_name, str_val, action_need);
}

/*-------------------------------------------------------------------------
 * TSRecordSetMlt
 *-------------------------------------------------------------------------
 * Basically iterates through each RecordEle in rec_list and calls the
 * appropriate "MgmtRecordSetxx" function for that record
 * Input: rec_list - queue of TSRecordEle*; each TSRecordEle* must have
 *        a valid record name (remains unchanged on return)
 * Output: if there is an error during the setting of one of the variables then
 *         will continue to try to set the other variables. Error response will
 *         indicate though that not all set operations were successful.
 *         TS_ERR_OKAY is returned if all the records are set successfully
 * Note: Determining the action needed is more complex b/c need to keep
 * track of which record change is the most drastic out of the group of
 * records; action_need will be set to the most severe action needed of
 * all the "Set" calls
 */
tsapi TSMgmtError
TSRecordSetMlt(TSList rec_list, TSActionNeedT *action_need)
{
  int num_recs, ret, i;
  TSRecordEle *ele;
  TSMgmtError status           = TS_ERR_OKAY;
  TSActionNeedT top_action_req = TS_ACTION_UNDEFINED;

  if (!rec_list || !action_need)
    return TS_ERR_PARAMS;

  num_recs = queue_len((LLQ *)rec_list);

  for (i = 0; i < num_recs; i++) {
    ele = (TSRecordEle *)dequeue((LLQ *)rec_list);
    if (ele) {
      switch (ele->rec_type) {
      case TS_REC_INT:
        ret = MgmtRecordSetInt(ele->rec_name, ele->valueT.int_val, action_need);
        break;
      case TS_REC_COUNTER:
        ret = MgmtRecordSetCounter(ele->rec_name, ele->valueT.counter_val, action_need);
        break;
      case TS_REC_FLOAT:
        ret = MgmtRecordSetFloat(ele->rec_name, ele->valueT.float_val, action_need);
        break;
      case TS_REC_STRING:
        ret = MgmtRecordSetString(ele->rec_name, ele->valueT.string_val, action_need);
        break;
      default:
        ret = TS_ERR_FAIL;
        break;
      }; /* end of switch (ele->rec_type) */
      if (ret != TS_ERR_OKAY)
        status = TS_ERR_FAIL;

      // keep track of most severe action; reset if needed
      // the TSACtionNeedT should be listed such that most severe actions have
      // a lower number (so most severe action == 0)
      if (*action_need < top_action_req) // a more severe action
        top_action_req = *action_need;
    }
    enqueue((LLQ *)rec_list, ele);
  }

  // set the action_need to be the most sever action needed of all the "set" calls
  *action_need = top_action_req;

  return status;
}

/*--- api initialization and shutdown -------------------------------------*/
tsapi TSMgmtError
TSInit(const char *socket_path, TSInitOptionT options)
{
  return Init(socket_path, options);
}

tsapi TSMgmtError
TSTerminate()
{
  return Terminate();
}

/*--- plugin initialization -----------------------------------------------*/
inkexp extern void
TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */ [])
{
}

/*--- network operations --------------------------------------------------*/
tsapi TSMgmtError
TSConnect(TSIpAddr /* ip_addr ATS_UNUSED */, int /* port ATS_UNUSED */)
{
  return TS_ERR_OKAY;
}
tsapi TSMgmtError
TSDisconnectCbRegister(TSDisconnectFunc * /* func ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  return TS_ERR_OKAY;
}
tsapi TSMgmtError
TSDisconnectRetrySet(int /* retries ATS_UNUSED */, int /* retry_sleep_msec ATS_UNUSED */)
{
  return TS_ERR_OKAY;
}
tsapi TSMgmtError
TSDisconnect()
{
  return TS_ERR_OKAY;
}

/*--- control operations --------------------------------------------------*/
/* NOTE: these operations are wrappers that make direct calls to the CoreAPI */

/* TSProxyStateGet: get the proxy state (on/off)
 * Input:  <none>
 * Output: proxy state (on/off)
 */
tsapi TSProxyStateT
TSProxyStateGet()
{
  return ProxyStateGet();
}

/* TSProxyStateSet: set the proxy state (on/off)
 * Input:  proxy_state - set to on/off
 *         clear - start TS with cache clearing option,
 *                 when stopping TS should always be TS_CACHE_CLEAR_OFF
 * Output: TSMgmtError
 */
tsapi TSMgmtError
TSProxyStateSet(TSProxyStateT proxy_state, TSCacheClearT clear)
{
  return ProxyStateSet(proxy_state, clear);
}

tsapi TSMgmtError
TSProxyBacktraceGet(unsigned options, TSString *trace)
{
  if (options != 0) {
    return TS_ERR_PARAMS;
  }

  if (trace == NULL) {
    return TS_ERR_PARAMS;
  }

  return ServerBacktrace(options, trace);
}

/* TSReconfigure: tell traffic_server to re-read its configuration files
 * Input:  <none>
 * Output: TSMgmtError
 */
tsapi TSMgmtError
TSReconfigure()
{
  return Reconfigure();
}

/* TSRestart: restarts Traffic Server
 * Input:  options - bitmask of TSRestartOptionT
 * Output: TSMgmtError
 */
tsapi TSMgmtError
TSRestart(unsigned options)
{
  return Restart(options);
}

/* TSActionDo: based on TSActionNeedT, will take appropriate action
 * Input: action - action that needs to be taken
 * Output: TSMgmtError
 */
tsapi TSMgmtError
TSActionDo(TSActionNeedT action)
{
  TSMgmtError ret;

  switch (action) {
  case TS_ACTION_RESTART:
    ret = Restart(true); // cluster wide by default?
    break;
  case TS_ACTION_RECONFIGURE:
    ret = Reconfigure();
    break;
  case TS_ACTION_DYNAMIC:
    /* do nothing - change takes effect immediately */
    return TS_ERR_OKAY;
  case TS_ACTION_SHUTDOWN:
  default:
    return TS_ERR_FAIL;
  }

  return ret;
}

/* TSBouncer: restarts the traffic_server process(es)
 * Input:  options - bitmask of TSRestartOptionT
 * Output: TSMgmtError
 */
tsapi TSMgmtError
TSBounce(unsigned options)
{
  return Bounce(options);
}

tsapi TSMgmtError
TSStorageDeviceCmdOffline(char const *dev)
{
  return StorageDeviceCmdOffline(dev);
}

/*--- diags output operations ---------------------------------------------*/
tsapi void
TSDiags(TSDiagsT mode, const char *fmt, ...)
{
  // need to find way to pass arguments to the function
  va_list ap;

  va_start(ap, fmt); // initialize the argument pointer ap
  DiagnosticMessage(mode, fmt, ap);
  va_end(ap);

  return;
}

/* NOTE: user must deallocate the memory for the string returned */
char *
TSGetErrorMessage(TSMgmtError err_id)
{
  char msg[1024]; // need to define a MAX_ERR_MSG_SIZE???
  char *err_msg = NULL;

  switch (err_id) {
  case TS_ERR_OKAY:
    snprintf(msg, sizeof(msg), "[%d] Everything's looking good.", err_id);
    break;
  case TS_ERR_READ_FILE: /* Error occur in reading file */
    snprintf(msg, sizeof(msg), "[%d] Unable to find/open file for reading.", err_id);
    break;
  case TS_ERR_WRITE_FILE: /* Error occur in writing file */
    snprintf(msg, sizeof(msg), "[%d] Unable to find/open file for writing.", err_id);
    break;
  case TS_ERR_PARSE_CONFIG_RULE: /* Error in parsing configuration file */
    snprintf(msg, sizeof(msg), "[%d] Error parsing configuration file.", err_id);
    break;
  case TS_ERR_INVALID_CONFIG_RULE: /* Invalid Configuration Rule */
    snprintf(msg, sizeof(msg), "[%d] Invalid configuration rule reached.", err_id);
    break;
  case TS_ERR_NET_ESTABLISH:
    snprintf(msg, sizeof(msg), "[%d] Error establishing socket connection.", err_id);
    break;
  case TS_ERR_NET_READ: /* Error reading from socket */
    snprintf(msg, sizeof(msg), "[%d] Error reading from socket.", err_id);
    break;
  case TS_ERR_NET_WRITE: /* Error writing to socket */
    snprintf(msg, sizeof(msg), "[%d] Error writing to socket.", err_id);
    break;
  case TS_ERR_NET_EOF: /* Hit socket EOF */
    snprintf(msg, sizeof(msg), "[%d] Reached socket EOF.", err_id);
    break;
  case TS_ERR_NET_TIMEOUT: /* Timed out waiting for socket read */
    snprintf(msg, sizeof(msg), "[%d] Timed out waiting for socket read.", err_id);
    break;
  case TS_ERR_SYS_CALL: /* Error in sys/utility call, eg.malloc */
    snprintf(msg, sizeof(msg), "[%d] Error in basic system/utility call.", err_id);
    break;
  case TS_ERR_PARAMS: /* Invalid parameters for a fn */
    snprintf(msg, sizeof(msg), "[%d] Invalid parameters passed into function call.", err_id);
    break;
  case TS_ERR_FAIL:
    snprintf(msg, sizeof(msg), "[%d] Generic Fail message (ie. CoreAPI call).", err_id);
    break;
  case TS_ERR_NOT_SUPPORTED:
    snprintf(msg, sizeof(msg), "[%d] Operation not supported on this platform.", err_id);
    break;
  case TS_ERR_PERMISSION_DENIED:
    snprintf(msg, sizeof(msg), "[%d] Operation not permitted.", err_id);
    break;

  default:
    snprintf(msg, sizeof(msg), "[%d] Invalid error type.", err_id);
    break;
  }

  err_msg = ats_strdup(msg);
  return err_msg;
}

/*--- password operations -------------------------------------------------*/
tsapi TSMgmtError
TSEncryptPassword(char *passwd, char **e_passwd)
{
  INK_DIGEST_CTX md5_context;
  char passwd_md5[16];
  char *passwd_md5_str;
  int passwd_md5_str_len = 32;

  ink_assert(passwd);
  ink_assert(TS_ENCRYPT_PASSWD_LEN <= passwd_md5_str_len);

  const size_t md5StringSize = (passwd_md5_str_len + 1) * sizeof(char);
  passwd_md5_str             = (char *)ats_malloc(md5StringSize);

  ink_code_incr_md5_init(&md5_context);
  ink_code_incr_md5_update(&md5_context, passwd, strlen(passwd));
  ink_code_incr_md5_final(passwd_md5, &md5_context);
  ink_code_md5_stringify(passwd_md5_str, md5StringSize, passwd_md5);

  // use only a subset of the MD5 string
  passwd_md5_str[TS_ENCRYPT_PASSWD_LEN] = '\0';
  *e_passwd                             = passwd_md5_str;

  return TS_ERR_OKAY;
}

/*--- direct file operations ----------------------------------------------*/
tsapi TSMgmtError
TSConfigFileRead(TSFileNameT file, char **text, int *size, int *version)
{
  return ReadFile(file, text, size, version);
}

tsapi TSMgmtError
TSConfigFileWrite(TSFileNameT file, char *text, int size, int version)
{
  return WriteFile(file, text, size, version);
}

/* ReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 * Output: TSMgmtError   - TS_ERR_OKAY if succeed, TS_ERR_FAIL otherwise
 * Obsolete:  tsapi TSMgmtError TSReadFromUrl (char *url, char **text, int *size);
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.example.com:80/products/network/index.html
 *       - http://www.example.com/products/network/index.html
 *       - http://www.example.com/products/network/
 *       - http://www.example.com/
 *       - http://www.example.com
 *       - www.example.com
 * NOTE: header and headerSize can be NULL
 */
tsapi TSMgmtError
TSReadFromUrl(char *url, char **header, int *headerSize, char **body, int *bodySize)
{
  // return ReadFromUrl(url, header, headerSize, body, bodySize);
  return TSReadFromUrlEx(url, header, headerSize, body, bodySize, URL_TIMEOUT);
}

tsapi TSMgmtError
TSReadFromUrlEx(const char *url, char **header, int *headerSize, char **body, int *bodySize, int timeout)
{
  int hFD        = -1;
  char *httpHost = NULL;
  char *httpPath = NULL;
  int httpPort   = HTTP_PORT;
  int bufsize    = URL_BUFSIZE;
  char buffer[URL_BUFSIZE];
  char request[BUFSIZE];
  char *hdr_temp;
  char *bdy_temp;
  TSMgmtError status = TS_ERR_OKAY;

  // Sanity check
  if (!url)
    return TS_ERR_FAIL;
  if (timeout < 0) {
    timeout = URL_TIMEOUT;
  }
  // Chop the protocol part, if it exists
  const char *doubleSlash = strstr(url, "//");
  if (doubleSlash) {
    url = doubleSlash + 2; // advance two positions to get rid of leading '//'
  }
  // the path starts after the first occurrence of '/'
  const char *tempPath = strstr(url, "/");
  char *host_and_port;
  if (tempPath) {
    host_and_port = ats_strndup(url, strlen(url) - strlen(tempPath));
    tempPath += 1; // advance one position to get rid of leading '/'
    httpPath = ats_strdup(tempPath);
  } else {
    host_and_port = ats_strdup(url);
    httpPath      = ats_strdup("");
  }

  // the port proceed by a ":", if it exists
  char *colon = strstr(host_and_port, ":");
  if (colon) {
    httpHost = ats_strndup(host_and_port, strlen(host_and_port) - strlen(colon));
    colon += 1; // advance one position to get rid of leading ':'
    httpPort = ink_atoi(colon);
    if (httpPort <= 0)
      httpPort = HTTP_PORT;
  } else {
    httpHost = ats_strdup(host_and_port);
  }
  ats_free(host_and_port);

  hFD = connectDirect(httpHost, httpPort, timeout);
  if (hFD == -1) {
    status = TS_ERR_NET_ESTABLISH;
    goto END;
  }

  /* sending the HTTP request via the established socket */
  snprintf(request, BUFSIZE, "http://%s:%d/%s", httpHost, httpPort, httpPath);
  if ((status = sendHTTPRequest(hFD, request, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  memset(buffer, 0, bufsize); /* empty the buffer */
  if ((status = readHTTPResponse(hFD, buffer, bufsize, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  if ((status = parseHTTPResponse(buffer, &hdr_temp, headerSize, &bdy_temp, bodySize)) != TS_ERR_OKAY)
    goto END;

  if (header && headerSize)
    *header = ats_strndup(hdr_temp, *headerSize);
  *body     = ats_strndup(bdy_temp, *bodySize);

END:
  ats_free(httpHost);
  ats_free(httpPath);

  return status;
}

/*--- cache inspector operations -------------------------------------------*/

tsapi TSMgmtError
TSLookupFromCacheUrl(TSString url, TSString *info)
{
  TSMgmtError err = TS_ERR_OKAY;
  int fd;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout   = URL_TIMEOUT;
  TSInt ts_port = 8080;

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/lookup_url?url=%s", url);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY)
    goto END;

  *info = ats_strndup(body, bdy_size);

END:
  return err;
}

tsapi TSMgmtError
TSLookupFromCacheUrlRegex(TSString url_regex, TSString *list)
{
  TSMgmtError err = TS_ERR_OKAY;
  int fd          = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout   = -1;
  TSInt ts_port = 8080;

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/lookup_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY)
    goto END;

  *list = ats_strndup(body, bdy_size);
END:
  return err;
}

tsapi TSMgmtError
TSDeleteFromCacheUrl(TSString url, TSString *info)
{
  TSMgmtError err = TS_ERR_OKAY;
  int fd          = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout   = URL_TIMEOUT;
  TSInt ts_port = 8080;

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/delete_url?url=%s", url);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY)
    goto END;

  *info = ats_strndup(body, bdy_size);

END:
  return err;
}

tsapi TSMgmtError
TSDeleteFromCacheUrlRegex(TSString url_regex, TSString *list)
{
  TSMgmtError err = TS_ERR_OKAY;
  int fd          = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout   = -1;
  TSInt ts_port = 8080;

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/delete_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY)
    goto END;

  *list = ats_strndup(body, bdy_size);
END:
  return err;
}

tsapi TSMgmtError
TSInvalidateFromCacheUrlRegex(TSString url_regex, TSString *list)
{
  TSMgmtError err = TS_ERR_OKAY;
  int fd          = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout   = -1;
  TSInt ts_port = 8080;

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/invalidate_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY)
    goto END;

  *list = ats_strndup(body, bdy_size);
END:
  return err;
}

/*--- snapshot operations -------------------------------------------------*/
tsapi TSMgmtError
TSSnapshotTake(char *snapshot_name)
{
  return SnapshotTake(snapshot_name);
}

tsapi TSMgmtError
TSSnapshotRestore(char *snapshot_name)
{
  return SnapshotRestore(snapshot_name);
}

tsapi TSMgmtError
TSSnapshotRemove(char *snapshot_name)
{
  return SnapshotRemove(snapshot_name);
}

tsapi TSMgmtError
TSSnapshotGetMlt(TSStringList snapshots)
{
  return SnapshotGetMlt((LLQ *)snapshots);
}

/*--- events --------------------------------------------------------------*/
tsapi TSMgmtError
TSEventSignal(char *event_name, ...)
{
  va_list ap;
  TSMgmtError ret;

  va_start(ap, event_name); // initialize the argument pointer ap
  ret = EventSignal(event_name, ap);
  va_end(ap);
  return ret;
}

tsapi TSMgmtError
TSEventResolve(const char *event_name)
{
  return EventResolve(event_name);
}

tsapi TSMgmtError
TSActiveEventGetMlt(TSList active_events)
{
  return ActiveEventGetMlt((LLQ *)active_events);
}

tsapi TSMgmtError
TSEventIsActive(char *event_name, bool *is_current)
{
  return EventIsActive(event_name, is_current);
}

tsapi TSMgmtError
TSEventSignalCbRegister(char *event_name, TSEventSignalFunc func, void *data)
{
  return EventSignalCbRegister(event_name, func, data);
}

tsapi TSMgmtError
TSEventSignalCbUnregister(char *event_name, TSEventSignalFunc func)
{
  return EventSignalCbUnregister(event_name, func);
}

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- abstracted file operations ------------------------------------------*/

/* calls teh CfgContext class constructor */
tsapi TSCfgContext
TSCfgContextCreate(TSFileNameT file)
{
  return ((void *)CfgContextCreate(file));
}

/* calls the CfgContext class destructor */
tsapi TSMgmtError
TSCfgContextDestroy(TSCfgContext ctx)
{
  return (CfgContextDestroy((CfgContext *)ctx));
}

tsapi TSMgmtError
TSCfgContextCommit(TSCfgContext ctx, TSActionNeedT * /* action_need ATS_UNUSED */, TSIntList errRules)
{
  return (CfgContextCommit((CfgContext *)ctx, (LLQ *)errRules));
}

tsapi TSMgmtError
TSCfgContextGet(TSCfgContext ctx)
{
  return (CfgContextGet((CfgContext *)ctx));
}

/*--- helper operations ---------------------------------------------------*/
/* returns number of ele's in the TSCfgContext */
int
TSCfgContextGetCount(TSCfgContext ctx)
{
  return CfgContextGetCount((CfgContext *)ctx);
}

/* user must typecast the TSCfgEle to appropriate TSEle before using */
TSCfgEle *
TSCfgContextGetEleAt(TSCfgContext ctx, int index)
{
  return CfgContextGetEleAt((CfgContext *)ctx, index);
}

/* iterator */
TSCfgEle *
TSCfgContextGetFirst(TSCfgContext ctx, TSCfgIterState *state)
{
  return CfgContextGetFirst((CfgContext *)ctx, state);
}

TSCfgEle *
TSCfgContextGetNext(TSCfgContext ctx, TSCfgIterState *state)
{
  return CfgContextGetNext((CfgContext *)ctx, state);
}

TSMgmtError
TSCfgContextMoveEleUp(TSCfgContext ctx, int index)
{
  return CfgContextMoveEleUp((CfgContext *)ctx, index);
}

TSMgmtError
TSCfgContextMoveEleDown(TSCfgContext ctx, int index)
{
  return CfgContextMoveEleDown((CfgContext *)ctx, index);
}

TSMgmtError
TSCfgContextAppendEle(TSCfgContext ctx, TSCfgEle *ele)
{
  return CfgContextAppendEle((CfgContext *)ctx, ele);
}

TSMgmtError
TSCfgContextInsertEleAt(TSCfgContext ctx, TSCfgEle *ele, int index)
{
  return CfgContextInsertEleAt((CfgContext *)ctx, ele, index);
}

TSMgmtError
TSCfgContextRemoveEleAt(TSCfgContext ctx, int index)
{
  return CfgContextRemoveEleAt((CfgContext *)ctx, index);
}

TSMgmtError
TSCfgContextRemoveAll(TSCfgContext ctx)
{
  return CfgContextRemoveAll((CfgContext *)ctx);
}

/* checks if the fields in the ele are all valid */
bool
TSIsValid(TSCfgEle *ele)
{
  CfgEleObj *ele_obj;

  if (!ele)
    return false;

  ele_obj = create_ele_obj_from_ele(ele);
  return (ele_obj->isValid());
}

TSConfigRecordDescription *
TSConfigRecordDescriptionCreate(void)
{
  TSConfigRecordDescription *val = (TSConfigRecordDescription *)ats_malloc(sizeof(TSConfigRecordDescription));

  ink_zero(*val);
  val->rec_type = TS_REC_UNDEFINED;

  return val;
}

void
TSConfigRecordDescriptionDestroy(TSConfigRecordDescription *val)
{
  TSConfigRecordDescriptionFree(val);
  ats_free(val);
}

void
TSConfigRecordDescriptionFree(TSConfigRecordDescription *val)
{
  if (val) {
    ats_free(val->rec_name);
    ats_free(val->rec_checkexpr);

    if (val->rec_type == TS_REC_STRING) {
      ats_free(val->rec_value.string_val);
    }

    ink_zero(*val);
    val->rec_type = TS_REC_UNDEFINED;
  }
}

TSMgmtError
TSConfigRecordDescribe(const char *rec_name, unsigned flags, TSConfigRecordDescription *val)
{
  if (!rec_name || !val) {
    return TS_ERR_PARAMS;
  }

  TSConfigRecordDescriptionFree(val);
  return MgmtConfigRecordDescribe(rec_name, flags, val);
}

TSMgmtError
TSConfigRecordDescribeMatchMlt(const char *rec_regex, unsigned flags, TSList rec_vals)
{
  if (!rec_regex || !rec_vals) {
    return TS_ERR_PARAMS;
  }

  return MgmtConfigRecordDescribeMatching(rec_regex, flags, rec_vals);
}
