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

#include "xdebug_types.h"
#include "xdebug_headers.h"

#include <array>
#include <unistd.h>
#include <sstream>
#include <cinttypes>
#include <string>

#include "ts/ts.h"

namespace xdebug
{

static const std::string_view MultipartBoundary{"\r\n--- ATS xDebug Probe Injection Boundary ---\r\n\r\n"};

static char Hostname[1024];

static DbgCtl dbg_ctl_xform{"xdebug_transform"};

void
init_transforms()
{
  gethostname(Hostname, 1024);
}

static std::string
getPreBody(TSHttpTxn txn)
{
  std::stringstream output;
  output << "{'xDebugProbeAt' : '" << Hostname << "',\n   'captured':[";
  print_request_headers(txn, output);
  output << "\n   ]\n}";
  output << MultipartBoundary;
  return output.str();
}

static std::string
getPreBodyFullJson(TSHttpTxn txn)
{
  std::stringstream output;
  print_request_headers_full_json(txn, output);
  output << R"(,"server-body": ")";
  return output.str();
}

static std::string
getPostBody(TSHttpTxn txn)
{
  std::stringstream output;
  output << MultipartBoundary;
  output << "{'xDebugProbeAt' : '" << Hostname << "',\n   'captured':[";
  print_response_headers(txn, output);
  output << "\n   ]\n}";
  return output.str();
}

static std::string
getPostBodyFullJson(TSHttpTxn txn)
{
  std::stringstream output;
  output << R"(",)"; // Close the origin-body field.
  print_response_headers_full_json(txn, output);
  output << '\n';
  return output.str();
}

static inline int64_t
write_hex(TSIOBuffer output_buffer, const char *src, int64_t len)
{
  int64_t written = 0;
  // Convert each byte to two hex characters
  static const char hex_chars[] = "0123456789abcdef";

  // Process in chunks to keep stack usage reasonable
  constexpr int64_t CHUNK = 1024; // 1KB of raw -> 2KB hex
  int64_t           idx   = 0;

  while (idx < len) {
    std::array<char, CHUNK * 2> hex_output;
    int64_t const               num_to_take  = std::min(len - idx, CHUNK);
    int64_t const               num_to_write = num_to_take * 2;
    TSReleaseAssert(static_cast<size_t>(num_to_write) <= hex_output.size());
    for (int64_t i = 0; i < num_to_take; ++i) {
      unsigned char src_byte = static_cast<unsigned char>(src[idx + i]);
      hex_output[i * 2]      = hex_chars[(src_byte >> 4) & 0x0F];
      hex_output[i * 2 + 1]  = hex_chars[src_byte & 0x0F];
    }
    TSIOBufferWrite(output_buffer, hex_output.data(), num_to_write);
    written += num_to_write;
    idx     += num_to_take;
  }
  return written;
}

/** JSON-escape the given input stream. */
static inline void
write_json_escaped(TSIOBuffer output_buffer, const char *data, int64_t len, int64_t &written)
{
  for (int64_t i = 0; i < len; ++i) {
    unsigned char c = static_cast<unsigned char>(data[i]);
    switch (c) {
    case '"': {
      const char *s = "\\\"";
      TSIOBufferWrite(output_buffer, s, 2);
      written += 2;
      break;
    }
    case '\\': {
      const char *s = "\\\\";
      TSIOBufferWrite(output_buffer, s, 2);
      written += 2;
      break;
    }
    case '\b': {
      const char *s = "\\b";
      TSIOBufferWrite(output_buffer, s, 2);
      written += 2;
      break;
    }
    case '\f': {
      const char *s = "\\f";
      TSIOBufferWrite(output_buffer, s, 2);
      written += 2;
      break;
    }
    case '\n': {
      const char *s = "\\n";
      TSIOBufferWrite(output_buffer, s, 2);
      written += 2;
      break;
    }
    case '\r': {
      const char *s = "\\r";
      TSIOBufferWrite(output_buffer, s, 2);
      written += 2;
      break;
    }
    case '\t': {
      const char *s = "\\t";
      TSIOBufferWrite(output_buffer, s, 2);
      written += 2;
      break;
    }
    default:
      if (c < 0x20) {
        written += write_hex(output_buffer, reinterpret_cast<const char *>(&c), 1);
        break;
      } else {
        TSIOBufferWrite(output_buffer, reinterpret_cast<const char *>(&c), 1);
        written += 1;
      }
    }
  }
}

void
writePostBody(TSHttpTxn txn, BodyBuilder *data)
{
  if (data->wrote_body && data->hdr_ready && !data->wrote_postbody.test_and_set()) {
    Dbg(dbg_ctl_xform, "body_transform(): Writing postbody headers...");
    // No cleanup needed for hex encoding - it processes all bytes immediately.
    std::string postbody;
    if (data->probe_type == ProbeType::PROBE_STANDARD) {
      postbody = getPostBody(txn);
    } else {
      postbody = getPostBodyFullJson(txn);
    }
    TSIOBufferWrite(data->output_buffer.get(), postbody.data(), postbody.length());
    data->nbytes += postbody.length();
    TSVIONBytesSet(data->output_vio, data->nbytes);
    TSVIOReenable(data->output_vio);
  }
}

int
body_transform(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */)
{
  TSHttpTxn    txn  = static_cast<TSHttpTxn>(TSContDataGet(contp));
  BodyBuilder *data = AuxDataMgr::data(txn).body_builder.get();
  if (!data) {
    return TS_ERROR;
  }
  if (TSVConnClosedGet(contp)) {
    // write connection destroyed.
    return 0;
  }

  TSVIO src_vio = TSVConnWriteVIOGet(contp);

  switch (event) {
  case TS_EVENT_ERROR: {
    // Notify input vio of this error event
    TSContCall(TSVIOContGet(src_vio), TS_EVENT_ERROR, src_vio);
    return 0;
  }
  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    return 0;
  }
  case TS_EVENT_VCONN_WRITE_READY:
    Dbg(dbg_ctl_xform, "body_transform(): Event is TS_EVENT_VCONN_WRITE_READY");
  // fall through
  default:
    if (!data->output_buffer.get()) {
      data->output_buffer.reset(TSIOBufferCreate());
      data->output_reader.reset(TSIOBufferReaderAlloc(data->output_buffer.get()));
      data->output_vio = TSVConnWrite(TSTransformOutputVConnGet(contp), contp, data->output_reader.get(), INT64_MAX);
    }

    if (data->wrote_prebody == false) {
      Dbg(dbg_ctl_xform, "body_transform(): Writing prebody headers...");
      std::string prebody;
      if (data->probe_type == ProbeType::PROBE_STANDARD) {
        prebody = getPreBody(txn);
      } else {
        prebody = getPreBodyFullJson(txn);
      }
      TSIOBufferWrite(data->output_buffer.get(), prebody.data(), prebody.length()); // write prebody
      data->wrote_prebody  = true;
      data->nbytes        += prebody.length();
      Dbg(dbg_ctl_xform, "Pre body content done, body will be %s",
          data->body_encoding == BodyEncoding_t::ESCAPE ? "escaped" : "hex-encoded");
    }

    TSIOBuffer src_buf = TSVIOBufferGet(src_vio);

    if (!src_buf) {
      // upstream continuation shuts down write operation.
      data->wrote_body = true;
      writePostBody(txn, data);
      return 0;
    }

    int64_t towrite = TSVIONTodoGet(src_vio);
    Dbg(dbg_ctl_xform, "body_transform(): %" PRId64 " bytes of body is expected", towrite);
    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(src_vio));
    towrite       = towrite > avail ? avail : towrite;
    if (towrite > 0) {
      if (data->probe_type == ProbeType::PROBE_STANDARD) {
        TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(src_vio), towrite, 0);
        TSIOBufferReaderConsume(TSVIOReaderGet(src_vio), towrite);
        TSVIONDoneSet(src_vio, TSVIONDoneGet(src_vio) + towrite);
        Dbg(dbg_ctl_xform, "body_transform(): writing %" PRId64 " bytes of body (standard)", towrite);
      } else {
        // Encode and write into our output buffer for full-json.
        int64_t          remaining  = towrite;
        TSIOBufferReader src_reader = TSVIOReaderGet(src_vio);
        while (remaining > 0) {
          TSIOBufferBlock src_block;
          int64_t         src_block_avail = 0;
          src_block                       = TSIOBufferReaderStart(src_reader);
          const char *src_block_start     = TSIOBufferBlockReadStart(src_block, src_reader, &src_block_avail);
          if (src_block_avail <= 0)
            break;
          int64_t take      = src_block_avail > remaining ? remaining : src_block_avail;
          int64_t wrote_now = 0;
          switch (data->body_encoding) {
          case BodyEncoding_t::OMIT_BODY:
            wrote_now = 0;
            break;
          case BodyEncoding_t::ESCAPE:
            write_json_escaped(data->output_buffer.get(), src_block_start, take, wrote_now);
            break;
          case BodyEncoding_t::HEX:
          case BodyEncoding_t::AUTO: // AUTO should have been resolved in header phase, fallback to HEX.
            wrote_now += write_hex(data->output_buffer.get(), src_block_start, take);
            break;
          }
          data->nbytes += wrote_now;
          TSIOBufferReaderConsume(src_reader, take);
          TSVIONDoneSet(src_vio, TSVIONDoneGet(src_vio) + take);
          remaining -= take;
        }
        Dbg(dbg_ctl_xform, "body_transform(): consumed %" PRId64 " bytes of origin body (encoded)", towrite);
      }
    }

    if (TSVIONTodoGet(src_vio) > 0) {
      TSVIOReenable(data->output_vio);
      TSContCall(TSVIOContGet(src_vio), TS_EVENT_VCONN_WRITE_READY, src_vio);
    } else {
      // End of src vio
      // Write post body content and update output VIO
      data->wrote_body = true;
      if (data->probe_type == ProbeType::PROBE_STANDARD) {
        data->nbytes += TSVIONDoneGet(src_vio);
      }
      writePostBody(txn, data);
      TSContCall(TSVIOContGet(src_vio), TS_EVENT_VCONN_WRITE_COMPLETE, src_vio);
    }
  }
  return 0;
}

} // namespace xdebug
