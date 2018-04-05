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
 * Filename: NetworkUtilsRemote.h
 * Purpose: This file contains functions used by remote api client to
 *          marshal requests to TM and unmarshal replies from TM.
 *          Also contains functions used to store information specific
 *          to a remote client connection. It helps to set up the necessary
 *          paths and fds for CoreAPIRemote.cc
 * Created: 8/9/00
 * Created by: lant
 *
 ***************************************************************************/

#pragma once

#include "mgmtapi.h"
#include "TSControlMain.h"
#include "EventCallback.h"
#include "rpc/ClientControl.h"

extern int main_socket_fd;
extern int event_socket_fd;

extern char *main_socket_path;
extern char *event_socket_path;

extern CallbackTable *remote_event_callbacks;

// From CoreAPIRemote.cc
extern ink_thread ts_event_thread;
extern TSInitOptionT ts_init_options;

/**********************************************************************
 * Socket Helper Functions
 **********************************************************************/
void set_socket_paths(const char *path);

/* The following functions are specific for a client connection; uses
 * the client connection information stored in the variables in
 * NetworkUtilsRemote.cc
 */
TSMgmtError ts_connect(); /* TODO: update documenation, Renamed due to conflict with connect() in <sys/socket.h> on some platforms*/
TSMgmtError disconnect();
TSMgmtError reconnect();
TSMgmtError reconnect_loop(int num_attempts);

void *socket_test_thread(void *arg);
void *event_poll_thread_main(void *arg);

#define MGMTAPI_MGMT_SOCKET_NAME "mgmtapi.sock"
#define MGMTAPI_EVENT_SOCKET_NAME "eventapi.sock"

/*****************************************************************************
 * Marshalling (create requests)
 *****************************************************************************/

TSMgmtError send_register_all_callbacks(int fd, const char *path, CallbackTable *cb_table);
TSMgmtError send_unregister_all_callbacks(int fd, const char *path, CallbackTable *cb_table);
