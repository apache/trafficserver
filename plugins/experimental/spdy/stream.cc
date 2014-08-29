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

#include "ts/ts.h"
#include "ink_defs.h"

#include <spdy/spdy.h>
#include <base/logging.h>
#include <base/inet.h>
#include "io.h"
#include "protocol.h"
#include "http.h"

#include <netdb.h>
#include <limits>

// NOTE: Reference counting SPDY streams.
//
// First, the primary protocol handler owns a reference count on each
// SPDY stream. However, this reference count can be released at almost
// any time when a RST request is received. So when we submit a request
// that for any asynchronous processing, we must hold an additional
// reference count in order to keep the stream alive until the request
// completes.
//
// Second, each stream keeps a reference to the SPDY IO control block
// which has an independent lifetime. This means that when we keep the
// stream alive (by taking a reference count), we also need to take a
// reference on the control block.

static int spdy_stream_io(TSCont, TSEvent, void *);

static bool
IN(const spdy_io_stream * s, spdy_io_stream::http_state_type h)
{
    return s->http_state & h;
}

static void
ENTER(spdy_io_stream * s, spdy_io_stream::http_state_type h)
{
    s->http_state |= h;
}

static void
LEAVE(spdy_io_stream * s, spdy_io_stream::http_state_type h)
{
    s->http_state &= ~h;
}

static bool
initiate_client_request(
        spdy_io_stream *        stream,
        const struct sockaddr * addr,
        TSCont                  contp)
{
    TSReleaseAssert(stream->vconn == nullptr);

    stream->vconn = TSHttpConnectWithProtoStack(addr, TSClientProtoStackCreate(TS_PROTO_TLS, TS_PROTO_SPDY, TS_PROTO_NULL));
    if (stream->vconn) {
        TSVConnRead(stream->vconn, contp, stream->input.buffer, std::numeric_limits<int64_t>::max());
        TSVConnWrite(stream->vconn, contp, stream->output.reader, std::numeric_limits<int64_t>::max());
    }

    return true;
}

static bool
write_http_request(spdy_io_stream * stream)
{
    spdy_io_buffer      iobuf;
    scoped_mbuffer      buffer;
    scoped_http_header  header(buffer.get(), stream->kvblock);
    int64_t             nwritten = 0;

    if (!header) {
        return false;
    }

    debug_http_header(stream, buffer.get(), header);

    // XXX Surely there's a better way to send the HTTP headers than forcing
    // ATS to reparse what we already have in pre-parsed form?
    TSHttpHdrPrint(buffer.get(), header, iobuf.buffer);

    TSIOBufferBlock blk = TSIOBufferReaderStart(iobuf.reader);
    while (blk) {
        const char *    ptr;
        int64_t         nbytes;

        ptr = TSIOBufferBlockReadStart(blk, iobuf.reader, &nbytes);
        if (ptr == nullptr || nbytes == 0) {
            goto next;
        }

        nwritten += TSIOBufferWrite(stream->output.buffer, ptr, nbytes);

next:
        blk = TSIOBufferBlockNext(blk);
    }

    // XXX is this needed?
    TSIOBufferProduce(stream->output.buffer, nwritten);
    return true;
}

static bool
read_http_headers(spdy_io_stream * stream)
{
    if (TSIsDebugTagSet("spdy.http")) {
        debug_http("[%p/%u] received %" PRId64 " header bytes",
                stream, stream->stream_id,
                TSIOBufferReaderAvail(stream->input.reader));
    }

    if (stream->hparser.parse(stream->input.reader) < 0) {
        // XXX handle header parsing error
        return false;
    }

    return true;
}

static int
spdy_stream_io(TSCont contp, TSEvent ev, void * edata)
{
    union {
        TSHostLookupResult dns;
        TSVIO vio;
    } context;

    spdy_io_stream * stream = spdy_io_stream::get(contp);

    debug_http("[%p/%u] received %s event",
            stream, stream->stream_id, cstringof(ev));

    if (!stream->is_open()) {
        debug_protocol("[%p/%u] received %s on closed stream",
                stream->io, stream->stream_id, cstringof(ev));
        release(stream->io);
        release(stream);
        return TS_EVENT_NONE;
    }

    std::lock_guard<spdy_io_stream::lock_type> lk(stream->lock);

    switch (ev) {
    case TS_EVENT_HOST_LOOKUP:
        context.dns = (TSHostLookupResult)edata;
        stream->action = nullptr;

        if (context.dns) {
            inet_address addr(TSHostLookupResultAddrGet(context.dns));
            debug_http("[%p/%u] resolved %s => %s",
                    stream->io, stream->stream_id,
                    stream->kvblock.url().hostport.c_str(), cstringof(addr));
            addr.port() = htons(80); // XXX should be parsed from hostport
            if (initiate_client_request(stream, addr.saddr(), contp)) {
                ENTER(stream, spdy_io_stream::http_send_headers);
                retain(stream);
                retain(stream->io);
            }

        } else {
            // Experimentally, if the DNS lookup fails, web proxies return 502
            // Bad Gateway.
            http_send_error(stream, TS_HTTP_STATUS_BAD_GATEWAY);
        }

        release(stream->io);
        release(stream);
        return TS_EVENT_NONE;

    case TS_EVENT_VCONN_WRITE_READY:
        context.vio = (TSVIO)edata;

        if (IN(stream, spdy_io_stream::http_send_headers)) {
            // The output VIO is ready. Write the HTTP request to the origin
            // server and kick the VIO to send it.
            if (write_http_request(stream)) {
                TSVIOReenable(context.vio);
                LEAVE(stream, spdy_io_stream::http_send_headers);
                ENTER(stream, spdy_io_stream::http_receive_headers);
            }
        }

        return TS_EVENT_NONE;

    case TS_EVENT_VCONN_WRITE_COMPLETE:
        debug_http("ignoring %s event", cstringof(ev));
        return TS_EVENT_NONE;

    case TS_EVENT_VCONN_READ_READY:
    case TS_EVENT_VCONN_READ_COMPLETE:
    case TS_EVENT_VCONN_EOS:
        context.vio = (TSVIO)edata;

        if (IN(stream, spdy_io_stream::http_receive_headers)) {
            if (read_http_headers(stream)) {
                LEAVE(stream, spdy_io_stream::http_receive_headers);
                ENTER(stream, spdy_io_stream::http_send_headers);
                ENTER(stream, spdy_io_stream::http_receive_content);
            }
        }

        // Parsing the headers might have completed and had more data left
        // over. If there's any data still buffered we can push it out now.
        if (IN(stream, spdy_io_stream::http_send_headers)) {
            http_send_response(stream, stream->hparser.mbuffer.get(),
                        stream->hparser.header.get());
            LEAVE(stream, spdy_io_stream::http_send_headers);
        }

        if (IN(stream, spdy_io_stream::http_receive_content)) {
            http_send_content(stream, stream->input.reader);
        }

        if (ev == TS_EVENT_VCONN_EOS || ev == TS_EVENT_VCONN_READ_COMPLETE) {
            stream->http_state = spdy_io_stream::http_closed;
            spdy_send_data_frame(stream, spdy::FLAG_FIN, nullptr, 0);
        }

        // Kick the IO control block write VIO to make it send the
        // SPDY frames we spooled.
        stream->io->reenable();

        if (IN(stream, spdy_io_stream::http_closed)) {
            stream->close();
        }

        return TS_EVENT_NONE;

    default:
        debug_plugin("unexpected stream event %s", cstringof(ev));
    }

    return TS_EVENT_NONE;
}

static bool
block_and_resolve_host(
        spdy_io_stream * stream,
        const std::string& hostport)
{
    int error;
    struct addrinfo * res0 = NULL;

    // XXX split the host and port and stash the port in the resulting sockaddr
    error = getaddrinfo(hostport.c_str(), "80", NULL, &res0);
    if (error != 0) {
        debug_http("failed to resolve hostname '%s', %s",
                hostport.c_str(), gai_strerror(error));
        http_send_error(stream, TS_HTTP_STATUS_BAD_GATEWAY);
        return false;
    }

    inet_address addr(res0->ai_addr);

    freeaddrinfo(res0);

    debug_http("[%p/%u] resolved %s => %s",
            stream, stream->stream_id,
            hostport.c_str(), cstringof(addr));
    addr.port() = htons(80); // XXX should be parsed from hostport

    if (initiate_client_request(stream, addr.saddr(), stream->continuation)) {
        ENTER(stream, spdy_io_stream::http_send_headers);
        return true;
    }

    return false;
}

static bool
initiate_host_resolution(
        spdy_io_stream * stream,
        const std::string& hostport)
{
    // XXX split the host and port and stash the port in the resulting sockaddr
    stream->action = TSHostLookup(stream->continuation, hostport.c_str(), hostport.size());
    if (TSActionDone(stream->action)) {
        stream->action = NULL;
    }

    debug_http("resolving hostname '%s'", hostport.c_str());
    return true;
}

spdy_io_stream::spdy_io_stream(unsigned s)
    : stream_id(s), http_state(0), action(nullptr), vconn(nullptr),
    continuation(nullptr), kvblock(), io(nullptr),
    input(), output(), hparser()
{
    this->continuation = TSContCreate(spdy_stream_io, TSMutexCreate());
    TSContDataSet(this->continuation, this);
}

spdy_io_stream::~spdy_io_stream()
{
    TSReleaseAssert(this->action == nullptr);
    TSReleaseAssert(this->vconn == nullptr);

    if (this->continuation) {
        TSContDestroy(this->continuation);
    }
}

void
spdy_io_stream::close()
{
    if (this->action) {
        TSActionCancel(this->action);
        this->action = nullptr;
    }

    if (this->vconn) {
        TSVConnClose(this->vconn);
        this->vconn = nullptr;
    }

    this->http_state = http_closed;
}

bool
spdy_io_stream::open(
        spdy::key_value_block& kv,
        open_options options)
{
    TSReleaseAssert(this->io != nullptr);

    if (this->is_closed()) {
        this->kvblock = kv;

        retain(this);
        retain(this->io);

        ENTER(this, spdy_io_stream::http_resolve_host);
        bool success = (options & open_with_system_resolver)
            ? block_and_resolve_host(this, kvblock.url().hostport)
            : initiate_host_resolution(this, kvblock.url().hostport);

        if (!success) {
            release(this);
            release(this->io);
        }

        // On the success path, the resulting continuation callback will
        // release the refcount we are holding.

        return success;
    }

    return false;
}

/* vim: set sw=4 ts=4 tw=79 et : */
