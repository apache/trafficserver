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

#pragma once

#include "tscore/ink_platform.h"

/*-------------------------------------------------------------------------
  LogSock

  This class implements a multiplexed socket class that supports both
  client and server functionality.
  -------------------------------------------------------------------------*/

class LogSock
{
public:
  enum Constant {
    LS_CONST_PACKETSIZE = 1024,
    LS_CONST_MAX_CONNS  = 256,
  };

  enum Err {
    LS_ERROR_UNKNOWN            = -1,
    LS_ERROR_CONNECT_TABLE_FULL = -3,
    LS_ERROR_SOCKET             = -4,
    LS_ERROR_BIND               = -5,
    LS_ERROR_CONNECT            = -6,
    LS_ERROR_ACCEPT             = -7,
    LS_ERROR_NO_SUCH_HOST       = -8,
    LS_ERROR_NO_CONNECTION      = -9,
    LS_ERROR_STATE              = -10,
    LS_ERROR_WRITE              = -11,
    LS_ERROR_READ               = -12
  };

  enum State {
    LS_STATE_UNUSED = 0,
    LS_STATE_INCOMING,
    LS_STATE_OUTGOING,
    LS_N_STATES,
  };

  LogSock(int max_connects = 1);
  ~LogSock();

  bool pending_any(int *cid, int timeout_msec = 0);
  bool pending_message_any(int *cid, int timeout_msec = 0);
  bool pending_message_on(int cid, int timeout_msec = 0);
  bool pending_connect(int timeout_msec = 0);

  int listen(int accept_port, int family = AF_INET);
  int accept();
  int connect(sockaddr const *ip);

  void close(int cid); // this connection
  void close();        // all connections

  int write(int cid, void *buf, int bytes);

  int read(int cid, void *buf, unsigned maxsize);
  void *read_alloc(int cid, int *size);

  char *
  on_host()
  {
    return ct[0].host;
  }

  int
  on_port()
  {
    return ct[0].port;
  }

  bool is_connected(int cid, bool ping = false) const;
  void check_connections();
  bool authorized_client(int cid, char *key);
  char *connected_host(int cid);
  int connected_port(int cid);

  // noncopyable
  LogSock(const LogSock &) = delete;
  LogSock &operator=(const LogSock &) = delete;

private:
  struct ConnectTable {
    char *host;  // hostname for this connection
    int port;    // port number for this connection
    int sd;      // socket descriptor for this connection
    State state; // state of this entry
  };

  struct MsgHeader {
    int msg_bytes; // length of the following message
  };

  bool pending_data(int *cid, int timeout_msec, bool include_connects);
  int new_cid();
  void init_cid(int cid, char *host, int port, int sd, State state);
  int read_header(int sd, MsgHeader *header);
  int read_body(int sd, void *buf, int bytes);

  ConnectTable *ct; // list of all connections; index 0 is
  // the accept port.
  bool m_accept_connections; // do we accept new connections?
  int m_max_connections;     // max size of all tables
};
