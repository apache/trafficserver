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

/**************************************************************************
  Connections

  Commonality across all platforms -- move out as required.

**************************************************************************/

#include "P_Connection.h"

//
// Functions
//
char const *
NetVCOptions::toString(addr_bind_style s)
{
  return ANY_ADDR == s ? "any" : INTF_ADDR == s ? "interface" : "foreign";
}

Connection::Connection()
{
  memset(&addr, 0, sizeof(addr));
}

Connection::~Connection()
{
  close();
}

int
Connection::close()
{
  is_connected = false;
  is_bound     = false;
  // don't close any of the standards
  if (sock.get_fd() >= 2) {
    return sock.close();
  } else {
    sock = UnixSocket{NO_FD};
    return -EBADF;
  }
}

/**
 * Move control of the socket from the argument object orig to the current object.
 * Orig is marked as zombie, so when it is freed, the socket will not be closed
 */
void
Connection::move(Connection &orig)
{
  this->is_connected = orig.is_connected;
  this->is_bound     = orig.is_bound;
  this->sock         = orig.sock;
  // The target has taken ownership of the file descriptor
  orig.sock       = UnixSocket{NO_FD};
  this->addr      = orig.addr;
  this->sock_type = orig.sock_type;
}
