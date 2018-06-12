/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "slice.h"

//#include "TransformData.h"
#include "HttpHeader.h"
//#include "transform.h"

#include "ts/remap.h"
#include "ts/ts.h"

#include <cassert>
#include <iostream>

std::string
rangeRequestStringFor
  ( std::string const & bytesstr
  )
{
  std::string get_request;
  get_request.append(TS_HTTP_METHOD_GET);
  get_request.append(" http://localhost:6010/voidlinux.iso HTTP/1.1\r\n");
  get_request.append(TS_MIME_FIELD_HOST);
  get_request.append(": localhost:6010\r\n");
  get_request.append(TS_MIME_FIELD_USER_AGENT);
  get_request.append(": ATS/whatever\r\n");
  get_request.append(TS_MIME_FIELD_ACCEPT);
  get_request.append(": */*\r\n");
  get_request.append(TS_MIME_FIELD_RANGE);
  get_request.append(": ");
  get_request.append(bytesstr);
  get_request.append("\r\n");
  get_request.append("X-Skip-Me");
  get_request.append(": absolutely\r\n");
  get_request.append("\r\n");
  return get_request;
}

struct State
{
  TSHttpTxn txnp;

  TSVConn vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBuffer resp_buffer;
  TSIOBuffer resp_reader;
};

static
int
intercept_hook
  ( TSCont contp
  , TSEvent event
  , void * edata
  )
{
  if (TS_EVENT_NET_ACCEPT == event)
  {
    TSVConn const downvc = (TSVConn)edata;

    // set up the 
    Stage * const stagedown = new Stage(downvc);
    stagedown->readio = Channel::forRead(downvc, contp);

    TSHttpTxn const txnp((TSHttpTxn)TSContDataGet(contp)); // this is a hack
    sockaddr const * const client_addr = TSHttpTxnClientAddrGet(txnp);

    {
      std::string const get_request
        (rangeRequestStringFor("bytes=0-1048575"));

std::cerr << get_request << std::endl;

      TSVConn const vconn = TSHttpConnect(client_addr);
/*
      Stage * const stagedown(new Stage);
      state->txnp = txnp;
      state->vc = downvc;
      state->req_buffer = TSIOBufferCreate();
      state->resp_buffer = TSIOBufferCreate();
      state->resp_reader = TSIOBufferReaderAlloc(state->resp_buffer);
*/

      TSIOBuffer req_buffer = TSIOBufferCreate();
      TSIOBufferReader reader = TSIOBufferReaderAlloc(req_buffer);
      int64_t const written
        ( TSIOBufferWrite
          ( req_buffer
          , get_request.data()
          , get_request.size() ) );

      TSVIO const vio = TSVConnWrite
        ( vconn
        , contp
        , reader
        , TSIOBufferReaderAvail(reader) );
/*
      TSFetchEvent event_ids = { 0, 0, 0 };
      TSFetchUrl
        ( get_request.c_str()
        , get_request.size()
        , client_addr
        , contp
        , NO_CALLBACK
        , event_ids );
*/
    }
  }
  else
  {
std::cerr << "intercept_hook got another event: " << event << std::endl;
  }

  return 0;
}

static
bool
read_request
  ( TSHttpTxn txnp
  )
{
  HttpHeader const headcreq(txnp, TSHttpTxnClientReqGet);
  if (headcreq.isMethodGet())
  {
    if (headcreq.skipMe())
    {
std::cerr << "Got a skip me directive, passing through" << std::endl;
    }
    else
    {
      // we'll intercept this GET and do it ourselfs
      TSCont const icontp
        (TSContCreate(intercept_hook, TSMutexCreate()));
      TSContDataSet(icontp, (void*)txnp);
      TSHttpTxnIntercept(icontp, txnp);
std::cerr << "created intercept hook" << std::endl;
      return true;
    }
  }

  return false;
}

static
int
global_read_request_hook
  ( TSCont // contp
  , TSEvent // event
  , void * edata
  )
{
  TSHttpTxn const txnp = static_cast<TSHttpTxn>(edata);
  read_request(txnp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

///// remap plugin engine

SLICE_EXPORT
TSRemapStatus
TSRemapDoRemap
  ( void * // ih
  , TSHttpTxn txnp
  , TSRemapRequestInfo * //rri
  )
{
  if (read_request(txnp))
  {
    return TSREMAP_DID_REMAP;
  }
  else
  {
    return TSREMAP_NO_REMAP;
  }
}

///// remap plugin setup and teardown
SLICE_EXPORT
void
TSRemapOSResponse
  ( void* // ih
  , TSHttpTxn // rh
  , int // os_response_type
  )
{
}

SLICE_EXPORT
TSReturnCode
TSRemapNewInstance
  ( int // argc
  , char * /* argv */[]
  , void ** //ih
  , char * // errbuf
  , int // errbuf_size
)
{
  return TS_SUCCESS;
}

SLICE_EXPORT
void
TSRemapDeleteInstance
  ( void * // ih
  )
{
}

SLICE_EXPORT
TSReturnCode
TSRemapInit
  ( TSRemapInterface * // api_info
  , char * // errbug
  , int // errbuf_size
  )
{
  return TS_SUCCESS;
}


///// global plugin
SLICE_EXPORT
void
TSPluginInit
  ( int // argc
  , char const * /* argv */[]
  )
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Comcast";
  info.support_email = (char *)"support@comcast.com";

  if (TS_SUCCESS != TSPluginRegister(&info))
  {
    ERROR_LOG("Plugin registration failed.\n");
    ERROR_LOG("Unable to initialize plugin (disabled).");
    return;
  }

  TSCont const contp(TSContCreate(global_read_request_hook, nullptr));
assert(nullptr != contp);

  // Called immediately after the request header is read from the client
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
