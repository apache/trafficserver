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
 * Filename: TSControlMain.h
 * Purpose: The main section for traffic server that TSMgmtError handles all the request
 *          from the user.
 * Created: 6/26/00
 * Created by: Stephanie Song
 *
 ***************************************************************************/

#pragma once

#include "mgmtapi.h"

#define REMOTE_DELIM ':'
#define REMOTE_DELIM_STR ":"

#define MAX_CONN_TRIES 10 // maximum number of attemps to reconnect to TM

// the possible operations or msg types sent from remote client to TM
#define RECORD_SET 0
#define RECORD_GET 1
#define PROXY_STATE_GET 2
#define PROXY_STATE_SET 3
#define RECONFIGURE 4
#define RESTART 5
#define BOUNCE 6
#define STOP 7
#define DRAIN 8
#define EVENT_RESOLVE 9
#define EVENT_GET_MLT 10
#define EVENT_ACTIVE 11
#define EVENT_REG_CALLBACK 12
#define EVENT_UNREG_CALLBACK 13
#define EVENT_NOTIFY 14 /* only msg sent from TM to client */
#define STATS_RESET_NODE 15
#define STORAGE_DEVICE_CMD_OFFLINE 16
#define RECORD_MATCH_GET 17
#define API_PING 18
#define SERVER_BACKTRACE 19
#define RECORD_DESCRIBE_CONFIG 20
#define LIFECYCLE_MESSAGE 21
#define HOST_STATUS_UP 22
#define HOST_STATUS_DOWN 23

enum {
  RECORD_DESCRIBE_FLAGS_MATCH = 0x0001,
};

/**
    All callback functions should be in the form: int, void*, size_t.
    These are all the callback functions for the CoreAPI functions. All these
    functions are loaded into the rpc server.

    Each function should expect to take in a marshalled message in the form of a
    void * which contains the correct parameters. It is the responsibility of the
    callback function to unmarshall the message into the parameters and execute the
    corresponding local api function call. Unmarshalling should be done with
    mgmt_marshall_parse because it will strip the header and check type info.
    Callback functions contain a @fd because they may need to send into back to remote
    clients (ie. config values).

    Note, the TSMgmtError returned by the handler is ultimately also sent back to the
    remote client. So, the remote api call should also expect a MgmtMarshallInt err
    after recieving any info from handler functions.
 */
TSMgmtError handle_record_set(int fd, void *req, size_t reqlen);
TSMgmtError handle_record_get(int fd, void *req, size_t reqlen);
TSMgmtError handle_proxy_state_get(int fd, void *req, size_t reqlen);
TSMgmtError handle_proxy_state_set(int fd, void *req, size_t reqlen);
TSMgmtError handle_reconfigure(int fd, void *req, size_t reqlen);
TSMgmtError handle_restart(int fd, void *req, size_t reqlen);
TSMgmtError handle_stop(int fd, void *req, size_t reqlen);
TSMgmtError handle_drain(int fd, void *req, size_t reqlen);
TSMgmtError handle_event_resolve(int fd, void *req, size_t reqlen);
TSMgmtError handle_event_get_mlt(int fd, void *req, size_t reqlen);
TSMgmtError handle_event_active(int fd, void *req, size_t reqlen);
TSMgmtError handle_stats_reset(int fd, void *req, size_t reqlen);
TSMgmtError handle_storage_device_cmd_offline(int fd, void *req, size_t reqlen);
TSMgmtError handle_record_match(int fd, void *req, size_t reqlen);
TSMgmtError handle_api_ping(int fd, void *req, size_t reqlen);
TSMgmtError handle_server_backtrace(int fd, void *req, size_t reqlen);
TSMgmtError handle_record_describe(int fd, void *req, size_t reqlen);
TSMgmtError handle_lifecycle_message(int fd, void *req, size_t reqlen);
TSMgmtError handle_host_status_up(int fd, void *req, size_t reqlen);
TSMgmtError handle_host_status_down(int fd, void *req, size_t reqlen);
