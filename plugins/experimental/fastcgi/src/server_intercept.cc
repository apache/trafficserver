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

#include "server_intercept.h"

#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

#include <netinet/in.h>
#include "atscppapi/HttpMethod.h"
#include "atscppapi/Transaction.h"
#include "atscppapi/TransactionPlugin.h"
#include "utils_internal.h"
#include <atscppapi/Headers.h>
#include <atscppapi/utils.h>

#include "ats_fcgi_client.h"
#include "fcgi_config.h"
#include "server.h"
#include "server_connection.h"
#include "ats_fastcgi.h"
using namespace atscppapi;
using namespace ats_plugin;

ServerIntercept::~ServerIntercept()
{
  TSDebug(PLUGIN_NAME, "~ServerIntercept : Shutting down server intercept._request_id: %d", _request_id);
  _txn = nullptr;
  if (!outputCompleteState) {
    clientAborted = true;
    Server::server()->removeIntercept(_request_id);
  }
  TSStatIntIncrement(InterceptGlobal::respEndId, 1);
}

void
ServerIntercept::consume(const string &data, InterceptPlugin::RequestDataType type)
{
  if (type == InterceptPlugin::REQUEST_HEADER) {
    streamReqHeader(data);
  } else {
    streamReqBody(data);
  }
}

void
ServerIntercept::streamReqHeader(const string &data)
{
  if (!Server::server()->writeRequestHeader(_request_id)) {
    dataBuffered = true;
    clientHeader += data;
  }
}

void
ServerIntercept::streamReqBody(const string &data)
{
  TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] bodyCount: %d", __FUNCTION__, bodyCount++);
  if (!Server::server()->writeRequestBody(_request_id, data)) {
    dataBuffered = true;
    clientBody += data;
  }
}

void
ServerIntercept::handleInputComplete()
{
  TSDebug(PLUGIN_NAME, "[ServerIntercept:%s] Count : %d \t_request_id: %d", __FUNCTION__, emptyCount++, _request_id);
  if (!Server::server()->writeRequestBodyComplete(_request_id)) {
    return;
  }
  inputCompleteState = true;
}

bool
ServerIntercept::writeResponseChunkToATS(std::string &data)
{
  return InterceptPlugin::produce(data);
}

bool
ServerIntercept::setResponseOutputComplete()
{
  bool status         = false;
  status              = InterceptPlugin::setOutputComplete();
  outputCompleteState = true;
  Server::server()->removeIntercept(_request_id);
  return status;
}
