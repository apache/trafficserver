/** @file
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

#include "intercept.h"

#include "Data.h"
#include "client.h"
#include "server.h"
#include "slice.h"

int
intercept_hook(TSCont contp, TSEvent event, void *edata)
{
  // DEBUG_LOG("intercept_hook: %d", event);

  Data *const data = static_cast<Data *>(TSContDataGet(contp));
  if (nullptr == data) {
    DEBUG_LOG("Events handled after data already torn down");
    TSContDestroy(contp);
    return TS_EVENT_ERROR;
  }

  // After the initial TS_EVENT_NET_ACCEPT
  // any "events" will be handled by the vio read or write channel handler
  switch (event) {
  case TS_EVENT_NET_ACCEPT: {
    // set up reader from client
    TSVConn const downvc = (TSVConn)edata;
    data->m_dnstream.setupConnection(downvc);
    data->m_dnstream.setupVioRead(contp);
  } break;

  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_VCONN_ACTIVE_TIMEOUT:
  case TS_EVENT_HTTP_TXN_CLOSE:
    delete data;
    TSContDestroy(contp);
    break;

  default: {
    // data from client -- only the initial header
    if (data->m_dnstream.m_read.isOpen() && edata == data->m_dnstream.m_read.m_vio) {
      if (handle_client_req(contp, event, data)) {
        // DEBUG_LOG("shutting down read from client pipe");
        TSVConnShutdown(data->m_dnstream.m_vc, 1, 0);
      }
    }
    // server wants more data from us, should never happen
    // every time TSHttpConnect is called this resets
    else if (data->m_upstream.m_write.isOpen() && edata == data->m_upstream.m_write.m_vio) {
      // DEBUG_LOG("shutting down send to server pipe");
      TSVConnShutdown(data->m_upstream.m_vc, 0, 1);
    }
    // server has data for us, typically handle just the header
    else if (data->m_upstream.m_read.isOpen() && edata == data->m_upstream.m_read.m_vio) {
      handle_server_resp(contp, event, data);
    }
    // client wants more data from us, only body content
    else if (data->m_dnstream.m_write.isOpen() && edata == data->m_dnstream.m_write.m_vio) {
      handle_client_resp(contp, event, data);
    } else {
      ERROR_LOG("Unhandled event: %d", event);
      /*
      std::cerr << __func__
              << ": events received after intercept state torn down"
              << std::endl;
      */
    }
  }
  }

  return TS_EVENT_CONTINUE;
}
