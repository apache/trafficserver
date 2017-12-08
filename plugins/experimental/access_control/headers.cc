/*
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

/**
 * @file headers.cc
 * @brief HTTP headers manipulation.
 */

#include <stdlib.h>
#include <string.h>

#include "headers.h"
#include "common.h"

/**
 * @brief Remove a header (fully) from an TSMLoc / TSMBuffer.
 *
 * @param bufp request's buffer
 * @param hdrLoc request's header location
 * @param header header name
 * @param headerlen header name length
 * @return the number of fields (header values) we removed.
 */
int
removeHeader(TSMBuffer bufp, TSMLoc hdrLoc, const char *header, int headerlen)
{
  TSMLoc fieldLoc = TSMimeHdrFieldFind(bufp, hdrLoc, header, headerlen);
  int cnt         = 0;

  while (fieldLoc) {
    TSMLoc tmp = TSMimeHdrFieldNextDup(bufp, hdrLoc, fieldLoc);

    ++cnt;
    TSMimeHdrFieldDestroy(bufp, hdrLoc, fieldLoc);
    TSHandleMLocRelease(bufp, hdrLoc, fieldLoc);
    fieldLoc = tmp;
  }

  return cnt;
}

/**
 * @brief Checks if the header exists.
 *
 * @param bufp request's buffer
 * @param hdrLoc request's header location
 * @return true - exists, false - does not exist
 */
bool
headerExist(TSMBuffer bufp, TSMLoc hdrLoc, const char *header, int headerlen)
{
  TSMLoc fieldLoc = TSMimeHdrFieldFind(bufp, hdrLoc, header, headerlen);
  if (TS_NULL_MLOC != fieldLoc) {
    TSHandleMLocRelease(bufp, hdrLoc, fieldLoc);
    return true;
  }
  return false;
}

/**
 * @brief Get the header value
 *
 * @param bufp request's buffer
 * @param hdrLoc request's header location
 * @param header header name
 * @param headerlen header name length
 * @param value buffer for the value
 * @param valuelen lenght of the buffer for the value
 * @return pointer to the string with the value.
 */
char *
getHeader(TSMBuffer bufp, TSMLoc hdrLoc, const char *header, int headerlen, char *value, int *valuelen)
{
  TSMLoc fieldLoc = TSMimeHdrFieldFind(bufp, hdrLoc, header, headerlen);
  char *dst       = value;
  while (fieldLoc) {
    TSMLoc next = TSMimeHdrFieldNextDup(bufp, hdrLoc, fieldLoc);

    int count = TSMimeHdrFieldValuesCount(bufp, hdrLoc, fieldLoc);
    for (int i = 0; i < count; ++i) {
      const char *v = nullptr;
      int vlen      = 0;
      v             = TSMimeHdrFieldValueStringGet(bufp, hdrLoc, fieldLoc, i, &vlen);
      if (v == nullptr || vlen == 0) {
        continue;
      }
      /* append the field content to the output buffer if enough space, plus space for ", " */
      bool first      = (dst == value);
      int neededSpace = ((dst - value) + vlen + (dst == value ? 0 : 2));
      if (neededSpace < *valuelen) {
        if (!first) {
          memcpy(dst, ", ", 2);
          dst += 2;
        }
        memcpy(dst, v, vlen);
        dst += vlen;
      }
    }
    TSHandleMLocRelease(bufp, hdrLoc, fieldLoc);
    fieldLoc = next;
  }

  *valuelen = dst - value;
  return value;
}

/**
 * @brief Set a header to a specific value.
 *
 * This will avoid going to through a remove / add sequence in case of an existing header but clean.
 *
 * @param bufp request's buffer
 * @param hdrLoc request's header location
 * @param header header name
 * @param headerlen header name len
 * @param value the new value
 * @param valuelen lenght of the value
 * @return true - OK, false - failed
 */
bool
setHeader(TSMBuffer bufp, TSMLoc hdrLoc, const char *header, int headerlen, const char *value, int valuelen)
{
  if (!bufp || !hdrLoc || !header || headerlen <= 0 || !value || valuelen <= 0) {
    return false;
  }

  bool ret        = false;
  TSMLoc fieldLoc = TSMimeHdrFieldFind(bufp, hdrLoc, header, headerlen);

  if (!fieldLoc) {
    // No existing header, so create one
    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(bufp, hdrLoc, header, headerlen, &fieldLoc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdrLoc, fieldLoc, -1, value, valuelen)) {
        TSMimeHdrFieldAppend(bufp, hdrLoc, fieldLoc);
        ret = true;
      }
      TSHandleMLocRelease(bufp, hdrLoc, fieldLoc);
    }
  } else {
    TSMLoc tmp = nullptr;
    bool first = true;

    while (fieldLoc) {
      if (first) {
        first = false;
        if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdrLoc, fieldLoc, -1, value, valuelen)) {
          ret = true;
        }
      } else {
        TSMimeHdrFieldDestroy(bufp, hdrLoc, fieldLoc);
      }
      tmp = TSMimeHdrFieldNextDup(bufp, hdrLoc, fieldLoc);
      TSHandleMLocRelease(bufp, hdrLoc, fieldLoc);
      fieldLoc = tmp;
    }
  }

  return ret;
}

/**
 * @brief Dump a header on stderr
 *
 * Useful together with TSDebug().
 *
 * @param bufp request's buffer
 * @param hdrLoc request's header location
 */
void
dumpHeaders(TSMBuffer bufp, TSMLoc hdrLoc)
{
  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  TSIOBufferBlock block;
  const char *block_start;
  int64_t block_avail;

  output_buffer = TSIOBufferCreate();
  reader        = TSIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not the http request line */
  TSMimeHdrPrint(bufp, hdrLoc, output_buffer);

  /* We need to loop over all the buffer blocks, there can be more than 1 */
  block = TSIOBufferReaderStart(reader);
  do {
    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);
    if (block_avail > 0) {
      AccessControlDebug("Headers are:\n%.*s", static_cast<int>(block_avail), block_start);
    }
    TSIOBufferReaderConsume(reader, block_avail);
    block = TSIOBufferReaderStart(reader);
  } while (block && block_avail != 0);

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);
}
