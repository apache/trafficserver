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

#include "server.h"

#include <sstream>

#include "server_intercept.h"
#include "server_connection.h"
#include "connection_pool.h"
#include "ats_fastcgi.h"
using namespace ats_plugin;

uint UniqueRequesID::_id = 1;

bool
interceptTransferData(ServerIntercept *intercept, ServerConnection *server_conn)
{
  TSIOBufferBlock block;
  int64_t consumed    = 0;
  bool responseStatus = false;
  std::ostringstream output;

  // Walk the list of buffer blocks in from the read VIO.
  for (block = TSIOBufferReaderStart(server_conn->readio.reader); block; block = TSIOBufferBlockNext(block)) {
    int64_t remain = 0;
    const char *ptr;
    ptr = TSIOBufferBlockReadStart(block, server_conn->readio.reader, &remain);

    if (remain) {
      responseStatus = server_conn->fcgiRequest()->fcgiDecodeRecordChunk((uchar *)ptr, remain, output);
    }
    consumed += remain;
  }

  if (consumed) {
    TSDebug(PLUGIN_NAME, "[%s] Read %ld bytes from server and writing it to client side.", __FUNCTION__, consumed);
    TSIOBufferReaderConsume(server_conn->readio.reader, consumed);
  }
  TSVIONDoneSet(server_conn->readio.vio, TSVIONDoneGet(server_conn->readio.vio) + consumed);

  std::string data = std::move(output.str());
  // TODO(oschaaf): check this if statement.
  if (data.size()) {
    // std::cout << "Output: " << data << std::endl;
    intercept->writeResponseChunkToATS(data);
  }
  return responseStatus;
}

static int
handlePHPConnectionEvents(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(PLUGIN_NAME, "[%s]:  event( %d )\tEventName: %s\tContp: %p ", __FUNCTION__, event, TSHttpEventNameLookup(event), contp);
  ServerConnectionInfo *conn_info     = (ServerConnectionInfo *)TSContDataGet(contp);
  Server *server                      = conn_info->server;
  ServerConnection *server_connection = conn_info->server_connection;

  switch (event) {
  case TS_EVENT_NET_CONNECT: {
    TSStatIntIncrement(InterceptGlobal::phpConnCount, 1);
    server_connection->vc_ = (TSVConn)edata;
    server_connection->setState(ServerConnection::READY);
    TSDebug(PLUGIN_NAME, "%s: New Connection success, %p", __FUNCTION__, server_connection);
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (intercept)
      server_connection->createFCGIClient(intercept);

  } break;

  case TS_EVENT_NET_CONNECT_FAILED: {
    // Try reconnection with new connection.
    // TODO: Have to stop trying to reconnect after some tries
    TSStatIntIncrement(InterceptGlobal::phpConnCount, 1);
    server->reConnect(server_connection->requestId());
    server_connection->setState(ServerConnection::CLOSED);
    server->connectionClosed(server_connection);
    return TS_EVENT_NONE;
  } break;

  case TS_EVENT_VCONN_READ_READY: {
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (intercept && interceptTransferData(intercept, server_connection)) {
      server_connection->setState(ServerConnection::COMPLETE);
      intercept->setResponseOutputComplete();
      TSStatIntIncrement(InterceptGlobal::respBegId, 1);
      // TSContCall(TSVIOContGet(server_connection->readio.vio), TS_EVENT_VCONN_READ_COMPLETE, server_connection->readio.vio);
    }
  } break;

  case TS_EVENT_VCONN_READ_COMPLETE: {
    TSDebug(PLUGIN_NAME, "[%s]: ResponseComplete...Sending Response to client stream. _request_id: %d", __FUNCTION__,
            server_connection->requestId());
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    server_connection->setState(ServerConnection::COMPLETE);
    intercept->setResponseOutputComplete();
    TSStatIntIncrement(InterceptGlobal::respBegId, 1);
  } break;

  case TS_EVENT_VCONN_WRITE_READY: {
    if (server_connection->writeio.readEnable) {
      TSContCall(TSVIOContGet(server_connection->writeio.vio), TS_EVENT_VCONN_WRITE_COMPLETE, server_connection->writeio.vio);
    }

  } break;

  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    TSStatIntIncrement(InterceptGlobal::reqEndId, 1);
    server_connection->readio.read(server_connection->vc_, server_connection->contp());
  } break;

  case TS_EVENT_VCONN_EOS: {
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (!server_connection->writeio.readEnable) {
      TSDebug(PLUGIN_NAME, "[%s]: EOS Request Failed. _request_id: %d, connection: %p,maxConn: %d, requestCount: %d", __FUNCTION__,
              server_connection->requestId(), server_connection, server_connection->maxRequests(),
              server_connection->requestCount());

      server->reConnect(server_connection->requestId());
      server_connection->setState(ServerConnection::CLOSED);
      server->connectionClosed(server_connection);
      break;
    }

    if (server_connection->getState() != ServerConnection::COMPLETE)
      if (intercept && !intercept->getOutputCompleteState()) {
        TSDebug(PLUGIN_NAME, "[%s]: EOS intercept->setResponseOutputComplete, _request_id: %d, connection: %p", __FUNCTION__,
                server_connection->requestId(), server_connection);
        Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
        transaction.error("Internal server error");
      }
    server_connection->setState(ServerConnection::CLOSED);
    server->connectionClosed(server_connection);
  } break;

  case TS_EVENT_ERROR: {
    TSVConnAbort(server_connection->vc_, 1);
    ServerIntercept *intercept = server->getIntercept(server_connection->requestId());
    if (intercept) {
      TSDebug(PLUGIN_NAME, "[%s]:ERROR  intercept->setResponseOutputComplete", __FUNCTION__);
      server_connection->setState(ServerConnection::CLOSED);
      Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
      transaction.setStatusCode(HTTP_STATUS_BAD_GATEWAY);
      transaction.error("Internal server error");
    }

    server->connectionClosed(server_connection);
  } break;

  default:
    break;
  }

  return TS_EVENT_NONE;
}

void
ThreadData::createConnectionPool(Server *server)
{
  _connection_pool = new ConnectionPool(server, handlePHPConnectionEvents);
}

Server *
Server::server()
{
  return InterceptGlobal::gServer;
}

Server::Server() : _reqId_mutex(TSMutexCreate()), _intecept_mutex(TSMutexCreate()) {}

bool
Server::setupThreadLocalStorage()
{
  int result = 0;
  if ((result = pthread_key_create(&InterceptGlobal::threadKey, nullptr)) == 0) {
    ThreadData *threadData;
    if ((threadData = static_cast<ThreadData *>(pthread_getspecific(InterceptGlobal::threadKey))) == nullptr) {
      threadData = new ThreadData(this);
      if (pthread_setspecific(InterceptGlobal::threadKey, threadData)) {
        TSDebug(PLUGIN_NAME, "[Server:%s] Unable to set threadData to the key", __FUNCTION__);
        pthread_key_delete(InterceptGlobal::threadKey);
        InterceptGlobal::threadKey = 0;
        return false;
      }

      TSStatIntIncrement(InterceptGlobal::threadCount, 1);
      TSDebug(PLUGIN_NAME, "[Server:%s] Data is set for this thread [threadData]%p [threadID]%lu", __FUNCTION__, threadData,
              pthread_self());
    }

    return true;
  }

  TSDebug(PLUGIN_NAME, "[Server:%s] Could not create key", __FUNCTION__);
  return false;
}

ServerIntercept *
Server::getIntercept(uint request_id)
{
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    TSMutexUnlock(_intecept_mutex);
    return std::get<0>(itr->second);
  }
  TSMutexUnlock(_intecept_mutex);
  return nullptr;
}

ServerConnection *
Server::getServerConnection(uint request_id)
{
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    TSMutexUnlock(_intecept_mutex);
    return std::get<1>(itr->second);
  }
  TSMutexUnlock(_intecept_mutex);
  return nullptr;
}

void
Server::removeIntercept(uint request_id)
{
  ThreadData *tdata = static_cast<ThreadData *>(pthread_getspecific(InterceptGlobal::threadKey));
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(request_id);
  if (itr != _intercept_list.end()) {
    ServerConnection *serv_conn = std::get<1>(itr->second);

    _intercept_list.erase(itr);
    TSMutexUnlock(_intecept_mutex);
    TSDebug(PLUGIN_NAME, "[Server:%s] ReqQueueLength:%d ,request_id: %d,ServerConn: %p ,max_requests: %d, req_count: %d ",
            __FUNCTION__, tdata->getRequestQueue()->getSize(), serv_conn->requestId(), serv_conn, serv_conn->maxRequests(),
            serv_conn->requestCount());

    serv_conn->releaseFCGIClient();
    serv_conn->setRequestId(0);
    // TODO(oschaaf): fix hang and re-enable.
    if (false && serv_conn->maxRequests() > serv_conn->requestCount()) {
      tdata->getConnectionPool()->reuseConnection(serv_conn);
    } else {
      serv_conn->setState(ServerConnection::CLOSED);
      connectionClosed(serv_conn);
    }

    ServerIntercept *intercept = tdata->getRequestQueue()->popFromQueue();
    if (intercept) {
      connect(intercept);
    }

    return;
  }
  TSMutexUnlock(_intecept_mutex);
  return;
}

bool
Server::writeRequestHeader(uint request_id)
{
  ServerConnection *server_conn = getServerConnection(request_id);
  if (!server_conn) {
    return false;
  }

  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Header: _request_id: %d,ServerConn: %p", __FUNCTION__, request_id,
          server_conn);

  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();
  unsigned char *clientReq;
  int reqLen = 0;
  // TODO: possibly move all this as one function in server_connection
  fcgiRequest->createBeginRequest();
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = false;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
  return true;
}

bool
Server::writeRequestBody(uint request_id, const string &data)
{
  ServerConnection *server_conn = getServerConnection(request_id);
  if (!server_conn) {
    return false;
  }

  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Body: request_id: %d,Server_conn: %p", __FUNCTION__, request_id, server_conn);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();
  // TODO: possibly move all this as one function in server_connection
  unsigned char *clientReq;
  int reqLen            = 0;
  fcgiRequest->postData = data;
  fcgiRequest->postBodyChunk();
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = false;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
  return true;
}

bool
Server::writeRequestBodyComplete(uint request_id)
{
  ServerConnection *server_conn = getServerConnection(request_id);
  if (!server_conn) {
    return false;
  }

  TSDebug(PLUGIN_NAME, "[Server::%s] : Write Request Complete: request_id: %d,Server_conn: %p", __FUNCTION__, request_id,
          server_conn);
  FCGIClientRequest *fcgiRequest = server_conn->fcgiRequest();
  // TODO: possibly move all this as one function in server_connection
  unsigned char *clientReq;
  int reqLen = 0;
  fcgiRequest->emptyParam();
  clientReq    = fcgiRequest->addClientRequest(reqLen);
  bool endflag = true;
  server_conn->writeio.phpWrite(server_conn->vc_, server_conn->contp(), clientReq, reqLen, endflag);
  return true;
}

const uint
Server::connect(ServerIntercept *intercept)
{
  // Get connections from thread Local storage and use it or store it in thread Queue
  ThreadData *tdata = static_cast<ThreadData *>(pthread_getspecific(InterceptGlobal::threadKey));
  if (tdata) {
    ServerConnection *conn = nullptr;
    conn                   = tdata->getConnectionPool()->getAvailableConnection();
    if (conn) {
      initiateBackendConnection(intercept, conn);
      return 0;
    }
    TSDebug(PLUGIN_NAME, "[Server:%s] : Added to RequestQueue. QueueSize: %d", __FUNCTION__, tdata->getRequestQueue()->getSize());
    tdata->getRequestQueue()->addToQueue(intercept);
    return 0;
  }
  return 1;
}

void
Server::reConnect(uint request_id)
{
  ServerIntercept *intercept = getIntercept(request_id);
  if (intercept) {
    TSMutexLock(_intecept_mutex);
    _intercept_list.erase(request_id);
    TSMutexUnlock(_intecept_mutex);
    connect(intercept);
    TSDebug(PLUGIN_NAME, "[Server:%s]: Initiating reconnection...", __FUNCTION__);
  }
}

void
Server::initiateBackendConnection(ServerIntercept *intercept, ServerConnection *conn)
{
  TSMutexLock(_reqId_mutex);
  const uint request_id = UniqueRequesID::getNext();
  TSMutexUnlock(_reqId_mutex);

  intercept->setRequestId(request_id);
  conn->setRequestId(request_id);

  TSMutexLock(_intecept_mutex);
  _intercept_list[request_id] = std::make_tuple(intercept, conn);
  TSMutexUnlock(_intecept_mutex);

  TSDebug(PLUGIN_NAME, "[Server: %s] ServerConn: %p,_request_id: %d", __FUNCTION__, conn, request_id);
  if (conn->getState() != ServerConnection::READY) {
    TSDebug(PLUGIN_NAME, "[Server: %s] Setting up a new php Connection..", __FUNCTION__);
    conn->createConnection();
    return;
  }

  conn->createFCGIClient(intercept);
  return;
}

void
Server::connectionClosed(ServerConnection *server_conn)
{
  TSMutexLock(_intecept_mutex);
  auto itr = _intercept_list.find(server_conn->requestId());
  if (itr != _intercept_list.end()) {
    _intercept_list.erase(itr);
  }
  TSMutexUnlock(_intecept_mutex);
  ThreadData *tdata = static_cast<ThreadData *>(pthread_getspecific(InterceptGlobal::threadKey));
  tdata->getConnectionPool()->connectionClosed(server_conn);
  TSStatIntDecrement(InterceptGlobal::phpConnCount, 1);
}
