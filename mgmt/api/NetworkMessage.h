/** @file

  Network message marshalling.

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

#ifndef _NETWORK_MESSAGE_H_
#define _NETWORK_MESSAGE_H_

#include "MgmtMarshall.h"

#define REMOTE_DELIM ':'
#define REMOTE_DELIM_STR ":"

#define MAX_CONN_TRIES 10 // maximum number of attemps to reconnect to TM

// the possible operations or msg types sent from remote client to TM
typedef enum {
  FILE_READ,
  FILE_WRITE,
  RECORD_SET,
  RECORD_GET,
  PROXY_STATE_GET,
  PROXY_STATE_SET,
  RECONFIGURE,
  RESTART,
  BOUNCE,
  EVENT_RESOLVE,
  EVENT_GET_MLT,
  EVENT_ACTIVE,
  EVENT_REG_CALLBACK,
  EVENT_UNREG_CALLBACK,
  EVENT_NOTIFY, /* only msg sent from TM to client */
  SNAPSHOT_TAKE,
  SNAPSHOT_RESTORE,
  SNAPSHOT_REMOVE,
  SNAPSHOT_GET_MLT,
  DIAGS,
  STATS_RESET_NODE,
  STATS_RESET_CLUSTER,
  STORAGE_DEVICE_CMD_OFFLINE,
  RECORD_MATCH_GET,
  API_PING,
  SERVER_BACKTRACE,
  RECORD_DESCRIBE_CONFIG,
  UNDEFINED_OP /* This must be last */
} OpType;

#define MGMT_OPERATION_TYPE_MAX (UNDEFINED_OP)

enum {
  RECORD_DESCRIBE_FLAGS_MATCH = 0x0001,
};

struct mgmt_message_sender {
  virtual TSMgmtError send(void *msg, size_t msglen) const = 0;
  virtual ~mgmt_message_sender(){};
};

// Marshall and send a request, prefixing the message length as a MGMT_MARSHALL_INT.
TSMgmtError send_mgmt_request(const mgmt_message_sender &snd, OpType optype, ...);
TSMgmtError send_mgmt_request(int fd, OpType optype, ...);

// Marshall and send an error respose for this operation type.
TSMgmtError send_mgmt_error(int fd, OpType op, TSMgmtError error);

// Parse a request message from a buffer.
TSMgmtError recv_mgmt_request(void *buf, size_t buflen, OpType optype, ...);

// Marshall and send a response, prefixing the message length as a MGMT_MARSHALL_INT.
TSMgmtError send_mgmt_response(int fd, OpType optype, ...);

// Parse a response message from a buffer.
TSMgmtError recv_mgmt_response(void *buf, size_t buflen, OpType optype, ...);

// Pull a management message (either request or response) off the wire.
TSMgmtError recv_mgmt_message(int fd, MgmtMarshallData &msg);

// Extract the first MGMT_MARSHALL_INT from the buffered message. This is the OpType.
OpType extract_mgmt_request_optype(void *msg, size_t msglen);

#endif /* _NETWORK_MESSAGE_H_ */
