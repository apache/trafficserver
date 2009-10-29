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

/* output_hdr.c: a plugin prints out the client request header 
 *                 fields to stdout
 * A sample internal plugin to use the HdrPrint functions and the INKIOBuffers
 * that the functions untilize.
 * 
 * The plugin simply prints all the incoming request headers
 * 
 *
 *
 *   Note: tested on Solaris only.  Probably doesn't compile
 *    on NT.  
 */

#include <stdio.h>
#include <string.h>

#if !defined (_WIN32)
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "InkAPI.h"

#define DEBUG_TAG "output-header"

static void
handle_dns(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;

  INKIOBuffer output_buffer;
  INKIOBufferReader reader;
  int total_avail;

  INKIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string;
  int output_len;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKDebug(DEBUG_TAG, "couldn't retrieve client request header");
    INKError("couldn't retrieve client request header\n");
    goto done;
  }

  output_buffer = INKIOBufferCreate();

  /* INKIOBufferCreate may return an error pointer */
  if ((void *) output_buffer == INK_ERROR_PTR) {
    INKDebug(DEBUG_TAG, "couldn't allocate IOBuffer");
    INKError("couldn't allocate IOBuffer\n");
    goto done;
  }

  reader = INKIOBufferReaderAlloc(output_buffer);

  /* INKIOBufferReaderAlloc may return an error pointer */
  if ((void *) reader == INK_ERROR_PTR) {
    INKDebug(DEBUG_TAG, "couldn't allocate IOBufferReader");
    INKError("couldn't allocate IOBufferReader\n");
    goto done;
  }

  /* This will print  just MIMEFields and not
     the http request line */
  INKDebug(DEBUG_TAG, "Printing the hdrs ... ");
  if (INKMimeHdrPrint(bufp, hdr_loc, output_buffer) == INK_ERROR) {
    INKDebug(DEBUG_TAG, "non-fatal: error printing mime-hdrs");
    INKError("non-fatal: error printing mime-hdrs\n");
  }

  if (INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc) == INK_ERROR) {
    INKDebug(DEBUG_TAG, "non-fatal: error releasing MLoc");
    INKError("non-fatal: error releasing MLoc\n");
  }

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = INKIOBufferReaderAvail(reader);

  /* INKIOBufferReaderAvail may send an INK_ERROR */
  if ((INKReturnCode) total_avail == INK_ERROR) {
    INKDebug(DEBUG_TAG, "couldn't get available byte-count from IO-read-buffer");
    INKError("couldn't get available byte-count from IO-read-buffer\n");
    goto done;
  }

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = INKIOBufferReaderStart(reader);

  /* INKIOBufferReaderStart may return an error pointer */
  if (block == INK_ERROR_PTR) {
    INKDebug(DEBUG_TAG, "couldn't get from IOBufferBlock");
    INKError("couldn't get from IOBufferBlock\n");
    goto done;
  }

  while (block) {

    block_start = INKIOBufferBlockReadStart(block, reader, &block_avail);

    /* INKIOBufferBlockReadStart may return an error pointer */
    if (block_start == INK_ERROR_PTR) {
      INKDebug(DEBUG_TAG, "couldn't read from IOBuffer");
      INKError("couldn't read from IOBuffer\n");
      goto done;
    }

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
    if (INKIOBufferReaderConsume(reader, block_avail) == INK_ERROR) {
      INKDebug(DEBUG_TAG, "error consuming data from the ReaderBlock");
      INKError("error consuming data from the ReaderBlock\n");
    }

    /* Get the next block now that we've consumed the
       data off the last block */
    block = INKIOBufferReaderStart(reader);

    /* INKIOBufferReaderStart may return an error pointer */
    if (block == INK_ERROR_PTR) {
      INKDebug(DEBUG_TAG, "couldn't get from IOBufferBlock");
      INKError("couldn't get from IOBufferBlock\n");
      goto done;
    }
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the INKIOBuffer that we used to print out the header */
  if (INKIOBufferReaderFree(reader) != INK_SUCCESS) {
    INKDebug(DEBUG_TAG, "non-fatal: error releasing IOBufferReader");
    INKError("non-fatal: error releasing IOBufferReader\n");
  }

  if (INKIOBufferDestroy(output_buffer) != INK_SUCCESS) {
    INKDebug(DEBUG_TAG, "non-fatal: error destroying IOBuffer");
    INKError("non-fatal: error destroying IOBuffer\n");
  }

  /* Although I'd never do this a production plugin, printf
     the header so that we can see it's all there */
  INKDebug("debug-output-header", "%s", output_string);

  INKfree(output_string);

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

static int
hdr_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_OS_DNS:
    handle_dns(txnp, contp);
    return 0;
  default:
    break;
  }

  return 0;
}

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 5.2 */
    if (major_ts_version > 5) {
      result = 1;
    } else if (major_ts_version == 5) {
      if (minor_ts_version >= 2) {
        result = 1;
      }
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;

  info.plugin_name = "output-header";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("[PluginInit] Plugin registration failed.\n");
    goto error;
  }

  if (!check_ts_version()) {
    INKError("[PluginInit] Plugin requires Traffic Server 5.2.0 or later\n");
    goto error;
  }


  INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, INKContCreate(hdr_plugin, NULL));

error:
  INKError("[PluginInit] Plugin not initialized");
}

