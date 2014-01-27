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
 * Purpose: The main section for traffic server that handles all the request
 *          from the user.
 * Created: 6/26/00
 * Created by: Stephanie Song
 *
 ***************************************************************************/

#ifndef TS_CONTROL_MAIN_H
#define TS_CONTROL_MAIN_H

#include "WebUtils.h"           // for SocketInfo
#include "mgmtapi.h"
#include "NetworkUtilsDefs.h"

typedef struct
{
  SocketInfo sock_info;
  struct sockaddr *adr;
} ClientT;

ClientT *create_client();
void delete_client(ClientT * client);

void *ts_ctrl_main(void *arg);

TSError handle_record_get(struct SocketInfo sock_info, char *req);
TSError handle_record_set(struct SocketInfo sock_info, char *req);

TSError handle_file_read(struct SocketInfo sock_info, char *req);
TSError handle_file_write(struct SocketInfo sock_info, char *req);

TSError handle_proxy_state_get(struct SocketInfo sock_info);
TSError handle_proxy_state_set(struct SocketInfo sock_info, char *req);
TSError handle_reconfigure(struct SocketInfo sock_info);
TSError handle_restart(struct SocketInfo sock_info, char *req, bool bounce);
TSError handle_storage_device_cmd_offline(struct SocketInfo sock_info, char *req);

TSError handle_event_resolve(struct SocketInfo sock_info, char *req);
TSError handle_event_get_mlt(struct SocketInfo sock_info);
TSError handle_event_active(struct SocketInfo sock_info, char *req);

TSError handle_snapshot(struct SocketInfo sock_info, char *req, OpType op);
TSError handle_snapshot_get_mlt(struct SocketInfo sock_info);

void handle_diags(struct SocketInfo sock_info, char *req);

TSError handle_stats_reset(struct SocketInfo sock_info, char *req, OpType op);


#endif
