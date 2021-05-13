/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

    @section license License

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

#include "configs.h"
#include "headers.h"

///////////////////////////////////////////////////////////////////////////
// Remove a header (fully) from an TSMLoc / TSMBuffer. Return the number
// of fields (header values) we removed.
int
remove_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header, int len)
{
  TSMLoc field = TSMimeHdrFieldFind(bufp, hdr_loc, header, len);
  int cnt      = 0;

  while (field) {
    TSMLoc tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field);

    ++cnt;
    TSMimeHdrFieldDestroy(bufp, hdr_loc, field);
    TSHandleMLocRelease(bufp, hdr_loc, field);
    field = tmp;
  }

  return cnt;
}

///////////////////////////////////////////////////////////////////////////
// Set a header to a specific value. This will avoid going to through a
// remove / add sequence in case of an existing header.
// but clean.
bool
set_header(TSMBuffer bufp, TSMLoc hdr_loc, const char *header, int len, const char *val, int val_len)
{
  if (!bufp || !hdr_loc || !header || len <= 0 || !val || val_len <= 0) {
    return false;
  }

  bool ret         = false;
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header, len);

  if (!field_loc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdr_loc, header, len, &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val, val_len)) {
        TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
        ret = true;
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }
  } else {
    TSMLoc tmp = nullptr;
    bool first = true;

    while (field_loc) {
      tmp = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, val, val_len)) {
          ret = true;
        }
      } else {
        TSMimeHdrFieldDestroy(bufp, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = tmp;
    }
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////
// Dump a header on stderr, useful together with TSDebug().
void
dump_headers(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  TSIOBufferBlock block;
  int64_t block_avail;

  output_buffer = TSIOBufferCreate();
  reader        = TSIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not the http request line */
  TSMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* We need to loop over all the buffer blocks, there can be more than 1 */
  block = TSIOBufferReaderStart(reader);
  do {
    const char *block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);
    if (block_avail > 0) {
      TSDebug(PLUGIN_NAME, "Headers are:\n%.*s", static_cast<int>(block_avail), block_start);
    }
    TSIOBufferReaderConsume(reader, block_avail);
    block = TSIOBufferReaderStart(reader);
  } while (block && block_avail != 0);

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);
}
