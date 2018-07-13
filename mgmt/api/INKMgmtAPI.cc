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
#include "ts/ink_memory.h"
#include "ts/ParseRules.h"
#include <climits>
#include "ts/I_Layout.h"

#include "mgmtapi.h"
#include "CoreAPI.h"
#include "CoreAPIShared.h"

#include "ts/TextBuffer.h"

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
  if (!l) {
    return;
  }

  delete_queue((LLQ *)l);
  return;
}

tsapi TSMgmtError
TSListEnqueue(TSList l, void *data)
{
  int ret;

  ink_assert(l && data);
  if (!l || !data) {
    return TS_ERR_PARAMS;
  }

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
  if (!l || queue_is_empty((LLQ *)l)) {
    return nullptr;
  }

  return dequeue((LLQ *)l);
}

tsapi bool
TSListIsEmpty(TSList l)
{
  ink_assert(l);
  if (!l) {
    return true; // list doesn't exist, so it's empty
  }

  return queue_is_empty((LLQ *)l);
}

tsapi int
TSListLen(TSList l)
{
  ink_assert(l);
  if (!l) {
    return -1;
  }

  return queue_len((LLQ *)l);
}

tsapi bool
TSListIsValid(TSList l)
{
  int i, len;
  void *ele;

  if (!l) {
    return false;
  }

  len = queue_len((LLQ *)l);
  for (i = 0; i < len; i++) {
    ele = (void *)dequeue((LLQ *)l);
    if (!ele) {
      return false;
    }
    enqueue((LLQ *)l, ele);
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
  if (!strl || !str) {
    return TS_ERR_PARAMS;
  }

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
  if (!strl || queue_is_empty((LLQ *)strl)) {
    return nullptr;
  }

  return (char *)dequeue((LLQ *)strl);
}

tsapi bool
TSStringListIsEmpty(TSStringList strl)
{
  ink_assert(strl);
  if (!strl) {
    return true;
  }

  return queue_is_empty((LLQ *)strl);
}

tsapi int
TSStringListLen(TSStringList strl)
{
  ink_assert(strl);
  if (!strl) {
    return -1;
  }

  return queue_len((LLQ *)strl);
}

// returns false if any element is NULL string
tsapi bool
TSStringListIsValid(TSStringList strl)
{
  int i, len;
  char *str;

  if (!strl) {
    return false;
  }

  len = queue_len((LLQ *)strl);
  for (i = 0; i < len; i++) {
    str = (char *)dequeue((LLQ *)strl);
    if (!str) {
      return false;
    }
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

  if (!intl) {
    return;
  }

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
  if (!intl || !elem) {
    return TS_ERR_PARAMS;
  }

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
  if (!intl || queue_is_empty((LLQ *)intl)) {
    return nullptr;
  }

  return (int *)dequeue((LLQ *)intl);
}

tsapi bool
TSIntListIsEmpty(TSIntList intl)
{
  ink_assert(intl);
  if (!intl) {
    return true;
  }

  return queue_is_empty((LLQ *)intl);
}

tsapi int
TSIntListLen(TSIntList intl)
{
  ink_assert(intl);
  if (!intl) {
    return -1;
  }

  return queue_len((LLQ *)intl);
}

tsapi bool
TSIntListIsValid(TSIntList intl, int min, int max)
{
  if (!intl) {
    return false;
  }

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

/*--- allocate/deallocate operations --------------------------------------*/
tsapi TSMgmtEvent *
TSEventCreate(void)
{
  TSMgmtEvent *event = (TSMgmtEvent *)ats_malloc(sizeof(TSMgmtEvent));

  event->id          = -1;
  event->name        = nullptr;
  event->description = nullptr;
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

  ele->rec_name = nullptr;
  ele->rec_type = TS_REC_UNDEFINED;

  return ele;
}

tsapi void
TSRecordEleDestroy(TSRecordEle *ele)
{
  if (ele) {
    ats_free(ele->rec_name);
    if (ele->rec_type == TS_REC_STRING && ele->valueT.string_val) {
      ats_free(ele->valueT.string_val);
    }
    ats_free(ele);
  }
  return;
}

/***************************************************************************
 * API Core
 ***************************************************************************/

/*--- host status operations ----------------------------------------------- */
tsapi TSMgmtError
TSHostStatusSetUp(const char *host_name, int down_time, const char *reason)
{
  return HostStatusSetUp(host_name, down_time, reason);
}

tsapi TSMgmtError
TSHostStatusSetDown(const char *host_name, int down_time, const char *reason)
{
  return HostStatusSetDown(host_name, down_time, reason);
}

/*--- statistics operations ----------------------------------------------- */
tsapi TSMgmtError
TSStatsReset(const char *name)
{
  return StatsReset(name);
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
  if (ret != TS_ERR_OKAY) {
    goto END;
  }

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
  if (ret != TS_ERR_OKAY) {
    goto END;
  }
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
  if (ret != TS_ERR_OKAY) {
    goto END;
  }
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
  if (ret != TS_ERR_OKAY) {
    goto END;
  }

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

  if (!rec_names || !rec_vals) {
    return TS_ERR_PARAMS;
  }

  num_recs = queue_len((LLQ *)rec_names);
  for (i = 0; i < num_recs; i++) {
    rec_name = (char *)dequeue((LLQ *)rec_names); // remove name from list
    if (!rec_name) {
      return TS_ERR_PARAMS; // NULL is invalid record name
    }

    ele = TSRecordEleCreate();

    ret = MgmtRecordGet(rec_name, ele);
    enqueue((LLQ *)rec_names, rec_name); // return name to list

    if (ret != TS_ERR_OKAY) { // RecordGet failed
      // need to free all the ele's allocated by MgmtRecordGet so far
      TSRecordEleDestroy(ele);
      for (j = 0; j < i; j++) {
        ele = (TSRecordEle *)dequeue((LLQ *)rec_vals);
        if (ele) {
          TSRecordEleDestroy(ele);
        }
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

  if (!rec_list || !action_need) {
    return TS_ERR_PARAMS;
  }

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
      if (ret != TS_ERR_OKAY) {
        status = TS_ERR_FAIL;
      }

      // keep track of most severe action; reset if needed
      // the TSACtionNeedT should be listed such that most severe actions have
      // a lower number (so most severe action == 0)
      if (*action_need < top_action_req) { // a more severe action
        top_action_req = *action_need;
      }
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
 *                 when stopping TS should always be TS_CACHE_CLEAR_NONE
 * Output: TSMgmtError
 */
tsapi TSMgmtError
TSProxyStateSet(TSProxyStateT proxy_state, unsigned clear)
{
  unsigned mask = TS_CACHE_CLEAR_NONE | TS_CACHE_CLEAR_CACHE | TS_CACHE_CLEAR_HOSTDB;

  if (clear & ~mask) {
    return TS_ERR_PARAMS;
  }

  return ProxyStateSet(proxy_state, static_cast<TSCacheClearT>(clear));
}

tsapi TSMgmtError
TSProxyBacktraceGet(unsigned options, TSString *trace)
{
  if (options != 0) {
    return TS_ERR_PARAMS;
  }

  if (trace == nullptr) {
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
TSStop(unsigned options)
{
  return Stop(options);
}

tsapi TSMgmtError
TSDrain(unsigned options)
{
  return Drain(options);
}

tsapi TSMgmtError
TSStorageDeviceCmdOffline(const char *dev)
{
  return StorageDeviceCmdOffline(dev);
}

tsapi TSMgmtError
TSLifecycleMessage(const char *tag, void const *data, size_t data_size)
{
  return LifecycleMessage(tag, data, data_size);
}

/* NOTE: user must deallocate the memory for the string returned */
char *
TSGetErrorMessage(TSMgmtError err_id)
{
  char msg[1024]; // need to define a MAX_ERR_MSG_SIZE???
  char *err_msg = nullptr;

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
  char *httpHost = nullptr;
  char *httpPath = nullptr;
  int httpPort   = HTTP_PORT;
  int bufsize    = URL_BUFSIZE;
  char buffer[URL_BUFSIZE];
  char request[BUFSIZE];
  char *hdr_temp;
  char *bdy_temp;
  TSMgmtError status = TS_ERR_OKAY;

  // Sanity check
  if (!url) {
    return TS_ERR_FAIL;
  }
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
    if (httpPort <= 0) {
      httpPort = HTTP_PORT;
    }
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
  if ((status = sendHTTPRequest(hFD, request, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  memset(buffer, 0, bufsize); /* empty the buffer */
  if ((status = readHTTPResponse(hFD, buffer, bufsize, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((status = parseHTTPResponse(buffer, &hdr_temp, headerSize, &bdy_temp, bodySize)) != TS_ERR_OKAY) {
    goto END;
  }

  if (header && headerSize) {
    *header = ats_strndup(hdr_temp, *headerSize);
  }
  *body = ats_strndup(bdy_temp, *bodySize);

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

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/lookup_url?url=%s", url);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY) {
    goto END;
  }

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

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/lookup_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY) {
    goto END;
  }

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

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/delete_url?url=%s", url);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY) {
    goto END;
  }

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

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/delete_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY) {
    goto END;
  }

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

  if ((err = TSRecordGetInt("proxy.config.http.server_port", &ts_port)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((fd = connectDirect("localhost", ts_port, timeout)) < 0) {
    err = TS_ERR_FAIL;
    goto END;
  }
  snprintf(request, BUFSIZE, "http://{cache}/invalidate_regex?url=%s", url_regex);
  if ((err = sendHTTPRequest(fd, request, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  memset(response, 0, URL_BUFSIZE);
  if ((err = readHTTPResponse(fd, response, URL_BUFSIZE, (uint64_t)timeout)) != TS_ERR_OKAY) {
    goto END;
  }

  if ((err = parseHTTPResponse(response, &header, &hdr_size, &body, &bdy_size)) != TS_ERR_OKAY) {
    goto END;
  }

  *list = ats_strndup(body, bdy_size);
END:
  return err;
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
