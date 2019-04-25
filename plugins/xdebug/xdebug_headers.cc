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

#include <cstdlib>
#include <stdio.h>
#include <cstdio>
#include <strings.h>
#include <string_view>
#include <sstream>
#include <cstring>
#include <getopt.h>

#define DEBUG_TAG_LOG_HEADERS "xdebug.headers"

std::string_view
escape_char_for_json(char const &c, bool &parsing_key)
{
  switch (c) {
  case '\'':
    return {"\\\'"};
  case '"':
    return {"\\\""};
  case '\\':
    return {"\\\\"};
  case '\b':
    return {"\\b"};
  case '\f':
    return {"\\f"};
  case '\t':
    return {"\\t"};

  // Special header reformatting
  case '\r':
    return {""};
  case '\n':
    parsing_key = true;
    return {"',\r\n\t'"}; // replace new line with pair delimiter
  case ':':
    if (parsing_key) {
      return {"' : "}; // replace colon after key with quote + colon
    }
    return {":"};
  case ' ':
    if (parsing_key) {
      parsing_key = false;
      return {"'"}; // replace first space after the key to be a quote
    }
    return {" "};
  default:
    return {&c, 1};
  }
}

///////////////////////////////////////////////////////////////////////////
// Dump a header on stderr, useful together with TSDebug().
void
print_headers(TSHttpTxn txn, TSMBuffer bufp, TSMLoc hdr_loc, std::stringstream &ss)
{
  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  TSIOBufferBlock block;
  const char *block_start;
  int64_t block_avail;
  bool parsing_key    = true;
  size_t print_rewind = ss.str().length();
  output_buffer       = TSIOBufferCreate();
  reader              = TSIOBufferReaderAlloc(output_buffer);

  ss << "\t'";
  /* This will print just MIMEFields and not the http request line */
  TSMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* We need to loop over all the buffer blocks, there can be more than 1 */
  block = TSIOBufferReaderStart(reader);
  do {
    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);
    for (const char *c = block_start; c < block_start + block_avail; ++c) {
      bool was_parsing_key = parsing_key;
      ss << escape_char_for_json(*c, parsing_key);
      if (parsing_key && !was_parsing_key) {
        print_rewind = ss.str().length() - 1;
      }
    }
    TSIOBufferReaderConsume(reader, block_avail);
    block = TSIOBufferReaderStart(reader);
  } while (block && block_avail != 0);

  ss.seekp(print_rewind);

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);

  TSDebug(DEBUG_TAG_LOG_HEADERS, "%s", ss.str().c_str());
}

void
log_headers(TSHttpTxn txn, TSMBuffer bufp, TSMLoc hdr_loc, char const *type_msg)
{
  if (TSIsDebugTagSet(DEBUG_TAG_LOG_HEADERS)) {
    std::stringstream output;
    print_headers(txn, bufp, hdr_loc, output);
    TSDebug(DEBUG_TAG_LOG_HEADERS, "\n=============\n %s headers are... \n %s", type_msg, output.str().c_str());
  }
}

void
print_request_headers(TSHttpTxn txn, std::stringstream &output)
{
  TSMBuffer buf_c, buf_s;
  TSMLoc hdr_loc;
  if (TSHttpTxnClientReqGet(txn, &buf_c, &hdr_loc) == TS_SUCCESS) {
    output << "{'type':'request', 'side':'client', 'headers': {\n";
    print_headers(txn, buf_c, hdr_loc, output);
    output << "}}";
    TSHandleMLocRelease(buf_c, TS_NULL_MLOC, hdr_loc);
  }
  if (TSHttpTxnServerReqGet(txn, &buf_s, &hdr_loc) == TS_SUCCESS) {
    output << ",{'type':'request', 'side':'server', 'headers': {\n";
    print_headers(txn, buf_s, hdr_loc, output);
    output << "}}";
    TSHandleMLocRelease(buf_s, TS_NULL_MLOC, hdr_loc);
  }
}

void
print_response_headers(TSHttpTxn txn, std::stringstream &output)
{
  TSMBuffer buf_c, buf_s;
  TSMLoc hdr_loc;
  if (TSHttpTxnServerRespGet(txn, &buf_s, &hdr_loc) == TS_SUCCESS) {
    output << "{'type':'response', 'side':'server', 'headers': {\n";
    print_headers(txn, buf_s, hdr_loc, output);
    output << "}},";
    TSHandleMLocRelease(buf_s, TS_NULL_MLOC, hdr_loc);
  }
  if (TSHttpTxnClientRespGet(txn, &buf_c, &hdr_loc) == TS_SUCCESS) {
    output << "{'type':'response', 'side':'client', 'headers': {\n";
    print_headers(txn, buf_c, hdr_loc, output);
    output << "}}";
    TSHandleMLocRelease(buf_c, TS_NULL_MLOC, hdr_loc);
  }
}
