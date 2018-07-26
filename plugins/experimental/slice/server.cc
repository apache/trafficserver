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

#include "server.h"

#include "ContentRange.h"
#include "response.h"
#include "transfer.h"

#include <cassert>
#include <cinttypes>
#include <iostream>

namespace
{
void
shutdown(TSCont const contp, Data *const data)
{
  // std::cerr << "server shutdown" << std::endl;
  DEBUG_LOG("shutting down transaction");
  delete data;
  TSContDestroy(contp);
}

ContentRange
contentRangeFrom(HttpHeader &header)
{
  ContentRange bcr;

  /* Pull content length off the response header
    and manipulate it into a client response header
   */
  static int const RLEN = 1024;
  char rangestr[RLEN];
  int rangelen = RLEN - 1;

  // look for expected Content-Range field
  bool const hasContentRange(header.valueForKey(TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE, rangestr, &rangelen));

  if (!hasContentRange) {
    DEBUG_LOG("invalid response header, no Content-Range");
  } else if (!bcr.fromStringClosed(rangestr)) {
    DEBUG_LOG("invalid response header, malformed Content-Range, %s", rangestr);
  }

  return bcr;
}

bool
handleFirstServerHeader(Data *const data, TSCont const contp)
{
  HttpHeader header(data->m_resp_hdrmgr.m_buffer, data->m_resp_hdrmgr.m_lochdr);

  data->m_dnstream.setupVioWrite(contp);

  // only process a 206, everything else gets a pass through
  if (TS_HTTP_STATUS_PARTIAL_CONTENT != header.status()) {
    DEBUG_LOG("Non 206 response from parent: %d", header.status());
    data->m_bail = true;

    TSHttpHdrPrint(header.m_buffer, header.m_lochdr, data->m_dnstream.m_write.m_iobuf);

    transfer_all_bytes(data);

    return false;
  }

  ContentRange const blockcr = contentRangeFrom(header);
  // 206 with bad content range?
  if (!blockcr.isValid()) {
    data->m_bail = true;

    static std::string const msg502 = string502();

    TSIOBufferWrite(data->m_dnstream.m_write.m_iobuf, msg502.data(), msg502.size());
    TSVIOReenable(data->m_dnstream.m_write.m_vio);

    return false;
  }

  // set the resource content length from block response
  data->m_contentlen = blockcr.m_length;

  // special case last N bytes
  if (data->m_req_range.isEndBytes()) {
    data->m_req_range.m_end += data->m_contentlen;
    data->m_req_range.m_beg += data->m_contentlen;
    data->m_req_range.m_beg = std::max((int64_t)0, data->m_req_range.m_beg);
  } else {
    // fix up request range end now that we have the content length
    data->m_req_range.m_end = std::min(data->m_contentlen, data->m_req_range.m_end);
  }

  int64_t const bodybytes = data->m_req_range.size();

  // range past end of data, assume 416 needs to be sent
  bool const send416 = (bodybytes <= 0 || TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE == data->m_statustype);
  if (send416) {
    data->m_bail = true;
    std::string const bodystr(bodyString416());
    form416HeaderAndBody(header, data->m_contentlen, bodystr);

    TSHttpHdrPrint(header.m_buffer, header.m_lochdr, data->m_dnstream.m_write.m_iobuf);

    TSIOBufferWrite(data->m_dnstream.m_write.m_iobuf, bodystr.data(), bodystr.size());

    TSVIOReenable(data->m_dnstream.m_write.m_vio);

    return false;
  }

  // size of the first block payload
  data->m_blockexpected = blockcr.rangeSize();

  // Now we can set up the expected client response
  if (TS_HTTP_STATUS_PARTIAL_CONTENT == data->m_statustype) {
    ContentRange respcr;
    respcr.m_beg    = data->m_req_range.m_beg;
    respcr.m_end    = data->m_req_range.m_end;
    respcr.m_length = data->m_contentlen;

    char rangestr[1024];
    int rangelen      = 1023;
    bool const crstat = respcr.toStringClosed(rangestr, &rangelen);

    // corner case, return 500 ??
    if (!crstat) {
      data->m_bail = true;

      data->m_upstream.close();
      data->m_dnstream.close();

      ERROR_LOG("Bad/invalid response content range");
      return false;
    }

    header.setKeyVal(TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE, rangestr, rangelen);
  }
  // fix up for 200 response
  else if (TS_HTTP_STATUS_OK == data->m_statustype) {
    header.setStatus(TS_HTTP_STATUS_OK);
    static char const *const reason = TSHttpHdrReasonLookup(TS_HTTP_STATUS_OK);
    header.setReason(reason, strlen(reason));
    header.removeKey(TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE);
  }

  char bufstr[1024];
  int const buflen = snprintf(bufstr, 1023, "%" PRId64, bodybytes);
  header.setKeyVal(TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, bufstr, buflen);

  // add the response header length to the total bytes to send
  int64_t const headerbytes = TSHttpHdrLengthGet(header.m_buffer, header.m_lochdr);

  data->m_bytestosend = headerbytes + bodybytes;

  TSHttpHdrPrint(header.m_buffer, header.m_lochdr, data->m_dnstream.m_write.m_iobuf);

  data->m_bytessent = headerbytes;

  TSVIOReenable(data->m_dnstream.m_write.m_vio);

  return true;
}

bool
handleNextServerHeader(Data *const data, TSCont const contp)
{
  HttpHeader header(data->m_resp_hdrmgr.m_buffer, data->m_resp_hdrmgr.m_lochdr);

  // only process a 206, everything else just aborts
  if (TS_HTTP_STATUS_PARTIAL_CONTENT != header.status()) {
    ERROR_LOG("Non 206 internal block response from parent: %d", header.status());
    data->m_bail = true;
    return false;
  }

  // can't parse the content range header, abort -- might be too strict
  ContentRange const blockcr = contentRangeFrom(header);
  if (!blockcr.isValid()) {
    ERROR_LOG("Unable to parse internal block Content-Range header");
    data->m_bail = true;
    return false;
  }

  data->m_blockexpected = blockcr.rangeSize();

  return true;
}

} // namespace

// this is called every time the server has data for us
void
handle_server_resp(TSCont contp, TSEvent event, Data *const data)
{
  // std::cerr << "handle_server_response " << (int)data->m_bail << " " << event << std::endl;

  if (TS_EVENT_VCONN_READ_READY == event || TS_EVENT_VCONN_READ_COMPLETE == event) {
    //    DEBUG_LOG("server has data ready to read");
    // has block reponse header been parsed??
    if (!data->m_server_block_header_parsed) {
      // the server response header didn't fit into the input buffer??
      if (TS_PARSE_DONE !=
          data->m_resp_hdrmgr.populateFrom(data->m_http_parser, data->m_upstream.m_read.m_reader, TSHttpHdrParseResp)) {
        return;
      }

      /*
      HttpHeader const header
        (data->m_resp_hdrmgr.m_buffer, data->m_resp_hdrmgr.m_lochdr);
      std::cerr << std::endl;
      std::cerr << "got a response header from server" << std::endl;
      std::cerr << header.toString() << std::endl;
      */

      // very first server response header
      bool headerStat = false;
      if (!data->m_server_first_header_parsed) {
        headerStat                         = handleFirstServerHeader(data, contp);
        data->m_server_first_header_parsed = true;
      } else {
        headerStat = handleNextServerHeader(data, contp);
      }

      data->m_server_block_header_parsed = true;

      // kill the upstream and allow dnstream to clean up
      if (!headerStat) {
        data->m_upstream.close();
        data->m_bail = true;
        if (data->m_dnstream.m_write.isOpen()) {
          TSVIOReenable(data->m_dnstream.m_write.m_vio);
        } else {
          shutdown(contp, data);
        }
        return;
      }

      // how much to fast forward into this data block
      data->m_blockskip = data->m_req_range.skipBytesForBlock(data->m_blockbytes_config, data->m_blocknum);
    }

    // std::cerr << "handle_server_response ready consumed: " <<
    transfer_content_bytes(data); // , "handle_server_resp READ_READY");
                                  // std::cerr << ' ' << data->m_blockconsumed << '/' << data->m_blockexpected
    //	<< ' ' << data->m_bytessent << '/' << data->m_bytestosend << '/' << TSVIONDoneGet(data->m_dnstream.m_write.m_vio)
    //	<< std::endl;
  } else if (TS_EVENT_VCONN_EOS == event) {
    // this is called when the upstream connection is done.
    // we still need to make sure to drain all the bytes out before
    // issuing the next block request
    data->m_iseos = true;

    // std::cerr << "eos hit" << std::endl;

    // corner condition, good source header + 0 length aborted content
    // results in no header being read, just an EOS.
    // trying to delete the upstream will crash ATS.
    if (0 == data->m_blockexpected) {
      //			data->m_dnstream.abort(); // <- crash if first block
      //			data->m_upstream.abort();
      //			TSContDestroy(contp);
      shutdown(contp, data); // this will crash if first block
      return;
    }

    // std::cerr << "handle_server_response eos consumed: " <<
    transfer_content_bytes(data); // , "handle_server_resp EOS");
                                  // std::cerr << ' ' << data->m_blockconsumed << '/' << data->m_blockexpected
    //	<< ' ' << data->m_bytessent << '/' << data->m_bytestosend << '/' << TSVIONDoneGet(data->m_dnstream.m_write.m_vio)
    //	<< std::endl;

    if (!data->m_dnstream.m_write.isOpen()) // server drain condition
    {
      // std::cerr << "client already closed, shutting down" << std::endl;
      shutdown(contp, data);
      return;
    }

    // all bytes left transferred to client buffer
    if (0 == TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader)) {
      data->m_upstream.close();
      TSVIOReenable(data->m_dnstream.m_write.m_vio);
    }

    // std::cerr << "Getting ready for next block" << std::endl;
    // data->m_upstream.close(); <-- can't do this!

    // prepare for the next request block
    ++data->m_blocknum;

    // when we get a "bytes=-<end>" last N bytes request the plugin
    // (like nginx) issues a speculative request for the first block
    // in that case fast forward to the real first in range block
    // Btw this isn't implemented yet, to be handled
    int64_t const firstblock(data->m_req_range.firstBlockFor(data->m_blockbytes_config));
    if (data->m_blocknum < firstblock) {
      // std::cerr << "setting first block" << std::endl;
      data->m_blocknum = firstblock;
    }

    // done processing blocks?
    if (!data->m_req_range.blockIsInside(data->m_blockbytes_config, data->m_blocknum)) {
      data->m_blocknum = -1; // signal value no more blocks
    }
  } else {
    std::cerr << __func__ << ": unhandled event: " << event << std::endl;
  }
}
