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

#include "ink_port.h"
#include "ink_mutex.h"

#include "INKMgmtAPI.h"
#include "NetworkUtilsDefs.h"
#include "EventCallback.h"

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/**********************************************************************
 * Socket Helper Functions
 **********************************************************************/
void set_socket_paths(const char *path);
int socket_test(int fd);

/* The following functions are specific for a client connection; uses
 * the client connection information stored in the variables in 
 * NetworkUtilsRemote.cc 
 */
INKError ts_connect(); /* TODO: update documenation, Renamed due to conflict with connect() in <sys/socket.h> on some platforms*/
INKError disconnect();
INKError reconnect();
INKError reconnect_loop(int num_attempts);
INKError connect_and_send(const char *msg, int msg_len);
INKError socket_write_conn(int fd, const char *msg_buf, int bytes);
void *socket_test_thread(void *arg);

/*****************************************************************************
 * Marshalling (create requests) 
 *****************************************************************************/
INKError send_request(int fd, OpType op);
INKError send_request_name(int fd, OpType op, char *name);
INKError send_request_name_value(int fd, OpType op, const char *name, const char *value);

INKError send_file_read_request(int fd, INKFileNameT file);
INKError send_file_write_request(int fd, INKFileNameT file, int ver, int size, char *text);
INKError send_record_get_request(int fd, char *rec_name);

INKError send_proxy_state_set_request(int fd, INKProxyStateT state, INKCacheClearT clear);
INKError send_restart_request(int fd, bool cluster);

INKError send_register_all_callbacks(int fd, CallbackTable * cb_table);
INKError send_unregister_all_callbacks(int fd, CallbackTable * cb_table);

INKError send_diags_msg(int fd, INKDiagsT mode, const char *diag_msg);

/*****************************************************************************
 * Un-marshalling (parse responses) 
 *****************************************************************************/
INKError parse_reply(int fd);
INKError parse_reply_list(int fd, char **list);

INKError parse_file_read_reply(int fd, int *version, int *size, char **text);

INKError parse_record_get_reply(int fd, INKRecordT * rec_type, void **rec_val);
INKError parse_record_set_reply(int fd, INKActionNeedT * action_need);

INKError parse_proxy_state_get_reply(int fd, INKProxyStateT * state);

INKError parse_event_active_reply(int fd, bool * is_active);
INKError parse_event_notification(int fd, INKEvent * event);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
