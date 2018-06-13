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
#include "intercept.h"

#include "ts/remap.h"
#include "ts/ts.h"

#include <cassert>
#include <iostream>

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
