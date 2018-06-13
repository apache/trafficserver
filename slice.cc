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
#include "Data.h"
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
  get_request.append(" /voidlinux.iso HTTP/1.1\r\n");
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

static
int
handle_client_req
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  // at the moment toss, we'll make one up
  if (TS_EVENT_VCONN_READ_READY == event)
  {
    // parse incoming request header here
    int64_t const bytesavail
        (TSIOBufferReaderAvail(data->dnstream->read->reader));
std::cerr << "Incoming header: " << bytesavail << std::endl;
    TSIOBufferReaderConsume(data->dnstream->read->reader, bytesavail);

    // create header
    std::string const get_request
      (rangeRequestStringFor("bytes=0-1048575"));

std::cerr << get_request << std::endl;

    // virtual connection
    TSVConn const upvc = TSHttpConnect((sockaddr*)data->ipaddr->ip());

    // set up and write to the server
    data->upstream = new Stage(upvc);
    data->upstream->setupWriter(contp);

    TSIOBufferWrite
      ( data->upstream->write->iobuf
      , get_request.data()
      , get_request.size() );
    TSVIOReenable(data->upstream->write->vio);

    // get ready for data coming back
    data->upstream->setupReader(contp);

    // no longer want anything from client
  }

  return 0;
}

static
int
handle_server_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_READ_READY == event)
  {
    int64_t read_avail
      (TSIOBufferReaderAvail(data->upstream->read->reader));

    if (0 < read_avail)
    {
      if (nullptr == data->dnstream->write) {
        data->dnstream->setupWriter(contp);
      }

      int64_t const copied
        ( TSIOBufferCopy
          ( data->dnstream->write->iobuf
          , data->upstream->read->reader
          , read_avail
          , 0 ) );

std::cerr << "copied: " << copied << std::endl;

      TSVIOReenable(data->dnstream->write->vio);

/*
      int64_t consumed(0);
      TSIOBufferBlock block = TSIOBufferReaderStart
          (data->upstream->read->reader);

      while (nullptr != block && 0 < read_avail)
      {
        int64_t read_block(0);
        char const * const block_start = TSIOBufferBlockReadStart
            (block, data->upstream->read->reader, &read_block);
        if (0 < read_block)
        {
          int64_t const tocopy(std::min(read_block, read_avail));
          std::cerr << std::string(block_start, block_start + tocopy);
          read_avail -= tocopy;
          consumed += tocopy;
          block = TSIOBufferBlockNext(block);
        }
      }

      TSIOBufferReaderConsume(data->upstream->read->reader, consumed);
*/
    }
  }

  return 0;
}

static
int
handle_client_resp
  ( TSCont contp
  , TSEvent event
  , Data * const data
  )
{
  if (TS_EVENT_VCONN_WRITE_READY == event)
  {
    int64_t read_avail
      (TSIOBufferReaderAvail(data->upstream->read->reader));

    if (0 < read_avail)
    {
      TSAssert(nullptr != data->dnstream->write);

      int64_t const copied
        ( TSIOBufferCopy
          ( data->dnstream->write->iobuf
          , data->upstream->read->reader
          , read_avail
          , 0 ) );

std::cerr << "copied: " << copied << std::endl;

      TSVIOReenable(data->dnstream->write->vio);

/*
      int64_t consumed(0);
      TSIOBufferBlock block = TSIOBufferReaderStart
          (data->upstream->read->reader);

      while (nullptr != block && 0 < read_avail)
      {
        int64_t read_block(0);
        char const * const block_start = TSIOBufferBlockReadStart
            (block, data->upstream->read->reader, &read_block);
        if (0 < read_block)
        {
          int64_t const tocopy(std::min(read_block, read_avail));
          std::cerr << std::string(block_start, block_start + tocopy);
          read_avail -= tocopy;
          consumed += tocopy;
          block = TSIOBufferBlockNext(block);
        }
      }

      TSIOBufferReaderConsume(data->upstream->read->reader, consumed);
*/
    }
    else // close it all out???
    {
      
    }
  }

  return 0;
}

static
int
intercept_hook
  ( TSCont contp
  , TSEvent event
  , void * edata
  )
{
  Data * const data = (Data*)TSContDataGet(contp);

std::cerr << "intercept_hook event: " << event << std::endl;

  if (TS_EVENT_NET_ACCEPT == event)
  {
    // set up reader from client
    TSVConn const downvc = (TSVConn)edata;
    data->dnstream = new Stage(downvc);
    data->dnstream->setupReader(contp);
  }
  else // more data from client
  if ( nullptr != data->dnstream
    && nullptr != data->dnstream->read
    && edata == data->dnstream->read->vio )
  {
    handle_client_req(contp, event, data);
    TSVConnShutdown(data->dnstream->vc, 1, 0);
  }
  else // server wants more data from us
  if ( nullptr != data->upstream
    && nullptr != data->upstream->write
    && edata == data->upstream->write->vio )
  {
    // already sent, not interested
    TSVConnShutdown(data->upstream->vc, 0, 1);
  }
  else // server has data for us
  if ( nullptr != data->upstream
    && nullptr != data->upstream->read
    && edata == data->upstream->read->vio )
  {
std::cerr << "server has data for us" << std::endl;
    handle_server_resp(contp, event, data);
  }
  else // client wants more data from us
  if ( nullptr != data->dnstream
    && nullptr != data->dnstream->write
    && edata == data->dnstream->write->vio )
  {
std::cerr << "client wants data from us" << std::endl;
    handle_client_resp(contp, event, data);
  }
  else
  {
std::cerr << "unhandled event: " << event << std::endl;
  }

/*
*/

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
    if (! headcreq.skipMe())
    {
      // we'll intercept this GET and do it ourselfs
      TSCont const icontp
        (TSContCreate(intercept_hook, TSMutexCreate()));

      // turn off any caching
      TSHttpTxnServerRespNoStoreSet(txnp, 1);

      // connect back to ATS
      sockaddr const * const client_addr = TSHttpTxnClientAddrGet(txnp);
      TSContDataSet(icontp, (void*)new Data(client_addr));
      TSHttpTxnIntercept(icontp, txnp);
std::cerr << "created intercept hook" << std::endl;
      return true;
    }
    else
    {
std::cerr << "Got a skip me directive, passing through" << std::endl;
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
  std::cerr << "TSRemapNewInstance: slicer" << std::endl;
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
