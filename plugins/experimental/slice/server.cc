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

#include "Config.h"
#include "ContentRange.h"
#include "response.h"
#include "transfer.h"
#include "util.h"

#include "ts/experimental.h"

#include <cinttypes>

namespace
{
ContentRange
contentRangeFrom(HttpHeader const &header)
{
  ContentRange bcr;

  /* Pull content length off the response header
    and manipulate it into a client response header
   */
  char rangestr[1024];
  int rangelen = sizeof(rangestr);

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

  //  DEBUG_LOG("First header\n%s", header.toString().c_str());

  data->m_dnstream.setupVioWrite(contp, INT64_MAX);

  TSVIO const output_vio      = data->m_dnstream.m_write.m_vio;
  TSIOBuffer const output_buf = data->m_dnstream.m_write.m_iobuf;

  // only process a 206, everything else gets a (possibly incomplete)
  // pass through
  if (TS_HTTP_STATUS_PARTIAL_CONTENT != header.status()) {
    DEBUG_LOG("Initial response other than 206: %d", header.status());

    // Should run TSVIONSetBytes(output_io, hlen + bodybytes);
    //    int const hlen = TSHttpHdrLengthGet(header.m_buffer, header.m_lochdr);
    //    TSVIONBytesSet(output_vio, hlen);
    TSHttpHdrPrint(header.m_buffer, header.m_lochdr, output_buf);
    transfer_all_bytes(data);

    return false;
  }

  ContentRange const blockcr = contentRangeFrom(header);
  // 206 with bad content range?
  if (!blockcr.isValid()) {
    static std::string const &msg502 = string502();
    TSVIONBytesSet(output_vio, msg502.size());
    TSIOBufferWrite(output_buf, msg502.data(), msg502.size());
    TSVIOReenable(output_vio);
    return false;
  }

  // set the resource content length from block response
  data->m_contentlen = blockcr.m_length;

  // special case last N bytes
  if (data->m_req_range.isEndBytes()) {
    data->m_req_range.m_end += data->m_contentlen;
    data->m_req_range.m_beg += data->m_contentlen;
    data->m_req_range.m_beg = std::max(static_cast<int64_t>(0), data->m_req_range.m_beg);
  } else {
    // fix up request range end now that we have the content length
    data->m_req_range.m_end = std::min(data->m_contentlen, data->m_req_range.m_end);
  }

  int64_t const bodybytes = data->m_req_range.size();

  // range begins past end of data but inside last block, send 416
  bool const send416 = (bodybytes <= 0 || TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE == data->m_statustype);
  if (send416) {
    std::string const &bodystr = bodyString416();
    form416HeaderAndBody(header, data->m_contentlen, bodystr);

    int const hlen     = TSHttpHdrLengthGet(header.m_buffer, header.m_lochdr);
    int64_t const blen = bodystr.size();

    TSVIONBytesSet(output_vio, int64_t(hlen) + blen);
    TSHttpHdrPrint(header.m_buffer, header.m_lochdr, output_buf);
    TSIOBufferWrite(output_buf, bodystr.data(), bodystr.size());
    TSVIOReenable(output_vio);

    data->m_upstream.m_read.close();

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
    int rangelen      = sizeof(rangestr);
    bool const crstat = respcr.toStringClosed(rangestr, &rangelen);

    // corner case, return 500 ??
    if (!crstat) {
      data->m_upstream.close();
      data->m_dnstream.close();

      ERROR_LOG("Bad/invalid response content range");
      return false;
    }

    header.setKeyVal(TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE, rangestr, rangelen);
  } else if (TS_HTTP_STATUS_OK == data->m_statustype) {
    header.setStatus(TS_HTTP_STATUS_OK);
    static char const *const reason = TSHttpHdrReasonLookup(TS_HTTP_STATUS_OK);
    header.setReason(reason, strlen(reason));
    header.removeKey(TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE);
  }

  char bufstr[1024];
  int const buflen = snprintf(bufstr, sizeof(bufstr), "%" PRId64, bodybytes);
  header.setKeyVal(TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, bufstr, buflen);

  // add the response header length to the total bytes to send
  int const hbytes = TSHttpHdrLengthGet(header.m_buffer, header.m_lochdr);

  TSVIONBytesSet(output_vio, hbytes + bodybytes);
  data->m_bytestosend = hbytes + bodybytes;
  TSHttpHdrPrint(header.m_buffer, header.m_lochdr, output_buf);
  data->m_bytessent = hbytes;
  TSVIOReenable(output_vio);

  return true;
}

void
logSliceError(char const *const message, Data const *const data, HttpHeader const &header_resp)
{
  Config *const config = data->m_config;

  bool const logToError = config->canLogError();

  // always write block stitch errors while in debug mode
  if (!logToError && !TSIsDebugTagSet(PLUGIN_NAME)) {
    return;
  }

  HttpHeader const header_req(data->m_req_hdrmgr.m_buffer, data->m_req_hdrmgr.m_lochdr);

  TSHRTime const timenowus = TShrtime();
  int64_t const msecs      = timenowus / 1000000;
  int64_t const secs       = msecs / 1000;
  int64_t const ms         = msecs % 1000;

  // Gather information on the request, must delete urlstr
  int urllen         = 0;
  char *const urlstr = header_req.urlString(&urllen);

  char urlpstr[16384];
  size_t urlplen = sizeof(urlpstr);
  TSStringPercentEncode(urlstr, urllen, urlpstr, urlplen, &urlplen, nullptr);

  if (nullptr != urlstr) {
    TSfree(urlstr);
  }

  // uas
  char uasstr[8192];
  int uaslen = sizeof(uasstr);
  header_req.valueForKey(TS_MIME_FIELD_USER_AGENT, TS_MIME_LEN_USER_AGENT, uasstr, &uaslen);

  // raw range request
  char rangestr[1024];
  int rangelen = sizeof(rangestr);
  header_req.valueForKey(SLICER_MIME_FIELD_INFO, strlen(SLICER_MIME_FIELD_INFO), rangestr, &rangelen);

  // Normalized range request
  ContentRange const crange(data->m_req_range.m_beg, data->m_req_range.m_end, data->m_contentlen);
  char normstr[1024];
  int normlen = sizeof(normstr);
  crange.toStringClosed(normstr, &normlen);

  // block range request
  int64_t const blockbeg = data->m_blocknum * data->m_config->m_blockbytes;
  int64_t const blockend = std::min(blockbeg + data->m_config->m_blockbytes, data->m_contentlen);

  // Block response data
  TSHttpStatus const statusgot = header_resp.status();

  // content range
  char crstr[1024];
  int crlen = sizeof(crstr);
  header_resp.valueForKey(TS_MIME_FIELD_CONTENT_RANGE, TS_MIME_LEN_CONTENT_RANGE, crstr, &crlen);

  // etag
  char etagstr[1024];
  int etaglen = sizeof(etagstr);
  header_resp.valueForKey(TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG, etagstr, &etaglen);

  // last modified
  char lmstr[1024];
  int lmlen = sizeof(lmstr);
  header_resp.valueForKey(TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, lmstr, &lmlen);

  // cc
  char ccstr[2048];
  int cclen = sizeof(ccstr);
  header_resp.valueForKey(TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL, ccstr, &cclen);

  // via tag
  char viastr[8192];
  int vialen = sizeof(viastr);
  header_resp.valueForKey(TS_MIME_FIELD_VIA, TS_MIME_LEN_VIA, viastr, &vialen);

  char etagexpstr[1024];
  size_t etagexplen = sizeof(etagexpstr);
  TSStringPercentEncode(data->m_etag, data->m_etaglen, etagexpstr, etagexplen, &etagexplen, nullptr);

  char etaggotstr[1024];
  size_t etaggotlen = sizeof(etaggotstr);
  TSStringPercentEncode(etagstr, etaglen, etaggotstr, etaggotlen, &etaggotlen, nullptr);

  DEBUG_LOG("Logging Block Stitch error");

  ERROR_LOG("%" PRId64 ".%" PRId64 " reason=\"%s\""
            " uri=\"%.*s\""
            " uas=\"%.*s\""
            " req_range=\"%.*s\""
            " norm_range=\"%.*s\""

            " etag_exp=\"%.*s\""
            " lm_exp=\"%.*s\""

            " blk_range=\"%" PRId64 "-%" PRId64 "\""

            " status_got=\"%d\""
            " cr_got=\"%.*s\""
            " etag_got=\"%.*s\""
            " lm_got=\"%.*s\""
            " cc=\"%.*s\""
            " via=\"%.*s\"",
            secs, ms, message, (int)urlplen, urlpstr, uaslen, uasstr, rangelen, rangestr, normlen, normstr, (int)etagexplen,
            etagexpstr, data->m_lastmodifiedlen, data->m_lastmodified, blockbeg, blockend - 1, statusgot, crlen, crstr,
            (int)etaggotlen, etaggotstr, lmlen, lmstr, cclen, ccstr, vialen, viastr);
}

bool
handleNextServerHeader(Data *const data, TSCont const contp)
{
  // block response header
  HttpHeader header(data->m_resp_hdrmgr.m_buffer, data->m_resp_hdrmgr.m_lochdr);
  //  DEBUG_LOG("Next Header:\n%s", header.toString().c_str());

  // only process a 206, everything else just aborts
  if (TS_HTTP_STATUS_PARTIAL_CONTENT != header.status()) {
    logSliceError("Non 206 internal block response", data, header);
    return false;
  }

  // can't parse the content range header, abort -- might be too strict
  ContentRange const blockcr = contentRangeFrom(header);
  if (!blockcr.isValid() || blockcr.m_length != data->m_contentlen) {
    logSliceError("Mismatch/Bad block Content-Range", data, header);
    return false;
  }

  bool same = true;

  // prefer the etag but use Last-Modified if we must.
  char etag[8192];
  int etaglen = sizeof(etag);
  header.valueForKey(TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG, etag, &etaglen);

  if (0 < data->m_etaglen || 0 < etaglen) {
    same = data->m_etaglen == etaglen && 0 == strncmp(etag, data->m_etag, etaglen);
    if (!same) {
      logSliceError("Mismatch block Etag", data, header);
    }
  } else {
    char lastmodified[8192];
    int lastmodifiedlen = sizeof(lastmodified);
    header.valueForKey(TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, lastmodified, &lastmodifiedlen);
    if (0 < data->m_lastmodifiedlen || 0 != lastmodifiedlen) {
      same = data->m_lastmodifiedlen == lastmodifiedlen && 0 == strncmp(lastmodified, data->m_lastmodified, lastmodifiedlen);
      if (!same) {
        logSliceError("Mismatch block Last-Modified", data, header);
      }
    }
  }

  if (!same) {
    data->m_upstream.close();
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
  DEBUG_LOG("%p handle_server_resp: %s", data, TSHttpEventNameLookup(event));

#if defined(COLLECT_STATS)
  TSStatIntIncrement(stats::Server, 1);
#endif

  switch (event) {
  case TS_EVENT_VCONN_READ_READY: {
    // has block response header been parsed??
    if (!data->m_server_block_header_parsed) {
      int64_t consumed              = 0;
      TSIOBufferReader const reader = data->m_upstream.m_read.m_reader;
      TSVIO const input_vio         = data->m_upstream.m_read.m_vio;
      TSParseResult const res       = data->m_resp_hdrmgr.populateFrom(data->m_http_parser, reader, TSHttpHdrParseResp, &consumed);

      TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + consumed);

      // the server response header didn't fit into the input buffer.
      if (TS_PARSE_CONT == res) {
        return;
      }

      // very first server response header
      bool headerStat = false;

      if (TS_PARSE_DONE == res) {
        if (!data->m_server_first_header_parsed) {
          headerStat                         = handleFirstServerHeader(data, contp);
          data->m_server_first_header_parsed = true;
        } else {
          headerStat = handleNextServerHeader(data, contp);
        }

        data->m_server_block_header_parsed = true;
      }

      // kill the upstream and allow dnstream to clean up
      if (!headerStat) {
        data->m_upstream.abort();
        data->m_blockstate = Data::BlockState::Fail;
        if (data->m_dnstream.m_write.isOpen()) {
          TSVIOReenable(data->m_dnstream.m_write.m_vio);
        } else {
          shutdown(contp, data);
        }
        return;
      }

      // how much to fast forward into this data block
      data->m_blockskip = data->m_req_range.skipBytesForBlock(data->m_config->m_blockbytes, data->m_blocknum);
    }

    transfer_content_bytes(data);
  } break;
  case TS_EVENT_VCONN_READ_COMPLETE: {
    // fprintf(stderr, "%p: TS_EVENT_VCONN_READ_COMPLETE\n", data);
  } break;
  case TS_EVENT_VCONN_EOS: {
    data->m_blockstate = Data::BlockState::Pending;
    data->m_upstream.close();

    // check for block truncation
    if (data->m_blockconsumed < data->m_blockexpected) {
      DEBUG_LOG("%p handle_server_resp truncation: %" PRId64 "\n", data, data->m_blockexpected - data->m_blockconsumed);
      data->m_blockstate = Data::BlockState::Fail;
      //      shutdown(contp, data);
      return;
    }

    // prepare for the next request block
    ++data->m_blocknum;

    // when we get a "bytes=-<end>" last N bytes request the plugin
    // issues a speculative request for the first block
    // in that case fast forward to the real first in range block
    // Btw this isn't implemented yet, to be handled
    int64_t const firstblock = data->m_req_range.firstBlockFor(data->m_config->m_blockbytes);
    if (data->m_blocknum < firstblock) {
      data->m_blocknum = firstblock;
    }

    // continue processing blocks?
    if (data->m_req_range.blockIsInside(data->m_config->m_blockbytes, data->m_blocknum)) {
      // Don't immediately request the next slice if the client
      // isn't keeping up

      bool start_next_block = true;

      // throttle condition
      if (data->m_config->m_throttle && data->m_dnstream.m_read.isOpen()) {
        TSVIO const output_vio    = data->m_dnstream.m_write.m_vio;
        int64_t const output_done = TSVIONDoneGet(output_vio);
        int64_t const output_sent = data->m_bytessent;
        int64_t const threshout   = data->m_config->m_blockbytes;

        if (threshout < (output_done - output_sent)) {
          start_next_block = false;
          DEBUG_LOG("%p handle_server_resp: throttling %" PRId64, data, (output_done - output_sent));
        }
      }

      if (start_next_block) {
        request_block(contp, data);
      }

    } else {
      data->m_upstream.close();
      data->m_blockstate = Data::BlockState::Done;
      if (!data->m_dnstream.m_read.isOpen()) {
        shutdown(contp, data);
      }
    }
  } break;
  default: {
    DEBUG_LOG("%p handle_server_resp uhandled event: %s", data, TSHttpEventNameLookup(event));
  } break;
  }
}
