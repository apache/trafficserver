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

/******************************************************************************
 * This plugin is composed of three parts:
 * 1. Play with the functions in TSMimeHdr* and TSMimeParser* categories.
 * 2. Play with the functions in TSHttpHdr* and TSUrl* categories.
 * 3. Call TSHttpHdrReasonLookup and print out the default reason for each
 *    status.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined (_WIN32)
#	include <unistd.h>
#else
#	include <windows.h>
#endif

#include "ts.h"

#define DEBUG_TAG "check-mime-1-dbg"
#define REASON_DEBUG_TAG "status-reason"

int sect = 0;


#define PLUGIN_NAME "check-mime-1"
#define VALID_POINTER(X) ((X != NULL) && (X != TS_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lcleanup; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


/******************************************************************************
 * print out the header information in the output buffer.
 ******************************************************************************/
static void
printHeader(TSMBuffer bufp, TSMLoc hdr_loc, TSIOBuffer output_buffer, int section, const char *str)
{
  LOG_SET_FUNCTION_NAME("printHeader");

  TSIOBufferReader reader = NULL;
  int total_avail;

  TSIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string = NULL;
  int output_len;

  if ((reader = TSIOBufferReaderAlloc(output_buffer)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferReaderAlloc");

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  if ((total_avail = TSIOBufferReaderAvail(reader)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferReaderAvail");

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) TSmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  if ((block = TSIOBufferReaderStart(reader)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSIOBufferReaderStart");
  while (block) {

    if ((block_start = TSIOBufferBlockReadStart(block, reader, &block_avail)) == TS_ERROR_PTR)
      LOG_ERROR_AND_CLEANUP("TSIOBufferBlockReadStart");

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
    if (TSIOBufferReaderConsume(reader, block_avail) == TS_ERROR)
      LOG_ERROR_AND_CLEANUP("TSIOBufferBlockReadStart");

    /* Get the next block now that we've consumed the
       data off the last block */
    if ((block = TSIOBufferReaderStart(reader)) == TS_ERROR_PTR)
      LOG_ERROR_AND_CLEANUP("TSIOBufferReaderStart");
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Although I'd never do this a production plugin, printf
     the header so that we can see it's all there */
  TSDebug(DEBUG_TAG, "(%d) **************** output header ****************", section);
  TSDebug(DEBUG_TAG, "%s", output_string);

  /* compare the output_string and the str passed in */
  if (strcmp(output_string, str) != 0) {
    TSDebug(DEBUG_TAG, "(%d) Some errors occurred in the above output header\n", section);
  }

  /* Clean up */
Lcleanup:
  if (VALID_POINTER(reader))
    TSIOBufferReaderFree(reader);
  if (VALID_POINTER(output_string))
    TSfree(output_string);
}

/*
 * Play with the functions in TSMimeHdr* and TSMimeParser* categories.
 * This function covers the following functions:
 *   - TSMimeHdrParse
 *   - TSMimeParserClear
 *   - TSMimeParserCreate
 *   - TSMimeParserDestroy
 *
 *   - TSMimeHdrClone
 *   - TSMimeHdrFieldClone
 *   - TSMimeHdrFieldCopy
 *   - TSMimeHdrDestroy
 *   - TSMimeHdrFieldDestroy
 */
static int
mimeHdrHandler()
{
  LOG_SET_FUNCTION_NAME("mimeHdrHandler");
  TSMBuffer parseBuffer = NULL;
  TSMBuffer destBuffer = NULL;
  TSMLoc parseHdrLoc = NULL;
  TSMLoc destHdrLoc = NULL;
  TSMLoc srcViaFieldLoc = NULL, destViaFieldLoc = NULL, srcCLFieldLoc = NULL, destCLFieldLoc = NULL;
  TSMimeParser mimeParser = NULL;
  int status;
  TSIOBuffer outBuf1 = NULL, outBuf2 = NULL, outBuf3 = NULL;

  const char *mime_hdr_str =
    "Server: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 1024\r\nVia: HTTP/1.1 ts-lnx12 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *str1 =
    "Server: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 1024\r\nVia: HTTP/1.1 ts-lnx12 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *str2 =
    "Server: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 2048\r\nVia: HTTP/1.1 ts-lnx12 (Traffic-Server/4.0.0 [cHs f ])\r\nVia: HTTP/1.1 ts-sun26 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *via = "HTTP/1.1 ts-sun26 (Traffic-Server/4.0.0 [cHs f ])";
  int content_len = 2048;

  /* Create a TSMBuffer and parse a mime header for it */
  if ((mimeParser = TSMimeParserCreate()) == TS_ERROR_PTR || mimeParser == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeParserCreate");
  if ((parseBuffer = TSMBufferCreate()) == TS_ERROR_PTR || parseBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("TSMBufferCreate");
  if ((parseHdrLoc = TSMimeHdrCreate(parseBuffer)) == TS_ERROR_PTR || parseHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrCreate");

  status = TSMimeHdrParse(mimeParser, parseBuffer, parseHdrLoc, &mime_hdr_str, mime_hdr_str + strlen(mime_hdr_str));
  if (status != TS_PARSE_DONE)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrParse");

  /* clear the parser and parse another mime header for it */
  if (TSMimeParserClear(mimeParser) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeParserClear");

  /* (0) output the parsed mime header */
  if ((outBuf1 = TSIOBufferCreate()) == TS_ERROR_PTR || outBuf1 == NULL)
    LOG_ERROR_AND_CLEANUP("TSIOBufferCreate");
  if (TSMimeHdrPrint(parseBuffer, parseHdrLoc, outBuf1) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrPrint");

  printHeader(parseBuffer, parseHdrLoc, outBuf1, sect++, str1);


  /* create another TSMBuffer and clone the mime header in the previous TSMBuffer to it */
  if ((destBuffer = TSMBufferCreate()) == TS_ERROR_PTR || destBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("TSMBufferCreate");
  if ((destHdrLoc = TSMimeHdrClone(destBuffer, parseBuffer, parseHdrLoc)) == TS_ERROR_PTR || destHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrClone");

  /* (1) output the cloned mime header */
  if ((outBuf2 = TSIOBufferCreate()) == TS_ERROR_PTR || outBuf2 == NULL)
    LOG_ERROR_AND_CLEANUP("TSIOBufferCreate");
  if ((TSMimeHdrPrint(destBuffer, destHdrLoc, outBuf2)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrPrint");

  printHeader(destBuffer, destHdrLoc, outBuf2, sect++, str1);


  /* clone the Via field */
  if ((srcViaFieldLoc = TSMimeHdrFieldFind(parseBuffer, parseHdrLoc, TS_MIME_FIELD_VIA, TS_MIME_LEN_VIA)) == TS_ERROR_PTR ||
      srcViaFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldFind");

  if (TSMimeHdrFieldLengthGet(parseBuffer, parseHdrLoc, srcViaFieldLoc) == TS_ERROR) {
    LOG_ERROR("TSMimeHdrFieldLengthGet");
  }

  if (TSMimeHdrFieldValueStringSet(parseBuffer, parseHdrLoc, srcViaFieldLoc, 0, via, strlen(via)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueStringSet");
  if ((destViaFieldLoc = TSMimeHdrFieldClone(destBuffer, destHdrLoc, parseBuffer,
                                              parseHdrLoc, srcViaFieldLoc)) == TS_ERROR_PTR || destViaFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldClone");
  if ((TSMimeHdrFieldAppend(destBuffer, destHdrLoc, destViaFieldLoc)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldAppend");

  /* copy the Content-Length field */
  if ((srcCLFieldLoc =
       TSMimeHdrFieldFind(parseBuffer, parseHdrLoc, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH)) == TS_ERROR_PTR ||
      srcCLFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldFind");
  if (TSMimeHdrFieldValueIntSet(parseBuffer, parseHdrLoc, srcCLFieldLoc, 0, content_len) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueIntSet");
  if ((destCLFieldLoc = TSMimeHdrFieldFind(destBuffer, destHdrLoc, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH)) == TS_ERROR_PTR
      || destCLFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldFind");
  if (TSMimeHdrFieldCopy(destBuffer, destHdrLoc, destCLFieldLoc, parseBuffer, parseHdrLoc, srcCLFieldLoc) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldCopy(");


  /* (2) output the modified cloned mime header */
  if ((outBuf3 = TSIOBufferCreate()) == TS_ERROR_PTR || outBuf3 == NULL)
    LOG_ERROR_AND_CLEANUP("TSIOBufferCreate");
  if (TSMimeHdrPrint(destBuffer, destHdrLoc, outBuf3) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrPrint");

  printHeader(destBuffer, destHdrLoc, outBuf3, sect++, str2);

  /* negative test */
#ifdef DEBUG
  if (TSMimeHdrCreate(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSMimeHdrCreate");

  if (TSMimeHdrClone(NULL, parseBuffer, parseHdrLoc) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSMimeHdrClone");
  if (TSMimeHdrClone(destBuffer, NULL, parseHdrLoc) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSMimeHdrClone");
  if (TSMimeHdrClone(destBuffer, parseBuffer, NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSMimeHdrClone");

  if (TSMimeHdrFieldClone(NULL, NULL, NULL, NULL, NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSMimeHdrFieldClone");

  if (TSMimeHdrFieldCopy(NULL, NULL, NULL, NULL, NULL, NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSMimeHdrFieldCopy");

  if (TSMimeParserClear(NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSMimeParserClear");

  if (TSMimeHdrFieldLengthGet(NULL, parseHdrLoc, srcViaFieldLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldLengthGet");
  }
  if (TSMimeHdrFieldLengthGet(parseBuffer, NULL, srcViaFieldLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldLengthGet");
  }
  if (TSMimeHdrFieldLengthGet(parseBuffer, parseHdrLoc, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeHdrFieldLengthGet");
  }
#endif

  /* Cleanup */
Lcleanup:

  /* negative test for cleanup functions */
#ifdef DEBUG
  if (TSMimeParserDestroy(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimeParserDestroy");
  }

  if (TSMimeHdrFieldDestroy(NULL, destHdrLoc, destViaFieldLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimerHdrFieldDestroy");
  }
  if (TSMimeHdrFieldDestroy(destBuffer, NULL, destViaFieldLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimerHdrFieldDestroy");
  }
  if (TSMimeHdrFieldDestroy(destBuffer, destHdrLoc, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimerHdrFieldDestroy");
  }

  if (TSMimeHdrDestroy(NULL, parseHdrLoc) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimerHdrDestroy");
  }
  if (TSMimeHdrDestroy(parseBuffer, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSMimerHdrDestroy");
  }
#endif

  /* destroy the parser */
  if (VALID_POINTER(mimeParser))
    TSMimeParserDestroy(mimeParser);

  /* destroy the output buffers */
  if (VALID_POINTER(outBuf1))
    TSIOBufferDestroy(outBuf1);
  if (VALID_POINTER(outBuf2))
    TSIOBufferDestroy(outBuf2);
  if (VALID_POINTER(outBuf3))
    TSIOBufferDestroy(outBuf3);

  /* release the field handles */
  if (VALID_POINTER(srcViaFieldLoc))
    TSHandleMLocRelease(parseBuffer, parseHdrLoc, srcViaFieldLoc);
  if (VALID_POINTER(destViaFieldLoc))
    TSMimeHdrFieldDestroy(destBuffer, destHdrLoc, destViaFieldLoc);
  if (VALID_POINTER(destViaFieldLoc))
    TSHandleMLocRelease(destBuffer, destHdrLoc, destViaFieldLoc);
  if (VALID_POINTER(srcCLFieldLoc))
    TSHandleMLocRelease(parseBuffer, parseHdrLoc, srcCLFieldLoc);
  if (VALID_POINTER(destCLFieldLoc))
    TSHandleMLocRelease(destBuffer, destHdrLoc, destCLFieldLoc);

  /* destroy the mime headers and buffers */
  if (VALID_POINTER(parseHdrLoc))
    TSMimeHdrDestroy(parseBuffer, parseHdrLoc);
  if (VALID_POINTER(parseHdrLoc))
    TSHandleMLocRelease(parseBuffer, TS_NULL_MLOC, parseHdrLoc);
  if (VALID_POINTER(parseBuffer))
    TSMBufferDestroy(parseBuffer);

  if (VALID_POINTER(destHdrLoc))
    TSMimeHdrDestroy(destBuffer, destHdrLoc);
  if (VALID_POINTER(destHdrLoc))
    TSHandleMLocRelease(destBuffer, TS_NULL_MLOC, destHdrLoc);
  if (VALID_POINTER(destBuffer))
    TSMBufferDestroy(destBuffer);
}

/******************************************************************************
 * play with the functions in TSHttpHdr* and TSUrl* categories.
 ******************************************************************************/
static void
httpHdrHandler()
{
  LOG_SET_FUNCTION_NAME("httpHdrHandler");

  TSMBuffer srcBuffer = NULL, destBuffer = NULL;
  TSMLoc srcHdrLoc = NULL, destHdrLoc = NULL, srcUrl = NULL, destUrl = NULL;
  TSHttpParser parser = NULL;
  TSIOBuffer outBuf1 = NULL, outBuf2 = NULL;

  int status;

  const char *request_header_str =
    "GET http://www.joes-hardware.com/ HTTP/1.0\r\nDate: Wed, 05 Jul 2000 22:12:26 GMT\r\nConnection: Keep-Alive\r\nUser-Agent: Mozilla/4.51 [en] (X11; U; IRIX 6.2 IP22)\r\nHost: www.joes-hardware.com\r\nCache-Control: no-cache\r\nAccept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\nAccept-Charset: iso-8859-1,*,utf-8\r\nAccept-Encoding: gzip\r\nAccept-Language: en\r\nX-Number-Header: 12345\r\nAccept-Charset: windows-1250, koi8-r\r\n\r\n";

  const char *url_str = "http://www.example.com/";

  const char *str3 =
    "GET http://www.example.com/ HTTP/1.0\r\nDate: Wed, 05 Jul 2000 22:12:26 GMT\r\nConnection: Keep-Alive\r\nUser-Agent: Mozilla/4.51 [en] (X11; U; IRIX 6.2 IP22)\r\nHost: www.joes-hardware.com\r\nCache-Control: no-cache\r\nAccept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\nAccept-Charset: iso-8859-1,*,utf-8\r\nAccept-Encoding: gzip\r\nAccept-Language: en\r\nX-Number-Header: 12345\r\nAccept-Charset: windows-1250, koi8-r\r\n\r\n";

  const char *str4 = "http://www.example.com/";


  /* create a http header */
  if ((srcBuffer = TSMBufferCreate()) == TS_ERROR_PTR || srcBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("TSMBufferCreate");
  if ((srcHdrLoc = TSHttpHdrCreate(srcBuffer)) == TS_ERROR_PTR || srcHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSHttpHdrCreate");

  /* parse the http header */
  if ((parser = TSHttpParserCreate()) == TS_ERROR_PTR || parser == NULL)
    LOG_ERROR_AND_CLEANUP("TSHttpParserCreate");
  status = TSHttpHdrParseReq(parser, srcBuffer, srcHdrLoc, &request_header_str,
                              request_header_str + strlen(request_header_str));
  if (status != TS_PARSE_DONE) {
    LOG_ERROR_AND_CLEANUP("TSHttpHdrParseReq");
  }


  /* create a url */
  if ((srcUrl = TSUrlCreate(srcBuffer)) == TS_ERROR_PTR || srcUrl == NULL)
    LOG_ERROR_AND_CLEANUP("TSUrlCreate");

  /* parse the str to srcUrl and set srcUrl to the http header */
  status = TSUrlParse(srcBuffer, srcUrl, &url_str, url_str + strlen(url_str));
  if (status != TS_PARSE_DONE) {
    LOG_ERROR_AND_CLEANUP("TSUrlParse");
  }

  if (TSHttpHdrUrlSet(srcBuffer, srcHdrLoc, srcUrl) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSHttpHdrUrlSet");

  /* negative test for TSHttpHdrUrlSet */
#ifdef DEBUG
  if (TSHttpHdrUrlSet(NULL, srcHdrLoc, srcUrl) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpHdrUrlSet");
  if (TSHttpHdrUrlSet(srcBuffer, NULL, srcUrl) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpHdrUrlSet");
  if (TSHttpHdrUrlSet(srcBuffer, srcHdrLoc, NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpHdrUrlSet");
#endif

  /* (3) output the http header */
  if ((outBuf1 = TSIOBufferCreate()) == TS_ERROR_PTR || outBuf1 == NULL)
    LOG_ERROR_AND_CLEANUP("TSIOBufferCreate");
  if (TSHttpHdrPrint(srcBuffer, srcHdrLoc, outBuf1) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSHttpHdrPrint");

  printHeader(srcBuffer, srcHdrLoc, outBuf1, sect++, str3);

  /* negative test for TSHttpHdrPrint */
#ifdef DEBUG
  if (TSHttpHdrPrint(NULL, srcHdrLoc, outBuf1) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpHdrPrint");
  if (TSHttpHdrPrint(srcBuffer, NULL, outBuf1) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpHdrPrint");
  if (TSHttpHdrPrint(srcBuffer, srcHdrLoc, NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpHdrPrint");
#endif

  /* clone the http header and url */
  if ((destBuffer = TSMBufferCreate()) == TS_ERROR_PTR || destBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("TSMBufferCreate");

  if ((destHdrLoc = TSHttpHdrClone(destBuffer, srcBuffer, srcHdrLoc)) == TS_ERROR_PTR || destHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("TSHttpHdrClone");
  if ((destUrl = TSUrlClone(destBuffer, srcBuffer, srcUrl)) == TS_ERROR_PTR || destUrl == NULL)
    LOG_ERROR_AND_CLEANUP("TSUrlClone");

  /* negative test for TSHttpHdrClone and TSUrlClone */
#ifdef DEBUG
  if (TSHttpHdrClone(NULL, srcBuffer, srcHdrLoc) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSHttpHdrClone");
  }
  if (TSHttpHdrClone(destBuffer, NULL, srcHdrLoc) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSHttpHdrClone");
  }
  if (TSHttpHdrClone(destBuffer, srcBuffer, NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSHttpHdrClone");
  }

  if (TSUrlClone(NULL, srcBuffer, srcUrl) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSUrlClone");
  }
  if (TSUrlClone(destBuffer, NULL, srcUrl) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSUrlClone");
  }
  if (TSUrlClone(destBuffer, srcBuffer, NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSUrlClone");
  }
#endif

  /* (4) output the cloned url */
  if ((outBuf2 = TSIOBufferCreate()) == TS_ERROR_PTR || outBuf2 == NULL)
    LOG_ERROR_AND_CLEANUP("TSIOBufferCreate");
  if (TSUrlPrint(destBuffer, destUrl, outBuf2) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSUrlPrint");

  printHeader(destBuffer, destUrl, outBuf2, sect++, str4);

  /* negative test for TSUrlPrint */
#ifdef DEBUG
  if (TSUrlPrint(NULL, destUrl, outBuf2) != TS_ERROR)
    LOG_ERROR_NEG("TSUrlPrint");
  if (TSUrlPrint(destBuffer, NULL, outBuf2) != TS_ERROR)
    LOG_ERROR_NEG("TSUrlPrint");
  if (TSUrlPrint(destBuffer, destUrl, NULL) != TS_ERROR)
    LOG_ERROR_NEG("TSUrlPrint");
#endif

  /* clean up */
Lcleanup:

  /* negative test for cleanup functions */
#ifdef DEBUG
  if (TSHttpParserDestroy(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpParserDestroy");
  }

  if (TSUrlDestroy(srcBuffer, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSUrlDestroy");
  }

  if (TSHttpHdrDestroy(srcBuffer, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpHdrDestroy");
  }
#endif

  /* destroy the parser */
  if (VALID_POINTER(parser))
    TSHttpParserDestroy(parser);

  /* destroy the output buffers */
  if (VALID_POINTER(outBuf1))
    TSIOBufferDestroy(outBuf1);
  if (VALID_POINTER(outBuf2))
    TSIOBufferDestroy(outBuf2);

  if (VALID_POINTER(srcUrl))
    TSUrlDestroy(srcBuffer, srcUrl);
  if (VALID_POINTER(srcUrl))
    TSHandleMLocRelease(srcBuffer, srcHdrLoc, srcUrl);

  if (VALID_POINTER(destUrl))
    TSUrlDestroy(destBuffer, destUrl);
  if (VALID_POINTER(destUrl))
    TSHandleMLocRelease(destBuffer, TS_NULL_MLOC, destUrl);

  if (VALID_POINTER(srcHdrLoc))
    TSHttpHdrDestroy(srcBuffer, srcHdrLoc);
  if (VALID_POINTER(srcHdrLoc))
    TSHandleMLocRelease(srcBuffer, TS_NULL_MLOC, srcHdrLoc);

  if (VALID_POINTER(destHdrLoc))
    TSHttpHdrDestroy(destBuffer, destHdrLoc);
  if (VALID_POINTER(destHdrLoc))
    TSHandleMLocRelease(destBuffer, TS_NULL_MLOC, destHdrLoc);

  if (VALID_POINTER(srcBuffer))
    TSMBufferDestroy(srcBuffer);
  if (VALID_POINTER(destBuffer))
    TSMBufferDestroy(destBuffer);
}

/******************************************************************************
 * call TSHttpHdrReasonLookup for each status and print out the default reason
 ******************************************************************************/
static int
httpHdrReasonHandler()
{
  LOG_SET_FUNCTION_NAME("httpHdrReasonHandler");
  const char *str;

#define CHECK_REASON_LOOKUP(R) {\
	str = TSHttpHdrReasonLookup(R); \
	if (str == TS_ERROR_PTR) \
	    LOG_ERROR_AND_RETURN(#R); \
	TSDebug(REASON_DEBUG_TAG, "%s: %s", #R, str); \
    }

  TSDebug(REASON_DEBUG_TAG, "********************** TS_HTTP_STATUS Reason ********************");

  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_NONE);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_CONTINUE);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_SWITCHING_PROTOCOL);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_OK);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_CREATED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_ACCEPTED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_NO_CONTENT);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_RESET_CONTENT);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_PARTIAL_CONTENT);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_MULTIPLE_CHOICES);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_MOVED_PERMANENTLY);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_MOVED_TEMPORARILY);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_SEE_OTHER);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_NOT_MODIFIED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_USE_PROXY);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_BAD_REQUEST);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_UNAUTHORIZED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_PAYMENT_REQUIRED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_FORBIDDEN);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_NOT_FOUND);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_METHOD_NOT_ALLOWED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_NOT_ACCEPTABLE);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_REQUEST_TIMEOUT);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_CONFLICT);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_GONE);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_LENGTH_REQUIRED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_PRECONDITION_FAILED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_REQUEST_URI_TOO_LONG);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_NOT_IMPLEMENTED);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_BAD_GATEWAY);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_SERVICE_UNAVAILABLE);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_GATEWAY_TIMEOUT);
  CHECK_REASON_LOOKUP(TS_HTTP_STATUS_HTTPVER_NOT_SUPPORTED);
}

void
TSPluginInit(int argc, const char *argv[])
{
  mimeHdrHandler();
  httpHdrHandler();
  httpHdrReasonHandler();
}
