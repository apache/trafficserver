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

#include "io.h"
#include "http.h"
#include "protocol.h"

#include <getopt.h>
#include <limits>

static bool use_system_resolver = false;

static int spdy_vconn_io(TSCont, TSEvent, void *);

static void
recv_rst_stream(const spdy::message_header &header, spdy_io_control *io, const uint8_t *ptr)
{
  spdy::rst_stream_message rst;

  rst = spdy::rst_stream_message::parse(ptr, header.datalen);

  debug_protocol("[%p/%u] received %s frame stream=%u status_code=%s (%u)", io, rst.stream_id, cstringof(header.control.type),
                 rst.stream_id, cstringof((spdy::error)rst.status_code), rst.status_code);

  io->destroy_stream(rst.stream_id);
}

static void
recv_syn_stream(const spdy::message_header &header, spdy_io_control *io, const uint8_t *ptr)
{
  spdy::syn_stream_message syn;
  spdy_io_stream *stream;

  syn = spdy::syn_stream_message::parse(ptr, header.datalen);

  debug_protocol("[%p/%u] received %s frame stream=%u associated=%u priority=%u", io, syn.stream_id, cstringof(header.control.type),
                 syn.stream_id, syn.associated_id, syn.priority);

  if (!io->valid_client_stream_id(syn.stream_id)) {
    debug_protocol("[%p/%u] invalid stream-id %u", io, syn.stream_id, syn.stream_id);
    spdy_send_reset_stream(io, syn.stream_id, spdy::PROTOCOL_ERROR);
    return;
  }

  switch (header.control.version) {
  case spdy::PROTOCOL_VERSION_2: // fallthru
  case spdy::PROTOCOL_VERSION_3:
    break;
  default:
    debug_protocol("[%p/%u] bad protocol version %d", io, syn.stream_id, header.control.version);
    spdy_send_reset_stream(io, syn.stream_id, spdy::PROTOCOL_ERROR);
    return;
  }

  spdy::key_value_block kvblock(spdy::key_value_block::parse((spdy::protocol_version)header.control.version, io->decompressor,
                                                             ptr + spdy::syn_stream_message::size,
                                                             header.datalen - spdy::syn_stream_message::size));

  if ((stream = io->create_stream(syn.stream_id)) == 0) {
    debug_protocol("[%p/%u] failed to create stream %u", io, syn.stream_id, syn.stream_id);
    spdy_send_reset_stream(io, syn.stream_id, spdy::INVALID_STREAM);
    return;
  }

  stream->io = io;
  stream->version = (spdy::protocol_version)header.control.version;

  if (!kvblock.url().is_complete()) {
    debug_protocol("[%p/%u] incomplete URL", io, stream->stream_id);
    // 3.2.1; missing URL, protocol error; 400 Bad Request
    http_send_error(stream, TS_HTTP_STATUS_BAD_REQUEST);
    spdy_send_reset_stream(io, stream->stream_id, spdy::CANCEL);
    io->destroy_stream(stream->stream_id);
    return;
  }

  spdy_io_stream::open_options options = spdy_io_stream::open_none;
  if (use_system_resolver) {
    options = spdy_io_stream::open_with_system_resolver;
  }

  std::lock_guard<spdy_io_stream::lock_type> lk(stream->lock);
  if (!stream->open(kvblock, options)) {
    io->destroy_stream(stream->stream_id);
  }
}

static void
recv_ping(const spdy::message_header &header, spdy_io_control *io, const uint8_t *ptr)
{
  spdy::ping_message ping;

  ping = spdy::ping_message::parse(ptr, header.datalen);

  debug_protocol("[%p] received PING id=%u", io, ping.ping_id);

  // Client must send even ping-ids. Ignore the odd ones since
  // we never send them.
  if ((ping.ping_id % 2) == 0) {
    return;
  }

  spdy_send_ping(io, (spdy::protocol_version)header.control.version, ping.ping_id);
}

static void
dispatch_spdy_control_frame(const spdy::message_header &header, spdy_io_control *io, const uint8_t *ptr)
{
  switch (header.control.type) {
  case spdy::CONTROL_SYN_STREAM:
    recv_syn_stream(header, io, ptr);
    break;
  case spdy::CONTROL_SYN_REPLY:
  case spdy::CONTROL_RST_STREAM:
    recv_rst_stream(header, io, ptr);
    break;
  case spdy::CONTROL_PING:
    recv_ping(header, io, ptr);
    break;
  case spdy::CONTROL_SETTINGS:
  case spdy::CONTROL_GOAWAY:
  case spdy::CONTROL_HEADERS:
  case spdy::CONTROL_WINDOW_UPDATE:
    debug_protocol("[%p] SPDY control frame, version=%u type=%s flags=0x%x, %u bytes", io, header.control.version,
                   cstringof(header.control.type), header.flags, header.datalen);
    break;
  default:
    // SPDY 2.2.1 - MUST ignore unrecognized control frames
    TSError("[spdy] ignoring invalid control frame type %u", header.control.type);
  }

  io->reenable();
}

static size_t
count_bytes_available(TSIOBufferReader reader)
{
  TSIOBufferBlock block;
  size_t count = 0;

  block = TSIOBufferReaderStart(reader);
  while (block) {
    const char *ptr;
    int64_t nbytes;

    ptr = TSIOBufferBlockReadStart(block, reader, &nbytes);
    if (ptr && nbytes) {
      count += nbytes;
    }

    block = TSIOBufferBlockNext(block);
  }

  return count;
}

static void
consume_spdy_frame(spdy_io_control *io)
{
  spdy::message_header header;
  TSIOBufferBlock blk;
  const uint8_t *ptr;
  int64_t nbytes;

next_frame:

  blk = TSIOBufferReaderStart(io->input.reader);
  ptr = (const uint8_t *)TSIOBufferBlockReadStart(blk, io->input.reader, &nbytes);
  if (!ptr) {
    // This should not fail because we only try to consume the header when
    // there are enough bytes to read the header. Experimentally, however,
    // it does fail. I wonder why.
    TSError("TSIOBufferBlockReadStart failed unexpectedly");
    return;
  }

  if (nbytes < spdy::message_header::size) {
    // We should never get here, because we check for space before
    // entering. Unfortunately this does happen :(
    debug_plugin("short read %" PRId64 " bytes, expected at least %u, real count %zu", nbytes, spdy::message_header::size,
                 count_bytes_available(io->input.reader));
    return;
  }

  header = spdy::message_header::parse(ptr, (size_t)nbytes);
  TSAssert(header.datalen > 0); // XXX

  if (header.is_control) {
    if (header.control.version != spdy::PROTOCOL_VERSION) {
      TSError("[spdy] client is version %u, but we implement version %u", header.control.version, spdy::PROTOCOL_VERSION);
    }
  } else {
    debug_protocol("[%p] SPDY data frame, stream=%u flags=0x%x, %u bytes", io, header.data.stream_id, header.flags, header.datalen);
  }

  if (header.datalen >= spdy::MAX_FRAME_LENGTH) {
    // XXX puke
  }

  if (header.datalen <= (nbytes - spdy::message_header::size)) {
    // We have all the data in-hand ... parse it.
    io->input.consume(spdy::message_header::size);
    io->input.consume(header.datalen);

    ptr += spdy::message_header::size;

    if (header.is_control) {
      dispatch_spdy_control_frame(header, io, ptr);
    } else {
      TSError("[spdy] no data frame support yet");
    }

    if (TSIOBufferReaderAvail(io->input.reader) >= spdy::message_header::size) {
      goto next_frame;
    }
  }

  // Push the high water mark to the end of the frame so that we don't get
  // called back until we have the whole thing.
  io->input.watermark(spdy::message_header::size + header.datalen);
}

static int
spdy_vconn_io(TSCont contp, TSEvent ev, void *edata)
{
  TSVIO vio = (TSVIO)edata;
  int nbytes;
  spdy_io_control *io;

  (void)vio;

  // Experimentally, we recieve the read or write TSVIO pointer as the
  // callback data.
  // debug_plugin("received IO event %s, VIO=%p", cstringof(ev), vio);

  switch (ev) {
  case TS_EVENT_VCONN_READ_READY:
  case TS_EVENT_VCONN_READ_COMPLETE:
    io = spdy_io_control::get(contp);
    nbytes = TSIOBufferReaderAvail(io->input.reader);
    debug_plugin("received %d bytes", nbytes);
    if ((unsigned)nbytes >= spdy::message_header::size) {
      consume_spdy_frame(io);
    }

    // XXX frame parsing can throw. If it does, best to catch it, log it
    // and drop the connection.
    break;
  case TS_EVENT_VCONN_WRITE_READY:
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    // No need to handle write events. We have already pushed all the data
    // we have into the write buffer.
    break;
  case TS_EVENT_VCONN_EOS: // fallthru
  default:
    if (ev != TS_EVENT_VCONN_EOS) {
      debug_plugin("unexpected accept event %s", cstringof(ev));
    }
    io = spdy_io_control::get(contp);
    TSVConnClose(io->vconn);
    release(io);
  }

  return TS_EVENT_NONE;
}

static int
spdy_accept_io(TSCont contp, TSEvent ev, void *edata)
{
  TSVConn vconn = (TSVConn)edata;
  ;
  spdy_io_control *io = nullptr;

  switch (ev) {
  case TS_EVENT_NET_ACCEPT:
    io = retain(new spdy_io_control(vconn));
    io->input.watermark(spdy::message_header::size);
    io->output.watermark(spdy::message_header::size);
    // XXX is contp leaked here?
    contp = TSContCreate(spdy_vconn_io, TSMutexCreate());
    TSContDataSet(contp, io);
    TSVConnRead(vconn, contp, io->input.buffer, std::numeric_limits<int64_t>::max());
    TSVConnWrite(vconn, contp, io->output.reader, std::numeric_limits<int64_t>::max());
    debug_protocol("accepted new SPDY session %p", io);
    break;
  default:
    debug_plugin("unexpected accept event %s", cstringof(ev));
  }

  return TS_EVENT_NONE;
}

static int
spdy_setup_protocol(TSCont /* contp ATS_UNUSED */, TSEvent ev, void * /* edata ATS_UNUSED */)
{
  switch (ev) {
  case TS_EVENT_LIFECYCLE_PORTS_INITIALIZED:
    TSReleaseAssert(TSNetAcceptNamedProtocol(TSContCreate(spdy_accept_io, TSMutexCreate()), TS_NPN_PROTOCOL_SPDY_2) == TS_SUCCESS);
    debug_plugin("registered named protocol endpoint for %s", TS_NPN_PROTOCOL_SPDY_2);
    break;
  default:
    TSError("[spdy] Protocol registration failed");
    break;
  }

  return TS_EVENT_NONE;
}

extern "C" void
TSPluginInit(int argc, const char *argv[])
{
  static const struct option longopts[] = {{const_cast<char *>("system-resolver"), no_argument, NULL, 's'}, {NULL, 0, NULL, 0}};

  TSPluginRegistrationInfo info;

  info.plugin_name = (char *)"spdy";
  info.vendor_name = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("[spdy] Plugin registration failed");
  }

  debug_plugin("initializing");

  for (;;) {
    switch (getopt_long(argc, (char *const *)argv, "s", longopts, NULL)) {
    case 's':
      use_system_resolver = true;
      break;
    case -1:
      goto init;
    default:
      TSError("[spdy] usage: spdy.so [--system-resolver]");
    }
  }

init:
  TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, TSContCreate(spdy_setup_protocol, NULL));
}

/* vim: set sw=4 tw=79 ts=4 et ai : */
