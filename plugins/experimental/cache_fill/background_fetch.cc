/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

    @section license License

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
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include <string>
#include <iostream>
#include <unordered_map>
#include <cinttypes>
#include <string_view>
#include <array>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ts/ts.h"
#include "ts/remap.h"
#include "background_fetch.h"
typedef std::unordered_map<std::string, bool> OutstandingRequests;

///////////////////////////////////////////////////////////////////////////
// Set a header to a specific value. This will avoid going to through a
// remove / add sequence in case of an existing header.
// but clean.
static bool
set_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header, int len, const char *val, int val_len)
{
  if (!bufp || !hdr_loc || !header || len <= 0 || !val || val_len <= 0) {
    return false;
  }

  bool ret         = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header, len);

  if (!field_loc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, header, len, &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val, val_len)) {
        TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        ret = true;
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }
  } else {
    TSMLoc tmp = nullptr;
    bool first = true;

    while (field_loc) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val, val_len)) {
          ret = true;
        }
      } else {
        TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// Dump a header on stderr, useful together with TSDebug().
static void
dump_headers(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  TSIOBufferBlock block;
  int64_t block_avail;

  output_buffer = TSIOBufferCreate();
  reader        = TSIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not the http request line */
  TSMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* We need to loop over all the buffer blocks, there can be more than 1 */
  block = TSIOBufferReaderStart(reader);
  do {
    const char *block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);
    if (block_avail > 0) {
      TSDebug(PLUGIN_NAME, "Headers are:\n%.*s", static_cast<int>(block_avail), block_start);
    }
    TSIOBufferReaderConsume(reader, block_avail);
    block = TSIOBufferReaderStart(reader);
  } while (block && block_avail != 0);

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);
}

bool
BgFetchData::initialize(TSMBuffer request, TSMLoc req_hdr, TSHttpTxn txnp)
{
  struct sockaddr const *ip = TSHttpTxnClientAddrGet(txnp);
  bool ret                  = false;

  TSAssert(TS_NULL_MLOC == hdr_loc);
  TSAssert(TS_NULL_MLOC == url_loc);

  if (ip) {
    if (ip->sa_family == AF_INET) {
      memcpy(&client_ip, ip, sizeof(sockaddr_in));
    } else if (ip->sa_family == AF_INET6) {
      memcpy(&client_ip, ip, sizeof(sockaddr_in6));
    } else {
      TSError("[%s] Unknown address family %d", PLUGIN_NAME, ip->sa_family);
    }
  } else {
    TSError("[%s] Failed to get client host info", PLUGIN_NAME);
    return false;
  }

  hdr_loc = TSHttpHdrCreate(mbuf);
  if (TS_SUCCESS == TSHttpHdrCopy(mbuf, hdr_loc, request, req_hdr)) {
    TSMLoc p_url;

    // Now copy the pristine request URL into our MBuf
    if (TS_SUCCESS == TSHttpTxnPristineUrlGet(txnp, &request, &p_url)) {
      if (TS_SUCCESS == TSUrlClone(mbuf, request, p_url, &url_loc)) {
        TSMLoc c_url = TS_NULL_MLOC;
        int len;
        char *url = nullptr;

        // Get the cache key URL (for now), since this has better lookup behavior when using
        // e.g. the cachekey plugin.
        if (TS_SUCCESS == TSUrlCreate(request, &c_url)) {
          if (TS_SUCCESS == TSHttpTxnCacheLookupUrlGet(txnp, request, c_url)) {
            url = TSUrlStringGet(request, c_url, &len);
            TSHandleMLocRelease(request, TS_NULL_MLOC, c_url);
            TSDebug(PLUGIN_NAME, "Cache URL is %.*s", len, url);
          }
        }

        if (url) {
          _url.assign(url, len); // Save away the cache URL for later use when acquiring lock
          TSfree(static_cast<void *>(url));

          if (TS_SUCCESS == TSHttpHdrUrlSet(mbuf, hdr_loc, url_loc)) {
            // Make sure we have the correct Host: header for this request.
            const char *hostp = TSUrlHostGet(mbuf, url_loc, &len);

            if (set_header(mbuf, hdr_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, hostp, len)) {
              TSDebug(PLUGIN_NAME, "Set header Host: %.*s", len, hostp);
            }
            ret = true;
          }
        }
      }
      TSHandleMLocRelease(request, TS_NULL_MLOC, p_url);
    }
  }

  return ret;
}

static int cont_bg_fetch(TSCont contp, TSEvent event, void *edata);

// Create, setup and schedule the background fetch continuation.
void
BgFetchData::schedule()
{
  TSAssert(nullptr == _cont);

  // Setup the continuation
  _cont = TSContCreate(cont_bg_fetch, TSMutexCreate());
  TSContDataSet(_cont, static_cast<void *>(this));

  // Initialize the VIO stuff (for the fetch)
  req_io_buf         = TSIOBufferCreate();
  req_io_buf_reader  = TSIOBufferReaderAlloc(req_io_buf);
  resp_io_buf        = TSIOBufferCreate();
  resp_io_buf_reader = TSIOBufferReaderAlloc(resp_io_buf);

  // Schedule
  TSContSchedule(_cont, 0, TS_THREAD_POOL_NET);
}

//////////////////////////////////////////////////////////////////////////////
// Continuation to perform a background fill of a URL. This is pretty
// expensive (memory allocations etc.), we could eliminate maybe the
// std::string, but I think it's fine for now.
static int
cont_bg_fetch(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  BgFetchData *data = static_cast<BgFetchData *>(TSContDataGet(contp));
  int64_t avail;

  switch (event) {
  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_TIMEOUT:
    // Debug info for this particular bg fetch (put all debug in here please)
    if (TSIsDebugTagSet(PLUGIN_NAME)) {
      char buf[INET6_ADDRSTRLEN];
      const sockaddr *sockaddress = reinterpret_cast<const sockaddr *>(&data->client_ip);

      switch (sockaddress->sa_family) {
      case AF_INET:
        inet_ntop(AF_INET, &(((struct sockaddr_in *)sockaddress)->sin_addr), buf, INET_ADDRSTRLEN);
        TSDebug(PLUGIN_NAME, "Client IPv4 = %s", buf);
        break;
      case AF_INET6:
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sockaddress)->sin6_addr), buf, INET6_ADDRSTRLEN);
        TSDebug(PLUGIN_NAME, "Client IPv6 = %s", buf);
        break;
      default:
        TSError("[%s] Unknown address family %d", PLUGIN_NAME, sockaddress->sa_family);
        break;
      }
      TSDebug(PLUGIN_NAME, "Starting background fetch, replaying:");
      dump_headers(data->mbuf, data->hdr_loc);
    }

    // Setup the NetVC for background fetch
    TSAssert(nullptr == data->vc);
    if ((data->vc = TSHttpConnectWithPluginId(reinterpret_cast<sockaddr *>(&data->client_ip), PLUGIN_NAME, 0)) != nullptr) {
      TSHttpHdrPrint(data->mbuf, data->hdr_loc, data->req_io_buf);
      // We never send a body with the request. ToDo: Do we ever need to support that ?
      TSIOBufferWrite(data->req_io_buf, "\r\n", 2);

      data->r_vio = TSVConnRead(data->vc, contp, data->resp_io_buf, INT64_MAX);
      data->w_vio = TSVConnWrite(data->vc, contp, data->req_io_buf_reader, TSIOBufferReaderAvail(data->req_io_buf_reader));
    } else {
      delete data;
      TSError("[%s] Failed to connect to internal process, major malfunction", PLUGIN_NAME);
    }
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    // TSVConnShutdown(data->vc, 0, 1);
    // TSVIOReenable(data->w_vio);
    TSDebug(PLUGIN_NAME, "Write Complete");
    break;

  case TS_EVENT_VCONN_READ_READY:
    avail = TSIOBufferReaderAvail(data->resp_io_buf_reader);
    data->addBytes(avail);
    TSIOBufferReaderConsume(data->resp_io_buf_reader, avail);
    TSVIONDoneSet(data->r_vio, TSVIONDoneGet(data->r_vio) + avail);
    TSVIOReenable(data->r_vio);
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_ERROR:
    if (event == TS_EVENT_VCONN_INACTIVITY_TIMEOUT) {
      TSDebug(PLUGIN_NAME, "Encountered Inactivity Timeout");
      TSVConnAbort(data->vc, TS_VC_CLOSE_ABORT);
    } else {
      TSVConnClose(data->vc);
    }

    TSDebug(PLUGIN_NAME, "Closing down background transaction, event= %s(%d)", TSHttpEventNameLookup(event), event);
    avail = TSIOBufferReaderAvail(data->resp_io_buf_reader);
    data->addBytes(avail);
    TSIOBufferReaderConsume(data->resp_io_buf_reader, avail);
    TSVIONDoneSet(data->r_vio, TSVIONDoneGet(data->r_vio) + avail);

    // Close, release and cleanup
    data->vc = nullptr;
    delete data;
    break;

  default:
    TSDebug(PLUGIN_NAME, "Unhandled event: %s (%d)", TSHttpEventNameLookup(event), event);
    break;
  }

  return 0;
}
