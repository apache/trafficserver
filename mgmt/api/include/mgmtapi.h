/** @file

  Definitions for internal management API.

  Purpose: This file contains all API wrapper functions in one class. In
  order to eliminate the interdependencies of other library calls, new
  types and structs will be defined and used in the wrapper function calls.

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

#pragma once

#include <cstdint>
#include <cstddef>

/***************************************************************************
 * System Specific Items
 ***************************************************************************/

#define tsapi
#define inkexp
#define inkimp

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef TS_RES_MEM_PATH
#define __TS_RES_PATH(x) #x
#define _TS_RES_PATH(x) __TS_RES_PATH(x)
#define TS_RES_PATH(x) x __FILE__ ":" _TS_RES_PATH(__LINE__)
#define TS_RES_MEM_PATH TS_RES_PATH("memory/")
#endif

#define TM_OPT_BIND_STDOUT "bind_stdout"
#define TM_OPT_BIND_STDERR "bind_stderr"

/***************************************************************************
 * Error and Return Values
 ***************************************************************************/

typedef enum {
  TS_ERR_OKAY = 0,

  TS_ERR_READ_FILE,           /* Error occur in reading file */
  TS_ERR_WRITE_FILE,          /* Error occur in writing file */
  TS_ERR_PARSE_CONFIG_RULE,   /* Error in parsing configuration file */
  TS_ERR_INVALID_CONFIG_RULE, /* Invalid Configuration Rule */

  TS_ERR_NET_ESTABLISH, /* Problem in establishing a TCP socket */
  TS_ERR_NET_READ,      /* Problem reading from socket */
  TS_ERR_NET_WRITE,     /* Problem writing to socket */
  TS_ERR_NET_EOF,       /* Hit socket EOF */
  TS_ERR_NET_TIMEOUT,   /* Timed out while waiting for socket read */

  TS_ERR_SYS_CALL, /* Error in basic system call, eg. malloc */
  TS_ERR_PARAMS,   /* Invalid parameters for a fn */

  TS_ERR_NOT_SUPPORTED,     /* Operation not supported */
  TS_ERR_PERMISSION_DENIED, /* Operation not permitted */

  TS_ERR_FAIL
} TSMgmtError;

/***************************************************************************
 * Constants
 ***************************************************************************/

#define TS_INVALID_HANDLE NULL
#define TS_INVALID_LIST TS_INVALID_HANDLE
#define TS_INVALID_CFG_CONTEXT TS_INVALID_HANDLE
#define TS_INVALID_THREAD TS_INVALID_HANDLE
#define TS_INVALID_MUTEX TS_INVALID_HANDLE

#define TS_INVALID_IP_ADDR NULL
#define TS_INVALID_IP_CIDR -1
#define TS_INVALID_PORT 0

#define TS_SSPEC_TIME 0x1
#define TS_SSPEC_SRC_IP 0x2
#define TS_SSPEC_PREFIX 0x4
#define TS_SSPEC_SUFFIX 0x8
#define TS_SSPEC_PORT 0x10
#define TS_SSPEC_METHOD 0x20
#define TS_SSPEC_SCHEME 0x40

#define TS_ENCRYPT_PASSWD_LEN 23

/***************************************************************************
 * Types
 ***************************************************************************/

typedef int64_t TSInt;
typedef int64_t TSCounter;
typedef float TSFloat;
typedef bool TSBool;
typedef char *TSString;
typedef char *TSIpAddr;

typedef void *TSHandle;
typedef TSHandle TSList;
typedef TSHandle TSStringList; /* contains char* 's         */
typedef TSHandle TSIntList;    /* contains int* 's          */

/*--- basic control operations --------------------------------------------*/

typedef enum {
  TS_ACTION_SHUTDOWN,    /* change requires user to stop then start the Traffic Server and Manager (restart Traffic Cop) */
  TS_ACTION_RESTART,     /* change requires restart Traffic Server and Traffic Manager */
  TS_ACTION_DYNAMIC,     /* change is already made in function call */
  TS_ACTION_RECONFIGURE, /* change requires TS to reread configuration files */
  TS_ACTION_UNDEFINED
} TSActionNeedT;

typedef enum {
  TS_PROXY_ON,
  TS_PROXY_OFF,
  TS_PROXY_UNDEFINED,
} TSProxyStateT;

/* used when starting Traffic Server process */
typedef enum {
  TS_CACHE_CLEAR_NONE   = 0,        /* starts TS in regular mode w/o any options */
  TS_CACHE_CLEAR_CACHE  = (1 << 0), /* run TS in  "clear cache" mode */
  TS_CACHE_CLEAR_HOSTDB = (1 << 1), /* run TS in "clear the host db cache" mode */
} TSCacheClearT;

/*--- event operations ----------------------------------------------------*/
typedef enum {
  TS_EVENT_PRIORITY_WARNING,
  TS_EVENT_PRIORITY_ERROR,
  TS_EVENT_PRIORITY_FATAL,
  TS_EVENT_PRIORITY_UNDEFINED
} TSEventPriorityT;

/*--- abstract file operations --------------------------------------------*/

typedef enum {
  TS_ACCESS_NONE,           /* no access */
  TS_ACCESS_MONITOR,        /* monitor only access */
  TS_ACCESS_MONITOR_VIEW,   /* monitor and view configuration access */
  TS_ACCESS_MONITOR_CHANGE, /* monitor and change configuration access */
  TS_ACCESS_UNDEFINED
} TSAccessT;

typedef enum {
  TS_REC_INT,
  TS_REC_COUNTER,
  TS_REC_FLOAT,
  TS_REC_STRING,
  TS_REC_UNDEFINED,
} TSRecordT;

/* ToDo: This should be moved over to the core, into the GenericParser.h */
typedef enum {
  TS_FNAME_CACHE_OBJ,       /* cache.config */
  TS_FNAME_HOSTING,         /* hosting.config */
  TS_FNAME_IP_ALLOW,        /* ip_allow.config */
  TS_FNAME_PARENT_PROXY,    /* parent.config */
  TS_FNAME_VOLUME,          /* volume.config */
  TS_FNAME_PLUGIN,          /* plugin.config */
  TS_FNAME_REMAP,           /* remap.config */
  TS_FNAME_SOCKS,           /* socks.config */
  TS_FNAME_SPLIT_DNS,       /* splitdns.config */
  TS_FNAME_STORAGE,         /* storage.config */
  TS_FNAME_VSCAN,           /* vscan.config */
  TS_FNAME_VS_TRUSTED_HOST, /* trusted-host.config */
  TS_FNAME_VS_EXTENSION,    /* extensions.config */
  TS_FNAME_UNDEFINED
} TSFileNameT;

/* These are initialization options for the Init() function. */
typedef enum {
  TS_MGMT_OPT_DEFAULTS = 0,
  TS_MGMT_OPT_NO_EVENTS,    /* No event callbacks and threads */
  TS_MGMT_OPT_NO_SOCK_TESTS /* No socket test thread */
} TSInitOptionT;

typedef enum {
  TS_RESTART_OPT_NONE  = 0x0,
  TS_RESTART_OPT_DRAIN = 0x02, /* Wait for traffic to drain before restarting. */
} TSRestartOptionT;

typedef enum {
  TS_STOP_OPT_NONE = 0x0,
  TS_STOP_OPT_DRAIN, /* Wait for traffic to drain before stopping. */
} TSStopOptionT;

typedef enum {
  TS_DRAIN_OPT_NONE = 0x0,
  TS_DRAIN_OPT_IDLE, /* Wait for idle from new connections before draining. */
  TS_DRAIN_OPT_UNDO, /* Recover TS from drain mode */
} TSDrainOptionT;

/***************************************************************************
 * Structures
 ***************************************************************************/

/*--- records -------------------------------------------------------------*/
typedef union { /* record value */
  TSInt int_val;
  TSCounter counter_val;
  TSFloat float_val;
  TSString string_val;
} TSRecordValueT;

typedef struct {
  char *rec_name;        /* record name */
  TSInt rec_class;       /* record class (RecT) */
  TSRecordT rec_type;    /* record type {TS_REC_INT...} */
  TSRecordValueT valueT; /* record value */
} TSRecordEle;

typedef struct {
  /* Common RecRecord fields ... */
  char *rec_name;
  TSRecordValueT rec_value;
  TSRecordValueT rec_default;
  TSRecordT rec_type; /* data type (RecDataT) */
  TSInt rec_class;    /* data class (RecT) */
  TSInt rec_version;
  TSInt rec_rsb; /* Raw Stat Block ID */
  TSInt rec_order;

  /* RecConfigMeta fields ... */
  TSInt rec_access;     /* access rights (RecAccessT) */
  TSInt rec_update;     /* update_required bitmask */
  TSInt rec_updatetype; /* update type (RecUpdateT) */
  TSInt rec_checktype;  /* syntax check type (RecCheckT) */
  TSInt rec_source;     /* source of data */
  char *rec_checkexpr;  /* syntax check expression */
} TSConfigRecordDescription;

/* Free (the contents of) a TSConfigRecordDescription */
tsapi void TSConfigRecordDescriptionFree(TSConfigRecordDescription *val);

/* Heap-allocate a TSConfigRecordDescription. */
tsapi TSConfigRecordDescription *TSConfigRecordDescriptionCreate(void);
/* Free and destroy a heap-allocated TSConfigRecordDescription. */
tsapi void TSConfigRecordDescriptionDestroy(TSConfigRecordDescription *);

/*--- events --------------------------------------------------------------*/

/* Note: Each event has a format String associated with it from which the
 *       description is constructed when an event is signalled. This format
 *       string though can be retrieved from the event-mapping table which
 *       is stored both locally and remotely.
 */

typedef struct {
  int id;
  char *name;                /* pre-set, immutable for PREDEFINED events */
  char *description;         /* predefined events have default */
  TSEventPriorityT priority; /* WARNING, ERROR, FATAL */
} TSMgmtEvent;

/* Will not be used until new Cougar Event Processor */
typedef struct {
  char *name;
  /*int signalCount; */         /* 0 is inactive, >= 1 is active event */
  /*unsigned long timestamp; */ /* only applies to active events */
} TSActiveEvent;

/***************************************************************************
 * Function Types
 ***************************************************************************/
typedef void (*TSEventSignalFunc)(char *name, char *msg, int pri, void *data);
typedef void (*TSDisconnectFunc)(void *data);

/***************************************************************************
 * API Memory Management
 ***************************************************************************/
#define TSmalloc(s) _TSmalloc((s), TS_RES_MEM_PATH)
#define TSrealloc(p, s) _TSrealloc((p), (s), TS_RES_MEM_PATH)
#define TSstrdup(p) _TSstrdup((p), -1, TS_RES_MEM_PATH)
#define TSstrndup(p, n) _TSstrdup((p), (n), TS_RES_MEM_PATH)
#define TSfree(p) _TSfree(p)

tsapi void *_TSmalloc(unsigned int size, const char *path);
tsapi void *_TSrealloc(void *ptr, unsigned int size, const char *path);
tsapi char *_TSstrdup(const char *str, int length, const char *path);
tsapi void _TSfree(void *ptr);

/***************************************************************************
 * API Helper Functions for Data Carrier Structures
 ***************************************************************************/

/*--- TSList operations --------------------------------------------------*/
tsapi TSList TSListCreate();
tsapi void TSListDestroy(TSList l); /* list must be empty */
tsapi TSMgmtError TSListEnqueue(TSList l, void *data);
tsapi void *TSListDequeue(TSList l);
tsapi bool TSListIsEmpty(TSList l);
tsapi int TSListLen(TSList l); /* returns -1 if list is invalid */
tsapi bool TSListIsValid(TSList l);

/*--- TSStringList operations --------------------------------------------*/
tsapi TSStringList TSStringListCreate();
tsapi void TSStringListDestroy(TSStringList strl);
tsapi TSMgmtError TSStringListEnqueue(TSStringList strl, char *str);
tsapi char *TSStringListDequeue(TSStringList strl);
tsapi bool TSStringListIsEmpty(TSStringList strl);
tsapi int TSStringListLen(TSStringList strl);
tsapi bool TSStringListIsValid(TSStringList strl);

/*--- TSIntList operations --------------------------------------------*/
tsapi TSIntList TSIntListCreate();
tsapi void TSIntListDestroy(TSIntList intl);
tsapi TSMgmtError TSIntListEnqueue(TSIntList intl, int *str);
tsapi int *TSIntListDequeue(TSIntList intl);
tsapi bool TSIntListIsEmpty(TSIntList intl);
tsapi int TSIntListLen(TSIntList intl);
tsapi bool TSIntListIsValid(TSIntList intl, int min, int max);

tsapi TSMgmtEvent *TSEventCreate();
tsapi void TSEventDestroy(TSMgmtEvent *event);
tsapi TSRecordEle *TSRecordEleCreate();
tsapi void TSRecordEleDestroy(TSRecordEle *ele);

/***************************************************************************
 * API Core
 ***************************************************************************/

/*--- api initialization and shutdown -------------------------------------*/
/* TSInit: initializations required for API clients
 * Input: socket_path - not applicable for local clients
 *                      for remote users, the path to the config directory.
 *                      If == NULL, we use the Layout engine by default.
 *        options - Control some features of the APIs
 * Output: TS_ERR_xx
 * Note: If remote client successfully connects, returns TS_ERR_OKAY; but
 *       even if not successful connection (eg. client program is started
 *       before TM) then can still make API calls and will try connecting then
 */
tsapi TSMgmtError TSInit(const char *socket_path, TSInitOptionT options);

/* TSTerminate: does clean up for API clients
 * Input: <none>
 * Output: <none>
 */
tsapi TSMgmtError TSTerminate();

/*--- plugin initialization -----------------------------------------------*/
/* TSPluginInit: called by traffic_manager to initialize the plugin
 * Input:  argc - argument count
 *         argv - argument array
 * Output: <none>
 */
inkexp extern void TSPluginInit(int argc, const char *argv[]);

/*--- network operations --------------------------------------------------*/
/* UNIMPLEMENTED: used for remote clients on a different machine */
tsapi TSMgmtError TSConnect(TSIpAddr ip_addr, int port);
tsapi TSMgmtError TSDisconnectCbRegister(TSDisconnectFunc *func, void *data);
tsapi TSMgmtError TSDisconnectRetrySet(int retries, int retry_sleep_msec);
tsapi TSMgmtError TSDisconnect();

/*--- control operations --------------------------------------------------*/
/* TSProxyStateGet: get the proxy state (on/off)
 * Input:  <none>
 * Output: proxy state (on/off)
 */
tsapi TSProxyStateT TSProxyStateGet();

/* TSProxyStateSet: set the proxy state (on/off)
 * Input:  proxy_state - set to on/off
 *         clear - a TSCacheClearT bitmask,
 *            specifies if want to start TS with clear_cache or
 *            clear_cache_hostdb option, or just run TS with no options;
 *            only applies when turning proxy on
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSProxyStateSet(TSProxyStateT proxy_state, unsigned clear);

/* TSProxyBacktraceGet: get a backtrace of the proxy
 * Input:  unsigned options - stack trace options
 * Output: formatted backtrace of the proxy
 * 	the caller must free this with TSfree
 */
tsapi TSMgmtError TSProxyBacktraceGet(unsigned, TSString *);

/* TSReconfigure: tell traffic_server to re-read its configuration files
 * Input:  <none>
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSReconfigure();

/* TSRestart: restarts Traffic Manager and Traffic Server
 * Input: options - bitmask of TSRestartOptionT
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSRestart(unsigned options);

/* TSActionDo: based on TSActionNeedT, will take appropriate action
 * Input: action - action that needs to be taken
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSActionDo(TSActionNeedT action);

/* TSBounce: restart the traffic_server process(es).
 * Input: options - bitmask of TSRestartOptionT
 * Output TSMgmtError
 */
tsapi TSMgmtError TSBounce(unsigned options);

/* TSStop: stop the traffic_server process(es).
 * Input: options - bitmask of TSRestartOptionT
 * Output TSMgmtError
 */
tsapi TSMgmtError TSStop(unsigned options);

/* TSDrain: drain requests of the traffic_server process.
 * Input: options - TSDrainOptionT
 * Output TSMgmtError
 */
tsapi TSMgmtError TSDrain(unsigned options);

/* TSStorageDeviceCmdOffline: Request to make a cache storage device offline.
 * @arg dev Target device, specified by path to device.
 * @return Success.
 */
tsapi TSMgmtError TSStorageDeviceCmdOffline(const char *dev);

/* TSLifecycleMessage: Send a lifecycle message to the plugins.
 * @arg tag Alert tag string (null-terminated)
 * @return Success
 */
tsapi TSMgmtError TSLifecycleMessage(const char *tag, void const *data, size_t data_size);

/* TSGetErrorMessage: convert error id to error message
 * Input:  error id (defined in TSMgmtError)
 * Output: corresponding error message (allocated memory)
 */
char *TSGetErrorMessage(TSMgmtError error_id);

/* TSReadFromUrl: reads a remotely located config file into a buffer
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
tsapi TSMgmtError TSReadFromUrl(char *url, char **header, int *headerSize, char **body, int *bodySize);

/* TSReadFromUrl: reads a remotely located config file into a buffer
 * Input:  url        - remote location of the file
 *         header     - a buffer is allocated on the header char* pointer
 *         headerSize - the size of the header buffer is returned
 *         body       - a buffer is allocated on the body char* pointer
 *         bodySize   - the size of the body buffer is returned
 *         timeout    - the max. connection timeout value before aborting.
 * Output: TSMgmtError   - TS_ERR_OKAY if succeed, TS_ERR_FAIL otherwise
 * NOTE: The URL can be expressed in the following forms:
 *       - http://www.example.com:80/products/network/index.html
 *       - http://www.example.com/products/network/index.html
 *       - http://www.example.com/products/network/
 *       - http://www.example.com/
 *       - http://www.example.com
 *       - www.example.com
 * NOTE: header and headerSize can be NULL
 */
tsapi TSMgmtError TSReadFromUrlEx(const char *url, char **header, int *headerSize, char **body, int *bodySize, int timeout);
tsapi TSMgmtError TSHostStatusSetUp(const char *name);
tsapi TSMgmtError TSHostStatusSetDown(const char *name);
/*--- statistics operations -----------------------------------------------*/
/* TSStatsReset: sets all the statistics variables to their default values
 * Outpue: TSErrr
 */
tsapi TSMgmtError TSStatsReset(const char *name);

/*--- variable operations -------------------------------------------------*/
/* TSRecordGet: gets a record
 * Input:  rec_name - the name of the record (proxy.config.record_name)
 *         rec_val  - allocated TSRecordEle structure, value stored inside
 * Output: TSMgmtError (if the rec_name does not exist, returns TS_ERR_FAIL)
 */
tsapi TSMgmtError TSRecordGet(const char *rec_name, TSRecordEle *rec_val);

/* TSRecordGet*: gets a record w/ a known type
 * Input:  rec_name - the name of the record (proxy.config.record_name)
 *         *_val    - allocated TSRecordEle structure, value stored inside
 * Output: TSMgmtError
 * Note: For TSRecordGetString, the function will allocate memory for the
 *       *string_val, so the caller must free (*string_val);
 */
tsapi TSMgmtError TSRecordGetInt(const char *rec_name, TSInt *int_val);
tsapi TSMgmtError TSRecordGetCounter(const char *rec_name, TSCounter *counter_val);
tsapi TSMgmtError TSRecordGetFloat(const char *rec_name, TSFloat *float_val);
tsapi TSMgmtError TSRecordGetString(const char *rec_name, TSString *string_val);

/* TSRecordGetMlt: gets a set of records
 * Input:  rec_list - list of record names the user wants to retrieve;
 *                    resulting gets will be stored in the same list;
 *                    if one get fails, transaction will be aborted
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSRecordGetMlt(TSStringList rec_names, TSList rec_vals);

/* TSRecordGetMatchMlt: gets a set of records
 * Input:  rec_regex - regular expression to match against record names
 * Output: TSMgmtError, TSList of TSRecordEle
 */
tsapi TSMgmtError TSRecordGetMatchMlt(const char *rec_regex, TSList list);

/* TSRecordSet*: sets a record w/ a known type
 * Input:  rec_name     - the name of the record (proxy.config.record_name)
 *         *_val        - the value to set the record to
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: TSMgmtError
 */

tsapi TSMgmtError TSRecordSet(const char *rec_name, const char *val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetInt(const char *rec_name, TSInt int_val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetCounter(const char *rec_name, TSCounter counter_val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetFloat(const char *rec_name, TSFloat float_val, TSActionNeedT *action_need);
tsapi TSMgmtError TSRecordSetString(const char *rec_name, const char *string_val, TSActionNeedT *action_need);

/* TSConfigRecordDescribe: fetch a full description of a configuration record
 * Input: rec_name  - name of the record
 *        flags     - (unused) fetch flags bitmask
 *        val       - output value;
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSConfigRecordDescribe(const char *rec_name, unsigned flags, TSConfigRecordDescription *val);
tsapi TSMgmtError TSConfigRecordDescribeMatchMlt(const char *rec_regex, unsigned flags, TSList list);

/* TSRecordSetMlt: sets a set of records
 * Input:  rec_list     - list of record names the user wants to set;
 *                        if one set fails, transaction will be aborted
 *         *action_need - indicates which operation required by user for changes to take effect
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSRecordSetMlt(TSList rec_list, TSActionNeedT *action_need);

/*--- events --------------------------------------------------------------*/
/* Only a set of statically defined events exist. An event is either
 * active or inactive. An event is active when it is triggered, and
 * becomes inactive when resolved. Events are triggered and resolved
 * by specifying the event's name (which is predefined and immutable).
 */

/* UNIMPLEMENTED - wait for new alarm processor */
/* TSEventSignal: enables the user to trigger an event
 * Input:  event_name - "MGMT_ALARM_ADD_ALARM"
 *         ...        - variable argument list of parameters that go
 *                       go into event description when it is signalled
 * Output: TSMgmtError
 */
/*tsapi TSMgmtError               TSEventSignal (char *event_name, ...); */

/* TSEventResolve: enables the user to resolve an event
 * Input:  event_name - event to resolve
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventResolve(const char *event_name);

/* TSActiveEventGetMlt: query for a list of all the currently active events
 * Input:  active_events - an empty TSList; if function call is successful,
 *                         active_events will contain names of the currently
 *                         active events
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSActiveEventGetMlt(TSList active_events);

/* TSEventIsActive: check if the specified event is active
 * Input:  event_name - name of event to check if active; must be one of
 *                      the predefined names
 *         is_current - when function completes, if true, then the event is
 *                      active
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventIsActive(char *event_name, bool *is_current);

/* TSEventSignalCbRegister: register a callback for a specific event or
 *                           for any event
 * Input:  event_name - the name of event to register callback for;
 *                      if NULL, the callback is registered for all events
 *         func       - callback function
 *         data       - data to pass to callback
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventSignalCbRegister(char *event_name, TSEventSignalFunc func, void *data);

/* TSEventSignalCbUnregister: unregister a callback for a specific event
 *                             or for any event
 * Input: event_name - the name of event to unregister callback for;
 *                     if NULL, the callback is unregistered for all events
 *         func       - callback function
 * Output: TSMgmtError
 */
tsapi TSMgmtError TSEventSignalCbUnregister(char *event_name, TSEventSignalFunc func);

/*--- TS Cache Inspector Operations --------------------------------------------*/

/* TSLookupFromCacheUrl
 *   Function takes an url and an 'info' buffer as input,
 *   lookups cache information of the url and saves the
 *   cache info to the info buffer
 */
tsapi TSMgmtError TSLookupFromCacheUrl(TSString url, TSString *info);

/* TSLookupFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls that match the regex
 ********************************************************/

tsapi TSMgmtError TSLookupFromCacheUrlRegex(TSString url_regex, TSString *list);

/* TSDeleteFromCacheUrl
 *   Function takes an url and an 'info' buffer as input,
 *   deletes the url from cache if it's in the cache and
 *   returns the status of deletion
 ********************************************************/

tsapi TSMgmtError TSDeleteFromCacheUrl(TSString url, TSString *info);

/* TSDeleteFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls deleted from cache
 ********************************************************/

tsapi TSMgmtError TSDeleteFromCacheUrlRegex(TSString url_regex, TSString *list);

/* TSInvalidateFromCacheUrlRegex
 *   Function takes a string in a regex form and returns
 *   a list of urls invalidated from cache
 ********************************************************/

tsapi TSMgmtError TSInvalidateFromCacheUrlRegex(TSString url_regex, TSString *list);

#ifdef __cplusplus
}
#endif /* __cplusplus */
