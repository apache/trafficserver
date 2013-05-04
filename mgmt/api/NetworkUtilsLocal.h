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

#include "ink_defs.h"
#include "WebUtils.h"           // for SocketInfo, socket_read, socket_write

#include "mgmtapi.h"
#include "NetworkUtilsDefs.h"

/*****************************************************************************
 * general socket functions
 *****************************************************************************/
TSError socket_flush(struct SocketInfo sock_info);
TSError socket_read_n(struct SocketInfo sock_info, char *buf, int bytes);
TSError socket_write_n(struct SocketInfo sock_info, const char *buf, int bytes);

/*****************************************************************************
 * Unmarshalling/marshalling
 *****************************************************************************/
TSError preprocess_msg(struct SocketInfo sock_info, OpType * op_t, char **msg);

TSError parse_request_name_value(char *req, char **name, char **val);
TSError parse_record_get_request(char *req, char **rec_name);
TSError parse_file_read_request(char *req, TSFileNameT * file);
TSError parse_file_write_request(char *req, TSFileNameT * file, int *ver, int *size, char **text);
TSError parse_diags_request(char *req, TSDiagsT * mode, char **diag_msg);
TSError parse_proxy_state_request(char *req, TSProxyStateT * state, TSCacheClearT * clear);

TSError send_reply(struct SocketInfo sock_info, TSError retval);
TSError send_reply_list(struct SocketInfo sock_info, TSError retval, char *list);

TSError send_record_get_reply(struct SocketInfo sock_info, TSError retval, void *val, int val_size,
                               TSRecordT rec_type);
TSError send_record_set_reply(struct SocketInfo sock_info, TSError retval, TSActionNeedT action_need);
TSError send_file_read_reply(struct SocketInfo sock_info, TSError retval, int ver, int size, char *text);
TSError send_proxy_state_get_reply(struct SocketInfo sock_info, TSProxyStateT state);

TSError send_event_active_reply(struct SocketInfo sock_info, TSError retval, bool active);

TSError send_event_notification(struct SocketInfo sock_info, TSEvent * event);

#endif
