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

const char *const INKEventStrId[] = {
  "INK_EVENT_HTTP_CONTINUE",    /* 60000 */
  "INK_EVENT_HTTP_ERROR",       /* 60001 */
  "INK_EVENT_HTTP_READ_REQUEST_HDR",    /* 60002 */
  "INK_EVENT_HTTP_OS_DNS",      /* 60003 */
  "INK_EVENT_HTTP_SEND_REQUEST_HDR",    /* 60004 */
  "INK_EVENT_HTTP_READ_CACHE_HDR",      /* 60005 */
  "INK_EVENT_HTTP_READ_RESPONSE_HDR",   /* 60006 */
  "INK_EVENT_HTTP_SEND_RESPONSE_HDR",   /* 60007 */
  "INK_EVENT_HTTP_REQUEST_TRANSFORM",   /* 60008 */
  "INK_EVENT_HTTP_RESPONSE_TRANSFORM",  /* 60009 */
  "INK_EVENT_HTTP_SELECT_ALT",  /* 60010 */
  "INK_EVENT_HTTP_TXN_START",   /* 60011 */
  "INK_EVENT_HTTP_TXN_CLOSE",   /* 60012 */
  "INK_EVENT_HTTP_SSN_START",   /* 60013 */
  "INK_EVENT_HTTP_SSN_CLOSE",   /* 60014 */

  "INK_EVENT_MGMT_UPDATE"       /* 60100 */
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
DisplayBufferContents(INKMBuffer bufp, INKMLoc hdr_loc, INKHttpType type)
{

  INKIOBuffer output_buffer;
  INKIOBufferReader reader;
  int total_avail;

  INKIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string;
  int output_len;

  output_buffer = INKIOBufferCreate();
  if (!output_buffer) {
    INKError("couldn't allocate IOBuffer\n");
  }
  reader = INKIOBufferReaderAlloc(output_buffer);


    /****** Print the HTTP header (for either a resp or req) first ********/
  INKHttpHdrPrint(bufp, hdr_loc, output_buffer);

  /* This will print MIMEFields (for either a resp or req)  */
  INKMimeHdrPrint(bufp, hdr_loc, output_buffer);

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  total_avail = INKIOBufferReaderAvail(reader);

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  block = INKIOBufferReaderStart(reader);
  while (block) {

    block_start = INKIOBufferBlockReadStart(block, reader, &block_avail);

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
    INKIOBufferReaderConsume(reader, block_avail);

    /* Get the next block now that we've consumed the
       data off the last block */
    block = INKIOBufferReaderStart(reader);
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Free up the INKIOBuffer that we used to print out the header */
  INKIOBufferReaderFree(reader);
  INKIOBufferDestroy(output_buffer);

  /* Although I'd never do this a production plugin, printf
     the header so that we can see it's all there */
  printf("%s", output_string);

  INKfree(output_string);

  /* Caller reenable the session/transaction: 
   * INKHttpTxnReenable (txnp, INK_EVENT_HTTP_CONTINUE); 
   */

}

/* This event registered at init (globally) */
static int
handle_HTTP_SEND_RESPONSE_HDR(INKCont contp, INKEvent event, void *eData)
{
  INKMBuffer reqBuf, respBuf;
  INKMLoc reqBufLoc, respBufLoc;
  INKHttpTxn txnp = (INKHttpTxn) eData;
  void *bufPtr = NULL, *fmtPtr = NULL;

  int re = 0, err = 0;

  return err;
}

static int
handle_READ_REQUEST_HDR(INKCont cont, INKEvent event, void *eData)
{
  INKMBuffer reqBuf;
  INKMLoc reqBufLoc;

  INKHttpTxn txnp = (INKHttpTxn) eData;

  int err = 0, re = 0;

  /* Non-cached, 
   * get client req after recieving INK_HTTP_READ_REQUEST_HDR_HOOK 
   */
  re = INKHttpTxnClientReqGet(txnp, &reqBuf, &reqBufLoc);
  if (re) {
    DisplayBufferContents(reqBuf, reqBufLoc, INK_HTTP_TYPE_REQUEST);
  } else {
    INKDebug("INKHttpTransaction", "INKHttpTxnClientReqGet(): Header not found.");
    err++;
  }
  INKHandleMLocRelease(reqBuf, INK_NULL_MLOC, reqBufLoc);
  return err;
}

static int
handle_READ_RESPONSE_HDR(INKCont contp, INKEvent event, void *eData)
{
  INKMBuffer respBuf;
  INKMLoc respBufLoc;
  INKHttpTxn txnp = (INKHttpTxn) eData;

  void *bufPtr = NULL;

  int err = 0, re = 0;

  /* Non-cached, 
   * get "client" resp after recieving INK_HTTP_READ_RESPONSE_HDR_HOOK 
   */
  re = INKHttpTxnClientRespGet(txnp, &respBuf, &respBufLoc);
  if (re) {
    DisplayBufferContents(respBuf, respBufLoc, INK_HTTP_TYPE_RESPONSE);
  } else {
    INKDebug("INKHttpTransaction", "INKHttpTxnClientRespGet(): Header not found.");
    err++;
  }
  INKHandleMLocRelease(respBuf, INK_NULL_MLOC, respBufLoc);
  return err;
}


static int
INKHttpTransaction(INKCont contp, INKEvent event, void *eData)
{
  INKHttpSsn ssnp = (INKHttpSsn) eData;
  INKHttpTxn txnp = (INKHttpTxn) eData;

  INKDebug("INKHttpTransaction", "INKHttpTxnCachedReqGet(): event: %s \n", INKEventStrId[index(event)]);
  /* printf("ChkEvents: -- %s -- \n",INKEventStrId[index(event)]); */


  switch (event) {

  case INK_EVENT_HTTP_SSN_START:
    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_HTTP_SEND_RESPONSE_HDR(contp, event, eData);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    handle_READ_REQUEST_HDR(contp, event, eData);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    handle_READ_RESPONSE_HDR(contp, event, eData);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;
  default:
    break;
  }
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKCont contp = INKContCreate(INKHttpTransaction, NULL);
  INKHttpHookAdd(INK_HTTP_SSN_START_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
