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
 * Filename: CoreAPIShared.h
 * Purpose: This file contains functions that are same for local and remote API
 * Created: 01/29/00
 * Created by: Lan Tran
 *
 *
 ***************************************************************************/

#pragma once

#include "mgmtapi.h"

#define NUM_EVENTS 19           // number of predefined TM events
#define MAX_EVENT_NAME_SIZE 100 // max length for an event name
#define MAX_RECORD_SIZE 20      // max length of buffer to hold record values

// LAN - BAD BHACK; copied from Alarms.h !!!!!
/* Must be same as defined in Alarms.h; the reason we had to
 * redefine them here is because the remote client also needs
 * access to these values for its event handling
 */
#define MGMT_ALARM_UNDEFINED 0

#define MGMT_ALARM_PROXY_PROCESS_DIED 1
#define MGMT_ALARM_PROXY_PROCESS_BORN 2
// Currently unused: 3
// Currently unused: 4
#define MGMT_ALARM_PROXY_CONFIG_ERROR 5
#define MGMT_ALARM_PROXY_SYSTEM_ERROR 6
// Currently unused: 7
#define MGMT_ALARM_PROXY_CACHE_ERROR 8
#define MGMT_ALARM_PROXY_CACHE_WARNING 9
#define MGMT_ALARM_PROXY_LOGGING_ERROR 10
#define MGMT_ALARM_PROXY_LOGGING_WARNING 11
// Currently unused: 12
// Currently unused: 13
#define MGMT_ALARM_CONFIG_UPDATE_FAILED 14
// Currently unused: 15
// Currently unused: 16
#define MGMT_ALARM_MGMT_CONFIG_ERROR 17

// used by TSReadFromUrl
#define HTTP_DIVIDER "\r\n\r\n"
#define URL_BUFSIZE 65536 // the max. length of URL obtainable (in bytes)
#define URL_TIMEOUT 5000  // the timeout value for send/recv HTTP in ms
#define HTTP_PORT 80
#define BUFSIZE 1024

// Flags for management API behaviour.
#define MGMT_API_PRIVILEGED 0x0001u

// used by TSReadFromUrl
TSMgmtError parseHTTPResponse(char *buffer, char **header, int *hdr_size, char **body, int *bdy_size);
TSMgmtError readHTTPResponse(int sock, char *buffer, int bufsize, uint64_t timeout);
TSMgmtError sendHTTPRequest(int sock, char *request, uint64_t timeout);
int connectDirect(const char *host, int port, uint64_t timeout);

// used for Events
int get_event_id(const char *event_name);
char *get_event_name(int id);
