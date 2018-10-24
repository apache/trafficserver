/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <list>
#include "ts/ts.h"

namespace ats_plugin
{
class Server;
class ServerConnection;

// Connection Pool class which creates a pool of connections when certain
// threshold is reached. Possibly used connections also can be re-added to pool
// if connection does not close.

class ConnectionPool
{
public:
  ConnectionPool(Server *server, TSEventFunc funcp);
  ~ConnectionPool();

  ServerConnection *getAvailableConnection();
  int checkAvailability();

  void addConnection(ServerConnection *);
  void reuseConnection(ServerConnection *connection);
  void connectionClosed(ServerConnection *connection);

private:
  void createConnections();
  uint _maxConn;
  Server *_server;
  TSEventFunc _funcp;
  TSMutex _availableConn_mutex, _conn_mutex;
  std::list<ServerConnection *> _available_connections;
  std::list<ServerConnection *> _connections;
};
} // namespace ats_plugin
