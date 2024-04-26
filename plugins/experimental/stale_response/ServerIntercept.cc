/** @file

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

#include "ServerIntercept.h"
#include "BodyData.h"
#include "stale_response.h"

#include "ts/ts.h"
#include "ts_wrap.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <search.h>
#include <strings.h>

#include <arpa/inet.h>

using namespace std;

#define PLUGIN_TAG_SERV "stale_response_intercept"
const int64_t c_max_single_write = 64 * 1024;

DEF_DBG_CTL(PLUGIN_TAG_SERV)

/*-----------------------------------------------------------------------------------------------*/
struct SContData {
  struct IoHandle {
    TSVIO            vio;
    TSIOBuffer       buffer;
    TSIOBufferReader reader;
    IoHandle() : vio(0), buffer(0), reader(0){};
    ~IoHandle()
    {
      if (reader) {
        TSIOBufferReaderFree(reader);
      }
      if (buffer) {
        TSIOBufferDestroy(buffer);
      }
    };
  };

  TSVConn net_vc;
  TSCont  contp;

  IoHandle     input;
  IoHandle     output;
  TSHttpParser http_parser;
  TSMBuffer    req_hdr_bufp;
  TSMLoc       req_hdr_loc;
  bool         req_hdr_parsed;

  bool conn_setup;
  bool write_setup;

  ConfigInfo *plugin_config;
  BodyData   *pBody;
  uint32_t    next_chunk_written;

  SContData(TSCont cont)
    : net_vc(0),
      contp(cont),
      input(),
      output(),
      req_hdr_bufp(0),
      req_hdr_loc(0),
      req_hdr_parsed(false),
      conn_setup(false),
      write_setup(false),
      plugin_config(nullptr),
      pBody(nullptr),
      next_chunk_written(0)
  {
    http_parser = TSHttpParserCreate();
  }

  ~SContData()
  {
    TSDebug(PLUGIN_TAG_SERV, "[%s] Destroying continuation data", __FUNCTION__);
    TSHttpParserDestroy(http_parser);
    if (req_hdr_loc) {
      TSHandleMLocRelease(req_hdr_bufp, TS_NULL_MLOC, req_hdr_loc);
    }
    if (req_hdr_bufp) {
      TSMBufferDestroy(req_hdr_bufp);
    }
  };
};

/*-----------------------------------------------------------------------------------------------*/
static bool
connSetup(SContData *cont_data, TSVConn vconn)
{
  if (cont_data->conn_setup) {
    TSDebug(PLUGIN_TAG_BAD, "[%s] SContData already init", __FUNCTION__);
    return false;
  }
  cont_data->conn_setup = true;
  cont_data->net_vc     = vconn;

  cont_data->input.buffer = TSIOBufferCreate();
  cont_data->input.reader = TSIOBufferReaderAlloc(cont_data->input.buffer);
  cont_data->input.vio    = TSVConnRead(cont_data->net_vc, cont_data->contp, cont_data->input.buffer, INT_MAX);

  cont_data->req_hdr_bufp = TSMBufferCreate();
  cont_data->req_hdr_loc  = TSHttpHdrCreate(cont_data->req_hdr_bufp);
  TSHttpHdrTypeSet(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, TS_HTTP_TYPE_REQUEST);

  TSDebug(PLUGIN_TAG_SERV, "[%s] Done", __FUNCTION__);
  return true;
}

/*-----------------------------------------------------------------------------------------------*/
static void
connShutdownDataDestory(SContData *cont_data)
{
  if (cont_data->net_vc) {
    TSVConnClose(cont_data->net_vc);
  }
  // we need to destroy the body data object
  if (cont_data->pBody->key_hash_active) {
    if (!async_remove_active(cont_data->pBody->key_hash, cont_data->plugin_config)) {
      TSDebug(PLUGIN_TAG_BAD, "[%s] didnt delete async active", __FUNCTION__);
    }
  } else {
    delete cont_data->pBody;
  }
  // clean up my cont
  TSContDestroy(cont_data->contp);
  delete cont_data;
  TSDebug(PLUGIN_TAG_SERV, "[%s] Done", __FUNCTION__);
}

/*-----------------------------------------------------------------------------------------------*/
static bool
writeOutData(SContData *cont_data)
{
  int64_t  total_current_write = 0;
  uint32_t max_chunk_count     = cont_data->pBody->getChunkCount();
  for (uint32_t chunk_index = cont_data->next_chunk_written; chunk_index < max_chunk_count; chunk_index++) {
    const char *start;
    int64_t     avail;
    if (!cont_data->pBody->getChunk(chunk_index, &start, &avail)) {
      TSDebug(PLUGIN_TAG_BAD, "[%s] Error while getting chunk_index %d", __FUNCTION__, chunk_index);
      TSError("[%s] Error while getting chunk_index %d", __FUNCTION__, chunk_index);
      break;
    }
    if (TSIOBufferWrite(cont_data->output.buffer, start, avail) != avail) {
      TSDebug(PLUGIN_TAG_BAD, "[%s] Error while writing content avail=%d", __FUNCTION__, (int)avail);
    }
    cont_data->pBody->removeChunk(chunk_index);
    total_current_write           += avail;
    cont_data->next_chunk_written  = chunk_index + 1;
    if (total_current_write >= c_max_single_write) {
      break;
    }
  }
  TSVIOReenable(cont_data->output.vio);

  // TSDebug(PLUGIN_TAG_SERV, "[%s] written=%d done=%d", __FUNCTION__, (int)total_current_write,(cont_data->next_chunk_written >=
  // max_chunk_count));
  return (cont_data->next_chunk_written >= max_chunk_count);
}

/*-----------------------------------------------------------------------------------------------*/
void
writeSetup(SContData *cont_data)
{
  if (!cont_data->write_setup) {
    cont_data->write_setup   = true;
    cont_data->output.buffer = TSIOBufferCreate();
    cont_data->output.reader = TSIOBufferReaderAlloc(cont_data->output.buffer);
    cont_data->output.vio    = TSVConnWrite(cont_data->net_vc, cont_data->contp, cont_data->output.reader, INT_MAX);
    // set the total length to write
    TSVIONBytesSet(cont_data->output.vio, cont_data->pBody->getSize());
    TSDebug(PLUGIN_TAG_SERV, "[%s] Done length=%d", __FUNCTION__, (int)cont_data->pBody->getSize());
  } else {
    TSDebug(PLUGIN_TAG_BAD, "[%s] Already init", __FUNCTION__);
  }
}

/*-----------------------------------------------------------------------------------------------*/
static bool
handleRead(SContData *cont_data)
{
  int avail = TSIOBufferReaderAvail(cont_data->input.reader);
  if (avail == TS_ERROR) {
    TSError("[%s] Error while getting number of bytes available", __FUNCTION__);
    return false;
  }

  TSDebug(PLUGIN_TAG_SERV, "[%s] avail %d", __FUNCTION__, avail);

  int consumed = 0;
  if (avail > 0) {
    int64_t         data_len;
    const char     *data;
    TSIOBufferBlock block = TSIOBufferReaderStart(cont_data->input.reader);
    while (block != nullptr) {
      data = TSIOBufferBlockReadStart(block, cont_data->input.reader, &data_len);

      if (!cont_data->req_hdr_parsed) {
        const char *endptr = data + data_len;
        if (TSHttpHdrParseReq(cont_data->http_parser, cont_data->req_hdr_bufp, cont_data->req_hdr_loc, &data, endptr) ==
            TS_PARSE_DONE) {
          cont_data->req_hdr_parsed = true;
          TSDebug(PLUGIN_TAG_SERV, "[%s] Parsed header", __FUNCTION__);
        }
      }
      consumed += data_len;
      block     = TSIOBufferBlockNext(block);
    }
  }

  TSIOBufferReaderConsume(cont_data->input.reader, consumed);

  TSDebug(PLUGIN_TAG_SERV, "[%s] Consumed %d bytes from input vio, avail: %d", __FUNCTION__, consumed, avail);

  // Modify the input VIO to reflect how much data we've completed.
  TSVIONDoneSet(cont_data->input.vio, TSVIONDoneGet(cont_data->input.vio) + consumed);

  if (!cont_data->req_hdr_parsed) {
    TSDebug(PLUGIN_TAG_SERV, "[%s] Reenabling input vio need more header data", __FUNCTION__);
    TSVIOReenable(cont_data->input.vio);
  }

  return true;
}

/*-----------------------------------------------------------------------------------------------*/
static int
serverIntercept(TSCont contp, TSEvent event, void *edata)
{
  SContData *cont_data = static_cast<SContData *>(TSContDataGet(contp));
  bool       shutdown  = false;

  switch (event) {
  case TS_EVENT_NET_ACCEPT:
    TSDebug(PLUGIN_TAG_SERV, "[%s] {%u} net accept event %d", __FUNCTION__, cont_data->pBody->key_hash, event);
    if (!connSetup(cont_data, static_cast<TSVConn>(edata))) {
      TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} connSetup aleady initalized", __FUNCTION__, cont_data->pBody->key_hash);
    }
    break;

  case TS_EVENT_NET_ACCEPT_FAILED:
    // not sure why this would happen, but it does
    TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} net accept failed %d", __FUNCTION__, cont_data->pBody->key_hash, event);
    shutdown = true;
    break;

  case TS_EVENT_VCONN_READ_READY:
    TSDebug(PLUGIN_TAG_SERV, "[%s] {%u} vconn read ready event %d", __FUNCTION__, cont_data->pBody->key_hash, event);
    if (!handleRead(cont_data)) {
      TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} handleRead failed", __FUNCTION__, cont_data->pBody->key_hash);
      break;
    }
    // VCONN_READ_READY should not happen again since we dont reenable input.vio
    if (cont_data->req_hdr_parsed && !cont_data->write_setup) {
      writeSetup(cont_data);
      writeOutData(cont_data);
    }
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    // intentional fall-through
    TSDebug(PLUGIN_TAG_SERV, "[%s] {%u} vconn read complete/eos event %d", __FUNCTION__, cont_data->pBody->key_hash, event);
    // shutdown id we havent parsed headers and get a read eos/complete
    if (!cont_data->req_hdr_parsed) {
      TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} read complete but headers not parsed", __FUNCTION__, cont_data->pBody->key_hash);
      shutdown = true;
    }
    break;

  case TS_EVENT_VCONN_WRITE_READY:
    // TSDebug(PLUGIN_TAG_SERV, "[%s] {%u} vconn write ready event %d", __FUNCTION__,cont_data->pBody->key_hash,event);
    // trying not to write out the whole body at once if its big
    writeOutData(cont_data);
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(PLUGIN_TAG_SERV, "[%s] {%u} vconn write complete event %d", __FUNCTION__, cont_data->pBody->key_hash, event);
    shutdown = true;
    break;

  case TS_EVENT_ERROR:
    // todo: do some error handling here
    TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} error event %d", __FUNCTION__, cont_data->pBody->key_hash, event);
    shutdown = true;
    break;

  default:
    TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} default event %d", __FUNCTION__, cont_data->pBody->key_hash, event);
    break;
  }

  if (shutdown) {
    connShutdownDataDestory(cont_data);
  }

  return 1;
}

/*-----------------------------------------------------------------------------------------------*/
bool
serverInterceptSetup(TSHttpTxn txnp, BodyData *pBody, ConfigInfo *plugin_config)
{
  // make sure we have data to deliver -- note called body but its headers+body
  if (!pBody || (pBody->getSize() <= 0)) {
    TSDebug(PLUGIN_TAG_BAD, "[%s] must have body and size > 0", __FUNCTION__);
    // remove async active if pBody exists but size <= 0
    if (pBody && pBody->key_hash_active) {
      if (async_remove_active(pBody->key_hash, plugin_config)) {
        TSDebug(PLUGIN_TAG_BAD, "[%s] removed async active due to pbody size <= 0", __FUNCTION__);
      } else {
        TSDebug(PLUGIN_TAG_BAD, "[%s] failed to delete async active when pbody size <= 0", __FUNCTION__);
      }
    }
    return false;
  }
  // make sure we have a contp
  TSCont contp = TSContCreate(serverIntercept, TSMutexCreate());
  if (!contp) {
    TSDebug(PLUGIN_TAG_BAD, "[%s] Could not create intercept contp", __FUNCTION__);
    // remove async active if couldn't create intercept contp
    if (pBody->key_hash_active) {
      if (async_remove_active(pBody->key_hash, plugin_config)) {
        TSDebug(PLUGIN_TAG_BAD, "[%s] removed async active since couldn't create intercept contp", __FUNCTION__);
      } else {
        TSDebug(PLUGIN_TAG_BAD, "[%s] failed to delete async active when couldn't create intercept contp", __FUNCTION__);
      }
    }
    return false;
  }
  // create the data block for the cont
  SContData *cont_data = new SContData(contp);
  // set some pointers
  cont_data->plugin_config = plugin_config;
  cont_data->pBody         = pBody;
  // set the intercept and turn on caching
  TSContDataSet(contp, cont_data);
  TSHttpTxnServerIntercept(contp, txnp);
  TSHttpTxnReqCacheableSet(txnp, 1);
  TSHttpTxnRespCacheableSet(txnp, 1);
  TSDebug(PLUGIN_TAG_SERV, "[%s] {%u} Success length=%d", __FUNCTION__, cont_data->pBody->key_hash,
          (int)cont_data->pBody->getSize());
  return true;
}

/*-----------------------------------------------------------------------------------------------*/
