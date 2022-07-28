/*
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

/**
 * @file prefetch.cpp
 * @brief Background fetch related classes (header file).
 */

#include "ts/ts.h" /* ATS API */
#include "prefetch.h"

bool
BgBlockFetch::schedule(Data *const data, int blocknum)
{
  bool ret         = false;
  BgBlockFetch *bg = new BgBlockFetch(blocknum);
  if (bg->fetch(data)) {
    ret = true;
  } else {
    delete bg;
  }
  return ret;
}

/**
 * Initialize and schedule the background fetch
 */
bool
BgBlockFetch::fetch(Data *const data)
{
  if (m_stream.m_read.isOpen()) {
    // should never happen since the connection was just initialized
    ERROR_LOG("Background block request already in flight!");
    return false;
  }

  int64_t const blockbeg = (data->m_config->m_blockbytes * m_blocknum);
  Range blockbe(blockbeg, blockbeg + data->m_config->m_blockbytes);

  char rangestr[1024];
  int rangelen      = sizeof(rangestr);
  bool const rpstat = blockbe.toStringClosed(rangestr, &rangelen);
  TSAssert(rpstat);

  DEBUG_LOG("Request background block: %s", rangestr);

  // reuse the incoming client header, just change the range
  HttpHeader header(data->m_req_hdrmgr.m_buffer, data->m_req_hdrmgr.m_lochdr);

  // add/set sub range key and add slicer tag
  bool const rangestat = header.setKeyVal(TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rangestr, rangelen);

  if (!rangestat) {
    ERROR_LOG("Error trying to set range request header %s", rangestr);
    return false;
  }
  TSAssert(nullptr == m_cont);

  // Setup the continuation
  m_cont = TSContCreate(handler, TSMutexCreate());
  TSContDataSet(m_cont, static_cast<void *>(this));

  // create virtual connection back into ATS
  TSHttpConnectOptions options = TSHttpConnectOptionsGet(TS_CONNECT_PLUGIN);
  options.addr                 = reinterpret_cast<sockaddr *>(&data->m_client_ip);
  options.tag                  = PLUGIN_NAME;
  options.id                   = 0;
  options.buffer_index         = data->m_buffer_index;
  options.buffer_water_mark    = data->m_buffer_water_mark;

  TSVConn const upvc = TSHttpConnectPlugin(&options);

  int const hlen = TSHttpHdrLengthGet(header.m_buffer, header.m_lochdr);

  // set up connection with the HttpConnect server
  m_stream.setupConnection(upvc);
  m_stream.setupVioWrite(m_cont, hlen);
  TSHttpHdrPrint(header.m_buffer, header.m_lochdr, m_stream.m_write.m_iobuf);

  if (TSIsDebugTagSet(PLUGIN_NAME)) {
    std::string const headerstr(header.toString());
    DEBUG_LOG("Headers\n%s", headerstr.c_str());
  }
  // ensure blocks are pulled through to cache
  m_stream.setupVioRead(m_cont, INT64_MAX);

  return true;
}

/**
 * @brief Continuation to close background fetch after
 * writing to cache is complete or error
 *
 */
int
BgBlockFetch::handler(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  BgBlockFetch *bg = static_cast<BgBlockFetch *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(bg->m_stream.m_vc, 0, 1);
    break;
  case TS_EVENT_VCONN_READ_READY: {
    TSIOBufferReader const reader = bg->m_stream.m_read.m_reader;
    if (nullptr != reader) {
      int64_t const avail = TSIOBufferReaderAvail(reader);
      TSIOBufferReaderConsume(reader, avail);
      TSVIO const rvio = bg->m_stream.m_read.m_vio;
      TSVIONDoneSet(rvio, TSVIONDoneGet(rvio) + avail);
      TSVIOReenable(rvio);
    }
  } break;
  case TS_EVENT_NET_ACCEPT_FAILED:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_VCONN_ACTIVE_TIMEOUT:
  case TS_EVENT_ERROR:
    bg->m_stream.abort();
    TSContDataSet(contp, nullptr);
    delete bg;
    TSContDestroy(contp);
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    bg->m_stream.close();
    TSContDataSet(contp, nullptr);
    delete bg;
    TSContDestroy(contp);
    break;
  default:
    DEBUG_LOG("Unhandled bg fetch event:%s (%d)", TSHttpEventNameLookup(event), event);
    break;
  }
  return 0;
}
