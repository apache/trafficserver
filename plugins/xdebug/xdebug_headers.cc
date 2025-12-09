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

#include "xdebug_headers.h"

#include <cstdlib>
#include <cstdio>
#include <cstdio>
#include <strings.h>
#include <string_view>
#include <sstream>
#include <cstring>
#include <getopt.h>

#define DEBUG_TAG_LOG_HEADERS "xdebug.headers"

namespace xdebug
{

namespace
{
  DbgCtl dbg_ctl_hdrs{DEBUG_TAG_LOG_HEADERS};
}

///////////////////////////////////////////////////////////////////////////
// Dump a header on stderr, useful together with Dbg().
void
print_headers(TSMBuffer bufp, TSMLoc hdr_loc, std::stringstream &ss, bool full_json)
{
  TSIOBuffer        output_buffer;
  TSIOBufferReader  reader;
  TSIOBufferBlock   block;
  const char       *block_start;
  int64_t           block_avail;
  EscapeCharForJson escape_char_for_json{full_json};
  output_buffer = TSIOBufferCreate();
  reader        = TSIOBufferReaderAlloc(output_buffer);

  if (full_json) {
    ss << R"("start-line":")";
  } else {
    ss << "\t'Start-Line' : '";
  }

  // Print all message header lines.
  TSHttpHdrPrint(bufp, hdr_loc, output_buffer);

  /* We need to loop over all the buffer blocks, there can be more than 1 */
  block = TSIOBufferReaderStart(reader);
  do {
    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);
    for (const char *c = block_start; c < block_start + block_avail; ++c) {
      ss << escape_char_for_json(*c);
    }
    TSIOBufferReaderConsume(reader, block_avail);
    block = TSIOBufferReaderStart(reader);
  } while (block && block_avail != 0);

  ss.seekp(-escape_char_for_json.backup(full_json), std::ios_base::end);

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);

  Dbg(dbg_ctl_hdrs, "%.*s", static_cast<int>(ss.tellp()), ss.str().data());
}

void
log_headers(TSHttpTxn /* txn ATS_UNUSED */, TSMBuffer bufp, TSMLoc hdr_loc, char const *type_msg)
{
  if (dbg_ctl_hdrs.on()) {
    std::stringstream output;
    print_headers(bufp, hdr_loc, output, FULL_JSON);
    Dbg(dbg_ctl_hdrs, "\n=============\n %s headers are... \n %s", type_msg, output.str().c_str());
  }
}

void
print_request_headers(TSHttpTxn txn, std::stringstream &output)
{
  TSMBuffer buf_c, buf_s;
  TSMLoc    hdr_loc;
  if (TSHttpTxnClientReqGet(txn, &buf_c, &hdr_loc) == TS_SUCCESS) {
    output << "{'type':'request', 'side':'client', 'headers': {\n";
    print_headers(buf_c, hdr_loc, output, !FULL_JSON);
    output << "\n\t}}";
    TSHandleMLocRelease(buf_c, TS_NULL_MLOC, hdr_loc);
  }
  if (TSHttpTxnServerReqGet(txn, &buf_s, &hdr_loc) == TS_SUCCESS) {
    output << ",{'type':'request', 'side':'server', 'headers': {\n";
    print_headers(buf_s, hdr_loc, output, !FULL_JSON);
    output << "\n\t}}";
    TSHandleMLocRelease(buf_s, TS_NULL_MLOC, hdr_loc);
  }
}

void
print_response_headers(TSHttpTxn txn, std::stringstream &output)
{
  TSMBuffer buf_c, buf_s;
  TSMLoc    hdr_loc;
  if (TSHttpTxnServerRespGet(txn, &buf_s, &hdr_loc) == TS_SUCCESS) {
    output << "{'type':'response', 'side':'server', 'headers': {\n";
    print_headers(buf_s, hdr_loc, output, !FULL_JSON);
    output << "\n\t}},";
    TSHandleMLocRelease(buf_s, TS_NULL_MLOC, hdr_loc);
  }
  if (TSHttpTxnClientRespGet(txn, &buf_c, &hdr_loc) == TS_SUCCESS) {
    output << "{'type':'response', 'side':'client', 'headers': {\n";
    print_headers(buf_c, hdr_loc, output, !FULL_JSON);
    output << "\n\t}}";
    TSHandleMLocRelease(buf_c, TS_NULL_MLOC, hdr_loc);
  }
}

void
print_request_headers_full_json(TSHttpTxn txn, std::stringstream &output)
{
  TSMBuffer buf_c, buf_s;
  TSMLoc    hdr_loc;

  bool has_client = false;

  Dbg(dbg_ctl_hdrs, "Printing client request headers for full JSON");
  if (TSHttpTxnClientReqGet(txn, &buf_c, &hdr_loc) == TS_SUCCESS) {
    output << "{\"client-request\":{";
    print_headers(buf_c, hdr_loc, output, FULL_JSON);
    output << "}";
    has_client = true;
    TSHandleMLocRelease(buf_c, TS_NULL_MLOC, hdr_loc);
  }

  if (TSHttpTxnServerReqGet(txn, &buf_s, &hdr_loc) == TS_SUCCESS) {
    if (has_client) {
      output << ",";
    }
    output << "\"proxy-request\":{";
    print_headers(buf_s, hdr_loc, output, FULL_JSON);
    output << "}";
    TSHandleMLocRelease(buf_s, TS_NULL_MLOC, hdr_loc);
  }
}

void
print_response_headers_full_json(TSHttpTxn txn, std::stringstream &output)
{
  TSMBuffer buf_c, buf_s;
  TSMLoc    hdr_loc;

  bool has_server = false;

  if (TSHttpTxnServerRespGet(txn, &buf_s, &hdr_loc) == TS_SUCCESS) {
    output << "\"server-response\":{";
    print_headers(buf_s, hdr_loc, output, FULL_JSON);
    output << "}";
    has_server = true;
    TSHandleMLocRelease(buf_s, TS_NULL_MLOC, hdr_loc);
  }

  if (TSHttpTxnClientRespGet(txn, &buf_c, &hdr_loc) == TS_SUCCESS) {
    if (has_server) {
      output << ",";
    }
    output << "\"proxy-response\":{";
    print_headers(buf_c, hdr_loc, output, FULL_JSON);
    output << "}}";
    TSHandleMLocRelease(buf_c, TS_NULL_MLOC, hdr_loc);
  }
}
} // namespace xdebug
