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

#include "client.h"

#include "transfer.h"

namespace
{
void
shutdown(TSCont const contp, Data *const data)
{
  DEBUG_LOG("shutting down transaction");
  delete data;
  TSContDestroy(contp);
}

// create and issue a block request
bool
requestBlock(TSCont contp, Data *const data)
{
  int64_t const blockbeg = (data->m_blockbytes_config * data->m_blocknum);
  Range blockbe(blockbeg, blockbeg + data->m_blockbytes_config);

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
  TSVConn const upvc = TSHttpConnect((sockaddr *)&data->m_client_ip);

  // set up connection with the HttpConnect server, maybe clear old one
  data->m_upstream.setupConnection(upvc);
  data->m_upstream.setupVioWrite(contp);

  TSHttpHdrPrint(header.m_buffer, header.m_lochdr, data->m_upstream.m_write.m_iobuf);
  TSVIOReenable(data->m_upstream.m_write.m_vio);

  /*
          std::string const headerstr(header.toString());
          DEBUG_LOG("Headers\n%s", headerstr.c_str());
  */

  // get ready for data back from the server
  data->m_upstream.setupVioRead(contp);

  // anticipate the next server response header
  TSHttpParserClear(data->m_http_parser);
  data->m_resp_hdrmgr.resetHeader();

  data->m_blockexpected              = 0;
  data->m_blockconsumed              = 0;
  data->m_iseos                      = false;
  data->m_server_block_header_parsed = false;

  return true;
}

} // namespace

// this is called once per transaction when the client sends a req header
bool
handle_client_req(TSCont contp, TSEvent event, Data *const data)
{
  if (TS_EVENT_VCONN_READ_READY == event || TS_EVENT_VCONN_READ_COMPLETE == event) {
    if (nullptr == data->m_http_parser) {
      data->m_http_parser = TSHttpParserCreate();
    }

    // the client request header didn't fit into the input buffer:
    if (TS_PARSE_DONE !=
        data->m_req_hdrmgr.populateFrom(data->m_http_parser, data->m_dnstream.m_read.m_reader, TSHttpHdrParseReq)) {
      return false;
    }

    // make the header manipulator
    HttpHeader header(data->m_req_hdrmgr.m_buffer, data->m_req_hdrmgr.m_lochdr);

    // set the request url back to pristine in case of plugin stacking
    header.setUrl(data->m_urlbuffer, data->m_urlloc);

    header.setKeyVal(TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, data->m_hostname, data->m_hostlen);

    // default: whole file (unknown, wait for first server response)
    Range rangebe;

    char rangestr[1024];
    int rangelen        = sizeof(rangestr);
    bool const hasRange = header.valueForKey(TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rangestr, &rangelen,
                                             0); // <-- first range only
    if (hasRange) {
      // write parsed header into slicer meta tag
      header.setKeyVal(SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO), rangestr, rangelen);
      bool const isRangeGood = rangebe.fromStringClosed(rangestr);

      if (isRangeGood) {
        DEBUG_LOG("Partial content request");
        data->m_statustype = TS_HTTP_STATUS_PARTIAL_CONTENT;
      } else // signal a 416 needs to be formed and sent
      {
        DEBUG_LOG("Ill formed/unhandled range: %s", rangestr);
        data->m_statustype = TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

        // First block will give Content-Length
        rangebe = Range(0, data->m_blockbytes_config);
      }
    } else {
      DEBUG_LOG("Full content request");
      static char const *const valstr = "-";
      static size_t const vallen      = strlen(valstr);
      header.setKeyVal(SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO), valstr, vallen);
      data->m_statustype = TS_HTTP_STATUS_OK;
      rangebe            = Range(0, Range::maxval);
    }

    // set to the first block in range
    data->m_blocknum  = rangebe.firstBlockFor(data->m_blockbytes_config);
    data->m_req_range = rangebe;

    // remove ATS keys to avoid 404 loop
    header.removeKey(TS_MIME_FIELD_VIA, TS_MIME_LEN_VIA);
    header.removeKey(TS_MIME_FIELD_X_FORWARDED_FOR, TS_MIME_LEN_X_FORWARDED_FOR);

    // send the first block request to server
    if (!requestBlock(contp, data)) {
      shutdown(contp, data);
      return false;
    }

    // for subsequent blocks remove any conditionals which may fail
    // an optimization would be to wait until the first block succeeds
    header.removeKey(TS_MIME_FIELD_IF_MATCH, TS_MIME_LEN_IF_MATCH);
    header.removeKey(TS_MIME_FIELD_IF_MODIFIED_SINCE, TS_MIME_LEN_IF_MODIFIED_SINCE);
    header.removeKey(TS_MIME_FIELD_IF_NONE_MATCH, TS_MIME_LEN_IF_NONE_MATCH);
    header.removeKey(TS_MIME_FIELD_IF_RANGE, TS_MIME_LEN_IF_RANGE);
    header.removeKey(TS_MIME_FIELD_IF_UNMODIFIED_SINCE, TS_MIME_LEN_IF_UNMODIFIED_SINCE);
  }

  return true;
}

// this is when the client starts asking us for more data
void
handle_client_resp(TSCont contp, TSEvent event, Data *const data)
{
  if (TS_EVENT_VCONN_WRITE_READY == event || TS_EVENT_VCONN_WRITE_COMPLETE == event) {
    transfer_content_bytes(data);

    // done transferring from server to client buffer?
    if (data->m_bytestosend <= data->m_bytessent) {
      // real amount transferred to client
      int64_t const bytessent(TSVIONDoneGet(data->m_dnstream.m_write.m_vio));

      // is the output buffer drained?
      if (data->m_bytestosend <= bytessent) {
        data->m_dnstream.close();
        if (!data->m_upstream.m_read.isOpen()) {
          shutdown(contp, data);
          return;
        }
      }

      // continue allowing the downstream to drain
      return;
    }

    // error condition from the server side
    if (data->m_bail) {
      shutdown(contp, data);
      return;
    }

    // check for upstream eos, maybe request next block
    if (data->m_iseos) {
      // still need to drain the server side
      if (0 < TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader)) {
        TSVIOReenable(data->m_dnstream.m_write.m_vio);
        return;
      }

      // if done or partial block
      if (data->m_blocknum < 0 || data->m_blockconsumed < data->m_blockexpected) {
        shutdown(contp, data);
        return;
      }

      // ready for next block
      requestBlock(contp, data);
    }
  }
  // client closed connection
  else if (TS_EVENT_ERROR == event) {
    DEBUG_LOG("got a TS_EVENT_ERROR from the client");

    // allow the upstream server to drain
    data->m_dnstream.close();
    if (!data->m_upstream.m_read.isOpen()) {
      shutdown(contp, data);
    }
  } else {
    DEBUG_LOG("Unhandled event: %d", event);
  }
}
