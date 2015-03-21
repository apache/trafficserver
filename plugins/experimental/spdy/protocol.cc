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

// protocol.cc - Low level routines to write SPDY frames.

#include <ts/ts.h>
#include <spdy/spdy.h>
#include <base/logging.h>
#include "io.h"
#include "protocol.h"

#include <algorithm>
#include <vector>

#include "ink_defs.h" // MAX

void
spdy_send_reset_stream(spdy_io_control *io, unsigned stream_id, spdy::error status)
{
  spdy::message_header hdr;
  spdy::rst_stream_message rst;

  uint8_t buffer[spdy::message_header::size + spdy::rst_stream_message::size];
  uint8_t *ptr = buffer;
  size_t nbytes = 0;

  hdr.is_control = true;
  hdr.control.version = spdy::PROTOCOL_VERSION;
  hdr.control.type = spdy::CONTROL_RST_STREAM;
  hdr.flags = 0;
  hdr.datalen = spdy::rst_stream_message::size;
  rst.stream_id = stream_id;
  rst.status_code = status;

  nbytes += spdy::message_header::marshall(hdr, ptr, sizeof(buffer));
  nbytes += spdy::rst_stream_message::marshall(rst, ptr, sizeof(buffer) - nbytes);

  debug_protocol("[%p/%u] sending %s stream %u with error %s", io, stream_id, cstringof(hdr.control.type), stream_id,
                 cstringof(status));
  TSIOBufferWrite(io->output.buffer, buffer, nbytes);
}

void
spdy_send_syn_reply(spdy_io_stream *stream, const spdy::key_value_block &kvblock)
{
  union {
    spdy::message_header hdr;
    spdy::syn_reply_message syn;
  } msg;

  uint8_t buffer[MAX((unsigned)spdy::message_header::size, (unsigned)spdy::syn_stream_message::size)];
  size_t nbytes = 0;

  std::vector<uint8_t> hdrs;

  // Compress the kvblock into a temp buffer before we start. We need to know
  // the size of this so we can fill in the datalen field. Since there's no
  // way to go back and rewrite the data length into the TSIOBuffer, we need
  // to use a temporary copy.
  hdrs.resize(kvblock.nbytes(stream->version));
  nbytes = spdy::key_value_block::marshall(stream->version, stream->io->compressor, kvblock, &hdrs[0], hdrs.capacity());
  hdrs.resize(nbytes);

  msg.hdr.is_control = true;
  msg.hdr.control.version = stream->version;
  msg.hdr.control.type = spdy::CONTROL_SYN_REPLY;
  msg.hdr.flags = 0;
  msg.hdr.datalen = spdy::syn_reply_message::size(stream->version) + hdrs.size();
  nbytes = TSIOBufferWrite(stream->io->output.buffer, buffer, spdy::message_header::marshall(msg.hdr, buffer, sizeof(buffer)));

  msg.syn.stream_id = stream->stream_id;
  nbytes += TSIOBufferWrite(stream->io->output.buffer, buffer,
                            spdy::syn_reply_message::marshall(stream->version, msg.syn, buffer, sizeof(buffer)));

  nbytes += TSIOBufferWrite(stream->io->output.buffer, &hdrs[0], hdrs.size());
  debug_protocol("[%p/%u] sending %s hdr.datalen=%u", stream->io, stream->stream_id, cstringof(spdy::CONTROL_SYN_REPLY),
                 (unsigned)msg.hdr.datalen);
}

void
spdy_send_data_frame(spdy_io_stream *stream, unsigned flags, const void *ptr, size_t nbytes)
{
  spdy::message_header hdr;
  uint8_t buffer[spdy::message_header::size];
  std::vector<uint8_t> tmp;
  ssize_t ret;

  TSReleaseAssert(nbytes < spdy::MAX_FRAME_LENGTH);

  // XXX If we are compressing the data, we need to make a temporary copy.
  // When there is an ATS API that will let us rewrite the header, then we
  // can marshall straight into the TSIOBiffer.
  if (flags & spdy::FLAG_COMPRESSED) {
    tmp.resize(nbytes + 64);
    stream->io->compressor.input(ptr, nbytes);
    nbytes = 0;

    do {
      ret = stream->io->compressor.consume(&tmp[nbytes], tmp.size() - nbytes);
      if (ret > 0) {
        nbytes += ret;
      }
    } while (ret > 0);

    tmp.resize(nbytes);
  }

  hdr.is_control = false;
  hdr.flags = flags;
  hdr.datalen = nbytes;
  hdr.data.stream_id = stream->stream_id;

  spdy::message_header::marshall(hdr, buffer, sizeof(buffer));
  TSIOBufferWrite(stream->io->output.buffer, buffer, spdy::message_header::size);

  if (nbytes) {
    if (flags & spdy::FLAG_COMPRESSED) {
      TSIOBufferWrite(stream->io->output.buffer, &tmp[0], nbytes);
    } else {
      TSIOBufferWrite(stream->io->output.buffer, ptr, nbytes);
    }
  }

  debug_protocol("[%p/%u] sending DATA flags=%x hdr.datalen=%u", stream->io, stream->stream_id, flags, (unsigned)hdr.datalen);
}

void
spdy_send_ping(spdy_io_control *io, spdy::protocol_version version, unsigned ping_id)
{
  union {
    spdy::message_header hdr;
    spdy::ping_message ping;
  } msg;

  size_t nbytes = 0;
  uint8_t buffer[spdy::ping_message::size + spdy::message_header::size];

  msg.hdr.is_control = true;
  msg.hdr.control.version = version;
  msg.hdr.control.type = spdy::CONTROL_PING;
  msg.hdr.flags = 0;
  msg.hdr.datalen = spdy::ping_message::size;
  nbytes += spdy::message_header::marshall(msg.hdr, buffer + nbytes, sizeof(buffer) - nbytes);

  msg.ping.ping_id = ping_id;
  nbytes += spdy::ping_message::marshall(msg.ping, buffer + nbytes, sizeof(buffer) - nbytes);

  TSIOBufferWrite(io->output.buffer, buffer, nbytes);

  debug_protocol("[%p] sending PING id=%u", io, msg.ping.ping_id);
}

/* vim: set sw=4 tw=79 ts=4 et ai : */
