/** @file

  A brief file description

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

/* output_header.c: a plugin prints out the client request header
 *                 fields to stdout
 * A sample internal plugin to use the HdrPrint functions and the TSIOBuffers
 * that the functions utilize.
 *
 * The plugin simply prints all the incoming request headers
 *
 *   Note: tested on Solaris only.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"

#define PLUGIN_NAME "output_header"

static void
handle_dns(TSHttpTxn txnp, TSCont contp ATS_UNUSED)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  int total_avail;

  TSIOBufferBlock block;
  const char *block_start;
  int64_t block_avail;

  char *output_string;
  int64_t output_len;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve client request header");
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    goto done;
  }

  output_buffer = TSIOBufferCreate();
  reader        = TSIOBufferReaderAlloc(output_buffer);

  /* This will print  just MIMEFields and not
     the http request line */
  TSDebug(PLUGIN_NAME, "Printing the hdrs ... ");
  TSMimeHdrPrint(bufp, hdr_loc, output_buffer);

  if (TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc) == TS_ERROR) {
    TSDebug(PLUGIN_NAME, "non-fatal: error releasing MLoc");
    TSError("[%s] non-fatal: Couldn't release MLoc", PLUGIN_NAME);
  }

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = TSIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *)TSmalloc(total_avail + 1);
  output_len    = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = TSIOBufferReaderStart(reader);
  while (block) {
    block_start = TSIOBufferBlockReadStart(block, reader, &block_avail);

    /* We'll get a block pointer back even if there is no data
       left to read so check for this condition and break out of
       the loop. A block with no data to read means we've exhausted
       buffer of data since if there was more data on a later
       block in the chain, this block would have been skipped over */
    if (block_avail == 0) {
      break;
    }

    memcpy(output_string + output_len, block_start, block_avail);
    output_len += block_avail;

    /* Consume the data so that we get to the next block */
    TSIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = TSIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the TSIOBuffer that we used to print out the header */
  TSIOBufferReaderFree(reader);
  TSIOBufferDestroy(output_buffer);

  /* Although I'd never do this a production plugin, printf
     the header so that we can see it's all there */
  TSDebug(PLUGIN_NAME, "%s", output_string);

  TSfree(output_string);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
hdr_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_OS_DNS:
    handle_dns(txnp, contp);
    return 0;
  default:
    break;
  }

  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);

    goto error;
  }

  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, TSContCreate(hdr_plugin, NULL));

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);
}
