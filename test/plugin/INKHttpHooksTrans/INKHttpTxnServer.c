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


#include "ts.h"

const char *const TSEventStrId[] = {
  "TS_EVENT_HTTP_CONTINUE",    /* 60000 */
  "TS_EVENT_HTTP_ERROR",       /* 60001 */
  "TS_EVENT_HTTP_READ_REQUEST_HDR",    /* 60002 */
  "TS_EVENT_HTTP_OS_DNS",      /* 60003 */
  "TS_EVENT_HTTP_SEND_REQUEST_HDR",    /* 60004 */
  "TS_EVENT_HTTP_READ_CACHE_HDR",      /* 60005 */
  "TS_EVENT_HTTP_READ_RESPONSE_HDR",   /* 60006 */
  "TS_EVENT_HTTP_SEND_RESPONSE_HDR",   /* 60007 */
  "TS_EVENT_HTTP_REQUEST_TRANSFORM",   /* 60008 */
  "TS_EVENT_HTTP_RESPONSE_TRANSFORM",  /* 60009 */
  "TS_EVENT_HTTP_SELECT_ALT",  /* 60010 */
  "TS_EVENT_HTTP_TXN_START",   /* 60011 */
  "TS_EVENT_HTTP_TXN_CLOSE",   /* 60012 */
  "TS_EVENT_HTTP_SSN_START",   /* 60013 */
  "TS_EVENT_HTTP_SSN_CLOSE",   /* 60014 */

  "TS_EVENT_MGMT_UPDATE"       /* 60100 */
};

#define		index(x)	((x)%(1000))


/*
 * handle_dns() from output-header.c:
 * prints in it's entirety either the
 * response or the request
 * TODO byte for byte buff compare alg that guarantees data integrity for cached and non-cached data buffs
*/

/* Type can be used to display/compare request/response differently */
static void
DisplayBufferContents(TSMBuffer bufp, TSMLoc hdr_loc, TSHttpType type)
{

  TSIOBuffer output_buffer;
  TSIOBufferReader reader;
  int total_avail;

  TSIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string;
  int output_len;

  output_buffer = TSIOBufferCreate();
  if (!output_buffer) {
    TSError("couldn't allocate IOBuffer\n");
  }
  reader = TSIOBufferReaderAlloc(output_buffer);


    /****** Print the HTTP header (for either a resp or req) first ********/
  TSHttpHdrPrint(bufp, hdr_loc, output_buffer);

  /* This will print MIMEFields (for either a resp or req)  */
  TSMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = TSIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) TSmalloc(total_avail + 1);
  output_len = 0;

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
  printf("%s", output_string);

  TSfree(output_string);

  /* Caller reenable the session/transaction:
   * TSHttpTxnReenable (txnp, TS_EVENT_HTTP_CONTINUE);
   */

}

/* This event registered at init (globally) */
static int
handle_HTTP_SEND_RESPONSE_HDR(TSCont contp, TSEvent event, void *eData)
{
  TSMBuffer reqBuf, respBuf;
  TSMLoc reqBufLoc, respBufLoc;
  TSHttpTxn txnp = (TSHttpTxn) eData;
  void *bufPtr = NULL, *fmtPtr = NULL;

  int re = 0, err = 0;

  return err;
}

static int
handle_READ_REQUEST_HDR(TSCont cont, TSEvent event, void *eData)
{
  TSMBuffer reqBuf;
  TSMLoc reqBufLoc;

  TSHttpTxn txnp = (TSHttpTxn) eData;

  int err = 0, re = 0;

  /* Non-cached,
   * get client req after recieving TS_HTTP_READ_REQUEST_HDR_HOOK
   */
  re = TSHttpTxnClientReqGet(txnp, &reqBuf, &reqBufLoc);
  if (re) {
    DisplayBufferContents(reqBuf, reqBufLoc, TS_HTTP_TYPE_REQUEST);
  } else {
    TSDebug("TSHttpTransaction", "TSHttpTxnClientReqGet(): Header not found.");
    err++;
  }
  TSHandleMLocRelease(reqBuf, TS_NULL_MLOC, reqBufLoc);
  return err;
}

static int
handle_READ_RESPONSE_HDR(TSCont contp, TSEvent event, void *eData)
{
  TSMBuffer respBuf;
  TSMLoc respBufLoc;
  TSHttpTxn txnp = (TSHttpTxn) eData;

  void *bufPtr = NULL;

  int err = 0, re = 0;

  /* Non-cached,
   * get "client" resp after recieving TS_HTTP_READ_RESPONSE_HDR_HOOK
   */
  re = TSHttpTxnClientRespGet(txnp, &respBuf, &respBufLoc);
  if (re) {
    DisplayBufferContents(respBuf, respBufLoc, TS_HTTP_TYPE_RESPONSE);
  } else {
    TSDebug("TSHttpTransaction", "TSHttpTxnClientRespGet(): Header not found.");
    err++;
  }
  TSHandleMLocRelease(respBuf, TS_NULL_MLOC, respBufLoc);
  return err;
}


static int
TSHttpTransaction(TSCont contp, TSEvent event, void *eData)
{
  TSHttpSsn ssnp = (TSHttpSsn) eData;
  TSHttpTxn txnp = (TSHttpTxn) eData;

  TSDebug("TSHttpTransaction", "TSHttpTxnCachedReqGet(): event: %s \n", TSEventStrId[index(event)]);
  /* printf("ChkEvents: -- %s -- \n",TSEventStrId[index(event)]); */


  switch (event) {

  case TS_EVENT_HTTP_SSN_START:
    TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_HTTP_SEND_RESPONSE_HDR(contp, event, eData);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    handle_READ_REQUEST_HDR(contp, event, eData);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    handle_READ_RESPONSE_HDR(contp, event, eData);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    break;
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSCont contp = TSContCreate(TSHttpTransaction, NULL);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
