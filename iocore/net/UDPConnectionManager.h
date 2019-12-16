/** @file

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

#include "UDPConnection.h"

class UDP2ConnectionManager : public Continuation
{
public:
  // create udp connection.
  UDP2ConnectionImpl *create_udp_connection(Continuation *con, EThread *ethread, sockaddr const *addr, sockaddr const *peer,
                                            int recv_buf = 0, int send_buf = 0);

  // create an accept udp connection.
  AcceptUDP2ConnectionImpl *create_accept_udp_connection(Continuation *c, EThread *thread, sockaddr *local, int recv_buf = 0,
                                                         int send_buf = 0);

  // UDP2Connection should removed by UDP2ConnectionManager
  // Do not call delete to free UDP2ConnectionImpl
  void close_connection(UDP2Connection *c, const char *line);

  int mainEvent(int event, void *data);

  UDP2ConnectionImpl *find_connection(sockaddr const *local, sockaddr const *peer);

  int
  size() const
  {
    return this->_size;
  }

  UDP2ConnectionManager(Ptr<ProxyMutex> &mutex) : Continuation(mutex) { SET_HANDLER(&UDP2ConnectionManager::mainEvent); }
  UDP2ConnectionManager(ProxyMutex *mutex) : Continuation(mutex) { SET_HANDLER(&UDP2ConnectionManager::mainEvent); }

private:
  // keep the closed connections, and delete it periodicly
  ASLL(UDP2Connection, closed_link) _closed_queue;

  // 2-tuple (dest ip and dest port) routes.
  std::unordered_map<uint64_t, std::list<UDP2ConnectionImpl *>> _routes;
  int _size = 0;
};
