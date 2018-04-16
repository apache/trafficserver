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

#pragma once

#include "MgmtMarshall.h"

#define REMOTE_DELIM ':'
#define REMOTE_DELIM_STR ":"

#define MAX_CONN_TRIES 10 // maximum number of attemps to reconnect to TM

// the possible operations or msg types sent from remote client to TM
enum class OpType : MgmtMarshallInt {
  RECORD_SET,
  RECORD_GET,
  PROXY_STATE_GET,
  PROXY_STATE_SET,
  RECONFIGURE,
  RESTART,
  BOUNCE,
  STOP,
  DRAIN,
  EVENT_RESOLVE,
  EVENT_GET_MLT,
  EVENT_ACTIVE,
  EVENT_REG_CALLBACK,
  EVENT_UNREG_CALLBACK,
  EVENT_NOTIFY, /* only msg sent from TM to client */
  STATS_RESET_NODE,
  STORAGE_DEVICE_CMD_OFFLINE,
  RECORD_MATCH_GET,
  API_PING,
  SERVER_BACKTRACE,
  RECORD_DESCRIBE_CONFIG,
  LIFECYCLE_MESSAGE,
  HOST_STATUS_UP,
  HOST_STATUS_DOWN,
  UNDEFINED_OP /* This must be last */
};

enum {
  RECORD_DESCRIBE_FLAGS_MATCH = 0x0001,
};

struct mgmt_message_sender {
  virtual TSMgmtError send(void *msg, size_t msglen) const = 0;
  virtual ~mgmt_message_sender(){};

  // Check if the sender is connected.
  virtual bool is_connected() const = 0;
};

// Marshall and send a request, prefixing the message length as a MGMT_MARSHALL_INT.
template <typename... Params>
TSMgmtError
send_mgmt_request(const mgmt_message_sender &snd, OpType optype, Params... params)
{
  ats_scoped_mem<char> msgbuf;
  MgmtMarshallInt msglen;

  if (!snd.is_connected()) {
    return TS_ERR_NET_ESTABLISH; // no connection.
  }

  msglen = mgmt_message_length(params...);
  msgbuf = (char *)ats_malloc(msglen + MGMT_HDR_LENGTH + 4);

  // Add data header
  memcpy((char *)msgbuf, &MGMT_DATA_HDR, MGMT_HDR_LENGTH);
  memcpy((char *)msgbuf + MGMT_HDR_LENGTH, &msglen, 4);

  // Now marshall the message itself.
  if (mgmt_message_marshall((char *)msgbuf + 4 + MGMT_HDR_LENGTH, msglen, params...) == -1) {
    return TS_ERR_PARAMS;
  }
  return snd.send(msgbuf, msglen + 4 + MGMT_HDR_LENGTH);
}

template <typename... Params>
TSMgmtError
send_mgmt_request(int fd, OpType optype, Params... params)
{
  MgmtMarshallInt msglen;
  MgmtMarshallData req = {nullptr, 0};

  // Figure out the payload length.
  msglen = mgmt_message_length(params...);

  ink_assert(msglen >= 0);

  req.ptr = (char *)ats_malloc(msglen);
  req.len = msglen;

  // Marshall the message itself.
  if (mgmt_message_marshall(req.ptr, req.len, params...) == -1) {
    ats_free(req.ptr);
    return TS_ERR_PARAMS;
  }

  // Send the response as the payload of a data object.
  if (mgmt_message_write(fd, &req) == -1) {
    ats_free(req.ptr);
    return TS_ERR_NET_WRITE;
  }

  ats_free(req.ptr);
  return TS_ERR_OKAY;
}

// Parse a request or response message from a buffer.
template <typename... Params>
TSMgmtError
parse_mgmt_message(void *buf, size_t buflen, OpType optype, Params... params)
{
  ssize_t err;

  err = mgmt_message_parse(buf, buflen, params...);
  return (err == -1) ? TS_ERR_PARAMS : TS_ERR_OKAY;
}

// Pull a management message (either request or response) off the wire.
TSMgmtError recv_mgmt_message(int fd, MgmtMarshallData &msg);

// Marshall and send a response, prefixing the message length as a MGMT_MARSHALL_INT.
// Send a management message response. We don't need to worry about retransmitting the message if we get
// disconnected, so this is much simpler. We can directly marshall the response as a data object.
// Note, there is no need to send the optype as this is a response not a request.
template <typename... Params>
TSMgmtError
send_mgmt_response(int fd, OpType optype, Params... params)
{
  MgmtMarshallInt msglen;
  MgmtMarshallData reply = {nullptr, 0};

  msglen = mgmt_message_length(params...);

  ink_assert(msglen >= 0);

  reply.ptr = (char *)ats_malloc(msglen);
  reply.len = msglen;

  // Marshall the message itself.
  if (mgmt_message_marshall(reply.ptr, reply.len, params...) == -1) {
    return TS_ERR_PARAMS;
  }

  // Send the response as the payload of a data object.
  if (mgmt_message_write(fd, &reply) == -1) {
    ats_free(reply.ptr);
    return TS_ERR_NET_WRITE;
  }

  ats_free(reply.ptr);
  return TS_ERR_OKAY;
}

// Marshall and send an error respose for this operation type.
TSMgmtError send_mgmt_error(int fd, OpType op, TSMgmtError error);

// Extract the first MGMT_MARSHALL_INT from the buffered message. This is the OpType.
OpType extract_mgmt_request_optype(void *msg, size_t msglen);