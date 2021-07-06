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
  } else {
    // ensure null termination
    rangestr[rangelen] = '\0';
    if (!bcr.fromStringClosed(rangestr)) {
      DEBUG_LOG("invalid response header, malformed Content-Range, %s", rangestr);
    }
  }

  return bcr;
}

int64_t
contentLengthFrom(HttpHeader const &header)
{
  int64_t bytes = 0;

  char constr[1024];
  int conlen = sizeof(constr);

  // look for expected Content-Length field
  bool const hasContentLength(header.valueForKey(TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH, constr, &conlen));

  if (!hasContentLength) {
    DEBUG_LOG("invalid response header, no Content-Length");
    bytes = INT64_MAX;
  } else {
    // ensure null termination
    constr[conlen] = '\0';
    char *endptr   = nullptr;
    bytes          = std::max(static_cast<int64_t>(0), static_cast<int64_t>(strtoll(constr, &endptr, 10)));
  }

  return bytes;
}

// Also reference server header
enum HeaderState {
  Good,
  Fail,
  Passthru,
};

HeaderState
handleFirstServerHeader(Data *const data, TSCont const contp)
{
  HttpHeader header(data->m_resp_hdrmgr.m_buffer, data->m_resp_hdrmgr.m_lochdr);

  if (TSIsDebugTagSet(PLUGIN_NAME)) {
    DEBUG_LOG("First header\n%s", header.toString().c_str());
  }

  data->m_dnstream.setupVioWrite(contp, INT64_MAX);

  TSVIO const output_vio      = data->m_dnstream.m_write.m_vio;
  TSIOBuffer const output_buf = data->m_dnstream.m_write.m_iobuf;

  // only process a 206, everything else gets a (possibly incomplete)
  // pass through
  if (TS_HTTP_STATUS_PARTIAL_CONTENT != header.status()) {
    DEBUG_LOG("Initial response other than 206: %d", header.status());

    // Should run TSVIONSetBytes(output_io, hlen + bodybytes);
    int64_t const hlen = TSHttpHdrLengthGet(header.m_buffer, header.m_lochdr);
    int64_t const clen = contentLengthFrom(header);
    DEBUG_LOG("Passthru bytes: header: %" PRId64 " body: %" PRId64, hlen, clen);
    if (clen != INT64_MAX) {
      TSVIONBytesSet(output_vio, hlen + clen);
    } else {
      TSVIONBytesSet(output_vio, clen);
    }
    TSHttpHdrPrint(header.m_buffer, header.m_lochdr, output_buf);
    return HeaderState::Passthru;
  }

  ContentRange const blockcr = contentRangeFrom(header);

  // 206 with bad content range -- should NEVER happen.
  if (!blockcr.isValid()) {
    std::string const msg502 = string502(header.version());
    TSVIONBytesSet(output_vio, msg502.size());
    TSIOBufferWrite(output_buf, msg502.data(), msg502.size());
    TSVIOReenable(output_vio);
    return HeaderState::Fail;
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
    return HeaderState::Fail;
  }

  // save data header string
  data->m_datelen = sizeof(data->m_date);
  header.valueForKey(TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE, data->m_date, &data->m_datelen);

  // save weak cache header identifiers (rfc7232 section 2)
  data->m_etaglen = sizeof(data->m_etag);
  header.valueForKey(TS_MIME_FIELD_ETAG, TS_MIME_LEN_ETAG, data->m_etag, &data->m_etaglen);
  data->m_lastmodifiedlen = sizeof(data->m_lastmodified);
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
      return HeaderState::Fail;
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

  return HeaderState::Good;
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
  time_t lmgot = 0;
  header_resp.timeForKey(TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, &lmgot);

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
            " lm_got=\"%jd\""
            " cc=\"%.*s\""
            " via=\"%.*s\"  - attempting to recover",
            secs, ms, message, (int)urlplen, urlpstr, uaslen, uasstr, rangelen, rangestr, normlen, normstr, (int)etagexplen,
            etagexpstr, data->m_lastmodifiedlen, data->m_lastmodified, blockbeg, blockend - 1, statusgot, crlen, crstr,
            (int)etaggotlen, etaggotstr, static_cast<intmax_t>(lmgot), cclen, ccstr, vialen, viastr);
}

bool
handleNextServerHeader(Data *const data, TSCont const contp)
{
  // block response header
  HttpHeader header(data->m_resp_hdrmgr.m_buffer, data->m_resp_hdrmgr.m_lochdr);
  if (TSIsDebugTagSet(PLUGIN_NAME)) {
    DEBUG_LOG("Next Header:\n%s", header.toString().c_str());
  }

  bool same = true;

  switch (header.status()) {
  case TS_HTTP_STATUS_NOT_FOUND:
    // need to reissue reference slice
    logSliceError("404 internal block response (asset gone)", data, header);
    same = false;
    break;
  case TS_HTTP_STATUS_PARTIAL_CONTENT:
    break;
  default:
    DEBUG_LOG("Non 206/404 internal block response encountered");
    return false;
    break;
  }

  // can't parse the content range header, abort -- might be too strict
  ContentRange blockcr;

  if (same) {
    blockcr = contentRangeFrom(header);
    if (!blockcr.isValid() || blockcr.m_length != data->m_contentlen) {
      logSliceError("Mismatch/Bad block Content-Range", data, header);
      same = false;
    }
  }

  if (same) {
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
      char lastmodified[33];
      int lastmodifiedlen = sizeof(lastmodified);
      header.valueForKey(TS_MIME_FIELD_LAST_MODIFIED, TS_MIME_LEN_LAST_MODIFIED, lastmodified, &lastmodifiedlen);
      if (0 < data->m_lastmodifiedlen || 0 < lastmodifiedlen) {
        same = data->m_lastmodifiedlen == lastmodifiedlen && 0 == strncmp(lastmodified, data->m_lastmodified, lastmodifiedlen);
        if (!same) {
          logSliceError("Mismatch block Last-Modified", data, header);
        }
      }
    }
  }

  // Header mismatch
  if (same) {
    // If we were in reference block refetch mode and the headers
    // still match there is a problem
    if (BlockState::ActiveRef == data->m_blockstate) {
      ERROR_LOG("Reference block refetched, got the same block back again");
      return false;
    }
  } else {
    switch (data->m_blockstate) {
    case BlockState::Active: {
      data->m_upstream.abort();

      // Refetch the current interior slice
      data->m_blockstate = BlockState::PendingInt;

      time_t date = 0;
      header.timeForKey(TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE, &date);

      // Ask for any slice newer than the cached one
      time_t const dateims = date + 1;

      DEBUG_LOG("Attempting to reissue interior slice block request with IMS header time: %jd", static_cast<intmax_t>(dateims));

      // add special CRR IMS header to the request
      HttpHeader headerreq(data->m_req_hdrmgr.m_buffer, data->m_req_hdrmgr.m_lochdr);
      if (!headerreq.setKeyTime(X_CRR_IMS_HEADER.data(), X_CRR_IMS_HEADER.size(), dateims)) {
        ERROR_LOG("Failed setting '%.*s'", (int)X_CRR_IMS_HEADER.size(), X_CRR_IMS_HEADER.data());
        return false;
      }

    } break;
    case BlockState::ActiveInt: {
      data->m_upstream.abort();

      // New interior slice still mismatches, refetch the reference slice
      data->m_blockstate = BlockState::PendingRef;

      // convert reference date header to time_t
      time_t const date = TSMimeParseDate(data->m_date, data->m_datelen);

      // Ask for any slice newer than the cached one
      time_t const dateims = date + 1;

      DEBUG_LOG("Attempting to reissue reference slice block request with IMS header time: %jd", static_cast<intmax_t>(dateims));

      // add special CRR IMS header to the request
      HttpHeader headerreq(data->m_req_hdrmgr.m_buffer, data->m_req_hdrmgr.m_lochdr);
      if (!headerreq.setKeyTime(X_CRR_IMS_HEADER.data(), X_CRR_IMS_HEADER.size(), dateims)) {
        ERROR_LOG("Failed setting '%.*s'", (int)X_CRR_IMS_HEADER.size(), X_CRR_IMS_HEADER.data());
        return false;
      }

      // Reset for first block
      if (Config::RefType::First == data->m_config->m_reftype) {
        data->m_blocknum = 0;
      } else {
        data->m_blocknum = data->m_req_range.firstBlockFor(data->m_config->m_blockbytes);
      }

      return true;

    } break;
      // Refetch the reference slice
    case BlockState::ActiveRef: {
      // In this state the reference changed otherwise the asset is toast
      // reset the content length (if content length drove the mismatch)
      data->m_contentlen = blockcr.m_length;
      return true;
    } break;
    default:
      break;
    }
  }

  data->m_blockexpected = blockcr.rangeSize();

  return true;
}

} // namespace

// this is called every time the server has data for us
void
handle_server_resp(TSCont contp, TSEvent event, Data *const data)
{
  switch (event) {
  case TS_EVENT_VCONN_READ_READY: {
    if (data->m_blockstate == BlockState::Passthru) {
      transfer_all_bytes(data);
      return;
    }

    // has block response header been parsed??
    if (!data->m_server_block_header_parsed) {
      int64_t consumed              = 0;
      TSIOBufferReader const reader = data->m_upstream.m_read.m_reader;
      TSVIO const input_vio         = data->m_upstream.m_read.m_vio;
      TSParseResult const res       = data->m_resp_hdrmgr.populateFrom(data->m_http_parser, reader, TSHttpHdrParseResp, &consumed);

      TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + consumed);

      // the server response header didn't fit into the input buffer.
      // wait for more data from upstream
      if (TS_PARSE_CONT == res) {
        return;
      }

      bool headerStat = false;

      if (TS_PARSE_DONE == res) {
        if (!data->m_server_first_header_parsed) {
          HeaderState const state = handleFirstServerHeader(data, contp);

          data->m_server_first_header_parsed = true;
          switch (state) {
          case HeaderState::Fail:
            data->m_blockstate = BlockState::Fail;
            headerStat         = false;
            break;
          case HeaderState::Passthru: {
            data->m_blockstate = BlockState::Passthru;
            transfer_all_bytes(data);
            DEBUG_LOG("Going into a passthru state");
            return;
          } break;
          case HeaderState::Good:
          default:
            headerStat = true;
            break;
          }
        } else {
          headerStat = handleNextServerHeader(data, contp);
        }

        data->m_server_block_header_parsed = true;
      }

      // kill the upstream and allow dnstream to clean up
      if (!headerStat) {
        data->m_upstream.abort();
        data->m_blockstate = BlockState::Fail;
        if (data->m_dnstream.m_write.isOpen()) {
          TSVIOReenable(data->m_dnstream.m_write.m_vio);
        } else {
          shutdown(contp, data);
        }
        return;
      }

      // header may have been successfully parsed but with caveats
      switch (data->m_blockstate) {
        // request new version of current internal slice
      case BlockState::PendingInt:
      case BlockState::PendingRef: {
        if (!request_block(contp, data)) {
          data->m_blockstate = BlockState::Fail;
          if (data->m_dnstream.m_write.isOpen()) {
            TSVIOReenable(data->m_dnstream.m_write.m_vio);
          } else {
            shutdown(contp, data);
          }
        }
        return;
      } break;
      case BlockState::ActiveRef: {
        // Mark the reference block for "skip".
        int64_t const blockbytes      = data->m_config->m_blockbytes;
        int64_t const firstblock      = data->m_req_range.firstBlockFor(blockbytes);
        int64_t const blockpos        = firstblock * blockbytes;
        int64_t const firstblockbytes = std::min(blockbytes, data->m_contentlen - blockpos);
        data->m_blockskip             = firstblockbytes;

        // Check if we should abort the client
        if (data->m_dnstream.isOpen()) {
          TSVIO const output_vio    = data->m_dnstream.m_write.m_vio;
          int64_t const output_done = TSVIONDoneGet(output_vio);
          int64_t const output_sent = data->m_bytessent;
          if (output_done == output_sent) {
            data->m_dnstream.abort();
          }
        }
      } break;
      default: {
        // how much to normally fast forward into this data block
        data->m_blockskip = data->m_req_range.skipBytesForBlock(data->m_config->m_blockbytes, data->m_blocknum);
      } break;
      }
    }

    transfer_content_bytes(data);
  } break;
  case TS_EVENT_VCONN_READ_COMPLETE: {
    // fprintf(stderr, "%p: TS_EVENT_VCONN_READ_COMPLETE\n", data);
  } break;
  case TS_EVENT_VCONN_EOS: {
    switch (data->m_blockstate) {
    case BlockState::ActiveRef:
    case BlockState::Passthru: {
      transfer_all_bytes(data);
      data->m_upstream.close();
      TSVIO const output_vio = data->m_dnstream.m_write.m_vio;
      if (nullptr != output_vio) {
        TSVIOReenable(output_vio);
      } else {
        shutdown(contp, data);
      }
      return;
    } break;
    default:
      break;
    }

    // corner condition, good source header + 0 length aborted content
    // results in no header being read, just an EOS.
    // trying to delete the upstream will crash ATS (??)
    if (0 == data->m_blockexpected) {
      shutdown(contp, data); // this will crash if first block
      return;
    }

    transfer_content_bytes(data);

    data->m_upstream.close();
    data->m_blockstate = BlockState::Pending;

    // check for block truncation
    if (data->m_blockconsumed < data->m_blockexpected) {
      DEBUG_LOG("%p handle_server_resp truncation: %" PRId64 "\n", data, data->m_blockexpected - data->m_blockconsumed);
      data->m_blockstate = BlockState::Fail;
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

      if (data->m_dnstream.m_write.isOpen()) {
        bool start_next_block = true;

        // check throttle condition
        TSVIO const output_vio    = data->m_dnstream.m_write.m_vio;
        int64_t const output_done = TSVIONDoneGet(output_vio);
        int64_t const output_sent = data->m_bytessent;
        int64_t const threshout   = data->m_config->m_blockbytes;
        int64_t const buffered    = output_sent - output_done;

        if (threshout < buffered) {
          start_next_block = false;
          DEBUG_LOG("%p handle_server_resp: throttling %" PRId64, data, buffered);
        }

        if (start_next_block) {
          if (!request_block(contp, data)) {
            data->m_blockstate = BlockState::Fail;
            abort(contp, data);
            return;
          }
        }
      }
    } else {
      data->m_upstream.close();
      data->m_blockstate = BlockState::Done;
      if (!data->m_dnstream.m_write.isOpen()) {
        shutdown(contp, data);
      }
    }
  } break;
  default: {
    DEBUG_LOG("%p handle_server_resp uhandled event: %s", data, TSHttpEventNameLookup(event));
  } break;
  }
}
