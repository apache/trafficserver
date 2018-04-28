/** @file

  A client connection to the rpc server.

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

#include "mgmtapi.h"
#include "ts/ink_apidefs.h"
#include "utils/MgmtMarshall.h"

#include <utility>

/** client_reconnect

    Reconnects to the socket specified in the @c path. This function modifies the input @a server_fd 
    to the fd of the server or -1 if it is unable to connect. Essentially just a disconnect and 
    then a connect. 
 */
TSMgmtError client_reconnect(const char *path, int &server_fd);

/** client_connect 

    Connect to a socket specified in the @c path. If successful, the @a server_fd is populated with the 
    fd for the rpc server. If unsuccessful, it will return a TSMgmtError != TS_ERR_OKAY and set.
    @c server_fd to -1. 
  */
TSMgmtError client_connect(const char *path, int &server_fd);

/** client_disconnect

    Disconnects from the fd specified in the @c server_fd and sets @c server_fd to -1.
 */
TSMgmtError client_disconnect(int &server_fd);

/** client_request

    Attempts to send a request to the rpc server. Requires a @a fd to the server and a @c sock_path for reconnections.
    The function parameters are marshalled into a buffer and the entire buffer is sent as MgmtMarshallData object. The reason
    for this is that the server will just pass the entire buffer over to the corresponding handler function which then parses
    the buffer. 

    In addition, the operation type, @a optype is also marshalled as part of the message. The @a optype is required by the server
    to figure out the corresponding handler function. The server itself will strip this away from the buffer passed to the handler 
    function. Therefore, in the corresponding handler function only @a params should be parsed from the buffer. ie. 

    client side request : client_request(fd, OP, MgmtMarshallInt, MgmtMarshallInt);
    server side handler : mgmt_message_parse(MgmtMarshallInt, MgmtMarshallInt); // don't need to parse OP. 
 */ 
template <typename... Params>
TSMgmtError
client_request(int fd, const char* sock_path, MgmtMarshallInt optype, Params &&... params)
{
  static constexpr unsigned retries = 5;

  MgmtMarshallInt msglen;
  MgmtMarshallData req = {nullptr, 0};

  // Figure out the payload length.
  msglen = mgmt_message_length(&optype, std::forward<Params>(params)...);

  ink_assert(msglen >= 0);

  req.ptr = (char *)ats_malloc(msglen);
  req.len = msglen;

  // Marshall the message itself.
  if (mgmt_message_marshall(req.ptr, req.len, &optype, std::forward<Params>(params)...) == -1) {
    ats_free(req.ptr);
    return TS_ERR_PARAMS;
  }

  for(unsigned i = 0; i < retries; ++i) {
    // Send the response as the payload of a data object.
    if (mgmt_message_write(fd, &req) != -1) {
      ats_free(req.ptr);
      return TS_ERR_OKAY;
    }
    
    // try to reconnect. 
    TSMgmtError err = client_reconnect(sock_path, fd);
    if(err != TS_ERR_OKAY) {
      return err;
    }
  }

  ats_free(req.ptr);
  return TS_ERR_NET_WRITE;

}

/** client_get_response 

    Parses responses from the rpc server. Every message from the rpc server should be sent in the form
    of a MgmtMarshallData object (essentially a buffer). The MgmtMarshallData object is parsed based on 
    the input @a params... The first parameter in the rpc message should be a operation type. This should 
    be the same as the input @a optype to ensure that we are reading a message for the correct operation 
    return value. 

    If at any point, there is a mismatch between the parameter expected to be read and the parameter in the
    buffer, there is a Fatal call and the remote client should terminate. 
 */ 
template <typename... Params>
TSMgmtError
client_get_response(int fd, MgmtMarshallInt optype, Params &&... params)
{
  ssize_t ret;
  MgmtMarshallInt op;
  MgmtMarshallData data = {nullptr, 0};

  ret = mgmt_message_read(fd, &data);
  if (ret == -1) {
    ats_free(data.ptr);
    return TS_ERR_FAIL;
  }

  ret = mgmt_message_parse(data.ptr, data.len, &op, std::forward<Params>(params)...);
  ats_free(data.ptr);
  if (ret == -1 || op != optype) { // we got an invalid response
    return TS_ERR_FAIL;
  }

  return TS_ERR_OKAY;
}

/** client_get_response
  
    Special case of templated client_get_response. In this case, we are not expecting any data from the
    server. The only thing sent from the server is a MgmtMarshallInt err code indicating if the local function
    call was successful. This function gets the err code from from the socket and returns it as a TSMgmtError. 
 */ 
TSMgmtError client_get_response(int fd, MgmtMarshallInt optype);
