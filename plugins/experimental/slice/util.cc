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

#include "util.h"

#include "Config.h"
#include "Data.h"

void
shutdown(TSCont const contp, Data *const data)
{
  DEBUG_LOG("shutting down transaction");
  TSContDataSet(contp, nullptr);
  delete data;
  TSContDestroy(contp);
}

void
abort(TSCont const contp, Data *const data)
{
  DEBUG_LOG("aborting transaction");
  TSContDataSet(contp, nullptr);
  data->m_upstream.abort();
  data->m_dnstream.abort();
  delete data;
  TSContDestroy(contp);
}

// create and issue a block request
bool
request_block(TSCont contp, Data *const data)
{
  // ensure no upstream connection
  if (data->m_upstream.m_read.isOpen()) {
    ERROR_LOG("Block request already in flight!");
    return false;
  }

  if (Data::BlockState::Pending != data->m_blockstate) {
    ERROR_LOG("request_block called with non Pending state!");
    return false;
  }

  int64_t const blockbeg = (data->m_config->m_blockbytes * data->m_blocknum);
  Range blockbe(blockbeg, blockbeg + data->m_config->m_blockbytes);

  char rangestr[1024];
  int rangelen      = sizeof(rangestr);
  bool const rpstat = blockbe.toStringClosed(rangestr, &rangelen);
  TSAssert(rpstat);

  DEBUG_LOG("requestBlock: %s", rangestr);

  // reuse the incoming client header, just change the range
  HttpHeader header(data->m_req_hdrmgr.m_buffer, data->m_req_hdrmgr.m_lochdr);

  // add/set sub range key and add slicer tag
  bool const rangestat = header.setKeyVal(TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rangestr, rangelen);

  if (!rangestat) {
    ERROR_LOG("Error trying to set range request header %s", rangestr);
    return false;
  }

  // create virtual connection back into ATS
  TSVConn const upvc = TSHttpConnectWithPluginId(reinterpret_cast<sockaddr *>(&data->m_client_ip), PLUGIN_NAME, 0);

  int const hlen = TSHttpHdrLengthGet(header.m_buffer, header.m_lochdr);

  // set up connection with the HttpConnect server
  data->m_upstream.setupConnection(upvc);
  data->m_upstream.setupVioWrite(contp, hlen);

  // Send full request
  TSHttpHdrPrint(header.m_buffer, header.m_lochdr, data->m_upstream.m_write.m_iobuf);
  TSVIOReenable(data->m_upstream.m_write.m_vio);

  /*
          std::string const headerstr(header.toString());
          DEBUG_LOG("Headers\n%s", headerstr.c_str());
  */

  // get ready for data back from the server
  data->m_upstream.setupVioRead(contp, INT64_MAX);

  // anticipate the next server response header
  TSHttpParserClear(data->m_http_parser);
  data->m_resp_hdrmgr.resetHeader();

  data->m_blockexpected              = 0;
  data->m_blockconsumed              = 0;
  data->m_blockstate                 = Data::BlockState::Active;
  data->m_server_block_header_parsed = false;

  return true;
}

bool
reader_avail_more_than(TSIOBufferReader const reader, int64_t bytes)
{
  TSIOBufferBlock block = TSIOBufferReaderStart(reader);

  if (nullptr == block) {
    return false;
  }

  while (nullptr != block) {
    int64_t const blockbytes = TSIOBufferBlockReadAvail(block, reader);
    if (bytes < blockbytes) {
      return true;
    } else {
      bytes -= blockbytes;
    }

    block = TSIOBufferBlockNext(block);
  }

  return false;
}
