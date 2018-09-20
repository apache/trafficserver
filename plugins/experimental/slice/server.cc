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

#include <cinttypes>

namespace
{
void
shutdown(TSCont const contp, Data *const data)
{
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

    static std::string const &msg502 = string502();

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
    data->m_bail               = true;
    std::string const &bodystr = bodyString416();
    form416HeaderAndBody(header, data->m_contentlen, bodystr);

    TSHttpHdrPrint(header.m_buffer, header.m_lochdr, data->m_dnstream.m_write.m_iobuf);

    TSIOBufferWrite(data->m_dnstream.m_write.m_iobuf, bodystr.data(), bodystr.size());

    TSVIOReenable(data->m_dnstream.m_write.m_vio);

    return false;
  }

  // save weak cache header identifiers (rfc7232 section 2)
  data->m_etaglen = sizeof(data->m_etag) - 1;
  header.valueForKey(TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG, data->m_etag, &data->m_etaglen);
  data->m_lastmodifiedlen = sizeof(data->m_lastmodified) - 1;
  header.valueForKey(TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, data->m_lastmodified, &data->m_lastmodifiedlen);

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

  // make sure the block comes from the same asset as the first block
  if (data->m_contentlen != blockcr.m_length) {
    ERROR_LOG("Mismatch in slice block Content-Range Len %" PRId64 " and %" PRId64, data->m_contentlen, blockcr.m_length);
    data->m_bail = true;
    return false;
  }

  bool same = true;

  // prefer the etag but use Last-Modified if we must.
  char etag[8192];
  int etaglen = sizeof(etag) - 1;
  header.valueForKey(TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG, etag, &etaglen);

  if (0 < data->m_etaglen || 0 < etaglen) {
    same = data->m_etaglen == etaglen && 0 == strncmp(etag, data->m_etag, etaglen);
    if (!same) {
      ERROR_LOG("Mismatch in slice block ETAG '%.*s' and '%.*s'", data->m_etaglen, data->m_etag, etaglen, etag);
    }
  } else {
    char lastmodified[8192];
    int lastmodifiedlen = sizeof(lastmodified) - 1;
    header.valueForKey(TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, lastmodified, &lastmodifiedlen);
    if (0 < data->m_lastmodifiedlen || 0 != lastmodifiedlen) {
      same = data->m_lastmodifiedlen == lastmodifiedlen && 0 == strncmp(lastmodified, data->m_lastmodified, lastmodifiedlen);
      if (!same) {
        ERROR_LOG("Mismatch in slice block Last-Modified '%.*s' and '%.*s'", data->m_lastmodifiedlen, data->m_lastmodified,
                  lastmodifiedlen, lastmodified);
      }
    }
  }

  if (!same) {
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
  if (TS_EVENT_VCONN_READ_READY == event || TS_EVENT_VCONN_READ_COMPLETE == event) {
    // has block reponse header been parsed??
    if (!data->m_server_block_header_parsed) {
      // the server response header didn't fit into the input buffer??
      if (TS_PARSE_DONE !=
          data->m_resp_hdrmgr.populateFrom(data->m_http_parser, data->m_upstream.m_read.m_reader, TSHttpHdrParseResp)) {
        return;
      }

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

    transfer_content_bytes(data);
  } else if (TS_EVENT_VCONN_EOS == event) {
    // from testing as far as I can tell, if the sub transaction returns
    // a valid header TS_EVENT_VCONN_READ_READY event is always called first.
    // this event being called means the input stream is null.
    // An upstream transaction that aborts immediately (or a few bytes)
    // after it sends a header may end up here with nothing in the upstream
    // buffer.

    // this is called when the upstream connection is done.
    // make sure to drain all the bytes out before
    // issuing the next block request
    data->m_iseos = true;

    // corner condition, good source header + 0 length aborted content
    // results in no header being read, just an EOS.
    // trying to delete the upstream will crash ATS (??)
    if (0 == data->m_blockexpected) {
      shutdown(contp, data); // this will crash if first block
      return;
    }

    transfer_content_bytes(data);

    if (!data->m_dnstream.m_write.isOpen()) // server drain condition
    {
      shutdown(contp, data);
      return;
    }

    // all bytes left transferred to client buffer
    if (0 == TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader)) {
      data->m_upstream.close();
      TSVIOReenable(data->m_dnstream.m_write.m_vio);
    }

    // prepare for the next request block
    ++data->m_blocknum;

    // when we get a "bytes=-<end>" last N bytes request the plugin
    // issues a speculative request for the first block
    // in that case fast forward to the real first in range block
    // Btw this isn't implemented yet, to be handled
    int64_t const firstblock(data->m_req_range.firstBlockFor(data->m_blockbytes_config));
    if (data->m_blocknum < firstblock) {
      data->m_blocknum = firstblock;
    }

    // done processing blocks?
    if (!data->m_req_range.blockIsInside(data->m_blockbytes_config, data->m_blocknum)) {
      data->m_blocknum = -1; // signal value no more blocks
    }
  } else {
    DEBUG_LOG("Unhandled event: %d", event);
  }
}
