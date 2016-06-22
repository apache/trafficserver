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
 * NetworkUtilsLocal.cc
 *
 * contains implementation of local networking utility functions, such as
 * unmarshalling requests from a remote client and marshalling replies
 *
 *
 ***************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_sock.h"
#include "ts/Diags.h"
#include "MgmtUtils.h"
#include "MgmtSocket.h"
#include "MgmtMarshall.h"
#include "CoreAPIShared.h"
#include "NetworkUtilsLocal.h"
#include "NetworkMessage.h"

/**********************************************************************
 * preprocess_msg
 *
 * purpose: reads in all the message; parses the message into header info
 *          (OpType + msg_len) and the request portion (used by the handle_xx fns)
 * input: sock_info - socket msg is read from
 *        msg       - the data from the network message (no OpType or msg_len)
 * output: TS_ERR_xx ( if TS_ERR_OKAY, then parameters set successfully)
 * notes: Since preprocess_msg already removes the OpType and msg_len, this part o
 *        the message is not dealt with by the other parsing functions
 **********************************************************************/
TSMgmtError
preprocess_msg(int fd, void **req, size_t *reqlen)
{
  TSMgmtError ret;
  MgmtMarshallData msg;

  *req    = NULL;
  *reqlen = 0;

  ret = recv_mgmt_message(fd, msg);
  if (ret != TS_ERR_OKAY) {
    return ret;
  }

  // We should never receive an empty payload.
  if (msg.ptr == NULL) {
    return TS_ERR_NET_READ;
  }

  *req    = msg.ptr;
  *reqlen = msg.len;
  Debug("ts_main", "[preprocess_msg] read message length = %zd\n", msg.len);
  return TS_ERR_OKAY;
}
