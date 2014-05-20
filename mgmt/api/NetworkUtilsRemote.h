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
 *          to a remote client connection.
 * Created: 8/9/00
 * Created by: lant
 *
 ***************************************************************************/

#ifndef _NETWORK_UTILS_H_
#define _NETWORK_UTILS_H_

#include "ink_defs.h"
#include "ink_mutex.h"

#include "mgmtapi.h"
#include "NetworkUtilsDefs.h"
#include "EventCallback.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

const int DEFAULT_STACK_SIZE = 1048576; // 1MB stack

/**********************************************************************
 * Socket Helper Functions
 **********************************************************************/
void set_socket_paths(const char *path);
int socket_test(int fd);

/* The following functions are specific for a client connection; uses
 * the client connection information stored in the variables in
 * NetworkUtilsRemote.cc
 */
TSMgmtError ts_connect(); /* TODO: update documenation, Renamed due to conflict with connect() in <sys/socket.h> on some platforms*/
TSMgmtError disconnect();
TSMgmtError reconnect();
TSMgmtError reconnect_loop(int num_attempts);
TSMgmtError connect_and_send(const char *msg, int msg_len);
void *socket_test_thread(void *arg);

/*****************************************************************************
 * Marshalling (create requests)
 *****************************************************************************/
TSMgmtError send_request(int fd, OpType op);
TSMgmtError send_request_name(int fd, OpType op, const char *name);
TSMgmtError send_request_name_value(int fd, OpType op, const char *name, const char *value);
TSMgmtError send_request_bool(int fd, OpType op, bool flag);

TSMgmtError send_file_read_request(int fd, TSFileNameT file);
TSMgmtError send_file_write_request(int fd, TSFileNameT file, int ver, int size, char *text);
TSMgmtError send_record_get_request(int fd, const char *rec_name);
TSMgmtError send_record_match_request(int fd, const char *rec_regex);

TSMgmtError send_proxy_state_set_request(int fd, TSProxyStateT state, TSCacheClearT clear);

TSMgmtError send_register_all_callbacks(int fd, CallbackTable * cb_table);
TSMgmtError send_unregister_all_callbacks(int fd, CallbackTable * cb_table);

TSMgmtError send_diags_msg(int fd, TSDiagsT mode, const char *diag_msg);

/*****************************************************************************
 * Un-marshalling (parse responses)
 *****************************************************************************/
TSMgmtError parse_reply(int fd);
TSMgmtError parse_reply_list(int fd, char **list);

TSMgmtError parse_file_read_reply(int fd, int *version, int *size, char **text);

TSMgmtError parse_record_get_reply(int fd, TSRecordT * rec_type, void **rec_val, char **rec_name);
TSMgmtError parse_record_set_reply(int fd, TSActionNeedT * action_need);

TSMgmtError parse_proxy_state_get_reply(int fd, TSProxyStateT * state);

TSMgmtError parse_event_active_reply(int fd, bool * is_active);
TSMgmtError parse_event_notification(int fd, TSMgmtEvent * event);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
