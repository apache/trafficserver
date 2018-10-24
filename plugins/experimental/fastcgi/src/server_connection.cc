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

#include "server_connection.h"

#include "ats_fcgi_client.h"
#include "ats_fastcgi.h"
using namespace ats_plugin;
InterceptIOChannel::InterceptIOChannel() : vio(nullptr), iobuf(nullptr), reader(nullptr), total_bytes_written(0), readEnable(false)
{
}
InterceptIOChannel::~InterceptIOChannel()
{
  if (this->reader) {
    TSIOBufferReaderFree(this->reader);
  }

  if (this->iobuf) {
    TSIOBufferDestroy(this->iobuf);
  }
  vio                 = nullptr;
  total_bytes_written = 0;
}

void
InterceptIOChannel::read(TSVConn vc, TSCont contp)
{
  if (TSVConnClosedGet(vc)) {
    TSError("[InterceptIOChannel:%s] Connection Closed...", __FUNCTION__);
    return;
  }
  if (!this->iobuf) {
    this->iobuf  = TSIOBufferCreate();
    this->reader = TSIOBufferReaderAlloc(this->iobuf);
    this->vio    = TSVConnRead(vc, contp, this->iobuf, INT64_MAX);
    if (this->vio == nullptr) {
      TSError("[InterceptIOChannel:%s] ERROR While reading from server", __FUNCTION__);
      return;
    }
    TSDebug(PLUGIN_NAME, "[InterceptIOChannel:%s] ReadIO.vio :%p ", __FUNCTION__, this->vio);
  }
}

void
InterceptIOChannel::write(TSVConn vc, TSCont contp)
{
  TSReleaseAssert(this->vio == nullptr);
  TSReleaseAssert((this->iobuf = TSIOBufferCreate()));
  TSReleaseAssert((this->reader = TSIOBufferReaderAlloc(this->iobuf)));
  if (TSVConnClosedGet(contp)) {
    TSError("[%s] Connection Closed...", __FUNCTION__);
    return;
  }
  this->vio = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
}

void
InterceptIOChannel::phpWrite(TSVConn vc, TSCont contp, unsigned char *buf, int data_size, bool endflag)
{
  if (TSVConnClosedGet(vc)) {
    TSError("[InterceptIOChannel:%s] Connection Closed...", __FUNCTION__);
    return;
  }

  if (!this->iobuf) {
    this->iobuf  = TSIOBufferCreate();
    this->reader = TSIOBufferReaderAlloc(this->iobuf);
    this->vio    = TSVConnWrite(vc, contp, this->reader, INT64_MAX);
    if (this->vio == nullptr) {
      TSError("[InterceptIOChannel:%s] Error TSVIO returns null. ", __FUNCTION__);
      return;
    }
  }

  int num_bytes_written = TSIOBufferWrite(this->iobuf, (const void *)buf, data_size);
  if (num_bytes_written != data_size) {
    TSError("[InterceptIOChannel:%s] Error while writing to buffer! Attempted %d bytes but only "
            "wrote %d bytes",
            PLUGIN_NAME, data_size, num_bytes_written);
    return;
  }

  total_bytes_written += data_size;
  if (!endflag) {
    TSMutexLock(TSVIOMutexGet(vio));
    TSVIOReenable(this->vio);
    TSMutexUnlock(TSVIOMutexGet(vio));
    return;
  }

  this->readEnable = true;
  TSDebug(PLUGIN_NAME, "[%s] Done: %ld \tnBytes: %ld", __FUNCTION__, TSVIONDoneGet(this->vio), TSVIONBytesGet(this->vio));
}

ServerConnection::ServerConnection(Server *server, TSEventFunc funcp)
  : vc_(nullptr),
    _fcgiRequest(nullptr),
    _state(INITIATED),
    _server(server),
    _funcp(funcp),
    _contp(nullptr),
    _sConnInfo(nullptr),
    _requestId(0),
    _max_requests(0),
    _req_count(0)
{
  ats_plugin::FcgiPluginConfig *gConfig = InterceptGlobal::plugin_data->getGlobalConfigObj();
  _max_requests                         = gConfig->getMaxReqLength();
}

ServerConnection::~ServerConnection()
{
  TSDebug(PLUGIN_NAME, "Destroying server Connection Obj.ServerConn: %p ,request_id: %d,max_requests: %d, req_count: %d ", this,
          _requestId, _max_requests, _req_count);

  if (vc_) {
    TSVConnClose(vc_);
    vc_ = nullptr;
  }
  // XXX(oschaaf): check commmented line below.
  // readio.vio = writeio.vio = nullptr;
  _requestId    = 0;
  _max_requests = 0;
  _req_count    = 0;
  TSContDestroy(_contp);
  _contp = nullptr;
  if (_fcgiRequest != nullptr)
    delete _fcgiRequest;
  delete _sConnInfo;
}

void
ServerConnection::createFCGIClient(ServerIntercept *intercept)
{
  if (_state == READY || _state == COMPLETE) {
    Transaction &transaction = utils::internal::getTransaction(intercept->_txn);
    transaction.addPlugin(intercept);
    transaction.resume();
    _fcgiRequest = new FCGIClientRequest(_requestId, intercept->_txn);
    _state       = INUSE;
    _req_count++;
  }
}

void
ServerConnection::releaseFCGIClient()
{
  if (_state == COMPLETE) {
    TSDebug(PLUGIN_NAME,
            "[ServerConnection:%s] Release FCGI resource of ServerConn: %p ,request_id: %d,max_requests: %d, req_count: %d ",
            __FUNCTION__, this, _requestId, _max_requests, _req_count);
    delete _fcgiRequest;
    _fcgiRequest = nullptr;
    _state       = READY;
  }
}

void
ServerConnection::createConnection()
{
  struct sockaddr_in ip_addr;
  unsigned short int a, b, c, d, p;
  char *arr  = InterceptGlobal::plugin_data->getGlobalConfigObj()->getServerIp();
  char *port = InterceptGlobal::plugin_data->getGlobalConfigObj()->getServerPort();
  sscanf(arr, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
  sscanf(port, "%hu", &p);
  int new_ip = (a << 24) | (b << 16) | (c << 8) | (d);
  memset(&ip_addr, 0, sizeof(ip_addr));
  ip_addr.sin_family      = AF_INET;
  ip_addr.sin_addr.s_addr = htonl(new_ip); /* Should be in network byte order */
  ip_addr.sin_port        = htons(p);      // server_port;

  // contp is a global netconnect handler  which will be used to connect with
  // php server
  _contp     = TSContCreate(_funcp, TSMutexCreate());
  _sConnInfo = new ServerConnectionInfo(_server, this);
  TSContDataSet(_contp, _sConnInfo);
  // TODO: Need to handle return value of NetConnect
  TSNetConnect(_contp, (struct sockaddr const *)&ip_addr);
}
