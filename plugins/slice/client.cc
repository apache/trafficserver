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

#include "Config.h"
#include "server.h"
#include "util.h"

#include "swoc/TextView.h"

#include <cctype>
#include <cinttypes>
#include <limits>

namespace
{
// Miss bound for this purge: the request header when usable, else the config value.
int
purge_miss_bound(HttpHeader const &header, Config const *const conf)
{
  char probestr[64];
  int  probelen = sizeof(probestr);

  if (!header.valueForKey(conf->m_purge_probe_header.data(), conf->m_purge_probe_header.size(), probestr, &probelen)) {
    return conf->m_purge_probe_blocks;
  }

  swoc::TextView value{probestr, static_cast<size_t>(probelen)};
  // isspace is only defined for values representable as unsigned char
  value.trim_if([](char c) { return 0 != isspace(static_cast<unsigned char>(c)); });

  swoc::TextView parsed;
  intmax_t const blocks = swoc::svtoi(value, &parsed, 10);

  // parsed must cover the whole value: "8abc" is a mistake, not eight blocks
  if (parsed.size() != value.size() || blocks <= 0 || std::numeric_limits<int>::max() < blocks) {
    ERROR_LOG("Ignoring invalid %.*s value '%.*s'", static_cast<int>(conf->m_purge_probe_header.size()),
              conf->m_purge_probe_header.data(), probelen, probestr);
    return conf->m_purge_probe_blocks;
  }

  return static_cast<int>(blocks);
}

} // namespace

// this is called once per transaction when the client sends a req header
bool
handle_client_req(TSCont contp, TSEvent event, Data *const data)
{
  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE: {
    if (nullptr == data->m_http_parser) {
      data->m_http_parser = TSHttpParserCreate();
    }

    // Read the header from the buffer
    int64_t consumed = 0;
    if (TS_PARSE_DONE !=
        data->m_req_hdrmgr.populateFrom(data->m_http_parser, data->m_dnstream.m_read.m_reader, TSHttpHdrParseReq, &consumed)) {
      return false;
    }

    // update the VIO
    TSVIO const input_vio = data->m_dnstream.m_read.m_vio;
    TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + consumed);

    // make the header manipulator
    HttpHeader header(data->m_req_hdrmgr.m_buffer, data->m_req_hdrmgr.m_lochdr);

    // set the request url back to pristine in case of plugin stacking
    header.setUrl(data->m_urlbuf, data->m_urlloc);

    header.setKeyVal(TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST, data->m_hostname, data->m_hostlen);

    // default: whole file (unknown, wait for first server response)
    Range rangebe;

    char                rangestr[1024];
    int                 rangelen = sizeof(rangestr);
    bool const          hasRange = header.valueForKey(TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE, rangestr, &rangelen,
                                                      0); // <-- first range only
    Config const *const conf     = data->m_config;
    if (hasRange) {
      // write parsed header into slicer meta tag
      header.setKeyVal(conf->m_skip_header.c_str(), conf->m_skip_header.size(), rangestr, rangelen);
      bool const isRangeGood = rangebe.fromStringClosed(rangestr);

      if (isRangeGood) {
        DEBUG_LOG("%p Partial content request", data);
        data->m_statustype = TS_HTTP_STATUS_PARTIAL_CONTENT;
      } else // signal a 416 needs to be formed and sent
      {
        DEBUG_LOG("%p Ill formed/unhandled range: %s", data, rangestr);
        data->m_statustype = TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

        // First block will give Content-Length
        rangebe = Range(0, conf->m_blockbytes);
      }
    } else {
      DEBUG_LOG("%p Full content request", data);
      static char const *const valstr = "-";
      static size_t const      vallen = strlen(valstr);
      header.setKeyVal(conf->m_skip_header.data(), conf->m_skip_header.size(), valstr, vallen);
      data->m_statustype = TS_HTTP_STATUS_OK;
      rangebe            = Range(0, Range::maxval);
    }

    if (Config::RefType::First == conf->m_reftype) {
      data->m_blocknum = 0;
    } else {
      data->m_blocknum = rangebe.firstBlockFor(conf->m_blockbytes);
    }

    data->m_req_range = rangebe;

    if (data->is_purge()) {
      // The substituted range covers block 0, so walking it would delete the head
      if (TS_HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE == data->m_statustype) {
        ERROR_LOG("Refusing PURGE with an unparseable range");
        finish_purge(contp, data, TS_HTTP_STATUS_BAD_REQUEST);
        return true;
      }

      data->m_purge_miss_bound = purge_miss_bound(header, conf);
      DEBUG_LOG("%p Purge miss bound %d block(s)", data, data->m_purge_miss_bound);

      // A suffix range cannot know its start block, so purge a superset: everything
      if (data->m_req_range.isEndBytes()) {
        data->m_req_range = Range(0, Range::maxval);
        data->m_blocknum  = 0;
        DEBUG_LOG("%p Purge suffix range widened to the whole object", data);
      }

      // The miss bound is for this proxy to act on, not to propagate
      header.removeKey(conf->m_purge_probe_header.data(), conf->m_purge_probe_header.size());
    }

    // remove ATS keys to avoid 404 loop
    header.removeKey(TS_MIME_FIELD_VIA, TS_MIME_LEN_VIA);
    header.removeKey(TS_MIME_FIELD_X_FORWARDED_FOR, TS_MIME_LEN_X_FORWARDED_FOR);

    // send block request to server
    if (!request_block(contp, data)) {
      abort(contp, data);
      return false;
    }

    // for subsequent blocks remove any conditionals which may fail
    // an optimization would be to wait until the first block succeeds
    header.removeKey(TS_MIME_FIELD_IF_MATCH, TS_MIME_LEN_IF_MATCH);
    header.removeKey(TS_MIME_FIELD_IF_MODIFIED_SINCE, TS_MIME_LEN_IF_MODIFIED_SINCE);
    header.removeKey(TS_MIME_FIELD_IF_NONE_MATCH, TS_MIME_LEN_IF_NONE_MATCH);
    header.removeKey(TS_MIME_FIELD_IF_RANGE, TS_MIME_LEN_IF_RANGE);
    header.removeKey(TS_MIME_FIELD_IF_UNMODIFIED_SINCE, TS_MIME_LEN_IF_UNMODIFIED_SINCE);
  } break;
  default: {
    DEBUG_LOG("%p handle_client_req unhandled event %d %s", data, event, TSHttpEventNameLookup(event));
  } break;
  }

  return true;
}

// this is when the client starts asking us for more data
void
handle_client_resp(TSCont contp, TSEvent event, Data *const data)
{
  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY: {
    // finish_purge writes the whole response at once; nothing to throttle or pull
    if (data->is_purge()) {
      break;
    }

    switch (data->m_blockstate) {
    case BlockState::Fail:
    case BlockState::PendingRef:
    case BlockState::ActiveRef: {
      TSVIO const   output_vio  = data->m_dnstream.m_write.m_vio;
      int64_t const output_done = TSVIONDoneGet(output_vio);
      int64_t const output_sent = data->m_bytessent;

      if (output_sent == output_done) {
        DEBUG_LOG("Downstream output is done, shutting down");
        shutdown(contp, data);
      }
    } break;

    case BlockState::Pending: {
      // throttle
      TSVIO const   output_vio  = data->m_dnstream.m_write.m_vio;
      int64_t const output_done = TSVIONDoneGet(output_vio);
      int64_t const output_sent = data->m_bytessent;
      int64_t const threshout   = data->m_config->m_blockbytes;
      int64_t const buffered    = output_sent - output_done;

      if (threshout < buffered) {
        DEBUG_LOG("%p handle_client_resp: throttling %" PRId64, data, buffered);
      } else {
        DEBUG_LOG("Starting next block request");
        if (!request_block(contp, data)) {
          data->m_blockstate = BlockState::Fail;
          return;
        }
      }
    } break;
    case BlockState::Passthru: {
    } break;
    default:
      break;
    }
  } break;
  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    if (dbg_ctl.on() && reader_avail_more_than(data->m_upstream.m_read.m_reader, 0)) {
      int64_t const left = TSIOBufferReaderAvail(data->m_upstream.m_read.m_reader);
      DEBUG_LOG("%p WRITE_COMPLETE called with %" PRId64 " bytes left", data, left);
    }

    data->m_dnstream.close();
    if (!data->m_upstream.m_read.isOpen()) {
      shutdown(contp, data);
    }
  } break;
  default: {
    DEBUG_LOG("%p handle_client_resp unhandled event %d %s", data, event, TSHttpEventNameLookup(event));
  } break;
  }
}
