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

#include "ts/ink_config.h"
#include "ts/ink_defs.h"
#include "ts/ink_assert.h"
#include "ts/ink_memory.h"
#include "mgmtapi.h"
#include "NetworkMessage.h"

#define MAX_OPERATION_BUFSZ 1024
#define MAX_OPERATION_FIELDS 16

TSMgmtError
send_mgmt_error(int fd, OpType optype, TSMgmtError error)
{
  MgmtMarshallInt ecode     = error;
  MgmtMarshallInt intval    = 0;
  MgmtMarshallData dataval  = {nullptr, 0};
  MgmtMarshallString strval = nullptr;

  // Switch on operations, grouped by response format.
  switch (optype) {
  case OpType::BOUNCE:
  case OpType::STOP:
  case OpType::DRAIN:
  case OpType::EVENT_RESOLVE:
  case OpType::LIFECYCLE_MESSAGE:
  case OpType::PROXY_STATE_SET:
  case OpType::RECONFIGURE:
  case OpType::RESTART:
  case OpType::STATS_RESET_NODE:
  case OpType::HOST_STATUS_UP:
  case OpType::HOST_STATUS_DOWN:
  case OpType::STORAGE_DEVICE_CMD_OFFLINE:
    return send_mgmt_response(fd, optype, &ecode);

  case OpType::RECORD_SET:
  case OpType::PROXY_STATE_GET:
  case OpType::EVENT_ACTIVE:
    return send_mgmt_response(fd, optype, &ecode, &intval);

  case OpType::EVENT_GET_MLT:
  case OpType::SERVER_BACKTRACE:
    return send_mgmt_response(fd, optype, &ecode, &strval);

  case OpType::RECORD_GET:
  case OpType::RECORD_MATCH_GET:
    return send_mgmt_response(fd, optype, &ecode, &intval, &intval, &strval, &dataval);

  case OpType::RECORD_DESCRIBE_CONFIG:
    return send_mgmt_response(fd, optype, &ecode, &strval /* name */, &dataval /* value */, &dataval /* default */,
                              &intval /* type */, &intval /* class */, &intval /* version */, &intval /* rsb */,
                              &intval /* order */, &intval /* access */, &intval /* update */, &intval /* updatetype */,
                              &intval /* checktype */, &intval /* source */, &strval /* checkexpr */);

  case OpType::EVENT_REG_CALLBACK:
  case OpType::EVENT_UNREG_CALLBACK:
  case OpType::EVENT_NOTIFY:
  case OpType::API_PING:
    /* no response for these */
    return TS_ERR_OKAY;

  case OpType::UNDEFINED_OP:
    return TS_ERR_OKAY;
  }

  // We should never get here unless OpTypes are added without
  // updating the switch statement above. Don't do that; this
  // code must be able to handle every OpType.

  ink_fatal("missing generic error support for type %d management message", static_cast<int>(optype));
  return TS_ERR_FAIL;
}

TSMgmtError
recv_mgmt_message(int fd, MgmtMarshallData &msg)
{
  if (mgmt_message_read(fd, &msg) == -1) {
    return TS_ERR_NET_READ;
  }

  return TS_ERR_OKAY;
}

OpType
extract_mgmt_request_optype(void *msg, size_t msglen)
{
  MgmtMarshallInt optype;

  if (mgmt_message_parse(msg, msglen, &optype) == -1) {
    return OpType::UNDEFINED_OP;
  }

  return (OpType)optype;
}
