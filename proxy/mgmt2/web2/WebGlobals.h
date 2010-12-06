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

#ifndef _WEB_GLOBALS_H_
#define _WEB_GLOBALS_H_

#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include "ink_thread.h"
#include "ink_bool.h"

#ifdef MGMT_WEB_TUNE
#include "ink_hrtime.h"
#include "WebTune.h"
#endif

/****************************************************************************
 *
 *  WebGlobals.h - Global Variables and types for the Web Interface
 *
 *
 *
 ****************************************************************************/

#include "ink_mutex.h"
#include "MgmtHashTable.h"
#include "WebHttpAuth.h"

extern "C"
{
  struct ssl_ctx_st;
  struct sockaddr_in;
}

// DG: Added NO_THR state for init in compile warning
enum UIthr_t
{
  NO_THR = 0,
  HTTP_THR,
  GRAPH_THR,
  CLI_THR,
  AUTOCONF_THR,
  OVERSEER_THR
};

struct serviceThr_t
{
  ink_thread threadId;
  int fd;                       // Client file descriptor the thread is working
  // with
  time_t startTime;

  UIthr_t type;
  bool waitingForJoin;
  bool alreadyShutdown;
  struct sockaddr_in *clientInfo;
#ifdef MGMT_WEB_TUNE
  WebTune *timing;
  int xactNumber;
#endif
};

// Each port that we server documents on
//   has context associated with it which
//   contains information for that interface
//
// Within the secure adminstration context, security
//  parameters can change at run time.  The struct
//  is copied for each connection.  Therefore, all
//  fields that need to change CAN NOT BE POINTERS
//  since prior transactions in the system will STILL
//  BE USING the memory pointed to.
//
struct WebContext
{
  const char *defaultFile;
  char *docRoot;
  int docRootLen;
  char *pluginDocRoot;
  int pluginDocRootLen;
  int adminAuthEnabled;
  WebHttpAuthUser admin_user;   // admin user (always available)
  MgmtHashTable *other_users_ht;        // other users (can change dynamically)
  MgmtHashTable *lang_dict_ht;  // language dictionary (tag to string)
  int SSLenabled;
  int AdvUIEnabled;             /* 0=Simple UI, 1=Full UI, 2=RNI UI */
  int FeatureSet;               /* bit field of features */
  ssl_ctx_st *SSL_Context;
};

struct WebInterFaceGlobals
{
  ink_mutex serviceThrLock;
  ink_mutex submitLock;
#if defined(darwin)
  ink_sem *serviceThrCount;
#else
  ink_sem serviceThrCount;
#endif
  serviceThr_t *serviceThrArray;
  int webPort;
  ink_thread_key tmpFile;       // used by WebFileEdit.cc
  ink_thread_key requestTSD;
  int logFD;
  bool logResolve;
  int refreshRate;
};

extern WebInterFaceGlobals wGlobals;
extern WebContext adminContext;
extern WebContext autoconfContext;

#define MAX_SERVICE_THREADS 100
#define MAX_VAR_LENGTH      256
#define MAX_VAL_LENGTH      512
#define MAX_PASSWD          32
#define FILE_NAME_MAX       255
#define MAX_CHECKSUM_LENGTH 32
#define REFRESH_RATE_MRTG   300

//-------------------------------------------------------------------------
// web2 items
//-------------------------------------------------------------------------

#define WEB_HTTP_ERR_OKAY                    0
#define WEB_HTTP_ERR_FAIL                   -1
#define WEB_HTTP_ERR_REQUEST_ERROR          -2
#define WEB_HTTP_ERR_REQUEST_FATAL          -3
#define WEB_HTTP_ERR_SESSION_EXPIRED        -4
#define WEB_HTTP_ERR_INVALID_CFG_RULE       -5

#define WEB_HTTP_SERVER_STATE_WIN32        0x01
#define WEB_HTTP_SERVER_STATE_UNIX         0x02
#define WEB_HTTP_SERVER_STATE_AUTH_ENABLED 0x08
#define WEB_HTTP_SERVER_STATE_SSL_ENABLED  0x10
#define WEB_HTTP_SERVER_STATE_AUTOCONF     0x20

#define WEB_HTTP_STATE_CONFIGURE           0x01 // MONITOR if bit is 0
#define WEB_HTTP_STATE_MORE_DETAIL         0x02 // LESS_DETAIL if bit is 0
#define WEB_HTTP_STATE_SUBMIT_WARN         0x04 // set if submission warning
#define WEB_HTTP_STATE_SUBMIT_NOTE         0x08 // set if submission note
#define WEB_HTTP_STATE_PLUGIN              0x10 // set if this a plugin request

#define WEB_MAX_PAGE_QUERY_LEN             (32+1)
#define WEB_MAX_EDIT_FILE_SIZE             (32*1024)    // Some browsers limit you to this

struct WebHttpConInfo
{
  int fd;
  WebContext *context;
  sockaddr_in *clientInfo;
};

#endif
