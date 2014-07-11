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

extern "C"
{
  struct sockaddr_in;
}

// DG: Added NO_THR state for init in compile warning
enum UIthr_t
{
  NO_THR = 0,
  AUTOCONF_THR
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
};

struct WebInterFaceGlobals
{
  ink_mutex serviceThrLock;
  ink_mutex submitLock;
  ink_semaphore serviceThrCount;
  serviceThr_t *serviceThrArray;
};

extern WebInterFaceGlobals wGlobals;
extern WebContext autoconfContext;

#define MAX_SERVICE_THREADS 100

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
#define WEB_HTTP_SERVER_STATE_AUTOCONF     0x20

#define WEB_HTTP_STATE_CONFIGURE           0x01 // MONITOR if bit is 0
#define WEB_HTTP_STATE_MORE_DETAIL         0x02 // LESS_DETAIL if bit is 0
#define WEB_HTTP_STATE_SUBMIT_WARN         0x04 // set if submission warning
#define WEB_HTTP_STATE_SUBMIT_NOTE         0x08 // set if submission note

#define WEB_MAX_PAGE_QUERY_LEN             (32+1)
#define WEB_MAX_EDIT_FILE_SIZE             (32*1024)    // Some browsers limit you to this

struct WebHttpConInfo
{
  int fd;
  WebContext *context;
  sockaddr_in *clientInfo;
};

#endif
