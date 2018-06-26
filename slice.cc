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
#include <cinttypes>
#include <iostream>
#include <netinet/in.h>

namespace
{

int64_t const BLOCKBYTESMIN = 1024 * 32;
int64_t const BLOCKBYTESMAX = 1024 * 1024 * 32;
int64_t const BLOCKBYTESDEF = 1024 * 1024;

int64_t globalBlockBytes = BLOCKBYTESDEF;

struct Config
{
  int64_t m_blockbytes { BLOCKBYTESDEF };

  void
  fromString
    ( char const * const bytesstr
    )
  {
    char * endp = nullptr;
    int64_t const bytesread = strtoll(bytesstr, &endp, 10);
    if ( bytesstr != endp
      && BLOCKBYTESMIN <= bytesread
      && bytesread <= BLOCKBYTESMAX )
    {
      m_blockbytes = bytesread;
      DEBUG_LOG("Override blockbytes %" PRId64, m_blockbytes);
    }
    else
    {
      ERROR_LOG("Invalid incoming blockbytes %s", bytesstr);
    }
  }
};

Config globalConfig;

bool
read_request
  ( TSHttpTxn txnp
  , int64_t const blockbytes
  )
{
DEBUG_LOG("slice read_request");
  TxnHdrMgr hdrmgr;
  hdrmgr.populateFrom(txnp, TSHttpTxnClientReqGet);
  HttpHeader const header(hdrmgr.m_buffer, hdrmgr.m_lochdr);

  if (TS_HTTP_METHOD_GET == header.method())
  {
static int const SLICER_MIME_LEN_INFO = strlen(SLICER_MIME_FIELD_INFO);
    if (! header.hasKey(SLICER_MIME_FIELD_INFO, SLICER_MIME_LEN_INFO))
    {
//std::cerr << "incoming slicer request" << std::endl;
      // turn off any and all transaction caching (shouldn't matter)
      TSHttpTxnServerRespNoStoreSet(txnp, 1);
      TSHttpTxnRespCacheableSet(txnp, 0);
      TSHttpTxnReqCacheableSet(txnp, 0);

DEBUG_LOG("slice accepting and slicing");
      // connection back into ATS
      sockaddr const * const ip = TSHttpTxnClientAddrGet(txnp);
      if (nullptr == ip)
      {
        return false;
      }

static int64_t const blocksize(1024 * 1024);
      Data * const data = new Data(blocksize);

      // set up feedback connect
      if (AF_INET == ip->sa_family) {
        memcpy(&data->m_client_ip, ip, sizeof(sockaddr_in));
      } else if (AF_INET6 == ip->sa_family) {
        memcpy(&data->m_client_ip, ip, sizeof(sockaddr_in6));
      } else {
        delete data;
        return false;
      }

#if defined(RESET_URL_AND_HOST)

      // need to reset the HOST field for global plugin
      data->m_hostlen = sizeof(data->m_hostname) - 1;
      if (! header.valueForKey
          ( TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST
          , data->m_hostname, &data->m_hostlen ) )
      {
        DEBUG_LOG("Unable to get hostname from header");
        delete data;
        return false;
      }

      data->m_blocksize = blockbytes;

      // need the pristine url, especially for global plugins
      TSMBuffer urlbuf;
      TSMLoc urlloc;
      TSReturnCode rcode
        = TSHttpTxnPristineUrlGet(txnp, &urlbuf, &urlloc);

      if (TS_SUCCESS == rcode)
      {
        data->m_urlbuffer = TSMBufferCreate();
        rcode = TSUrlCreate(data->m_urlbuffer, &data->m_urlloc);
        if (TS_SUCCESS == rcode)
        {
          TSUrlCopy(data->m_urlbuffer, data->m_urlloc, urlbuf, urlloc);
        }
        TSHandleMLocRelease(urlbuf, TS_NULL_MLOC, urlloc);
      }
#endif // RESET_URL_AND_HOST

      // we'll intercept this GET and do it ourselfs
      TSCont const icontp
        (TSContCreate(intercept_hook, TSMutexCreate()));
      TSContDataSet(icontp, (void*)data);
//      TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, icontp);
      TSHttpTxnIntercept(icontp, txnp);
      return true;
    }
    else
    {
DEBUG_LOG("slice passing GET request downstream");
    }
  }

  return false;
}

int
global_read_request_hook
  ( TSCont // contp
  , TSEvent // event
  , void * edata
  )
{
  TSHttpTxn const txnp = static_cast<TSHttpTxn>(edata);
  read_request(txnp, globalBlockBytes);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // private namespace

///// remap plugin engine

SLICE_EXPORT
TSRemapStatus
TSRemapDoRemap
  ( void * ih
  , TSHttpTxn txnp
  , TSRemapRequestInfo * //rri
  )
{
  Config * const config = static_cast<Config *>(ih);

  if (read_request(txnp, config->m_blockbytes))
  {
    return TSREMAP_DID_REMAP_STOP;
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
  ( int argc
  , char * argv[]
  , void ** ih
  , char * // errbuf
  , int // errbuf_size
  )
{
  Config * const config = new Config;
  if (1 < argc)
  {
    config->fromString(argv[1]);
  }
  *ih = static_cast<void*>(config);

  std::cerr << "TSRemapNewInstance: slicer" << std::endl;
  return TS_SUCCESS;
}

SLICE_EXPORT
void
TSRemapDeleteInstance
  ( void * ih
  )
{
  Config * const config = static_cast<Config*>(ih);
  delete config;
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
  ( int argc
  , char const * argv[]
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

  if (1 < argc)
  {
    globalConfig.fromString(argv[1]);
  }

  TSCont const contp(TSContCreate(global_read_request_hook, nullptr));

  // Called immediately after the request header is read from the client
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
