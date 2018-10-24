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

#include "ts/ts.h"
#include "ats_fastcgi.h"
#include "connection_pool.h"
#include "server_connection.h"
#include "server.h"

using namespace ats_plugin;
ConnectionPool::ConnectionPool(Server *server, TSEventFunc funcp)
  : _server(server), _funcp(funcp), _availableConn_mutex(TSMutexCreate()), _conn_mutex(TSMutexCreate())
{
  // TODO: For now we are setting maxConn as hard coded values
  ats_plugin::FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  _maxConn                              = gConfig->getMaxConnLength() / 6;
}

ConnectionPool::~ConnectionPool()
{
  TSDebug(PLUGIN_NAME, "Destroying connectionPool Obj...");
  TSMutexDestroy(_availableConn_mutex);
  TSMutexDestroy(_conn_mutex);
}

int
ConnectionPool::checkAvailability()
{
  return _available_connections.size();
}

ServerConnection *
ConnectionPool::getAvailableConnection()
{
  ServerConnection *conn = nullptr;
  if (!_available_connections.empty() && _connections.size() >= _maxConn) {
    TSDebug(PLUGIN_NAME, "%s: available connections %ld", __FUNCTION__, _available_connections.size());
    conn = _available_connections.front();
    _available_connections.pop_front();
    conn->setState(ServerConnection::READY);
    TSDebug(PLUGIN_NAME, "%s: available connections %ld. Connection from available pool, %p", __FUNCTION__,
            _available_connections.size(), conn);
  }

  if (_connections.size() < _maxConn) {
    TSDebug(PLUGIN_NAME, "%s: Setting up new connection, maxConn: %d", __FUNCTION__, _maxConn);
    conn = new ServerConnection(_server, _funcp);
    addConnection(conn);
  }
  return conn;
}

void
ConnectionPool::addConnection(ServerConnection *connection)
{
  _connections.push_back(connection);
}

void
ConnectionPool::reuseConnection(ServerConnection *connection)
{
  connection->readio.readEnable  = false;
  connection->writeio.readEnable = false;

  connection->setState(ServerConnection::READY);
  _available_connections.push_back(connection);
  TSDebug(PLUGIN_NAME, "%s: Connection added, available connections %ld", __FUNCTION__, _available_connections.size());
}

void
ConnectionPool::connectionClosed(ServerConnection *connection)
{
  _available_connections.remove(connection);
  _connections.remove(connection);
  delete connection;
}
