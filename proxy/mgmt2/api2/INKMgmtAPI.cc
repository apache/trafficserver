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
#include <limits.h>
#include "inktomi++.h"

#include "INKMgmtAPI.h"
#include "CfgContextManager.h"
#include "CfgContextImpl.h"
#include "CfgContextUtils.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"

#include "TextBuffer.h"

// TODO: consolidate location of these defaults
#define DEFAULT_ROOT_DIRECTORY            PREFIX
#define DEFAULT_LOCAL_STATE_DIRECTORY     "var/trafficserver"
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "etc/trafficserver"
#define DEFAULT_LOG_DIRECTORY             "var/log/trafficserver"
#define DEFAULT_TS_DIRECTORY_FILE         PREFIX "/etc/traffic_server"

// forward declarations
void init_pdss_format(INKPdSsFormat * info);


/***************************************************************************
 * API Memory Management
 ***************************************************************************/
void *
_INKmalloc(unsigned int size, const char *path)
{
#ifdef PURIFY
  return xmalloc(size);
#else
  return _xmalloc(size, path);
#endif
}

void *
_INKrealloc(void *ptr, unsigned int size, const char *path)
{
#ifdef PURIFY
  return xrealloc(ptr, size);
#else
  return _xrealloc(ptr, size, path);
#endif
}

char *
_INKstrdup(const char *str, int length, const char *path)
{
#ifdef PURIFY
  return xstrndup(str, length);
#else
  return _xstrdup(str, length, path);
#endif
}

void
_INKfree(void *ptr)
{
#ifdef PURIFY
  xfree(ptr);
#else
  _xfree(ptr);
#endif
}


/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- INKList operations -------------------------------------------------*/
inkapi INKList
INKListCreate(void)
{
  return (void *) create_queue();
}

/* NOTE: The List must be EMPTY */
inkapi void
INKListDestroy(INKList l)
{
  if (!l)
    return;

  delete_queue((LLQ *) l);
  return;
}

inkapi INKError
INKListEnqueue(INKList l, void *data)
{
  int ret;

  ink_assert(l && data);
  if (!l || !data)
    return INK_ERR_PARAMS;

  ret = enqueue((LLQ *) l, data);       /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return INK_ERR_FAIL;
  } else {
    return INK_ERR_OKAY;
  }
}

inkapi void *
INKListDequeue(INKList l)
{
  ink_assert(l);
  if (!l || queue_is_empty((LLQ *) l))
    return NULL;

  return dequeue((LLQ *) l);
}

inkapi bool
INKListIsEmpty(INKList l)
{
  int ret;

  ink_assert(l);
  if (!l)
    return true;                // list doesn't exist, so it's empty

  ret = queue_is_empty((LLQ *) l);      /* returns 0 if empty, non-zero if not empty */
  if (ret == 0) {               /* empty */
    return true;
  } else {
    return false;
  }
}

inkapi int
INKListLen(INKList l)
{
  ink_assert(l);
  if (!l)
    return -1;

  return queue_len((LLQ *) l);
}

inkapi bool
INKListIsValid(INKList l)
{
  int i, len;
  void *ele;

  if (!l)
    return false;

  len = queue_len((LLQ *) l);
  for (i = 0; i < len; i++) {
    ele = (void *) dequeue((LLQ *) l);
    if (!ele)
      return false;
    enqueue((LLQ *) l, ele);
  }
  return true;
}

/*--- INKIpAddrList operations -------------------------------------------------*/
inkapi INKIpAddrList
INKIpAddrListCreate(void)
{
  return (void *) create_queue();       /* this queue will be a list of IpAddrEle* */
}

inkapi void
INKIpAddrListDestroy(INKIpAddrList ip_addrl)
{
  INKIpAddrEle *ipaddr_ele;

  if (!ip_addrl) {
    return;
  }

  /* dequeue each element and free it; 
     currently, an element can only be an INKIpAddrEle
     or it can be an INKIpAddr ?? */
  while (!queue_is_empty((LLQ *) ip_addrl)) {
    ipaddr_ele = (INKIpAddrEle *) dequeue((LLQ *) ip_addrl);

    if (!ipaddr_ele)
      continue;

    INKIpAddrEleDestroy(ipaddr_ele);
  }

  /* we have removed everything on the list so free list */
  delete_queue((LLQ *) ip_addrl);
  return;
}

inkapi INKError
INKIpAddrListEnqueue(INKIpAddrList ip_addrl, INKIpAddrEle * ip_addr)
{
  int ret;

  ink_assert(ip_addrl && ip_addr);
  if (!ip_addrl || !ip_addr)
    return INK_ERR_PARAMS;

  ret = enqueue((LLQ *) ip_addrl, ip_addr);
  if (ret == 0) {
    return INK_ERR_FAIL;
  } else {
    return INK_ERR_OKAY;
  }
}


/* The the INKIpAddrEle returned is actually removed from the end of list */
inkapi INKIpAddrEle *
INKIpAddrListDequeue(INKIpAddrList ip_addrl)
{
  ink_assert(ip_addrl);
  if (!ip_addrl || queue_is_empty((LLQ *) ip_addrl))
    return NULL;

  return (INKIpAddrEle *) dequeue((LLQ *) ip_addrl);
}


inkapi int
INKIpAddrListLen(INKIpAddrList ip_addrl)
{
  ink_assert(ip_addrl);
  if (!ip_addrl)
    return -1;

  return queue_len((LLQ *) ip_addrl);
}

inkapi bool
INKIpAddrListIsEmpty(INKIpAddrList ip_addrl)
{
  ink_assert(ip_addrl);
  if (!ip_addrl)
    return true;

  return queue_is_empty((LLQ *) ip_addrl);
}

// returns false if any of the IpAddrEle is not an valid IP address by making
// use of ccu_checkIpAddrEle; if return false, the ip's may be reordered 
// from the original list
inkapi bool
INKIpAddrListIsValid(INKIpAddrList ip_addrl)
{
  int i, len;
  INKIpAddrEle *ele;

  if (!ip_addrl)
    return false;

  len = queue_len((LLQ *) ip_addrl);
  for (i = 0; i < len; i++) {
    ele = (INKIpAddrEle *) dequeue((LLQ *) ip_addrl);
    if (!ccu_checkIpAddrEle(ele)) {
      enqueue((LLQ *) ip_addrl, ele);
      return false;
    }
    enqueue((LLQ *) ip_addrl, ele);
  }
  return true;
}

/*--- INKPortList operations ----------------------------------------------*/
inkapi INKPortList
INKPortListCreate()
{
  return (void *) create_queue();       /* this queue will be a list of INKPortEle* */
}

inkapi void
INKPortListDestroy(INKPortList portl)
{
  INKPortEle *port_ele;

  if (!portl) {
    return;
  }
  // dequeue each element and free it 
  while (!queue_is_empty((LLQ *) portl)) {
    port_ele = (INKPortEle *) dequeue((LLQ *) portl);

    if (!port_ele)
      continue;

    INKPortEleDestroy(port_ele);
  }

  /* we have removed everything on the list so free list */
  delete_queue((LLQ *) portl);
  return;
}

inkapi INKError
INKPortListEnqueue(INKPortList portl, INKPortEle * port)
{
  int ret;

  ink_assert(portl && port);
  if (!portl || !port)
    return INK_ERR_PARAMS;

  ret = enqueue((LLQ *) portl, port);   /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return INK_ERR_FAIL;
  } else {
    return INK_ERR_OKAY;
  }
}

inkapi INKPortEle *
INKPortListDequeue(INKPortList portl)
{
  ink_assert(portl);
  if (!portl || queue_is_empty((LLQ *) portl))
    return NULL;

  return (INKPortEle *) dequeue((LLQ *) portl);
}

inkapi int
INKPortListLen(INKPortList portl)
{
  ink_assert(portl);
  if (!portl)
    return -1;

  return queue_len((LLQ *) portl);
}

inkapi bool
INKPortListIsEmpty(INKPortList portl)
{
  ink_assert(portl);
  if (!portl)
    return true;

  return queue_is_empty((LLQ *) portl);
}

// returns false if any of the PortEle's has a port_a <= 0;
// if returns false, then will return the entire port list
// intact, although the ports may not be ordered in the same way
inkapi bool
INKPortListIsValid(INKPortList portl)
{
  int i, len;
  INKPortEle *ele;

  if (!portl)
    return false;

  len = queue_len((LLQ *) portl);
  for (i = 0; i < len; i++) {
    ele = (INKPortEle *) dequeue((LLQ *) portl);
    if (!ccu_checkPortEle(ele)) {
      enqueue((LLQ *) portl, ele);
      return false;
    }
    enqueue((LLQ *) portl, ele);
  }
  return true;
}


/*--- INKDomainList operations -----------------------------------------*/
inkapi INKDomainList
INKDomainListCreate()
{
  return (void *) create_queue();       /* this queue will be a list of char* */
}

inkapi void
INKDomainListDestroy(INKDomainList domainl)
{
  INKDomain *domain;

  if (!domainl) {
    return;
  }

  /* dequeue each element and free it */
  while (!queue_is_empty((LLQ *) domainl)) {
    domain = (INKDomain *) dequeue((LLQ *) domainl);

    if (!domain)
      continue;

    INKDomainDestroy(domain);
  }

  delete_queue((LLQ *) domainl);
}

inkapi INKError
INKDomainListEnqueue(INKDomainList domainl, INKDomain * domain)
{
  int ret;

  ink_assert(domainl && domain);
  if (!domainl || !domain)
    return INK_ERR_PARAMS;

  ret = enqueue((LLQ *) domainl, domain);       /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return INK_ERR_FAIL;
  } else {
    return INK_ERR_OKAY;
  }
}

inkapi INKDomain *
INKDomainListDequeue(INKDomainList domainl)
{
  ink_assert(domainl);
  if (!domainl || queue_is_empty((LLQ *) domainl))
    return NULL;

  return (INKDomain *) dequeue((LLQ *) domainl);
}

inkapi bool
INKDomainListIsEmpty(INKDomainList domainl)
{
  ink_assert(domainl);
  if (!domainl)
    return true;

  return queue_is_empty((LLQ *) domainl);
}

inkapi int
INKDomainListLen(INKDomainList domainl)
{
  ink_assert(domainl);
  if (!domainl)
    return -1;

  return queue_len((LLQ *) domainl);
}

// returns false if encounter a NULL hostname and ip
inkapi bool
INKDomainListIsValid(INKDomainList domainl)
{
  int i, len;
  INKDomain *dom;

  if (!domainl)
    return false;

  len = queue_len((LLQ *) domainl);
  for (i = 0; i < len; i++) {
    dom = (INKDomain *) dequeue((LLQ *) domainl);
    if (!dom) {
      return false;
    }
    if (!dom->domain_val) {
      return false;
    }
    enqueue((LLQ *) domainl, dom);
  }
  return true;

}

/*--- INKStringList operations --------------------------------------*/
inkapi INKStringList
INKStringListCreate()
{
  return (void *) create_queue();       /* this queue will be a list of char* */
}

/* usually, must be an empty list before destroying*/
inkapi void
INKStringListDestroy(INKStringList strl)
{
  char *str;

  if (!strl) {
    return;
  }

  /* dequeue each element and free it */
  while (!queue_is_empty((LLQ *) strl)) {
    str = (char *) dequeue((LLQ *) strl);

    if (!str)
      continue;

    xfree(str);
  }

  delete_queue((LLQ *) strl);
}

inkapi INKError
INKStringListEnqueue(INKStringList strl, char *str)
{
  int ret;

  ink_assert(strl && str);
  if (!strl || !str)
    return INK_ERR_PARAMS;

  ret = enqueue((LLQ *) strl, str);     /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return INK_ERR_FAIL;
  } else {
    return INK_ERR_OKAY;
  }
}

inkapi char *
INKStringListDequeue(INKStringList strl)
{
  ink_assert(strl);
  if (!strl || queue_is_empty((LLQ *) strl))
    return NULL;

  return (char *) dequeue((LLQ *) strl);
}

inkapi bool
INKStringListIsEmpty(INKStringList strl)
{
  ink_assert(strl);
  if (!strl)
    return true;

  return queue_is_empty((LLQ *) strl);
}

inkapi int
INKStringListLen(INKStringList strl)
{
  ink_assert(strl);
  if (!strl)
    return -1;

  return queue_len((LLQ *) strl);
}

// returns false if any element is NULL string
inkapi bool
INKStringListIsValid(INKStringList strl)
{
  int i, len;
  char *str;

  if (!strl)
    return false;

  len = queue_len((LLQ *) strl);
  for (i = 0; i < len; i++) {
    str = (char *) dequeue((LLQ *) strl);
    if (!str)
      return false;
    enqueue((LLQ *) strl, str);
  }
  return true;
}

/*--- INKIntList operations --------------------------------------*/
inkapi INKIntList
INKIntListCreate()
{
  return (void *) create_queue();       /* this queue will be a list of int* */
}

/* usually, must be an empty list before destroying*/
inkapi void
INKIntListDestroy(INKIntList intl)
{
  int *iPtr;

  if (!intl)
    return;

  /* dequeue each element and free it */
  while (!queue_is_empty((LLQ *) intl)) {
    iPtr = (int *) dequeue((LLQ *) intl);

    if (!iPtr)
      continue;

    xfree(iPtr);
  }

  delete_queue((LLQ *) intl);
  return;
}

inkapi INKError
INKIntListEnqueue(INKIntList intl, int *elem)
{
  int ret;

  ink_assert(intl && elem);
  if (!intl || !elem)
    return INK_ERR_PARAMS;

  ret = enqueue((LLQ *) intl, elem);    /* returns TRUE=1 or FALSE=0 */
  if (ret == 0) {
    return INK_ERR_FAIL;
  } else {
    return INK_ERR_OKAY;
  }
}

inkapi int *
INKIntListDequeue(INKIntList intl)
{
  ink_assert(intl);
  if (!intl || queue_is_empty((LLQ *) intl))
    return NULL;

  return (int *) dequeue((LLQ *) intl);
}

inkapi bool
INKIntListIsEmpty(INKIntList intl)
{
  ink_assert(intl);
  if (!intl)
    return true;

  return queue_is_empty((LLQ *) intl);
}

inkapi int
INKIntListLen(INKIntList intl)
{
  ink_assert(intl);
  if (!intl)
    return -1;

  return queue_len((LLQ *) intl);
}

inkapi bool
INKIntListIsValid(INKIntList intl, int min, int max)
{
  if (!intl)
    return false;

  for (unsigned long i = 0; i < queue_len((LLQ *) intl); i++) {
    int *item = (int *) dequeue((LLQ *) intl);
    if (*item < min) {
      return false;
    }
    if (*item > max) {
      return false;
    }
    enqueue((LLQ *) intl, item);
  }
  return true;
}


// helper fn that sets default values for the info passed in
void
init_pdss_format(INKPdSsFormat * info)
{
  info->pd_type = INK_PD_UNDEFINED;
  info->pd_val = NULL;
  info->sec_spec.active = 0;
  info->sec_spec.time.hour_a = 0;
  info->sec_spec.time.min_a = 0;
  info->sec_spec.time.hour_b = 0;
  info->sec_spec.time.min_b = 0;
  info->sec_spec.src_ip = INK_INVALID_IP_ADDR;
  info->sec_spec.prefix = NULL;
  info->sec_spec.suffix = NULL;
  info->sec_spec.port = INK_INVALID_PORT;
  info->sec_spec.method = INK_METHOD_UNDEFINED;
  info->sec_spec.scheme = INK_SCHEME_UNDEFINED;
  info->sec_spec.mixt = INK_MIXT_UNDEFINED;
}

/*--- allocate/deallocate operations --------------------------------------*/
inkapi INKEvent *
INKEventCreate(void)
{
  INKEvent *event;
  event = (INKEvent *) xmalloc(sizeof(INKEvent));
  if (!event)
    return NULL;

  event->id = -1;
  event->name = NULL;
  event->description = NULL;
  event->priority = INK_EVENT_PRIORITY_UNDEFINED;

  return event;
}

inkapi void
INKEventDestroy(INKEvent * event)
{
  if (event) {
    if (event->name)
      xfree(event->name);
    if (event->description)
      xfree(event->description);
    xfree(event);
  }
  return;
}

inkapi INKRecordEle *
INKRecordEleCreate(void)
{
  INKRecordEle *ele;
  ele = (INKRecordEle *) xmalloc(sizeof(INKRecordEle));
  if (!ele)
    return NULL;

  ele->rec_name = NULL;
  ele->rec_type = INK_REC_UNDEFINED;

  //ele->int_val = -1;
  //ele->counter_val = -1;
  //ele->float_val = -1;
  //ele->string_val = NULL;

  return ele;
}

inkapi void
INKRecordEleDestroy(INKRecordEle * ele)
{
  if (ele) {
    if (ele->rec_name)
      xfree(ele->rec_name);
    if (ele->rec_type == INK_REC_STRING && ele->string_val)
      xfree(ele->string_val);
    xfree(ele);
  }
  return;
}

inkapi INKIpAddrEle *
INKIpAddrEleCreate(void)
{
  INKIpAddrEle *ele;

  ele = (INKIpAddrEle *) xmalloc(sizeof(INKIpAddrEle));
  if (!ele)
    return NULL;

  /* set default values */
  ele->type = INK_IP_UNDEFINED;
  ele->ip_a = INK_INVALID_IP_ADDR;
  ele->cidr_a = INK_INVALID_IP_CIDR;
  ele->port_a = INK_INVALID_PORT;
  ele->ip_b = INK_INVALID_IP_ADDR;
  ele->cidr_b = INK_INVALID_IP_CIDR;
  ele->port_b = INK_INVALID_PORT;
  return ele;
}

inkapi void
INKIpAddrEleDestroy(INKIpAddrEle * ele)
{
  if (ele) {
    if (ele->ip_a)
      xfree(ele->ip_a);
    if (ele->ip_b)
      xfree(ele->ip_b);
    xfree(ele);
  }

  return;
}

inkapi INKPortEle *
INKPortEleCreate(void)
{
  INKPortEle *ele;

  ele = (INKPortEle *) xmalloc(sizeof(INKPortEle));
  if (!ele)
    return NULL;

  ele->port_a = INK_INVALID_PORT;
  ele->port_b = INK_INVALID_PORT;

  return ele;
}

inkapi void
INKPortEleDestroy(INKPortEle * ele)
{
  if (ele)
    xfree(ele);
  return;
}

inkapi INKDomain *
INKDomainCreate()
{
  INKDomain *ele;

  ele = (INKDomain *) xmalloc(sizeof(INKDomain));
  if (!ele)
    return NULL;

  ele->domain_val = NULL;
  ele->port = INK_INVALID_PORT;

  return ele;
}

inkapi void
INKDomainDestroy(INKDomain * ele)
{
  if (ele) {
    // this is okay because INKIpAddr is also a char*
    if (ele->domain_val)
      xfree(ele->domain_val);
    xfree(ele);
  }
}

inkapi INKSspec *
INKSspecCreate(void)
{
  INKSspec *sec_spec;

  sec_spec = (INKSspec *) xmalloc(sizeof(INKSspec));
  if (!sec_spec)
    return NULL;

  /* set defaults */
  sec_spec->active = 0;
  (sec_spec->time).hour_a = 0;
  (sec_spec->time).min_a = 0;
  (sec_spec->time).hour_b = 0;
  (sec_spec->time).min_b = 0;
  sec_spec->src_ip = INK_INVALID_IP_ADDR;
  sec_spec->prefix = NULL;
  sec_spec->suffix = NULL;
  sec_spec->port = NULL;
  sec_spec->method = INK_METHOD_UNDEFINED;
  sec_spec->scheme = INK_SCHEME_UNDEFINED;
  sec_spec->mixt = INK_MIXT_UNDEFINED;
  return sec_spec;
}

inkapi void
INKSspecDestroy(INKSspec * ele)
{
  if (ele) {
    if (ele->prefix)
      xfree(ele->prefix);
    if (ele->suffix)
      xfree(ele->suffix);
    if (ele->port)
      INKPortEleDestroy(ele->port);
    xfree(ele);
  }
  return;
}

inkapi INKPdSsFormat *
INKPdSsFormatCreate(void)
{
  INKPdSsFormat *ele;

  ele = (INKPdSsFormat *) xmalloc(sizeof(INKPdSsFormat));
  if (!ele)
    return NULL;

  /* should set default values here */
  ele->pd_type = INK_PD_UNDEFINED;
  ele->pd_val = NULL;

  ele->sec_spec.active = 0;
  (ele->sec_spec.time).hour_a = -1;
  (ele->sec_spec.time).min_a = -1;
  (ele->sec_spec.time).hour_b = -1;
  (ele->sec_spec.time).min_b = -1;
  ele->sec_spec.src_ip = INK_INVALID_IP_ADDR;
  ele->sec_spec.prefix = NULL;
  ele->sec_spec.suffix = NULL;
  ele->sec_spec.port = NULL;
  ele->sec_spec.method = INK_METHOD_UNDEFINED;
  ele->sec_spec.scheme = INK_SCHEME_UNDEFINED;

  return ele;
}

inkapi void
INKPdSsFormatDestroy(INKPdSsFormat * ele)
{
  if (ele) {
    if (ele->pd_val)
      xfree(ele->pd_val);
    if (ele->sec_spec.src_ip)
      xfree(ele->sec_spec.src_ip);
    if (ele->sec_spec.prefix)
      xfree(ele->sec_spec.prefix);
    if (ele->sec_spec.suffix)
      xfree(ele->sec_spec.suffix);
    if (ele->sec_spec.port)
      INKPortEleDestroy(ele->sec_spec.port);
  }
  return;
}

/*-------------------------------------------------------------
 * INKAdminAccessEle 
 *-------------------------------------------------------------*/
inkapi INKAdminAccessEle *
INKAdminAccessEleCreate()
{
  INKAdminAccessEle *ele;

  ele = (INKAdminAccessEle *) xmalloc(sizeof(INKAdminAccessEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_ADMIN_ACCESS;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->user = NULL;
  ele->password = NULL;
  ele->access = INK_ACCESS_UNDEFINED;

  return ele;
}

inkapi void
INKAdminAccessEleDestroy(INKAdminAccessEle * ele)
{
  if (ele) {
    if (ele->user)
      xfree(ele->user);
    if (ele->password)
      xfree(ele->password);
    xfree(ele);
  }
  return;
}


/*-------------------------------------------------------------
 * CacheObj
 *-------------------------------------------------------------*/
inkapi INKCacheEle *
INKCacheEleCreate(INKRuleTypeT type)
{
  INKCacheEle *ele;

  if (type != INK_CACHE_NEVER &&
      type != INK_CACHE_IGNORE_NO_CACHE &&
      type != INK_CACHE_IGNORE_CLIENT_NO_CACHE &&
      type != INK_CACHE_IGNORE_SERVER_NO_CACHE &&
      type != INK_CACHE_PIN_IN_CACHE &&
      type != INK_CACHE_REVALIDATE &&
      type != INK_CACHE_TTL_IN_CACHE && type != INK_CACHE_AUTH_CONTENT && type != INK_TYPE_UNDEFINED)
    return NULL;                // invalid type

  ele = (INKCacheEle *) xmalloc(sizeof(INKCacheEle));
  if (!ele)
    return NULL;

  /* set defaults */
  ele->cfg_ele.type = type;
  ele->cfg_ele.error = INK_ERR_OKAY;
  init_pdss_format(&(ele->cache_info));
  ele->time_period.d = 0;
  ele->time_period.h = 0;
  ele->time_period.m = 0;
  ele->time_period.s = 0;

  return ele;
}

inkapi void
INKCacheEleDestroy(INKCacheEle * ele)
{
  if (ele) {
    INKPdSsFormatDestroy(&(ele->cache_info));
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * CongestionObj
 *-------------------------------------------------------------*/
// FIXME: for now use defaults specified in feature spec; the 
// defaults though are configurable as records, so should use 
// records values instead
inkapi INKCongestionEle *
INKCongestionEleCreate()
{
  INKCongestionEle *ele;

  ele = (INKCongestionEle *) xmalloc(sizeof(INKCongestionEle));
  if (!ele)
    return NULL;

  /* set defaults */
  ele->cfg_ele.type = INK_CONGESTION;
  ele->cfg_ele.error = INK_ERR_OKAY;
  //init_pdss_format(&(ele->congestion_info));
  ele->pd_type = INK_PD_UNDEFINED;
  ele->pd_val = NULL;
  ele->prefix = NULL;
  ele->port = INK_INVALID_PORT;
  ele->scheme = INK_HTTP_CONGEST_PER_IP;
  ele->max_connection_failures = 5;
  ele->fail_window = 120;
  ele->proxy_retry_interval = 10;
  ele->client_wait_interval = 300;
  ele->wait_interval_alpha = 30;
  ele->live_os_conn_timeout = 60;
  ele->live_os_conn_retries = 2;
  ele->dead_os_conn_timeout = 15;
  ele->dead_os_conn_retries = 1;
  ele->max_connection = -1;
  ele->error_page_uri = xstrdup("congestion#retryAfter");

  return ele;
}

inkapi void
INKCongestionEleDestroy(INKCongestionEle * ele)
{
  if (ele) {
    if (ele->pd_val)
      xfree(ele->pd_val);
    if (ele->prefix)
      xfree(ele->prefix);
    if (ele->error_page_uri)
      xfree(ele->error_page_uri);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * FilterObject
 *-------------------------------------------------------------*/
inkapi INKFilterEle *
INKFilterEleCreate(INKRuleTypeT type)
{
  INKFilterEle *ele;

  if (type != INK_FILTER_ALLOW &&
      type != INK_FILTER_DENY &&
      type != INK_FILTER_LDAP &&
      type != INK_FILTER_KEEP_HDR && type != INK_FILTER_STRIP_HDR && type != INK_TYPE_UNDEFINED)
    return NULL;

  ele = (INKFilterEle *) xmalloc(sizeof(INKFilterEle));
  if (!ele)
    return NULL;

  /* set defaults */
  ele->cfg_ele.type = type;
  ele->cfg_ele.error = INK_ERR_OKAY;
  init_pdss_format(&(ele->filter_info));
  ele->hdr = INK_HDR_UNDEFINED;
  ele->server = NULL;
  ele->dn = NULL;
  ele->realm = NULL;
  ele->uid_filter = NULL;
  ele->attr = NULL;
  ele->attr_val = NULL;
  ele->redirect_url = NULL;
  ele->bind_dn = NULL;
  ele->bind_pwd_file = NULL;
  return ele;
}

inkapi void
INKFilterEleDestroy(INKFilterEle * ele)
{
  if (ele) {
    INKPdSsFormatDestroy(&(ele->filter_info));
    if (ele->server) {
      xfree(ele->server);
    }
    if (ele->dn) {
      xfree(ele->dn);
    }
    if (ele->realm) {
      xfree(ele->realm);
    }
    if (ele->uid_filter) {
      xfree(ele->uid_filter);
    }
    if (ele->attr) {
      xfree(ele->attr);
    }
    if (ele->attr_val) {
      xfree(ele->attr_val);
    }
    if (ele->redirect_url) {
      xfree(ele->redirect_url);
    }
    if (ele->bind_dn) {
      xfree(ele->bind_dn);
    }
    if (ele->bind_pwd_file) {
      xfree(ele->bind_pwd_file);
    }

    xfree(ele);
  }
  return;
}


/*-------------------------------------------------------------
 * FtpRemapObj
 *-------------------------------------------------------------*/
inkapi INKFtpRemapEle *
INKFtpRemapEleCreate()
{
  INKFtpRemapEle *ele;

  ele = (INKFtpRemapEle *) xmalloc(sizeof(INKFtpRemapEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_FTP_REMAP;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->from_val = NULL;
  ele->from_port = INK_INVALID_PORT;
  ele->to_val = NULL;
  ele->to_port = INK_INVALID_PORT;

  return ele;
}

inkapi void
INKFtpRemapEleDestroy(INKFtpRemapEle * ele)
{
  if (ele) {
    if (ele->from_val)
      xfree(ele->from_val);
    if (ele->to_val)
      xfree(ele->to_val);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * HostingObj
 *-------------------------------------------------------------*/
inkapi INKHostingEle *
INKHostingEleCreate()
{
  INKHostingEle *ele;

  ele = (INKHostingEle *) xmalloc(sizeof(INKHostingEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_HOSTING;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->pd_type = INK_PD_UNDEFINED;
  ele->pd_val = NULL;
  ele->partitions = INK_INVALID_LIST;

  return ele;
}

inkapi void
INKHostingEleDestroy(INKHostingEle * ele)
{
  if (ele) {
    if (ele->pd_val)
      xfree(ele->pd_val);
    if (ele->partitions)
      INKIntListDestroy(ele->partitions);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * IcpObject
 *-------------------------------------------------------------*/
inkapi INKIcpEle *
INKIcpEleCreate()
{
  INKIcpEle *ele;

  ele = (INKIcpEle *) xmalloc(sizeof(INKIcpEle));
  if (!ele)
    return NULL;

  /* set defaults */
  ele->cfg_ele.type = INK_ICP;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->peer_hostname = NULL;
  ele->peer_host_ip_addr = INK_INVALID_IP_ADDR;
  ele->peer_type = INK_ICP_UNDEFINED;
  ele->peer_proxy_port = INK_INVALID_PORT;
  ele->peer_icp_port = INK_INVALID_PORT;
  ele->is_multicast = FALSE;
  ele->mc_ip_addr = INK_INVALID_IP_ADDR;
  ele->mc_ttl = INK_MC_TTL_SINGLE_SUBNET;       // default value

  return ele;

}

inkapi void
INKIcpEleDestroy(INKIcpEle * ele)
{
  if (ele) {
    if (ele->peer_hostname)
      xfree(ele->peer_hostname);
    if (ele->peer_host_ip_addr)
      xfree(ele->peer_host_ip_addr);
    if (ele->mc_ip_addr)
      xfree(ele->mc_ip_addr);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKIpAllowEle
 *-------------------------------------------------------------*/
inkapi INKIpAllowEle *
INKIpAllowEleCreate()
{

  INKIpAllowEle *ele;

  ele = (INKIpAllowEle *) xmalloc(sizeof(INKIpAllowEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_IP_ALLOW;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->src_ip_addr = INK_INVALID_IP_ADDR;
  ele->action = INK_IP_ALLOW_UNDEFINED;

  return ele;
}

inkapi void
INKIpAllowEleDestroy(INKIpAllowEle * ele)
{
  if (ele) {
    if (ele->src_ip_addr)
      INKIpAddrEleDestroy(ele->src_ip_addr);
    xfree(ele);
  }
  return;

}


/*-------------------------------------------------------------
 * INKLogFilterEle
 *-------------------------------------------------------------*/
inkapi INKLogFilterEle *
INKLogFilterEleCreate()
{
  INKLogFilterEle *ele;

  ele = (INKLogFilterEle *) xmalloc(sizeof(INKLogFilterEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_LOG_FILTER;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->action = INK_LOG_FILT_UNDEFINED;
  ele->filter_name = NULL;
  ele->log_field = NULL;
  ele->compare_op = INK_LOG_COND_UNDEFINED;
  ele->compare_str = NULL;
  ele->compare_int = -1;
  return ele;
}

inkapi void
INKLogFilterEleDestroy(INKLogFilterEle * ele)
{
  if (ele) {
    if (ele->filter_name)
      xfree(ele->filter_name);
    if (ele->log_field)
      xfree(ele->log_field);
    if (ele->compare_str)
      xfree(ele->compare_str);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKLogFormatEle
 *-------------------------------------------------------------*/
inkapi INKLogFormatEle *
INKLogFormatEleCreate()
{
  INKLogFormatEle *ele;

  ele = (INKLogFormatEle *) xmalloc(sizeof(INKLogFormatEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_LOG_FORMAT;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->name = NULL;
  ele->format = NULL;
  ele->aggregate_interval_secs = 0;

  return ele;
}

inkapi void
INKLogFormatEleDestroy(INKLogFormatEle * ele)
{
  if (ele) {
    if (ele->name)
      xfree(ele->name);
    if (ele->format)
      xfree(ele->format);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKLogObjectEle
 *-------------------------------------------------------------*/
inkapi INKLogObjectEle *
INKLogObjectEleCreate()
{
  INKLogObjectEle *ele;

  ele = (INKLogObjectEle *) xmalloc(sizeof(INKLogObjectEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_LOG_OBJECT;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->format_name = NULL;
  ele->file_name = NULL;
  ele->log_mode = INK_LOG_MODE_UNDEFINED;
  ele->collation_hosts = INK_INVALID_LIST;
  ele->filters = INK_INVALID_LIST;
  ele->protocols = INK_INVALID_LIST;
  ele->server_hosts = INK_INVALID_LIST;

  return ele;
}

inkapi void
INKLogObjectEleDestroy(INKLogObjectEle * ele)
{
  if (ele) {
    if (ele->format_name)
      xfree(ele->format_name);
    if (ele->file_name)
      xfree(ele->file_name);
    if (ele->collation_hosts)
      INKDomainListDestroy(ele->collation_hosts);
    if (ele->filters)
      INKStringListDestroy(ele->filters);
    if (ele->protocols)
      INKStringListDestroy(ele->protocols);
    if (ele->server_hosts)
      INKStringListDestroy(ele->server_hosts);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKMgmtAllowEle
 *-------------------------------------------------------------*/
inkapi INKMgmtAllowEle *
INKMgmtAllowEleCreate()
{

  INKMgmtAllowEle *ele;

  ele = (INKMgmtAllowEle *) xmalloc(sizeof(INKMgmtAllowEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_MGMT_ALLOW;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->src_ip_addr = INK_INVALID_IP_ADDR;
  ele->action = INK_MGMT_ALLOW_UNDEFINED;

  return ele;
}

inkapi void
INKMgmtAllowEleDestroy(INKMgmtAllowEle * ele)
{
  if (ele) {
    if (ele->src_ip_addr)
      INKIpAddrEleDestroy(ele->src_ip_addr);
    xfree(ele);
  }
  return;

}

/*-------------------------------------------------------------
 * INKNntpAccessEle
 *-------------------------------------------------------------*/
inkapi INKNntpAccessEle *
INKNntpAccessEleCreate()
{

  INKNntpAccessEle *ele;

  ele = (INKNntpAccessEle *) xmalloc(sizeof(INKNntpAccessEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_NNTP_ACCESS;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->client_t = INK_CLIENT_GRP_UNDEFINED;
  ele->clients = NULL;
  ele->access = INK_NNTP_ACC_UNDEFINED;
  ele->authenticator = NULL;
  ele->user = NULL;
  ele->pass = NULL;
  ele->group_wildmat = INK_INVALID_LIST;
  ele->deny_posting = false;

  return ele;
}

inkapi void
INKNntpAccessEleDestroy(INKNntpAccessEle * ele)
{
  if (ele) {
    if (ele->clients)
      xfree(ele->clients);
    if (ele->authenticator)
      xfree(ele->authenticator);
    if (ele->user)
      xfree(ele->user);
    if (ele->pass)
      xfree(ele->pass);
    if (ele->group_wildmat)
      INKStringListDestroy(ele->group_wildmat);
    xfree(ele);
  }

  return;
}

/*-------------------------------------------------------------
 * INKNntpSrvrEle
 *-------------------------------------------------------------*/
inkapi INKNntpSrvrEle *
INKNntpSrvrEleCreate()
{

  INKNntpSrvrEle *ele;

  ele = (INKNntpSrvrEle *) xmalloc(sizeof(INKNntpSrvrEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_NNTP_SERVERS;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->hostname = NULL;
  ele->group_wildmat = INK_INVALID_LIST;
  ele->treatment = INK_NNTP_TRMT_UNDEFINED;
  ele->priority = 0;
  ele->interface = NULL;

  return ele;
}

inkapi void
INKNntpSrvrEleDestroy(INKNntpSrvrEle * ele)
{
  if (ele) {
    if (ele->hostname)
      xfree(ele->hostname);
    if (ele->interface)
      xfree(ele->interface);
    if (ele->group_wildmat)
      INKStringListDestroy(ele->group_wildmat);
    xfree(ele);
  }

  return;
}

/*-------------------------------------------------------------
 * INKParentProxyEleCreate
 *-------------------------------------------------------------*/
inkapi INKParentProxyEle *
INKParentProxyEleCreate(INKRuleTypeT type)
{
  INKParentProxyEle *ele;

  if (type != INK_PP_PARENT && type != INK_PP_GO_DIRECT && type != INK_TYPE_UNDEFINED)
    return NULL;

  ele = (INKParentProxyEle *) xmalloc(sizeof(INKParentProxyEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = type;
  ele->cfg_ele.error = INK_ERR_OKAY;
  init_pdss_format(&(ele->parent_info));
  ele->rr = INK_RR_NONE;
  ele->proxy_list = INK_INVALID_LIST;
  ele->direct = false;

  return ele;
}

inkapi void
INKParentProxyEleDestroy(INKParentProxyEle * ele)
{
  if (ele) {
    INKPdSsFormatDestroy(&(ele->parent_info));
    if (ele->proxy_list)
      INKDomainListDestroy(ele->proxy_list);
    xfree(ele);
  }

  return;
}

/*-------------------------------------------------------------
 * INKPartitionEle
 *-------------------------------------------------------------*/
inkapi INKPartitionEle *
INKPartitionEleCreate()
{
  INKPartitionEle *ele;

  ele = (INKPartitionEle *) xmalloc(sizeof(INKPartitionEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_PARTITION;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->partition_num = 0;
  ele->scheme = INK_PARTITION_UNDEFINED;
  ele->partition_size = 0;
  ele->size_format = INK_SIZE_FMT_UNDEFINED;

  return ele;
}

inkapi void
INKPartitionEleDestroy(INKPartitionEle * ele)
{
  if (ele) {
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKPluginEle
 *-------------------------------------------------------------*/
inkapi INKPluginEle *
INKPluginEleCreate()
{
  INKPluginEle *ele;

  ele = (INKPluginEle *) xmalloc(sizeof(INKPluginEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_PLUGIN;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->name = NULL;
  ele->args = INK_INVALID_LIST;

  return ele;
}

inkapi void
INKPluginEleDestroy(INKPluginEle * ele)
{
  if (ele) {
    if (ele->name)
      xfree(ele->name);
    if (ele->args)
      INKStringListDestroy(ele->args);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKRemapEle
 *-------------------------------------------------------------*/
INKRemapEle *
INKRemapEleCreate(INKRuleTypeT type)
{
  INKRemapEle *ele;

  if (type != INK_REMAP_MAP &&
      type != INK_REMAP_REVERSE_MAP &&
      type != INK_REMAP_REDIRECT && type != INK_REMAP_REDIRECT_TEMP && type != INK_TYPE_UNDEFINED)
    return NULL;

  ele = (INKRemapEle *) xmalloc(sizeof(INKRemapEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = type;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->map = true;
  ele->from_scheme = INK_SCHEME_UNDEFINED;
  ele->from_host = NULL;
  ele->from_port = INK_INVALID_PORT;
  ele->from_path_prefix = NULL;
  ele->to_scheme = INK_SCHEME_UNDEFINED;
  ele->to_host = NULL;
  ele->to_port = INK_INVALID_PORT;
  ele->to_path_prefix = NULL;
  ele->mixt = INK_MIXT_UNDEFINED;

  return ele;
}

void
INKRemapEleDestroy(INKRemapEle * ele)
{
  if (ele) {
    if (ele->from_host)
      xfree(ele->from_host);
    if (ele->from_path_prefix)
      xfree(ele->from_path_prefix);
    if (ele->to_host)
      xfree(ele->to_host);
    if (ele->to_path_prefix)
      xfree(ele->to_path_prefix);
    xfree(ele);
  }
}

/*-------------------------------------------------------------
 * INKSocksEle
 *-------------------------------------------------------------*/
INKSocksEle *
INKSocksEleCreate(INKRuleTypeT type)
{
  INKSocksEle *ele;
  ele = (INKSocksEle *) xmalloc(sizeof(INKSocksEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = type;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->ip_addrs = INK_INVALID_LIST;
  ele->dest_ip_addr = INK_INVALID_IP_ADDR;
  ele->socks_servers = INK_INVALID_LIST;
  ele->rr = INK_RR_NONE;
  ele->username = NULL;
  ele->password = NULL;

  return ele;
}

void
INKSocksEleDestroy(INKSocksEle * ele)
{
  if (ele) {
    if (ele->ip_addrs)
      INKIpAddrListDestroy(ele->ip_addrs);
    if (ele->dest_ip_addr)
      INKIpAddrEleDestroy(ele->dest_ip_addr);
    if (ele->socks_servers)
      INKDomainListDestroy(ele->socks_servers);
    if (ele->username)
      xfree(ele->username);
    if (ele->password)
      xfree(ele->password);
    xfree(ele);
  }
}

/*-------------------------------------------------------------
 * INKSplitDnsEle
 *-------------------------------------------------------------*/
INKSplitDnsEle *
INKSplitDnsEleCreate()
{
  INKSplitDnsEle *ele;
  ele = (INKSplitDnsEle *) xmalloc(sizeof(INKSplitDnsEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_SPLIT_DNS;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->pd_type = INK_PD_UNDEFINED;
  ele->pd_val = NULL;
  ele->dns_servers_addrs = INK_INVALID_LIST;
  ele->def_domain = NULL;
  ele->search_list = INK_INVALID_LIST;

  return ele;
}

void
INKSplitDnsEleDestroy(INKSplitDnsEle * ele)
{
  if (ele) {
    if (ele->pd_val)
      xfree(ele->pd_val);
    if (ele->dns_servers_addrs)
      INKDomainListDestroy(ele->dns_servers_addrs);
    if (ele->def_domain)
      xfree(ele->def_domain);
    if (ele->search_list)
      INKDomainListDestroy(ele->search_list);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKStorageEle
 *-------------------------------------------------------------*/
INKStorageEle *
INKStorageEleCreate()
{
  INKStorageEle *ele;
  ele = (INKStorageEle *) xmalloc(sizeof(INKStorageEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_STORAGE;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->pathname = NULL;
  ele->size = -1;

  return ele;
}

void
INKStorageEleDestroy(INKStorageEle * ele)
{
  if (ele) {
    if (ele->pathname)
      xfree(ele->pathname);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKUpdateEle
 *-------------------------------------------------------------*/
INKUpdateEle *
INKUpdateEleCreate()
{
  INKUpdateEle *ele;
  ele = (INKUpdateEle *) xmalloc(sizeof(INKUpdateEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_UPDATE_URL;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->url = NULL;
  ele->headers = INK_INVALID_LIST;
  ele->offset_hour = -1;
  ele->interval = -1;
  ele->recursion_depth = 0;

  return ele;
}

void
INKUpdateEleDestroy(INKUpdateEle * ele)
{
  if (ele) {
    if (ele->url)
      xfree(ele->url);
    if (ele->headers)
      INKStringListDestroy(ele->headers);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKVirtIpAddrEle
 *-------------------------------------------------------------*/
INKVirtIpAddrEle *
INKVirtIpAddrEleCreate()
{
  INKVirtIpAddrEle *ele;
  ele = (INKVirtIpAddrEle *) xmalloc(sizeof(INKVirtIpAddrEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_VADDRS;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->intr = NULL;
  ele->sub_intr = -1;
  ele->ip_addr = INK_INVALID_IP_ADDR;

  return ele;
}

void
INKVirtIpAddrEleDestroy(INKVirtIpAddrEle * ele)
{
  if (ele) {
    if (ele->intr)
      xfree(ele->intr);
    if (ele->ip_addr)
      xfree(ele->ip_addr);
    xfree(ele);
  }
}

#if defined(OEM)
/*-------------------------------------------------------------
 * INKRmServerEle
 *-------------------------------------------------------------*/
INKRmServerEle *
INKRmServerEleCreate(INKRuleTypeT type)
{
  INKRmServerEle *ele;
  ele = (INKRmServerEle *) xmalloc(sizeof(INKRmServerEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = type;
  ele->Vname = NULL;
  ele->str_val = NULL;
  ele->int_val = -1;
  return ele;
}

void
INKRmServerEleDestroy(INKRmServerEle * ele)
{
  if (ele) {
    if (ele->Vname)
      xfree(ele->Vname);
    if (ele->str_val)
      xfree(ele->str_val);
    xfree(ele);
  }
}

/*-------------------------------------------------------------
 * INKVscanEle
 *-------------------------------------------------------------*/
INKVscanEle *
INKVscanEleCreate()
{
  INKVscanEle *ele;
  ele = (INKVscanEle *) xmalloc(sizeof(INKVscanEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_VSCAN;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->attr_name = NULL;
  ele->attr_val = NULL;

  return ele;
}

void
INKVscanEleDestroy(INKVscanEle * ele)
{
  if (ele) {
    if (ele->attr_name)
      xfree(ele->attr_name);
    if (ele->attr_val)
      xfree(ele->attr_val);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKVsTrustedHostEle
 *-------------------------------------------------------------*/
INKVsTrustedHostEle *
INKVsTrustedHostEleCreate()
{
  INKVsTrustedHostEle *ele;
  ele = (INKVsTrustedHostEle *) xmalloc(sizeof(INKVsTrustedHostEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_VS_TRUSTED_HOST;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->hostname = NULL;

  return ele;
}

void
INKVsTrustedHostEleDestroy(INKVsTrustedHostEle * ele)
{
  if (ele) {
    if (ele->hostname)
      xfree(ele->hostname);
    xfree(ele);
  }
  return;
}

/*-------------------------------------------------------------
 * INKVsExtensionEle
 *-------------------------------------------------------------*/
INKVsExtensionEle *
INKVsExtensionEleCreate()
{
  INKVsExtensionEle *ele;
  ele = (INKVsExtensionEle *) xmalloc(sizeof(INKVsExtensionEle));
  if (!ele)
    return NULL;

  ele->cfg_ele.type = INK_VS_EXTENSION;
  ele->cfg_ele.error = INK_ERR_OKAY;
  ele->file_ext = NULL;

  return ele;
}

void
INKVsExtensionEleDestroy(INKVsExtensionEle * ele)
{
  if (ele) {
    if (ele->file_ext)
      xfree(ele->file_ext);
    xfree(ele);
  }
  return;
}

#endif
/***************************************************************************
 * API Core
 ***************************************************************************/

/*--- statistics operations ----------------------------------------------- */
inkapi INKError
INKStatsReset()
{
  return StatsReset();
}

/*--- variable operations ------------------------------------------------- */
/* Call the CfgFileIO variable operations */

inkapi INKError
INKRecordGet(char *rec_name, INKRecordEle * rec_val)
{
  return MgmtRecordGet(rec_name, rec_val);
}

INKError
INKRecordGetInt(char *rec_name, INKInt * int_val)
{
  INKError ret = INK_ERR_OKAY;

  INKRecordEle *ele = INKRecordEleCreate();
  ret = MgmtRecordGet(rec_name, ele);
  if (ret != INK_ERR_OKAY)
    goto END;

  *int_val = ele->int_val;

END:
  INKRecordEleDestroy(ele);
  return ret;
}

INKError
INKRecordGetCounter(char *rec_name, INKCounter * counter_val)
{
  INKError ret;

  INKRecordEle *ele = INKRecordEleCreate();
  ret = MgmtRecordGet(rec_name, ele);
  if (ret != INK_ERR_OKAY)
    goto END;
  *counter_val = ele->counter_val;

END:
  INKRecordEleDestroy(ele);
  return ret;
}

INKError
INKRecordGetFloat(char *rec_name, INKFloat * float_val)
{
  INKError ret;

  INKRecordEle *ele = INKRecordEleCreate();
  ret = MgmtRecordGet(rec_name, ele);
  if (ret != INK_ERR_OKAY)
    goto END;
  *float_val = ele->float_val;

END:
  INKRecordEleDestroy(ele);
  return ret;
}

INKError
INKRecordGetString(char *rec_name, INKString * string_val)
{
  INKError ret;
  char *str;
  size_t str_size = 0;

  INKRecordEle *ele = INKRecordEleCreate();
  ret = MgmtRecordGet(rec_name, ele);
  if (ret != INK_ERR_OKAY)
    goto END;

  str_size = strlen(ele->string_val) + 1;
  str = (char *) xmalloc(sizeof(char) * str_size);
  if (!str)
    return INK_ERR_SYS_CALL;
  ink_strncpy(str, ele->string_val, str_size);
  *string_val = str;

END:
  INKRecordEleDestroy(ele);
  return ret;
}


/*-------------------------------------------------------------------------
 * INKRecordGetMlt 
 *-------------------------------------------------------------------------
 * Purpose: Retrieves list of record values specified in the rec_names list
 * Input: rec_names - list of record names to retrieve
 *        rec_vals  - queue of INKRecordEle* that correspons to rec_names 
 * Output: If at any point, while retrieving one of the records there's a 
 *         a failure then the entire process is aborted, all the allocated 
 *         INKRecordEle's are deallocated and INK_ERR_FAIL is returned.
 * Note: rec_names is not freed; if function is successful, the rec_names
 *       list is unchanged!
 * 
 * IS THIS FUNCTION AN ATOMIC TRANSACTION? Technically, all the variables
 * requested should refer to the same config file. But a lock is only 
 * put on each variable it is looked up. Need to be able to lock 
 * a file while retrieving all the requested records!  
 */

inkapi INKError
INKRecordGetMlt(INKStringList rec_names, INKList rec_vals)
{
  INKRecordEle *ele;
  char *rec_name;
  int num_recs, i, j;
  INKError ret;

  if (!rec_names || !rec_vals)
    return INK_ERR_PARAMS;

  num_recs = queue_len((LLQ *) rec_names);
  for (i = 0; i < num_recs; i++) {
    rec_name = (char *) dequeue((LLQ *) rec_names);     // remove name from list 
    if (!rec_name)
      return INK_ERR_PARAMS;    // NULL is invalid record name

    ele = INKRecordEleCreate();

    ret = MgmtRecordGet(rec_name, ele);
    enqueue((LLQ *) rec_names, rec_name);       // return name to list

    if (ret != INK_ERR_OKAY) {  // RecordGet failed
      // need to free all the ele's allocated by MgmtRecordGet so far
      INKRecordEleDestroy(ele);
      for (j = 0; j < i; j++) {
        ele = (INKRecordEle *) dequeue((LLQ *) rec_vals);
        if (ele)
          INKRecordEleDestroy(ele);
      }
      return ret;
    }
    enqueue((LLQ *) rec_vals, ele);     // all is good; add ele to end of list    
  }

  return INK_ERR_OKAY;
}


inkapi INKError
INKRecordSet(char *rec_name, INKString val, INKActionNeedT * action_need)
{
  return MgmtRecordSet(rec_name, val, action_need);
}


inkapi INKError
INKRecordSetInt(char *rec_name, INKInt int_val, INKActionNeedT * action_need)
{
  return MgmtRecordSetInt(rec_name, int_val, action_need);
}

inkapi INKError
INKRecordSetCounter(char *rec_name, INKCounter counter_val, INKActionNeedT * action_need)
{
  return MgmtRecordSetCounter(rec_name, counter_val, action_need);
}

inkapi INKError
INKRecordSetFloat(char *rec_name, INKFloat float_val, INKActionNeedT * action_need)
{
  return MgmtRecordSetFloat(rec_name, float_val, action_need);
}

inkapi INKError
INKRecordSetString(char *rec_name, INKString str_val, INKActionNeedT * action_need)
{
  return MgmtRecordSetString(rec_name, str_val, action_need);
}


/*-------------------------------------------------------------------------
 * INKRecordSetMlt 
 *-------------------------------------------------------------------------
 * Basically iterates through each RecordEle in rec_list and calls the 
 * appropriate "MgmtRecordSetxx" function for that record
 * Input: rec_list - queue of INKRecordEle*; each INKRecordEle* must have
 *        a valid record name (remains unchanged on return)
 * Output: if there is an error during the setting of one of the variables then
 *         will continue to try to set the other variables. Error response will
 *         indicate though that not all set operations were successful. 
 *         INK_ERR_OKAY is returned if all the records are set successfully
 * Note: Determining the action needed is more complex b/c need to keep 
 * track of which record change is the most drastic out of the group of 
 * records; action_need will be set to the most severe action needed of 
 * all the "Set" calls  
 */
inkapi INKError
INKRecordSetMlt(INKList rec_list, INKActionNeedT * action_need)
{
  int num_recs, ret, i;
  INKRecordEle *ele;
  INKError status = INK_ERR_OKAY;
  INKActionNeedT top_action_req = INK_ACTION_UNDEFINED;

  if (!rec_list || !action_need)
    return INK_ERR_PARAMS;

  num_recs = queue_len((LLQ *) rec_list);

  for (i = 0; i < num_recs; i++) {
    ele = (INKRecordEle *) dequeue((LLQ *) rec_list);
    if (ele) {
      switch (ele->rec_type) {
      case INK_REC_INT:
        ret = MgmtRecordSetInt(ele->rec_name, ele->int_val, action_need);
        break;
      case INK_REC_COUNTER:
        ret = MgmtRecordSetCounter(ele->rec_name, ele->counter_val, action_need);
        break;
      case INK_REC_FLOAT:
        ret = MgmtRecordSetFloat(ele->rec_name, ele->float_val, action_need);
        break;
      case INK_REC_STRING:
        ret = MgmtRecordSetString(ele->rec_name, ele->string_val, action_need);
        break;
      default:
        ret = INK_ERR_FAIL;
        break;
      };                        /* end of switch (ele->rec_type) */
      if (ret != INK_ERR_OKAY)
        status = INK_ERR_FAIL;

      // keep track of most severe action; reset if needed
      // the INKACtionNeedT should be listed such that most severe actions have
      // a lower number (so most severe action == 0)
      if (*action_need < top_action_req)        // a more severe action
        top_action_req = *action_need;
    }
    enqueue((LLQ *) rec_list, ele);
  }

  // set the action_need to be the most sever action needed of all the "set" calls
  *action_need = top_action_req;

  return status;
}

/*--- api initialization and shutdown -------------------------------------*/
inkapi INKError
INKInit(char *socket_path)
{
  return Init(socket_path);
}

inkapi INKError
INKTerminate()
{
  return Terminate();
}

/*--- plugin initialization -----------------------------------------------*/
inkexp extern void
INKPluginInit(int argc, const char *argv[])
{
}

/*--- network operations --------------------------------------------------*/
inkapi INKError
INKConnect(INKIpAddr ip_addr, int port)
{
  return INK_ERR_OKAY;
}
inkapi INKError
INKDisconnectCbRegister(INKDisconnectFunc * func, void *data)
{
  return INK_ERR_OKAY;
}
inkapi INKError
INKDisconnectRetrySet(int retries, int retry_sleep_msec)
{
  return INK_ERR_OKAY;
}
inkapi INKError
INKDisconnect()
{
  return INK_ERR_OKAY;
}

/*--- control operations --------------------------------------------------*/
/* NOTE: these operations are wrappers that make direct calls to the CoreAPI */

/* INKProxyStateGet: get the proxy state (on/off)
 * Input:  <none>
 * Output: proxy state (on/off)
 */
inkapi INKProxyStateT
INKProxyStateGet()
{
  return ProxyStateGet();
}

/* INKProxyStateSet: set the proxy state (on/off)
 * Input:  proxy_state - set to on/off
 *         clear - start TS with cache clearing option, 
 *                 when stopping TS should always be INK_CACHE_CLEAR_OFF
 * Output: INKError
 */
inkapi INKError
INKProxyStateSet(INKProxyStateT proxy_state, INKCacheClearT clear)
{
  return ProxyStateSet(proxy_state, clear);
}

/* INKReconfigure: tell traffic_server to re-read its configuration files
 * Input:  <none>
 * Output: INKError
 */
inkapi INKError
INKReconfigure()
{
  return Reconfigure();
}

/* INKRestart: restarts Traffic Server
 * Input:  cluster - local or cluster-wide
 * Output: INKError
 */
inkapi INKError
INKRestart(bool cluster)
{
  return Restart(cluster);
}

/* INKHardRestart: a traffic_cop restart (restarts TM and TS),
 * essentially does a "start_traffic_server"/"stop_traffic_server" sequence 
 * Input:  <none>
 * Output: INKError
 * Note: only for remote API clients
 */
/* CAN ONLY BE IMPLEMENTED ON THE REMOTE SIDE !!! */
inkapi INKError
INKHardRestart()
{
  return HardRestart();         // should return INK_ERR_FAIL
}

/* INKActionDo: based on INKActionNeedT, will take appropriate action
 * Input: action - action that needs to be taken
 * Output: INKError
 */
inkapi INKError
INKActionDo(INKActionNeedT action)
{
  INKError ret;

  switch (action) {
  case INK_ACTION_SHUTDOWN:
    ret = HardRestart();
    break;
  case INK_ACTION_RESTART:
    ret = Restart(true);        // cluster wide by default?
    break;
  case INK_ACTION_RECONFIGURE:
    ret = Reconfigure();
    break;
  case INK_ACTION_DYNAMIC:
    /* do nothing - change takes effect immediately */
    return INK_ERR_OKAY;
  default:
    return INK_ERR_FAIL;
  }

  return ret;
}


/*--- diags output operations ---------------------------------------------*/
inkapi void
INKDiags(INKDiagsT mode, const char *fmt, ...)
{
  // need to find way to pass arguments to the function
  va_list ap;

  va_start(ap, fmt);            // initialize the argument pointer ap
  Diags(mode, fmt, ap);
  va_end(ap);

  return;
}

/* NOTE: user must deallocate the memory for the string returned */
char *
INKGetErrorMessage(INKError err_id)
{
  char msg[1024];               // need to define a MAX_ERR_MSG_SIZE???
  char *err_msg = NULL;

  switch (err_id) {
  case INK_ERR_OKAY:
    snprintf(msg, sizeof(msg), "[%d] Everything's looking good.", err_id);
    break;
  case INK_ERR_READ_FILE:      /* Error occur in reading file */
    snprintf(msg, sizeof(msg), "[%d] Unable to find/open file for reading.", err_id);
    break;
  case INK_ERR_WRITE_FILE:     /* Error occur in writing file */
    snprintf(msg, sizeof(msg), "[%d] Unable to find/open file for writing.", err_id);
    break;
  case INK_ERR_PARSE_CONFIG_RULE:      /* Error in parsing configuration file */
    snprintf(msg, sizeof(msg), "[%d] Error parsing configuration file.", err_id);
    break;
  case INK_ERR_INVALID_CONFIG_RULE:    /* Invalid Configuration Rule */
    snprintf(msg, sizeof(msg), "[%d] Invalid configuration rule reached.", err_id);
    break;
  case INK_ERR_NET_ESTABLISH:
    snprintf(msg, sizeof(msg), "[%d] Error establishing socket conenction.", err_id);
    break;
  case INK_ERR_NET_READ:       /* Error reading from socket */
    snprintf(msg, sizeof(msg), "[%d] Error reading from socket.", err_id);
    break;
  case INK_ERR_NET_WRITE:      /* Error writing to socket */
    snprintf(msg, sizeof(msg), "[%d] Error writing to socket.", err_id);
    break;
  case INK_ERR_NET_EOF:        /* Hit socket EOF */
    snprintf(msg, sizeof(msg), "[%d] Reached socket EOF.", err_id);
    break;
  case INK_ERR_NET_TIMEOUT:    /* Timed out waiting for socket read */
    snprintf(msg, sizeof(msg), "[%d] Timed out waiting for socket read.", err_id);
    break;
  case INK_ERR_SYS_CALL:       /* Error in sys/utility call, eg.malloc */
    snprintf(msg, sizeof(msg), "[%d] Error in basic system/utility call.", err_id);
    break;
  case INK_ERR_PARAMS:         /* Invalid parameters for a fn */
    snprintf(msg, sizeof(msg), "[%d] Invalid parameters passed into function call.", err_id);
    break;
  case INK_ERR_FAIL:
    snprintf(msg, sizeof(msg), "[%d] Generic Fail message (ie. CoreAPI call).", err_id);
    break;

  default:
    snprintf(msg, sizeof(msg), "[%d] Invalid error type.", err_id);
    break;
  }

  err_msg = xstrdup(msg);
  return err_msg;
}


/*--- password operations -------------------------------------------------*/
inkapi INKError
INKEncryptPassword(char *passwd, char **e_passwd)
{

  INK_DIGEST_CTX md5_context;
  char passwd_md5[16];
  char *passwd_md5_str;
  int passwd_md5_str_len = 32;

  ink_debug_assert(passwd);
  ink_debug_assert(INK_ENCRYPT_PASSWD_LEN <= passwd_md5_str_len);

  const size_t md5StringSize = (passwd_md5_str_len + 1) * sizeof(char);
  passwd_md5_str = (char *) xmalloc(md5StringSize);
  if (!passwd_md5_str)
    return INK_ERR_FAIL;

  ink_code_incr_md5_init(&md5_context);
  ink_code_incr_md5_update(&md5_context, passwd, strlen(passwd));
  ink_code_incr_md5_final(passwd_md5, &md5_context);
  ink_code_md5_stringify(passwd_md5_str, md5StringSize, passwd_md5);

  // use only a subset of the MD5 string
  passwd_md5_str[INK_ENCRYPT_PASSWD_LEN] = '\0';
  *e_passwd = passwd_md5_str;

  return INK_ERR_OKAY;
}

inkapi INKError
INKEncryptToFile(char *passwd, char *filepath)
{
  return EncryptToFile(passwd, filepath);
}

/*--- direct file operations ----------------------------------------------*/
inkapi INKError
INKConfigFileRead(INKFileNameT file, char **text, int *size, int *version)
{
  return ReadFile(file, text, size, version);
}

inkapi INKError
INKConfigFileWrite(INKFileNameT file, char *text, int size, int version)
{
  return WriteFile(file, text, size, version);
}


/* ReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 * Output: INKError   - INK_ERR_OKAY if succeed, INK_ERR_FAIL otherwise
 * Obsolete:  inkapi INKError INKReadFromUrl (char *url, char **text, int *size);
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.inktomi.com:80/products/network/index.html
 *       - http://www.inktomi.com/products/network/index.html
 *       - http://www.inktomi.com/products/network/
 *       - http://www.inktomi.com/
 *       - http://www.inktomi.com
 *       - www.inktomi.com
 * NOTE: header and headerSize can be NULL
 */
inkapi INKError
INKReadFromUrl(char *url, char **header, int *headerSize, char **body, int *bodySize)
{
  //return ReadFromUrl(url, header, headerSize, body, bodySize);
  return INKReadFromUrlEx(url, header, headerSize, body, bodySize, URL_TIMEOUT);

}

inkapi INKError
INKReadFromUrlEx(char *url, char **header, int *headerSize, char **body, int *bodySize, int timeout)
{
  int hFD = -1;
  char *httpHost = NULL;
  char *httpPath = NULL;
  int httpPort = HTTP_PORT;
  int bufsize = URL_BUFSIZE;
  char buffer[URL_BUFSIZE];
  char request[BUFSIZE];
  char *hdr_temp;
  char *bdy_temp;
  INKError status = INK_ERR_OKAY;

  // Sanity check
  if (!url)
    return INK_ERR_FAIL;
  if (timeout < 0) {
    timeout = URL_TIMEOUT;
  }
  // Chop the protocol part, if it exists
  char *doubleSlash = strstr(url, "//");
  if (doubleSlash) {
    url = doubleSlash + 2;      // advance two positions to get rid of leading '//'
  }
  // the path starts after the first occurrence of '/'
  char *tempPath = strstr(url, "/");
  char *host_and_port;
  if (tempPath) {
    host_and_port = xstrndup(url, strlen(url) - strlen(tempPath));
    tempPath += 1;              // advance one position to get rid of leading '/'
    httpPath = xstrdup(tempPath);
  } else {
    host_and_port = xstrdup(url);
    httpPath = xstrdup("");
  }

  // the port proceed by a ":", if it exists
  char *colon = strstr(host_and_port, ":");
  if (colon) {
    httpHost = xstrndup(host_and_port, strlen(host_and_port) - strlen(colon));
    colon += 1;                 // advance one position to get rid of leading ':'
    httpPort = ink_atoi(colon);
    if (httpPort <= 0)
      httpPort = HTTP_PORT;
  } else {
    httpHost = xstrdup(host_and_port);
  }
  xfree(host_and_port);

  hFD = connectDirect(httpHost, httpPort, timeout);
  if (hFD == -1) {
    status = INK_ERR_NET_ESTABLISH;
    goto END;
  }

  /* sending the HTTP request via the established socket */
  ink_snprintf(request, BUFSIZE, "http://%s:%d/%s", httpHost, httpPort, httpPath);
  if ((status = sendHTTPRequest(hFD, request, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  memset(buffer, 0, bufsize);   /* empty the buffer */
  if ((status = readHTTPResponse(hFD, buffer, bufsize, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  if ((status = parseHTTPResponse(buffer, &hdr_temp, headerSize, &bdy_temp, bodySize))
      != INK_ERR_OKAY)
    goto END;

  if (header && headerSize)
    *header = xstrndup(hdr_temp, *headerSize);
  *body = xstrndup(bdy_temp, *bodySize);

END:
  if (httpHost)
    xfree(httpHost);
  if (httpPath)
    xfree(httpPath);
  return status;
}

/*--- cache inspector operations -------------------------------------------*/

inkapi INKError
INKLookupFromCacheUrl(INKString url, INKString * info)
{
  INKError err = INK_ERR_OKAY;
  int fd;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout = URL_TIMEOUT;
  INKInt ts_port = 8080;

  if ((err = INKRecordGetInt("proxy.config.http.server_port", &ts_port)) != INK_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = INK_ERR_FAIL;
    goto END;
  }
  ink_snprintf(request, BUFSIZE, "http://{cache}/lookup_url?url=%s", url);
  if ((err = sendHTTPRequest(fd, request, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != INK_ERR_OKAY)
    goto END;

  *info = xstrndup(body, bdy_size);

END:
  return err;
}

inkapi INKError
INKLookupFromCacheUrlRegex(INKString url_regex, INKString * list)
{
  INKError err = INK_ERR_OKAY;
  int fd = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout = -1;
  INKInt ts_port = 8080;

  if ((err = INKRecordGetInt("proxy.config.http.server_port", &ts_port)) != INK_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = INK_ERR_FAIL;
    goto END;
  }
  ink_snprintf(request, BUFSIZE, "http://{cache}/lookup_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != INK_ERR_OKAY)
    goto END;

  *list = xstrndup(body, bdy_size);
END:
  return err;
}

inkapi INKError
INKDeleteFromCacheUrl(INKString url, INKString * info)
{
  INKError err = INK_ERR_OKAY;
  int fd = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout = URL_TIMEOUT;
  INKInt ts_port = 8080;

  if ((err = INKRecordGetInt("proxy.config.http.server_port", &ts_port)) != INK_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = INK_ERR_FAIL;
    goto END;
  }
  ink_snprintf(request, BUFSIZE, "http://{cache}/delete_url?url=%s", url);
  if ((err = sendHTTPRequest(fd, request, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != INK_ERR_OKAY)
    goto END;

  *info = xstrndup(body, bdy_size);

END:
  return err;
}

inkapi INKError
INKDeleteFromCacheUrlRegex(INKString url_regex, INKString * list)
{
  INKError err = INK_ERR_OKAY;
  int fd = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout = -1;
  INKInt ts_port = 8080;

  if ((err = INKRecordGetInt("proxy.config.http.server_port", &ts_port)) != INK_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = INK_ERR_FAIL;
    goto END;
  }
  ink_snprintf(request, BUFSIZE, "http://{cache}/delete_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != INK_ERR_OKAY)
    goto END;

  *list = xstrndup(body, bdy_size);
END:
  return err;
}

inkapi INKError
INKInvalidateFromCacheUrlRegex(INKString url_regex, INKString * list)
{
  INKError err = INK_ERR_OKAY;
  int fd = -1;
  char request[BUFSIZE];
  char response[URL_BUFSIZE];
  char *header;
  char *body;
  int hdr_size;
  int bdy_size;
  int timeout = -1;
  INKInt ts_port = 8080;

  if ((err = INKRecordGetInt("proxy.config.http.server_port", &ts_port)) != INK_ERR_OKAY)
    goto END;

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = INK_ERR_FAIL;
    goto END;
  }
  ink_snprintf(request, BUFSIZE, "http://{cache}/invalidate_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (inku64) timeout)) != INK_ERR_OKAY)
    goto END;

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != INK_ERR_OKAY)
    goto END;

  *list = xstrndup(body, bdy_size);
END:
  return err;
}

/*--- snapshot operations -------------------------------------------------*/
inkapi INKError
INKSnapshotTake(char *snapshot_name)
{
  return SnapshotTake(snapshot_name);
}

inkapi INKError
INKSnapshotRestore(char *snapshot_name)
{
  return SnapshotRestore(snapshot_name);
}

inkapi INKError
INKSnapshotRemove(char *snapshot_name)
{
  return SnapshotRemove(snapshot_name);
}

inkapi INKError
INKSnapshotGetMlt(INKStringList snapshots)
{
  return SnapshotGetMlt((LLQ *) snapshots);
}

/*--- events --------------------------------------------------------------*/
inkapi INKError
INKEventSignal(char *event_name, ...)
{
  va_list ap;
  INKError ret;

  va_start(ap, event_name);     // initialize the argument pointer ap
  ret = EventSignal(event_name, ap);
  va_end(ap);
  return ret;
}

inkapi INKError
INKEventResolve(char *event_name)
{
  return EventResolve(event_name);
}

inkapi INKError
INKActiveEventGetMlt(INKList active_events)
{
  return ActiveEventGetMlt((LLQ *) active_events);
}

inkapi INKError
INKEventIsActive(char *event_name, bool * is_current)
{
  return EventIsActive(event_name, is_current);
}

inkapi INKError
INKEventSignalCbRegister(char *event_name, INKEventSignalFunc func, void *data)
{
  return EventSignalCbRegister(event_name, func, data);
}

inkapi INKError
INKEventSignalCbUnregister(char *event_name, INKEventSignalFunc func)
{
  return EventSignalCbUnregister(event_name, func);
}

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- abstracted file operations ------------------------------------------*/

/* calls teh CfgContext class constructor */
inkapi INKCfgContext
INKCfgContextCreate(INKFileNameT file)
{
  return ((void *) CfgContextCreate(file));
}

/* calls the CfgContext class destructor */
inkapi INKError
INKCfgContextDestroy(INKCfgContext ctx)
{
  return (CfgContextDestroy((CfgContext *) ctx));
}

inkapi INKError
INKCfgContextCommit(INKCfgContext ctx, INKActionNeedT * action_need, INKIntList errRules)
{
  return (CfgContextCommit((CfgContext *) ctx, (LLQ *) errRules));
}

inkapi INKError
INKCfgContextGet(INKCfgContext ctx)
{
  return (CfgContextGet((CfgContext *) ctx));
}

/*--- helper operations ---------------------------------------------------*/
/* returns number of ele's in the INKCfgContext */
int
INKCfgContextGetCount(INKCfgContext ctx)
{
  return CfgContextGetCount((CfgContext *) ctx);
}

/* user must typecast the INKCfgEle to appropriate INKEle before using */
INKCfgEle *
INKCfgContextGetEleAt(INKCfgContext ctx, int index)
{
  return CfgContextGetEleAt((CfgContext *) ctx, index);
}

/* iterator */
INKCfgEle *
INKCfgContextGetFirst(INKCfgContext ctx, INKCfgIterState * state)
{
  return CfgContextGetFirst((CfgContext *) ctx, state);
}

INKCfgEle *
INKCfgContextGetNext(INKCfgContext ctx, INKCfgIterState * state)
{
  return CfgContextGetNext((CfgContext *) ctx, state);
}

INKError
INKCfgContextMoveEleUp(INKCfgContext ctx, int index)
{
  return CfgContextMoveEleUp((CfgContext *) ctx, index);
}

INKError
INKCfgContextMoveEleDown(INKCfgContext ctx, int index)
{
  return CfgContextMoveEleDown((CfgContext *) ctx, index);
}


INKError
INKCfgContextAppendEle(INKCfgContext ctx, INKCfgEle * ele)
{
  return CfgContextAppendEle((CfgContext *) ctx, ele);
}

INKError
INKCfgContextInsertEleAt(INKCfgContext ctx, INKCfgEle * ele, int index)
{
  return CfgContextInsertEleAt((CfgContext *) ctx, ele, index);
}

INKError
INKCfgContextRemoveEleAt(INKCfgContext ctx, int index)
{
  return CfgContextRemoveEleAt((CfgContext *) ctx, index);
}

INKError
INKCfgContextRemoveAll(INKCfgContext ctx)
{
  return CfgContextRemoveAll((CfgContext *) ctx);
}

/* checks if the fields in the ele are all valid */
bool
INKIsValid(INKCfgEle * ele)
{
  CfgEleObj *ele_obj;

  if (!ele)
    return false;

  ele_obj = create_ele_obj_from_ele(ele);
  return (ele_obj->isValid());
}


/*--- External FTP tcl script operations --------------------------------*/

/* Process forking function for ftp.tcl helper */

static int
ftpProcessSpawn(char *args[], char *output)
{
  int status = 0;

#ifndef _WIN32
  char buffer[1024];
  size_t nbytes;
  int stdoutPipe[2];
  pid_t pid;
  size_t output_size = 4096;
  size_t count = 0;

  if (pipe(stdoutPipe) == -1)
    fprintf(stderr, "[ftpProcessSpawn] unable to create stdout pipe\n");

  pid = fork();
  if (pid == 0) {               // child process
    dup2(stdoutPipe[1], STDOUT_FILENO);
    close(stdoutPipe[0]);

    pid = execv(args[0], &args[0]);
    if (pid == -1) {
      fprintf(stderr, "[ftpProcessSpawn] unable to execv [%s,%s...]\n", args[0], args[1]);
    }
    _exit(1);
  } else if (pid == -1) {       // failed to create child process
    fprintf(stderr, "[ftpProcessSpawn] unable to fork [%d '%s']\n", errno, strerror(errno));
    status = 1;
  } else {                      // parent process
    close(stdoutPipe[1]);
    /* read the output from child script process */
    while ((nbytes = read(stdoutPipe[0], buffer, 1024))) {
      if ((count + nbytes) < output_size) {
        strncpy(&output[count], buffer, nbytes);
        count += nbytes;
      } else {
        break;
      }
    }
    close(stdoutPipe[0]);

    waitpid(pid, &status, 0);
    if (status) {
      fprintf(stderr, "[ftpProcessSpawn] script %s returns non-zero status '%d'", args[0], status);
      status = -1;
    }
  }

#endif
  return status;
}

/* Process forking function for tcl_checker.sh helper */

static int
tclCheckProcessSpawn(char *args[], char *output)
{
  int status = 0;

#ifndef _WIN32
  char buffer[1024];
  size_t nbytes;
  int stdoutPipe[2];
  pid_t pid;
  size_t output_size = 256;
  size_t count = 0;

  if (pipe(stdoutPipe) == -1)
    fprintf(stderr, "[tclCheckProcessSpawn] unable to create stdout pipe\n");

  pid = fork();
  if (pid == 0) {               // child process
    dup2(stdoutPipe[1], STDOUT_FILENO);
    close(stdoutPipe[0]);

    pid = execv(args[0], &args[0]);
    if (pid == -1) {
      fprintf(stderr, "[tclCheckProcessSpawn] unable to execv [%s,%s...]\n", args[0], args[1]);
    }
    _exit(1);
  } else if (pid == -1) {       // failed to create child process
    fprintf(stderr, "[tclCheckProcessSpawn] unable to fork [%d '%s']\n", errno, strerror(errno));
    status = 1;
  } else {                      // parent process
    close(stdoutPipe[1]);
    /* read the output from child script process */
    while ((nbytes = read(stdoutPipe[0], buffer, 1024))) {
      if ((count + nbytes) < output_size) {
        strncpy(&output[count], buffer, nbytes);
        count += nbytes;
      } else {
        break;
      }
    }
    close(stdoutPipe[0]);

    waitpid(pid, &status, 0);
    if (status) {
      fprintf(stderr, "[tclCheckProcessSpawn] script %s returns non-zero status '%d'", args[0], status);
      status = -1;
    }
  }

#endif
  return status;
}

/* Snapshot Interface-centric function */

inkapi INKError
INKMgmtFtp(char *ftpCmd, char *ftp_server_name, char *ftp_login, char *ftp_password, char *local, char *remote,
           char *output)
{
  char script_path[1024];
  char chk_script_path[1024];
  int chk_status = 0;
  int status = 0;
  char *ui_path = NULL;
  INKRecordGetString("proxy.config.admin.html_doc_root", &ui_path);
  if (ui_path != NULL) {

    /* First we check to make sure we can use tcl on this plat */
    snprintf(chk_script_path, sizeof(chk_script_path), "%s/configure/helper/INKMgmtAPICheckTcl.sh", ui_path);
    char *chk_args[] = {
      chk_script_path,
      NULL
    };
    /* tclCheckProcessSpawn.sh sends nothing back on stdout if check OK... */
    chk_status = tclCheckProcessSpawn(chk_args, output);
    if (chk_status == 0) {
      /* Go ahead and try the using the FTP .tcl script */
      snprintf(script_path, sizeof(script_path), "%s/configure/helper/INKMgmtAPIFtp.tcl", ui_path);
      char *args[] = {
        script_path,
        ftpCmd,
        ftp_server_name,
        ftp_login,
        ftp_password,
        local,
        remote,
        NULL
      };
      status = ftpProcessSpawn(args, output);
    } else {
      status = -1;
    }
  }
  if (status < 0)
    return INK_ERR_FAIL;
  return INK_ERR_OKAY;
}



/* Network conifguration functions */



/*-------------------------------------------------------------
 * rmserver.cfg 
 *-------------------------------------------------------------*/

char *
get_rmserver_path()
{
  char *path = NULL;
#if (HOST_OS == linux)

  FILE *ts_file, *rec_file;
  int i = 0, num_args = 0;
  char buffer[1024];
  char proxy_restart_cmd[1024];
  char ts_base_dir[1024];
  char rec_config[1024];
  static char *restart_cmd_args[100];
  INKString tmp;
  INKString tmp2;
  char *env_path;

  if ((env_path = getenv("TS_ROOT"))) {
    ink_strncpy(ts_base_dir, env_path, sizeof(ts_base_dir));
  } else {
    if ((ts_file = fopen(DEFAULT_TS_DIRECTORY_FILE, "r")) == NULL) {
      ink_strncpy(ts_base_dir, "/usr/local", sizeof(ts_base_dir));
    } else {
      NOWARN_UNUSED_RETURN(fgets(buffer, 1024, ts_file));
      fclose(ts_file);
      while (!isspace(buffer[i])) {
        ts_base_dir[i] = buffer[i];
        i++;
      }
      ts_base_dir[i] = '\0';
    }
  }

  snprintf(rec_config, sizeof(rec_config), "%s/etc/trafficserver/records.config", ts_base_dir);

  if ((rec_file = fopen(rec_config, "r")) == NULL) {
    //fprintf(stderr, "Error: unable to open %s.\n", rec_config);
    return NULL;
  }

  while (fgets(buffer, 1024, rec_file) != NULL) {
    if (strstr(buffer, "proxy.config.rni.proxy_restart_cmd") != NULL) {
      if ((tmp = strstr(buffer, "STRING ")) != NULL) {
        tmp += strlen("STRING ");
        for (i = 0; tmp[i] != '\n' && tmp[i] != '\0'; i++) {
          proxy_restart_cmd[i] = tmp[i];
        }
        proxy_restart_cmd[i] = '\0';

        tmp = proxy_restart_cmd;
        while ((tmp2 = strtok(tmp, " \t")) != NULL) {
          restart_cmd_args[num_args++] = strdup(tmp2);
          tmp = NULL;
        }
        restart_cmd_args[num_args] = NULL;
      }
    }
  }
  fclose(rec_file);

  path = restart_cmd_args[num_args - 1];
  //printf("rmservercfgpath: %s \n",path);
#endif

  return path;
}


inkapi INKError rm_change_ip(int cnt, char **ip)
{

#if (HOST_OS == linux)

  int status;
  char rmserver_path[512], rmserver_path1[512];
  char buf[1024], temp[512];
  FILE *fp;
  FILE *fp1;
  INKString path;
  INKString tmp;
  INKString tmp1;
  pid_t pid;

  if (cnt == 0) {
    //fprintf(stderr,"Error:No IP to change!\n"); 
    return INK_ERR_FAIL;
  }
  if (!ip[0]) {
    //fprintf(stderr,"Error[rm_change_ip]:Null IP passed!\n"); 
    return INK_ERR_FAIL;
  }
  //printf("ip[0]: %s",ip[0]);


  path = get_rmserver_path();
  if (!path) {
    //fprintf(stderr,"Error:rmserver.cfg path not found!\n"); 
    return INK_ERR_FAIL;
  }
  ink_strncpy(temp, path, sizeof(temp));
  ink_strncpy(rmserver_path, path, sizeof(rmserver_path));

  tmp = temp;
  if ((tmp1 = strstr(tmp, "/rmserver.cfg")) != NULL)
    tmp[tmp1 - tmp] = '\0';

  snprintf(rmserver_path1, sizeof(rmserver_path1), "%s/rmservernew.cfg", tmp);

  //printf("rmservercfgpath: %s \n",rmserver_path);
  //printf("new rmserver.cfg path: %s \n",rmserver_path1);
  //printf("cnt: %d \n",cnt);

  if ((fp = fopen(rmserver_path, "r")) == NULL) {
    //fprintf(stderr, "Error: unable to open rmserver.cfg\n");
    return INK_ERR_READ_FILE;
  }
  if ((fp1 = fopen(rmserver_path1, "w")) == NULL) {
    //fprintf(stderr, "Error: unable to create new rmserver.cfg\n");
    return INK_ERR_WRITE_FILE;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  while (!feof(fp)) {
    if (strstr(buf, "ProxyHost")) {
      //printf("%s \n",buf);
      fprintf(fp1, "<Var ProxyHost=\"%s\"/> \n", ip[0]);
    } else if (strstr(buf, "RedirectToAddress")) {
      //printf("%s \n",buf);
      fprintf(fp1, "<Var RedirectToAddress=\"%s\"/> \n", ip[0]);
    } else if (strstr(buf, "Address_01")) {
      //printf("%s \n",buf);
      // Currently we always put 0.0.0.0 since this solves all kinds of problems the field encountered
      //for(i = 0;i < cnt;i++) {
      //  if(!ip[i]) {
      //      fprintf(stderr,"Error[rm_change_ip]:Null IP passed!\n"); 
      //      return INK_ERR_FAIL;
      //  }
      fprintf(fp1, "<Var Address_01=\"0.0.0.0\"/> \n");
      //}
    } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }
  fclose(fp);
  fclose(fp1);

  const char *mv_binary = "/bin/mv";

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", rmserver_path1, rmserver_path, NULL);
    if (res != 0) {
      perror("[rm_change_ip] mv of rmserver.cfg failed! ");
    }
    _exit(res);
  }

#endif

  return INK_ERR_OKAY;
}

inkapi INKError rm_remove_ip(int cnt, char **ip)
{

#if (HOST_OS == linux)

  int i, status, flag;
  char rmserver_path[512], rmserver_path1[512];
  char buf[1024], temp[512];
  FILE *fp;
  FILE *fp1;
  INKString path;
  INKString tmp;
  INKString tmp1;
  pid_t pid;

  if (cnt == 0) {
    //fprintf(stderr,"Error[rm_remove_ip]:No IP to change!\n"); 
    return INK_ERR_FAIL;
  }
  if (!ip[0]) {
    //fprintf(stderr,"Error[rm_remove_ip]:Null IP passed!\n"); 
    return INK_ERR_FAIL;
  }
  //printf("ip[0]: %s",ip[0]);

  path = get_rmserver_path();
  if (!path) {
    //fprintf(stderr,"Error:rmserver.cfg path not found!\n"); 
    return INK_ERR_FAIL;
  }
  ink_strncpy(temp, path, sizeof(temp));
  ink_strncpy(rmserver_path, path, sizeof(rmserver_path));

  tmp = temp;
  if ((tmp1 = strstr(tmp, "/rmserver.cfg")) != NULL)
    tmp[tmp1 - tmp] = '\0';

  snprintf(rmserver_path1, sizeof(rmserver_path1), "%s/rmservernew.cfg", tmp);

  //printf("rmservercfgpath: %s \n",rmserver_path);
  //printf("new rmserver.cfg path: %s \n",rmserver_path1);

  if ((fp = fopen(rmserver_path, "r")) == NULL) {
    //fprintf(stderr, "Error: unable to open rmserver.cfg\n");
    return INK_ERR_READ_FILE;
  }
  if ((fp1 = fopen(rmserver_path1, "w")) == NULL) {
    //fprintf(stderr, "Error: unable to create new rmserver.cfg\n");
    return INK_ERR_WRITE_FILE;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  while (!feof(fp)) {
    if (strstr(buf, "Address_01")) {
      //printf("%s \n",buf);
      flag = 0;
      for (i = 0; i < cnt; i++) {
        if (!ip[i]) {
          //fprintf(stderr,"Error[rm_remove_ip]:Null IP passed!\n"); 
          fclose(fp);
          fclose(fp1);
          return INK_ERR_FAIL;
        }
        if (strstr(buf, ip[i])) {
          flag = 1;
          break;
        }
      }
      if (flag == 0)
        fputs(buf, fp1);
    } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }
  fclose(fp);
  fclose(fp1);

  const char *mv_binary = "/bin/mv";
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {                      /* Exec the up */
    int res;
    res = execl(mv_binary, "mv", rmserver_path1, rmserver_path, NULL);
    if (res != 0) {
      perror("[rm_remove_ip] mv of rmserver.cfg failed! ");
    }
    _exit(res);
  }

#endif

  return INK_ERR_OKAY;
}


inkapi INKError rm_change_hostname(INKString hostname)
{

#if (HOST_OS == linux)

  char rmserver_path[512], rmserver_path1[512];
  char buf[1024], temp[512];
  FILE *fp;
  FILE *fp1;
  INKString path = NULL, tmp = NULL, tmp1 = NULL, part1 = NULL, part3 = NULL;
  int status;
  pid_t pid;

  if (!hostname) {
    //fprintf(stderr,"Error[rm_change_hostname]:no hostname specified!\n"); 
    return INK_ERR_FAIL;
  }

  path = get_rmserver_path();
  if (!path) {
    //fprintf(stderr,"Error[rm_change_hostname]:rmserver.cfg path not found!\n"); 
    return INK_ERR_FAIL;
  }
  ink_strncpy(temp, path, sizeof(temp));
  ink_strncpy(rmserver_path, path, sizeof(rmserver_path));

  tmp = temp;
  if ((tmp1 = strstr(tmp, "/rmserver.cfg")) != NULL)
    tmp[tmp1 - tmp] = '\0';

  snprintf(rmserver_path1, sizeof(rmserver_path1), "%s/rmservernew.cfg", tmp);
  //printf("rmserver new path: %s\n",rmserver_path1); 
  if ((fp = fopen(rmserver_path, "r")) == NULL) {
    //fprintf(stderr, "Error: unable to open rmserver.cfg\n");
    return INK_ERR_READ_FILE;
  }
  if ((fp1 = fopen(rmserver_path1, "w")) == NULL) {
    //fprintf(stderr, "Error: unable to create new rmserver.cfg\n");
    return INK_ERR_WRITE_FILE;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  tmp1 = NULL;
  while (!feof(fp)) {
    if (strstr(buf, "Realm=")) {
      //printf("%s \n",buf);
      if ((tmp = strstr(buf, "\"")) != NULL) {
        if (tmp1)
          free(tmp1);
        tmp1 = strdup(tmp + 1);
        //printf("rem: %s,i\n",tmp1);
      }
      if ((tmp = strtok(buf, "\"")) != NULL) {
        if (part1)
          free(part1);
        part1 = strdup(tmp + 1);
        //printf("part1:%s \n",part1);
      }
      if (tmp1) {
        tmp = strstr(tmp1, "Connect");
        if (tmp != NULL) {
          if (part3)
            free(part3);
          part3 = strdup(tmp);
          //printf("part3: %s,i\n",part3);
        } else {
          tmp = strstr(tmp1, "Admin");
          if (tmp != NULL) {
            if (part3)
              free(part3);
            part3 = strdup(tmp);
            //printf("part3: %s,i\n",part3);
          }
        }
      }
      if (!part1 || !part3) {
        //fprintf(stderr,"Error[rm_change_hostname]:realm string not proper!\n"); 
        if (tmp1)
          free(tmp1);
        if (part1)
          free(part1);
        if (part3)
          free(part3);
        fclose(fp);
        fclose(fp1);
        return INK_ERR_FAIL;
      }
      //printf("hostname: %s\n",hostname);
      //printf("%s\"%s.%s",part1,hostname,part3);
      fprintf(fp1, "%s\"%s.%s", part1, hostname, part3);
    } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }
  if (tmp1)
    free(tmp1);
  if (part1)
    free(part1);
  if (part3)
    free(part3);
  fclose(fp);
  fclose(fp1);

  const char *mv_binary = "/bin/mv";
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {                      /* Exec the up */
    int res;
    res = execl(mv_binary, "mv", rmserver_path1, rmserver_path, NULL);
    if (res != 0) {
      perror("[rm_change_hostname] mv of rmserver.cfg failed! ");
    }
    _exit(res);
  }

#endif

  return INK_ERR_OKAY;
}

#if (HOST_OS == linux)
int
getTSdirectory(char *ts_path, size_t ts_path_len)
{
  FILE *fp;
  char *env_path;

  if ((env_path = getenv("TS_ROOT"))) {
    ink_strncpy(ts_path, env_path, ts_path_len);
    return 0;
  }

  if ((fp = fopen(DEFAULT_TS_DIRECTORY_FILE, "r")) == NULL) {
    ink_strncpy(ts_path, "/usr/local", ts_path_len);
    return 0;
  }

  if (fgets(ts_path, ts_path_len, fp) == NULL) {
    fclose(fp);
    return INK_ERR_READ_FILE;
  }
  // strip newline if it exists
  int len = strlen(ts_path);
  if (ts_path[len - 1] == '\n') {
    ts_path[len - 1] = '\0';
  }
  // strip trailing "/" if it exists
  len = strlen(ts_path);
  if (ts_path[len - 1] == '/') {
    ts_path[len - 1] = '\0';
  }

  fclose(fp);
  return INK_ERR_OKAY;
}

// close all file descriptors belong to process specified by pid
void
closeAllFds()
{
  const int BUFFLEN = 200;
  char command[BUFFLEN];
  char buffer[BUFFLEN];         // this is assumption.. so would break if some one try to hack this.
  int num;

  // WARNING:  this part of code doesn't work yet.  for some strange reason, we can not upgrade
  //           to root
  if (getuid() != 0) {          // if not super user, need to upgrade to root
    //printf("before upgrade:current uid%d, euid %d\n", getuid(), geteuid()); fflush(stdout);
    seteuid(0);
    setreuid(0, 0);
    //printf("after upgrade:current uid %d, euid %d\n", getuid(), geteuid()); fflush(stdout);
  }

  if (getuid() == 0 || geteuid() == 0) {        // only if it's successful
    snprintf(command, sizeof(command), "/bin/ls -1 /proc/%d/fd", getpid());
    FILE *fd = popen(command, "r");
    if (fd) {
      while (!feof(fd)) {
        NOWARN_UNUSED_RETURN(fgets(buffer, BUFFLEN, fd));
        num = atoi(buffer);
        if (num != fd->_fileno && num != 0 && num != 1 && num != 2) {   // for out put 
          //printf("closing fd (%d)\n", num); fflush(stdout);
          close(num);
        }
      }
      pclose(fd);
    }
  }
}

#endif

inkapi INKError rm_start_proxy()
{

#if (HOST_OS == linux)
  static time_t rm_last_stop = 0;

  time_t time_diff = time(NULL) - rm_last_stop;

  if (time_diff > 60 || time_diff < 0) {        // wrap around??  shall never happen 
    pid_t pid;
    char *argv[3];
    argv[0] = "net_config";
    argv[1] = "7";
    argv[2] = NULL;
    char command_path[512];
    char ts_path[256];
    if (getTSdirectory(ts_path, sizeof(ts_path))) {
      perror("[rm_start_proxy] unable to determine install directory\n");
      return INK_ERR_READ_FILE;
    }
    snprintf(command_path, sizeof(command_path), "%s/bin/net_config", ts_path);

    rm_last_stop = time(NULL);

    if ((pid = fork()) < 0) {
      exit(1);
    } else if (pid > 0) {
      // do not wait
    } else {
      closeAllFds();
      close(1);                 // close STDOUT
      close(2);                 // close STDERR

      int res = execv(command_path, argv);
      if (res != 0) {
        perror("[rm_start_proxy] net_config stop_proxy failed! ");
      }
      _exit(res);
    }
  }                             // else already try to stop within 60s Window, skip
#endif
  return INK_ERR_OKAY;
}


/*****************************************************************
* Traffic server changes necessary when network config is changed
*****************************************************************/

inkapi INKError
INKSetHostname(INKString hostname)
{
  INKActionNeedT action_need = INK_ACTION_UNDEFINED, top_action_req = INK_ACTION_UNDEFINED;
  INKInt val = 0;

  /* Here we should handle these cases:
   * rmserver.cfg - different API currently, records.config, mrtg, and hostname_FQ
   */

  if (INKRecordGetInt("proxy.local.cluster.type", &val) == INK_ERR_OKAY) {      //If error??
    if (val == 3) {
      if (MgmtRecordSet("proxy.config.proxy_name", hostname, &action_need) != INK_ERR_OKAY)
        return INK_ERR_FAIL;
    }
  }

  if (action_need < top_action_req)     // a more severe action
    top_action_req = action_need;

  //FIXME - currently MRTG is all about hard coded hostname - this is where we should fix it
  //Currently it is fixed in net_config

  //if (!(mrtg_update_hostname(hostname)))
  //     return INK_ERR_WRITE_FILE;

  //Also, we use this variable sometimes - needs to be fixed

  if (MgmtRecordSet("proxy.node.hostname_FQ", hostname, &action_need) != INK_ERR_OKAY)
    return INK_ERR_FAIL;

  //carry out the appropriate action
  if (action_need < top_action_req)     // a more severe action
    top_action_req = action_need;

  if (top_action_req != INK_ACTION_UNDEFINED) {
    //return INKActionDo(top_action_req); //right now we mark this out as this is not needed and causes hangs - verify this FIX - bz49778
    return INK_ERR_OKAY;
  } else {
    return INK_ERR_OKAY;
  }
}

inkapi INKError
INKSetGateway(INKString gateway_ip)
{
  //Nothing to be done for now
  return INK_ERR_OKAY;

}

inkapi INKError
INKSetDNSServers(INKString dns_ips)
{
  //Nothing to be done for now
  return INK_ERR_OKAY;

}

inkapi INKError
INKSetNICUp(INKString nic_name, bool static_ip, INKString ip, INKString old_ip, INKString netmask, bool onboot,
            INKString gateway_ip)
{
  /* there is no ipnat conf file anymore,
     commenting out the rest of this function */
  return INK_ERR_READ_FILE;

  /*
     INKCfgContext ctx;
     INKIpFilterEle *ele, *ele_copy;
     INKActionNeedT action_need=INK_ACTION_UNDEFINED;
     int i, index;

     //ctx = INKCfgContextCreate(INK_FNAME_IPNAT);

     if (!ctx) {
     //Debug("config", "[INKSetNICUp] can't allocate ctx memory");
     return INK_ERR_FAIL;
     }

     if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_READ_FILE;
     } 

     if ((i =  INKCfgContextGetCount(ctx)) <=0 ) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_FAIL;
     }

     if (strcmp(nic_name, "eth0") == 0) {  //currently we hard code it - should be changed
     for (index=0 ; index<i ; index++) {
     ele = (INKIpFilterEle *)INKCfgContextGetEleAt(ctx, index);
     if (ele != NULL) { 
     if (strcmp(ele->intr,nic_name) == 0) {
     //if (strcmp(ele->dest_ip_addr, old_ip) == 0) //INKqa12486 
     ele->dest_ip_addr =  string_to_ip_addr(ip);
     }
     }
     }
     } else { //it is not eth0

     bool found = false;
     for (index=0 ; index<i ; index++) {
     ele = (INKIpFilterEle *)INKCfgContextGetEleAt(ctx, index);
     if (ele != NULL) { 
     if (strcmp(ele->intr,nic_name) == 0) {
     found = true;
     //if (strcmp(ele->dest_ip_addr, old_ip) == 0)  //INKqa12486
     ele->dest_ip_addr =  string_to_ip_addr(ip); 
     }
     }
     }
     if (!found) { //create the rules for the new NIC according to eth0
     for (index=0 ; index<i ; index++) {
     ele = (INKIpFilterEle *)INKCfgContextGetEleAt(ctx, index);
     if (ele != NULL) { 
     if (strcmp(ele->intr, "eth0") == 0) {
     ele_copy = INKIpFilterEleCreate();
     //copy the ele
     ele_copy->intr =  xstrdup(nic_name); // ethernet interface
     ele_copy->src_ip_addr = ele->src_ip_addr;  // from IP
     ele_copy->src_cidr = ele->src_cidr; 
     ele_copy->src_port = ele->src_port;  // from port
     ele_copy->dest_ip_addr =  string_to_ip_addr(ip); // to IP
     ele_copy->dest_port = ele->dest_port;     // to port
     ele_copy->type_con = ele->type_con;
     ele_copy->protocol = ele->protocol;

     INKCfgContextAppendEle(ctx, (INKCfgEle*)ele_copy); // add new ele to end of list     
     }
     }
     }
     }
     }

     // commit the CfgContext to write a new version of the file
     if (INKCfgContextCommit(ctx, &action_need, NULL) != INK_ERR_OKAY) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_FAIL;
     }   
     //if (ctx) INKCfgContextDestroy(ctx); 


     if ( action_need != INK_ACTION_UNDEFINED ){
     return INKActionDo(action_need);
     }
     else {
     return INK_ERR_OKAY;
     }
   */
}

inkapi INKError
INKSetProxyPort(INKString proxy_port)
{
  /* there is no ipnat.conf file anymore, 
     commenting out the rest of this function */
  return INK_ERR_READ_FILE;

  /*
     INKCfgContext ctx;
     INKIpFilterEle *ele;
     INKActionNeedT action_need=INK_ACTION_UNDEFINED;
     int i, index;

     //ctx = INKCfgContextCreate(INK_FNAME_IPNAT);
     if (!ctx) {
     //Debug("config", "[INKSetNICUp] can't allocate ctx memory");
     return INK_ERR_FAIL;
     }

     if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_READ_FILE;
     } 

     if ((i =  INKCfgContextGetCount(ctx)) <=0 ) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_FAIL;
     }

     for (index=0 ; index<i ; index++) {
     ele = (INKIpFilterEle *)INKCfgContextGetEleAt(ctx, index);
     if (ele != NULL) { 
     if (ele->src_port == 80) {
     ele->dest_port = ink_atoi(proxy_port);
     //  Debug("api2","[INKSetProxyPort] %d is the dest_port for port %d now.\n",ele->dest_port, ele->src_port); 
     }
     }
     }

     // commit the CfgContext to write a new version of the file
     if (INKCfgContextCommit(ctx, &action_need, NULL) != INK_ERR_OKAY) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_FAIL;
     }   
     //if (ctx) INKCfgContextDestroy(ctx); 

     if ( action_need != INK_ACTION_UNDEFINED ){
     return INKActionDo(action_need);
     }
     else {
     return INK_ERR_OKAY;
     }
   */
}

inkapi INKError
INKSetNICDown(INKString nic_name, INKString ip_addrr)
{
  /* there is no ipnat.conf file anymore,
     commenting out the rest of this function */
  return INK_ERR_READ_FILE;

  /*
     INKCfgContext ctx;
     INKIpFilterEle *ele;
     INKActionNeedT action_need = INK_ACTION_UNDEFINED;
     int index;
     // This isn't used any more, code commented out below. /leif
     //INKCfgIterState ctx_state;
     bool found;

     //ctx = INKCfgContextCreate(INK_FNAME_IPNAT);

     if (!ctx) {
     //Debug("config", "[INKSetNicDown] can't allocate ctx memory");
     return INK_ERR_FAIL;
     }

     if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_READ_FILE; 
     }

     ele = (INKIpFilterEle *)INKCfgContextGetFirst(ctx, &ctx_state); 

     while (ele) {
     if (strcmp(ele->intr, nic_name) == 0)  INKCfgContextRemoveEleAt (ctx, index);
     ele = (INKIpFilterEle *)INKCfgContextGetNext(ctx, &ctx_state); 

     }
     found = true;
     while (found) {
     found = false;
     for (index=0 ; index< INKCfgContextGetCount(ctx); index++) {
     ele = (INKIpFilterEle *)INKCfgContextGetEleAt(ctx, index);
     if (ele != NULL) {
     if (strcmp(ele->intr, nic_name) == 0)  {
     INKCfgContextRemoveEleAt (ctx, index);
     found = true;
     }
     }
     } 
     }

     // commit the CfgContext to write a new version of the file
     if (INKCfgContextCommit(ctx, &action_need, NULL) != INK_ERR_OKAY) {
     //if (ctx) INKCfgContextDestroy(ctx); 
     return INK_ERR_FAIL;
     }   
     //if (ctx) INKCfgContextDestroy(ctx); 

     if ( action_need != INK_ACTION_UNDEFINED) {
     return INKActionDo(action_need);
     }
     else {
     return INK_ERR_OKAY;
     }
   */
}


inkapi INKError
INKSetSearchDomain(INKString search_name)
{
  //Nothing to be done for now
  return INK_ERR_OKAY;
}

/* The following 2 functions set the Realm field in rmserver.cfg file. */
void
resetHostName(INKRmServerEle * ele, const char *hostname, const char *tail)
{
  char buff[MAX_RULE_SIZE];
  xfree(ele->str_val);
  snprintf(buff, sizeof(buff), "%s.%s", hostname, tail);
  ele->str_val = xstrdup(buff);
  return;
}

INKError
INKSetRmRealm(const char *hostname)
{

  INKCfgContext ctx;
  INKRmServerEle *ele;
  Tokenizer tokens("\n");
  INKActionNeedT action_need;
  INKError response;
  INKError err = INK_ERR_OKAY;


  ctx = INKCfgContextCreate(INK_FNAME_RMSERVER);
  if (!ctx) {
//    Debug("config", "[net_config:Config] can't allocate ctx memory");
    goto Lerror;
  }
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
//    Debug("config", "[net_config:Config] Failed to Get CfgContext"); 
    goto Lerror;
  }
  ele = (INKRmServerEle *) CfgContextGetEleAt((CfgContext *) ctx, (int) INK_RM_RULE_SCU_ADMIN_REALM);
  resetHostName(ele, hostname, "AdminRealm");
  ele = (INKRmServerEle *) CfgContextGetEleAt((CfgContext *) ctx, (int) INK_RM_RULE_CNN_REALM);
  resetHostName(ele, hostname, "ConnectRealm");
  ele = (INKRmServerEle *) CfgContextGetEleAt((CfgContext *) ctx, (int) INK_RM_RULE_ADMIN_FILE_REALM);
  resetHostName(ele, hostname, "AdminRealm");
  ele = (INKRmServerEle *) CfgContextGetEleAt((CfgContext *) ctx, (int) INK_RM_RULE_AUTH_REALM);
  resetHostName(ele, hostname, "ConnectRealm");
  response = INKCfgContextCommit(ctx, &action_need, NULL);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    //    MgmtAPI should return INK_ not WEB_
    //    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    err = INK_ERR_INVALID_CONFIG_RULE;
  } else if (response != INK_ERR_OKAY) {
    goto Lerror;
  }
  INKCfgContextDestroy(ctx);
  return err;

Lerror:
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

/* this function change the PNA_REDIREDT IP address of rmserver.cfg file. */
INKError
INKSetRmPNA_RDT_IP(const char *ip)
{
  INKCfgContext ctx;
  INKRmServerEle *ele;
  Tokenizer tokens("\n");
  INKActionNeedT action_need;
  INKError response;
  INKError err = INK_ERR_OKAY;
  char buff[MAX_RULE_SIZE];

  ctx = INKCfgContextCreate(INK_FNAME_RMSERVER);
  if (!ctx) {
//    Debug("config", "[net_config:Config] can't allocate ctx memory");
    goto Lerror;
  }
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
//    Debug("config", "[net_config:Config] Failed to Get CfgContext"); 
    goto Lerror;
  }
  ele = (INKRmServerEle *) CfgContextGetEleAt((CfgContext *) ctx, (int) INK_RM_RULE_PNA_RDT_IP);
  if (ele->str_val)
    xfree(ele->str_val);
  snprintf(buff, sizeof(buff), "%s", ip);
  ele->str_val = xstrdup(buff);

  response = INKCfgContextCommit(ctx, &action_need, NULL);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    //    MgmtAPI should return INK_ not WEB_
    //    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    err = INK_ERR_INVALID_CONFIG_RULE;
  } else if (response != INK_ERR_OKAY) {
    goto Lerror;
  }
  //INKCfgContextDestroy(ctx);
  return err;

Lerror:
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

/* this function change the PNA_REDIRET port in  ipnat.conf file. */
inkapi INKError
INKSetPNA_RDT_Port(const int port)
{
  /* there is no ipnat conf file anymore,
     commenting out the rest of this function */
  return INK_ERR_READ_FILE;

  /*
     INKCfgContext ctx;
     INKIpFilterEle *ele;
     INKActionNeedT action_need=INK_ACTION_UNDEFINED;
     int i, index;


     //ctx = INKCfgContextCreate(INK_FNAME_IPNAT);

     if (!ctx) {
     return INK_ERR_FAIL;
     }

     if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
     return INK_ERR_READ_FILE;
     }
     if ((i =  INKCfgContextGetCount(ctx)) <=0 ) {
     return INK_ERR_FAIL;
     }

     for (index=0 ; index<i ; index++) {
     ele = (INKIpFilterEle *)INKCfgContextGetEleAt(ctx, index);
     if (ele != NULL) {
     if (ele->src_port == 7070) {
     ele->dest_port = port;
     break;
     }
     }
     }

     // commit the CfgContext to write a new version of the file
     if (INKCfgContextCommit(ctx, &action_need, NULL) != INK_ERR_OKAY) {
     return INK_ERR_FAIL;
     }

     if ( action_need != INK_ACTION_UNDEFINED) {
     return INKActionDo(action_need);
     }
     else {
     return INK_ERR_OKAY;
     }
   */
}
