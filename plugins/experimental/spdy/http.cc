/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// http.cc - Low level routines to write HTTP messages.

#include "ts/ts.h"
#include "ink_defs.h"

#include <spdy/spdy.h>
#include <base/logging.h>
#include "io.h"
#include "http.h"
#include "protocol.h"

static void
populate_http_headers(TSMBuffer buffer, TSMLoc header, spdy::protocol_version version, spdy::key_value_block &kvblock)
{
  char status[128];
  char httpvers[sizeof("HTTP/xx.xx")];

  int vers = TSHttpHdrVersionGet(buffer, header);
  TSHttpStatus code = TSHttpHdrStatusGet(buffer, header);

  snprintf(status, sizeof(status), "%u %s", (unsigned)code, TSHttpHdrReasonLookup(code));
  snprintf(httpvers, sizeof(httpvers), "HTTP/%u.%u", TS_HTTP_MAJOR(vers), TS_HTTP_MINOR(vers));

  if (version == spdy::PROTOCOL_VERSION_2) {
    kvblock["status"] = status;
    kvblock["version"] = httpvers;
  } else {
    kvblock[":status"] = status;
    kvblock[":version"] = httpvers;
  }
}

void
http_send_response(spdy_io_stream *stream, TSMBuffer buffer, TSMLoc header)
{
  TSMLoc field;
  spdy::key_value_block kvblock;

  debug_http_header(stream, buffer, header);

  field = TSMimeHdrFieldGet(buffer, header, 0);
  while (field) {
    TSMLoc next;
    std::pair<const char *, int> name;
    std::pair<const char *, int> value;

    name.first = TSMimeHdrFieldNameGet(buffer, header, field, &name.second);

    // XXX zwoop says that pointer comparisons ought to be
    // sufficient here. If we can test and confirm, we can
    // remove all the strcmp()s.

    // The Connection, Keep-Alive, Proxy-Connection, and
    // Transfer-Encoding headers are not valid and MUST not be
    // sent.
    if (strcmp(name.first, TS_MIME_FIELD_CONNECTION) == 0 || strcmp(name.first, TS_MIME_FIELD_KEEP_ALIVE) == 0 ||
        strcmp(name.first, TS_MIME_FIELD_PROXY_CONNECTION) == 0 || strcmp(name.first, TS_MIME_FIELD_TRANSFER_ENCODING) == 0) {
      debug_http("[%p/%u] skipping %s header", stream->io, stream->stream_id, name.first);
      goto skip;
    }

    value.first = TSMimeHdrFieldValueStringGet(buffer, header, field, -1, &value.second);
    kvblock.insert(std::string(name.first, name.second), std::string(value.first, value.second));

  skip:
    next = TSMimeHdrFieldNext(buffer, header, field);
    TSHandleMLocRelease(buffer, header, field);
    field = next;
  }

  populate_http_headers(buffer, header, stream->version, kvblock);
  spdy_send_syn_reply(stream, kvblock);
}

void
http_send_error(spdy_io_stream *stream, TSHttpStatus status)
{
  scoped_mbuffer buffer;
  scoped_http_header header(buffer.get());

  TSHttpHdrTypeSet(buffer.get(), header.get(), TS_HTTP_TYPE_RESPONSE);
  TSHttpHdrVersionSet(buffer.get(), header.get(), TS_HTTP_VERSION(1, 1));
  TSHttpHdrStatusSet(buffer.get(), header.get(), status);

  debug_http("[%p/%u] sending a HTTP %d result for %s %s://%s%s", stream->io, stream->stream_id, status,
             stream->kvblock.url().method.c_str(), stream->kvblock.url().scheme.c_str(), stream->kvblock.url().hostport.c_str(),
             stream->kvblock.url().path.c_str());

  http_send_response(stream, buffer.get(), header.get());
  spdy_send_data_frame(stream, spdy::FLAG_FIN, nullptr, 0);
}

void
http_send_content(spdy_io_stream *stream, TSIOBufferReader reader)
{
  TSIOBufferBlock blk;
  int64_t consumed = 0;

  blk = TSIOBufferReaderStart(stream->input.reader);
  while (blk) {
    const char *ptr;
    int64_t nbytes;

    ptr = TSIOBufferBlockReadStart(blk, reader, &nbytes);
    if (ptr && nbytes) {
      spdy_send_data_frame(stream, 0 /* flags */, ptr, nbytes);
      consumed += nbytes;
    }

    blk = TSIOBufferBlockNext(blk);
  }

  TSIOBufferReaderConsume(reader, consumed);
}

void
debug_http_header(const spdy_io_stream *stream, TSMBuffer buffer, TSMLoc header)
{
  if (unlikely(TSIsDebugTagSet("spdy.http"))) {
    spdy_io_buffer iobuf;
    int64_t nbytes;
    int64_t avail;
    const char *ptr;
    TSIOBufferBlock blk;

    TSHttpHdrPrint(buffer, header, iobuf.buffer);
    blk = TSIOBufferReaderStart(iobuf.reader);
    avail = TSIOBufferBlockReadAvail(blk, iobuf.reader);
    ptr = (const char *)TSIOBufferBlockReadStart(blk, iobuf.reader, &nbytes);

    debug_http("[%p/%u] http request (%" PRIu64 " of %" PRIu64 " bytes):\n%*.*s", stream, stream->stream_id, nbytes, avail,
               (int)nbytes, (int)nbytes, ptr);
  }
}

http_parser::http_parser() : parser(TSHttpParserCreate()), mbuffer(), header(mbuffer.get()), complete(false)
{
}

http_parser::~http_parser()
{
  if (parser) {
    TSHttpParserDestroy(parser);
  }
}

ssize_t
http_parser::parse(TSIOBufferReader reader)
{
  TSIOBufferBlock blk;
  ssize_t consumed = 0;

  for (blk = TSIOBufferReaderStart(reader); blk; blk = TSIOBufferBlockNext(blk)) {
    const char *ptr;
    const char *end;
    int64_t nbytes;
    TSParseResult result;

    ptr = TSIOBufferBlockReadStart(blk, reader, &nbytes);
    if (ptr == nullptr || nbytes == 0) {
      continue;
    }

    end = ptr + nbytes;
    result = TSHttpHdrParseResp(parser, mbuffer.get(), header.get(), &ptr, end);
    switch (result) {
    case TS_PARSE_ERROR:
      return (ssize_t)result;
    case TS_PARSE_DONE:
    case TS_PARSE_OK:
      this->complete = true;
    case TS_PARSE_CONT:
      // We consumed the buffer we got minus the remainder.
      consumed += (nbytes - std::distance(ptr, end));
    }

    if (this->complete) {
      break;
    }
  }

  TSIOBufferReaderConsume(reader, consumed);
  return consumed;
}

static void
make_ts_http_url(TSMBuffer buffer, TSMLoc header, const spdy::key_value_block &kvblock)
{
  TSReturnCode tstatus;
  TSMLoc url;

  tstatus = TSHttpHdrUrlGet(buffer, header, &url);
  if (tstatus == TS_ERROR) {
    tstatus = TSUrlCreate(buffer, &url);
  }

  TSUrlSchemeSet(buffer, url, kvblock.url().scheme.data(), kvblock.url().scheme.size());
  TSUrlHostSet(buffer, url, kvblock.url().hostport.data(), kvblock.url().hostport.size());
  TSUrlPathSet(buffer, url, kvblock.url().path.data(), kvblock.url().path.size());
  TSHttpHdrMethodSet(buffer, header, kvblock.url().method.data(), kvblock.url().method.size());

  TSHttpHdrUrlSet(buffer, header, url);

  TSAssert(tstatus == TS_SUCCESS);
}

static TSMLoc
make_ts_http_header(TSMBuffer buffer, const spdy::key_value_block &kvblock)
{
  scoped_http_header header(buffer);

  TSHttpHdrTypeSet(buffer, header, TS_HTTP_TYPE_REQUEST);

  // XXX extract the real HTTP version header from kvblock.url()
  TSHttpHdrVersionSet(buffer, header, TS_HTTP_VERSION(1, 1));
  make_ts_http_url(buffer, header, kvblock);

  // Duplicate the header fields into the MIME header for the HTTP request we
  // are building.
  for (auto ptr(kvblock.begin()); ptr != kvblock.end(); ++ptr) {
    if (ptr->first[0] != ':') {
      TSMLoc field;

      // XXX Need special handling for duplicate headers; we should
      // append them as a multi-value

      TSMimeHdrFieldCreateNamed(buffer, header, ptr->first.c_str(), -1, &field);
      TSMimeHdrFieldValueStringInsert(buffer, header, field, -1, ptr->second.c_str(), -1);
      TSMimeHdrFieldAppend(buffer, header, field);
    }
  }

  return header.release();
}

scoped_http_header::scoped_http_header(TSMBuffer b, const spdy::key_value_block &kvblock) : buffer(b)
{
  this->header = make_ts_http_header(buffer, kvblock);
}

scoped_http_header::scoped_http_header(TSMBuffer b) : header(TS_NULL_MLOC), buffer(b)
{
  header = TSHttpHdrCreate(buffer);
}

/* vim: set sw=4 ts=4 tw=79 et : */
