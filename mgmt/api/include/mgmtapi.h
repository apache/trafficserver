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
#define _TS_RES_PATH(x)  __TS_RES_PATH(x)
#define TS_RES_PATH(x)   x __FILE__ ":" _TS_RES_PATH(__LINE__)
#define TS_RES_MEM_PATH  TS_RES_PATH("memory/")
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
  TS_ACTION_SHUTDOWN,    /* change requires user to stop then start the Traffic Server and Manager */
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

typedef enum {
  TS_REC_INT,
  TS_REC_COUNTER,
  TS_REC_FLOAT,
  TS_REC_STRING,
  TS_REC_UNDEFINED,
} TSRecordT;

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

/***************************************************************************
 * Function Types
 ***************************************************************************/
typedef void (*TSEventSignalFunc)(char *name, char *msg, int pri, void *data);
typedef void (*TSDisconnectFunc)(void *data);

/***************************************************************************
 * API Memory Management
 ***************************************************************************/
#define TSmalloc(s)     _TSmalloc((s), TS_RES_MEM_PATH)
#define TSrealloc(p, s) _TSrealloc((p), (s), TS_RES_MEM_PATH)
#define TSstrdup(p)     _TSstrdup((p), -1, TS_RES_MEM_PATH)
#define TSstrndup(p, n) _TSstrdup((p), (n), TS_RES_MEM_PATH)
#define TSfree(p)       _TSfree(p)

tsapi void *_TSmalloc(size_t size, const char *path);
tsapi void *_TSrealloc(void *ptr, size_t size, const char *path);
tsapi char *_TSstrdup(const char *str, int64_t length, const char *path);
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

#ifdef __cplusplus
}
#endif /* __cplusplus */
