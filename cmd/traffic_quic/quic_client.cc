/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "quic_client.h"

QUICClient::~QUICClient()
{
  freeaddrinfo(this->_remote_addr_info);
}

void
QUICClient::start()
{
  SET_HANDLER(&QUICClient::state_http_server_open);

  struct addrinfo hints;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags    = 0;
  hints.ai_protocol = 0;

  int res = getaddrinfo(this->_remote_addr, this->_remote_port, &hints, &this->_remote_addr_info);
  if (res < 0) {
    Debug("quic_client", "Error: %s (%d)", strerror(errno), errno);
    return;
  }

  for (struct addrinfo *info = this->_remote_addr_info; info != nullptr; info = info->ai_next) {
    NetVCOptions opt;
    opt.ip_proto            = NetVCOptions::USE_UDP;
    opt.ip_family           = info->ai_family;
    opt.etype               = ET_NET;
    opt.socket_recv_bufsize = 1048576;
    opt.socket_send_bufsize = 1048576;

    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

    Action *action = quic_NetProcessor.connect_re(this, info->ai_addr, &opt);
    if (action == ACTION_RESULT_DONE) {
      break;
    }
  }
}

// Similar to HttpSM::state_http_server_open(int event, void *data)
int
QUICClient::state_http_server_open(int event, void *data)
{
  switch (event) {
  case NET_EVENT_OPEN: {
    // TODO: create ProxyServerSession / ProxyServerTransaction
    // TODO: send HTTP/0.9 message
    Debug("quic_client", "start proxy server ssn/txn");
    break;
  }
  case NET_EVENT_OPEN_FAILED: {
    ink_assert(false);
    break;
  }
  case NET_EVENT_ACCEPT: {
    // do nothing
    break;
  }
  default:
    ink_assert(false);
  }

  return 0;
}
