/** @file

Server RPC Manager

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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_assert.h"
#include "ts/ink_string.h"
#include "utils/MgmtSocket.h"
#include "ClientControl.h"

// get the err code from the socket.
TSMgmtError
client_get_response(int fd, MgmtMarshallInt optype)
{
  ssize_t ret;
  MgmtMarshallInt op;
  MgmtMarshallInt err;
  MgmtMarshallData data = {nullptr, 0};

  ret = mgmt_message_read(fd, &data);
  if (ret == -1) {
    ats_free(data.ptr);
    return TS_ERR_FAIL;
  }

  ret = mgmt_message_parse(data.ptr, data.len, &op, &err);
  ats_free(data.ptr);
  if (ret == -1 || op != optype) { // we got an invalid response
    return TS_ERR_FAIL;
  }

  // return the err code sent from the rpc server.
  return (TSMgmtError)err;
}

TSMgmtError
client_connect(const char* path, int &server_fd)
{
  struct sockaddr_un client_sock;

  int sockaddr_len;
  // make sure a socket path is set up
  if (path == nullptr) {
    goto ERROR;
  }
  // make sure the length of path do not exceed the sizeof(sun_path)
  if (strlen(path) > sizeof(client_sock.sun_path) - 1) {
    goto ERROR;
  }

  // create a socket
  server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd < 0) {
    goto ERROR; // ERROR - can't open socket
  }
  // setup Unix domain socket
  memset(&client_sock, 0, sizeof(sockaddr_un));
  client_sock.sun_family = AF_UNIX;
  ink_strlcpy(client_sock.sun_path, path, sizeof(client_sock.sun_path));
#if defined(darwin) || defined(freebsd)
  sockaddr_len = sizeof(sockaddr_un);
#else
  sockaddr_len = sizeof(client_sock.sun_family) + strlen(client_sock.sun_path);
#endif
  // connect call
  if (connect(server_fd, (struct sockaddr *)&client_sock, sockaddr_len) < 0) {
    close(server_fd);
    server_fd = -1;
    goto ERROR; // connection is down
  }
  return TS_ERR_OKAY;

ERROR:
  return TS_ERR_NET_ESTABLISH;
}

TSMgmtError
client_disconnect(int &server_fd)
{
  int ret;
  if (server_fd > 0) {
    ret       = close(server_fd);
    server_fd = -1;
    if (ret < 0) {
      return TS_ERR_FAIL;
    }
  }
  return TS_ERR_OKAY;
}

TSMgmtError
client_reconnect(const char* sock_path, int &server_fd)
{
  if(sock_path == nullptr) {
    return TS_ERR_NET_ESTABLISH;
  }

  TSMgmtError err;

  err = client_disconnect(server_fd);
  if (err != TS_ERR_OKAY) { // problem disconnecting
    return err;
  }

  err = client_connect(sock_path, server_fd);
  if (err != TS_ERR_OKAY) { // problem establishing connection
    return err;
  }

  // makes sure the descriptor is writable
  if (mgmt_write_timeout(server_fd, MAX_TIME_WAIT, 0) <= 0) {
    return TS_ERR_NET_TIMEOUT;
  }

  return TS_ERR_OKAY;
}
