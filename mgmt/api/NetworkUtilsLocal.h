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

/***************************************************************************
 * NetworkUtils.h
 *
 * Defines interface for marshalling requests and unmarshalling responses
 * between the remote API client and Traffic Manager
 *
 *
 ***************************************************************************/

/*****************************************************************************
 * NetworkUtils.h
 *
 * Defines interface for marshalling requests and unmarshalling responses
 * between the remote API client and Traffic Manager
 *****************************************************************************/

#ifndef _NETWORK_UTILS_H_
#define _NETWORK_UTILS_H_

#include "ink_port.h"
#include "WebUtils.h"           // for SocketInfo, socket_read, socket_write

#include "mgmtapi.h"
#include "NetworkUtilsDefs.h"

/*****************************************************************************
 * general socket functions
 *****************************************************************************/
INKError socket_flush(struct SocketInfo sock_info);
INKError socket_read_n(struct SocketInfo sock_info, char *buf, int bytes);
INKError socket_write_n(struct SocketInfo sock_info, const char *buf, int bytes);

/*****************************************************************************
 * Unmarshalling/marshalling
 *****************************************************************************/
INKError preprocess_msg(struct SocketInfo sock_info, OpType * op_t, char **msg);

INKError parse_request_name_value(char *req, char **name, char **val);
INKError parse_record_get_request(char *req, char **rec_name);
INKError parse_file_read_request(char *req, INKFileNameT * file);
INKError parse_file_write_request(char *req, INKFileNameT * file, int *ver, int *size, char **text);
INKError parse_diags_request(char *req, INKDiagsT * mode, char **diag_msg);
INKError parse_proxy_state_request(char *req, INKProxyStateT * state, INKCacheClearT * clear);

INKError send_reply(struct SocketInfo sock_info, INKError retval);
INKError send_reply_list(struct SocketInfo sock_info, INKError retval, char *list);

INKError send_record_get_reply(struct SocketInfo sock_info, INKError retval, void *val, int val_size,
                               INKRecordT rec_type);
INKError send_record_set_reply(struct SocketInfo sock_info, INKError retval, INKActionNeedT action_need);
INKError send_file_read_reply(struct SocketInfo sock_info, INKError retval, int ver, int size, char *text);
INKError send_proxy_state_get_reply(struct SocketInfo sock_info, INKProxyStateT state);

INKError send_event_active_reply(struct SocketInfo sock_info, INKError retval, bool active);

INKError send_event_notification(struct SocketInfo sock_info, INKEvent * event);

#endif
