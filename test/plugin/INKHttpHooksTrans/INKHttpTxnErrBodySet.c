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


#include "InkAPI.h"

/* This is a response to a client (req), which will be executed after reciept
 * of: 
 *   INK_HTTP_SEND_RESPONSE_HDR_HOOK
 *
 * htmlBody: text 
 * INKHttpTxnErrorBodySet(txnp, htmlBody, sizeof(htmlBody), NULL); 
 *
 * htmlBody: other fmt (?) 
 * INKHttpTxnErrorBodySet(txnp, htmlBody, sizeof(htmlBody), "image/jpeg"); 
 *
 * INKHttpTxnErrorBodySet(txnp, "inktomi.gif", 1308, "image/jpeg");
 * 
 * This api requires that a GET request of a site that does not exist or
 * of a site that is not answering requests. This API will not overwrite 
 * the requested content if that content can be succsesfully returned.
 * 
 * TODO API should be tested by sending a request that indicates the type of error body to be set in the response
 * For example: 
 * GET http://www.bogusIMAGE.com HTTP/1.0 
 * GET http://www.bogusHTML.com HTTP/1.0 
 * GET http://www.bogusOTHER.com HTTP/1.0 
 * 
 * retrieve the URL: INKHttpHdrURLGet() 
 * strstr to parse and find a substring in the URL string  
 *        search for the above predefined sites and return the appropriate 
 *        body type. 
 * Perhaps not the most creative tests, but this should nicely tests this 
 * API in a single plug in. 
 */

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

/* Used in INKHttpTxnErrrorBodySet */
#define		FMT_TXT_HTML	("text/html")
#define		FMT_IMAGE_JPEG	("image/jpeg")
#define		FMT_TXT		(NULL)


/* Body of HTML page sent by INKHttpErrorBodySet() 
*/
const char htmlBody[] = " \
<html> \
<body> \
<table> \
 \
<tr> \
<td id=\"tablePropsWidth\" width=\"400\" colspan=\"2\"><font style=\"COLOR: black; FONT: 8pt/11pt verdana\">The page you are looking for might have been removed, had its name changed, or is temporarily unavailable.</font></td> \
</tr> \
 \
</table> \
</body> \
</html>";



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
}

/* TODO this is probably not visible in a browser--get one that is */
static unsigned char marker_gif_data[] = {
  0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00,
  0x01, 0x00, 0x80, 0x00, 0x00, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x01, 0x0a,
  0x00, 0x01, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x4c,
  0x01, 0x00, 0x3b,
};

/* Test of INKHttpTxnErrorBodySet() with an image/jpeg from 
 * INK_HTTP_SSN_START.  This code should be working. However,
 * there is no such thing as the image is too small. The telnet
 * client should see "GIF" in the body of the response.
*/
static int
handle_HTTP_SSN_START(INKCont contp, INKEvent event, void *eData)
{
  /* Just send back the body */
  INKHttpTxn txnp = (INKHttpTxn) eData;
  void *markerPtr = NULL, *fmtPtr = NULL;

  int err = 0;

  INKDebug("INKHttpTxnErrorBodySet", "HTTP_SSN_START: ********* INKHttpTxnErrorBodySet\n");

  markerPtr = INKmalloc(sizeof(marker_gif_data));
  memcpy(markerPtr, marker_gif_data, sizeof(marker_gif_data));
  fmtPtr = INKmalloc(sizeof(FMT_IMAGE_JPEG) + 1);
  strncpy(fmtPtr, FMT_IMAGE_JPEG, sizeof(FMT_IMAGE_JPEG));

  INKHttpTxnErrorBodySet(txnp, (char *) markerPtr, sizeof(marker_gif_data), fmtPtr);
  /* TS managed space: 
   * INKfree(markerPtr);
   * INKfree(fmtPtr); 
   */

  return err;
}


/* Test of INKHttpTxnErrorBodySet() by returning an HTML page from
 * INK_HTTP_SEND_RESPONSE hook.
*/
static int
handle_HTTP_SEND_RESPONSE_HDR(INKCont contp, INKEvent event, void *eData)
{
  INKMBuffer reqBuf, respBuf;
  INKMLoc reqBufLoc, respBufLoc;
  INKHttpTxn txnp = (INKHttpTxn) eData;
  void *bufPtr = NULL, *fmtPtr = NULL;

  int re = 0, err = 0;

  /* This is the response back to the client */
  re = INKHttpTxnClientRespGet(txnp, &respBuf, &respBufLoc);
  if (re) {
    INKDebug("INKHttpTxnErrorBodySet", "HTTP_SEND_RESPONSE_HDR: ********* INKHttpTxnClientRespGet\n");

    DisplayBufferContents(respBuf, respBufLoc, INK_HTTP_TYPE_REQUEST);

    INKHandleMLocRelease(respBuf, INK_NULL_MLOC, respBufLoc);

    bufPtr = INKmalloc(sizeof(htmlBody));
    strncpy(bufPtr, htmlBody, sizeof(htmlBody));
    fmtPtr = INKmalloc(sizeof(FMT_TXT_HTML) + 1);
    strncpy(fmtPtr, FMT_TXT_HTML, sizeof(FMT_TXT_HTML));

    INKHttpTxnErrorBodySet(txnp, (char *) bufPtr, sizeof(htmlBody), fmtPtr);
    /* TS frees when no longer needed:
     * INKfree(bufPtr);
     * INKfree(fmtPtr); 
     */
  }
  return err;
}


static int
INKHttpTransaction(INKCont contp, INKEvent event, void *eData)
{
  INKHttpSsn ssnp = (INKHttpSsn) eData;
  INKHttpTxn txnp = (INKHttpTxn) eData;

  INKDebug("INKHttpTxnErrorBodySet", "INKHttpTxnCachedReqGet(): event: %s \n", INKEventStrId[index(event)]);

  switch (event) {

  case INK_EVENT_HTTP_SSN_START:
    handle_HTTP_SSN_START(contp, event, eData);
    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_HTTP_SEND_RESPONSE_HDR(contp, event, eData);
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
}
