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
 * 1. Play with the functions in INKMimeHdr* and INKMimeParser* categories.
 * 2. Play with the functions in INKHttpHdr* and INKUrl* categories.
 * 3. Call INKHttpHdrReasonLookup and print out the default reason for each
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

#include "InkAPI.h"

#define DEBUG_TAG "check-mime-1-dbg"
#define REASON_DEBUG_TAG "status-reason"

int sect = 0;


#define PLUGIN_NAME "check-mime-1"
#define VALID_POINTER(X) ((X != NULL) && (X != INK_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
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
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


/******************************************************************************
 * print out the header information in the output buffer.
 ******************************************************************************/
static void
printHeader(INKMBuffer bufp, INKMLoc hdr_loc, INKIOBuffer output_buffer, int section, const char *str)
{
  LOG_SET_FUNCTION_NAME("printHeader");

  INKIOBufferReader reader = NULL;
  int total_avail;

  INKIOBufferBlock block;
  const char *block_start;
  int block_avail;

  char *output_string = NULL;
  int output_len;

  if ((reader = INKIOBufferReaderAlloc(output_buffer)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferReaderAlloc");

  /* Find out how the big the complete header is by
     seeing the total bytes in the buffer.  We need to
     look at the buffer rather than the first block to
     see the size of the entire header */
  if ((total_avail = INKIOBufferReaderAvail(reader)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferReaderAvail");

  /* Allocate the string with an extra byte for the string
     terminator */
  output_string = (char *) INKmalloc(total_avail + 1);
  output_len = 0;

  /* We need to loop over all the buffer blocks to make
     sure we get the complete header since the header can
     be in multiple blocks */
  if ((block = INKIOBufferReaderStart(reader)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKIOBufferReaderStart");
  while (block) {

    if ((block_start = INKIOBufferBlockReadStart(block, reader, &block_avail)) == INK_ERROR_PTR)
      LOG_ERROR_AND_CLEANUP("INKIOBufferBlockReadStart");

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
    if (INKIOBufferReaderConsume(reader, block_avail) == INK_ERROR)
      LOG_ERROR_AND_CLEANUP("INKIOBufferBlockReadStart");

    /* Get the next block now that we've consumed the
       data off the last block */
    if ((block = INKIOBufferReaderStart(reader)) == INK_ERROR_PTR)
      LOG_ERROR_AND_CLEANUP("INKIOBufferReaderStart");
  }

  /* Terminate the string */
  output_string[output_len] = '\0';
  output_len++;

  /* Although I'd never do this a production plugin, printf
     the header so that we can see it's all there */
  INKDebug(DEBUG_TAG, "(%d) **************** output header ****************", section);
  INKDebug(DEBUG_TAG, "%s", output_string);

  /* compare the output_string and the str passed in */
  if (strcmp(output_string, str) != 0) {
    INKDebug(DEBUG_TAG, "(%d) Some errors occurred in the above output header\n", section);
  }

  /* Clean up */
Lcleanup:
  if (VALID_POINTER(reader))
    INKIOBufferReaderFree(reader);
  if (VALID_POINTER(output_string))
    INKfree(output_string);
}

/* 
 * Play with the functions in INKMimeHdr* and INKMimeParser* categories.
 * This function covers the following functions:
 *   - INKMimeHdrParse
 *   - INKMimeParserClear 
 *   - INKMimeParserCreate
 *   - INKMimeParserDestroy
 *
 *   - INKMimeHdrClone
 *   - INKMimeHdrFieldClone
 *   - INKMimeHdrFieldCopy
 *   - INKMimeHdrDestroy
 *   - INKMimeHdrFieldDestroy
 */
static int
mimeHdrHandler()
{
  LOG_SET_FUNCTION_NAME("mimeHdrHandler");
  INKMBuffer parseBuffer = NULL;
  INKMBuffer destBuffer = NULL;
  INKMLoc parseHdrLoc = NULL;
  INKMLoc destHdrLoc = NULL;
  INKMLoc srcViaFieldLoc = NULL, destViaFieldLoc = NULL, srcCLFieldLoc = NULL, destCLFieldLoc = NULL;
  INKMimeParser mimeParser = NULL;
  int status;
  INKIOBuffer outBuf1 = NULL, outBuf2 = NULL, outBuf3 = NULL;

  const char *mime_hdr_str =
    "Server: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 1024\r\nVia: HTTP/1.1 ts-lnx12 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *str1 =
    "Server: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 1024\r\nVia: HTTP/1.1 ts-lnx12 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *str2 =
    "Server: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 2048\r\nVia: HTTP/1.1 ts-lnx12 (Traffic-Server/4.0.0 [cHs f ])\r\nVia: HTTP/1.1 ts-sun26 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *via = "HTTP/1.1 ts-sun26 (Traffic-Server/4.0.0 [cHs f ])";
  int content_len = 2048;

  /* Create a INKMBuffer and parse a mime header for it */
  if ((mimeParser = INKMimeParserCreate()) == INK_ERROR_PTR || mimeParser == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeParserCreate");
  if ((parseBuffer = INKMBufferCreate()) == INK_ERROR_PTR || parseBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("INKMBufferCreate");
  if ((parseHdrLoc = INKMimeHdrCreate(parseBuffer)) == INK_ERROR_PTR || parseHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrCreate");

  status = INKMimeHdrParse(mimeParser, parseBuffer, parseHdrLoc, &mime_hdr_str, mime_hdr_str + strlen(mime_hdr_str));
  if (status != INK_PARSE_DONE)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrParse");

  /* clear the parser and parse another mime header for it */
  if (INKMimeParserClear(mimeParser) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeParserClear");

  /* (0) output the parsed mime header */
  if ((outBuf1 = INKIOBufferCreate()) == INK_ERROR_PTR || outBuf1 == NULL)
    LOG_ERROR_AND_CLEANUP("INKIOBufferCreate");
  if (INKMimeHdrPrint(parseBuffer, parseHdrLoc, outBuf1) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrPrint");

  printHeader(parseBuffer, parseHdrLoc, outBuf1, sect++, str1);


  /* create another INKMBuffer and clone the mime header in the previous INKMBuffer to it */
  if ((destBuffer = INKMBufferCreate()) == INK_ERROR_PTR || destBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("INKMBufferCreate");
  if ((destHdrLoc = INKMimeHdrClone(destBuffer, parseBuffer, parseHdrLoc)) == INK_ERROR_PTR || destHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrClone");

  /* (1) output the cloned mime header */
  if ((outBuf2 = INKIOBufferCreate()) == INK_ERROR_PTR || outBuf2 == NULL)
    LOG_ERROR_AND_CLEANUP("INKIOBufferCreate");
  if ((INKMimeHdrPrint(destBuffer, destHdrLoc, outBuf2)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrPrint");

  printHeader(destBuffer, destHdrLoc, outBuf2, sect++, str1);


  /* clone the Via field */
  if ((srcViaFieldLoc = INKMimeHdrFieldRetrieve(parseBuffer, parseHdrLoc, INK_MIME_FIELD_VIA)) == INK_ERROR_PTR ||
      srcViaFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldRetrieve");

  if (INKMimeHdrFieldLengthGet(parseBuffer, parseHdrLoc, srcViaFieldLoc) == INK_ERROR) {
    LOG_ERROR("INKMimeHdrFieldLengthGet");
  }

  if (INKMimeHdrFieldValueStringSet(parseBuffer, parseHdrLoc, srcViaFieldLoc, 0, via, strlen(via)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueStringSet");
  if ((destViaFieldLoc = INKMimeHdrFieldClone(destBuffer, destHdrLoc, parseBuffer,
                                              parseHdrLoc, srcViaFieldLoc)) == INK_ERROR_PTR || destViaFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldClone");
  if ((INKMimeHdrFieldAppend(destBuffer, destHdrLoc, destViaFieldLoc)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldAppend");

  /* copy the Content-Length field */
  if ((srcCLFieldLoc =
       INKMimeHdrFieldRetrieve(parseBuffer, parseHdrLoc, INK_MIME_FIELD_CONTENT_LENGTH)) == INK_ERROR_PTR ||
      srcCLFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldRetrieve");
  if (INKMimeHdrFieldValueIntSet(parseBuffer, parseHdrLoc, srcCLFieldLoc, 0, content_len) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueIntSet");
  if ((destCLFieldLoc = INKMimeHdrFieldRetrieve(destBuffer, destHdrLoc, INK_MIME_FIELD_CONTENT_LENGTH)) == INK_ERROR_PTR
      || destCLFieldLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldRetrieve");
  if (INKMimeHdrFieldCopy(destBuffer, destHdrLoc, destCLFieldLoc, parseBuffer, parseHdrLoc, srcCLFieldLoc) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldCopy(");


  /* (2) output the modified cloned mime header */
  if ((outBuf3 = INKIOBufferCreate()) == INK_ERROR_PTR || outBuf3 == NULL)
    LOG_ERROR_AND_CLEANUP("INKIOBufferCreate");
  if (INKMimeHdrPrint(destBuffer, destHdrLoc, outBuf3) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrPrint");

  printHeader(destBuffer, destHdrLoc, outBuf3, sect++, str2);

  /* negative test */
#ifdef DEBUG
  if (INKMimeHdrCreate(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKMimeHdrCreate");

  if (INKMimeHdrClone(NULL, parseBuffer, parseHdrLoc) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKMimeHdrClone");
  if (INKMimeHdrClone(destBuffer, NULL, parseHdrLoc) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKMimeHdrClone");
  if (INKMimeHdrClone(destBuffer, parseBuffer, NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKMimeHdrClone");

  if (INKMimeHdrFieldClone(NULL, NULL, NULL, NULL, NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKMimeHdrFieldClone");

  if (INKMimeHdrFieldCopy(NULL, NULL, NULL, NULL, NULL, NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKMimeHdrFieldCopy");

  if (INKMimeParserClear(NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKMimeParserClear");

  if (INKMimeHdrFieldLengthGet(NULL, parseHdrLoc, srcViaFieldLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldLengthGet");
  }
  if (INKMimeHdrFieldLengthGet(parseBuffer, NULL, srcViaFieldLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldLengthGet");
  }
  if (INKMimeHdrFieldLengthGet(parseBuffer, parseHdrLoc, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeHdrFieldLengthGet");
  }
#endif

  /* Cleanup */
Lcleanup:

  /* negative test for cleanup functions */
#ifdef DEBUG
  if (INKMimeParserDestroy(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimeParserDestroy");
  }

  if (INKMimeHdrFieldDestroy(NULL, destHdrLoc, destViaFieldLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimerHdrFieldDestroy");
  }
  if (INKMimeHdrFieldDestroy(destBuffer, NULL, destViaFieldLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimerHdrFieldDestroy");
  }
  if (INKMimeHdrFieldDestroy(destBuffer, destHdrLoc, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimerHdrFieldDestroy");
  }

  if (INKMimeHdrDestroy(NULL, parseHdrLoc) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimerHdrDestroy");
  }
  if (INKMimeHdrDestroy(parseBuffer, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMimerHdrDestroy");
  }
#endif

  /* destroy the parser */
  if (VALID_POINTER(mimeParser))
    INKMimeParserDestroy(mimeParser);

  /* destroy the output buffers */
  if (VALID_POINTER(outBuf1))
    INKIOBufferDestroy(outBuf1);
  if (VALID_POINTER(outBuf2))
    INKIOBufferDestroy(outBuf2);
  if (VALID_POINTER(outBuf3))
    INKIOBufferDestroy(outBuf3);

  /* release the field handles */
  if (VALID_POINTER(srcViaFieldLoc))
    INKHandleMLocRelease(parseBuffer, parseHdrLoc, srcViaFieldLoc);
  if (VALID_POINTER(destViaFieldLoc))
    INKMimeHdrFieldDestroy(destBuffer, destHdrLoc, destViaFieldLoc);
  if (VALID_POINTER(destViaFieldLoc))
    INKHandleMLocRelease(destBuffer, destHdrLoc, destViaFieldLoc);
  if (VALID_POINTER(srcCLFieldLoc))
    INKHandleMLocRelease(parseBuffer, parseHdrLoc, srcCLFieldLoc);
  if (VALID_POINTER(destCLFieldLoc))
    INKHandleMLocRelease(destBuffer, destHdrLoc, destCLFieldLoc);

  /* destroy the mime headers and buffers */
  if (VALID_POINTER(parseHdrLoc))
    INKMimeHdrDestroy(parseBuffer, parseHdrLoc);
  if (VALID_POINTER(parseHdrLoc))
    INKHandleMLocRelease(parseBuffer, INK_NULL_MLOC, parseHdrLoc);
  if (VALID_POINTER(parseBuffer))
    INKMBufferDestroy(parseBuffer);

  if (VALID_POINTER(destHdrLoc))
    INKMimeHdrDestroy(destBuffer, destHdrLoc);
  if (VALID_POINTER(destHdrLoc))
    INKHandleMLocRelease(destBuffer, INK_NULL_MLOC, destHdrLoc);
  if (VALID_POINTER(destBuffer))
    INKMBufferDestroy(destBuffer);
}

/******************************************************************************
 * play with the functions in INKHttpHdr* and INKUrl* categories.
 ******************************************************************************/
static void
httpHdrHandler()
{
  LOG_SET_FUNCTION_NAME("httpHdrHandler");

  INKMBuffer srcBuffer = NULL, destBuffer = NULL;
  INKMLoc srcHdrLoc = NULL, destHdrLoc = NULL, srcUrl = NULL, destUrl = NULL;
  INKHttpParser parser = NULL;
  INKIOBuffer outBuf1 = NULL, outBuf2 = NULL;

  int status;

  const char *request_header_str =
    "GET http://www.joes-hardware.com/ HTTP/1.0\r\nDate: Wed, 05 Jul 2000 22:12:26 GMT\r\nConnection: Keep-Alive\r\nUser-Agent: Mozilla/4.51 [en] (X11; U; IRIX 6.2 IP22)\r\nHost: www.joes-hardware.com\r\nCache-Control: no-cache\r\nAccept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\nAccept-Charset: iso-8859-1,*,utf-8\r\nAccept-Encoding: gzip\r\nAccept-Language: en\r\nX-Number-Header: 12345\r\nAccept-Charset: windows-1250, koi8-r\r\n\r\n";

  const char *url_str = "http://www.inktomi.com/";

  const char *str3 =
    "GET http://www.inktomi.com/ HTTP/1.0\r\nDate: Wed, 05 Jul 2000 22:12:26 GMT\r\nConnection: Keep-Alive\r\nUser-Agent: Mozilla/4.51 [en] (X11; U; IRIX 6.2 IP22)\r\nHost: www.joes-hardware.com\r\nCache-Control: no-cache\r\nAccept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\nAccept-Charset: iso-8859-1,*,utf-8\r\nAccept-Encoding: gzip\r\nAccept-Language: en\r\nX-Number-Header: 12345\r\nAccept-Charset: windows-1250, koi8-r\r\n\r\n";

  const char *str4 = "http://www.inktomi.com/";


  /* create a http header */
  if ((srcBuffer = INKMBufferCreate()) == INK_ERROR_PTR || srcBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("INKMBufferCreate");
  if ((srcHdrLoc = INKHttpHdrCreate(srcBuffer)) == INK_ERROR_PTR || srcHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKHttpHdrCreate");

  /* parse the http header */
  if ((parser = INKHttpParserCreate()) == INK_ERROR_PTR || parser == NULL)
    LOG_ERROR_AND_CLEANUP("INKHttpParserCreate");
  status = INKHttpHdrParseReq(parser, srcBuffer, srcHdrLoc, &request_header_str,
                              request_header_str + strlen(request_header_str));
  if (status != INK_PARSE_DONE) {
    LOG_ERROR_AND_CLEANUP("INKHttpHdrParseReq");
  }


  /* create a url */
  if ((srcUrl = INKUrlCreate(srcBuffer)) == INK_ERROR_PTR || srcUrl == NULL)
    LOG_ERROR_AND_CLEANUP("INKUrlCreate");

  /* parse the str to srcUrl and set srcUrl to the http header */
  status = INKUrlParse(srcBuffer, srcUrl, &url_str, url_str + strlen(url_str));
  if (status != INK_PARSE_DONE) {
    LOG_ERROR_AND_CLEANUP("INKUrlParse");
  }

  if (INKHttpHdrUrlSet(srcBuffer, srcHdrLoc, srcUrl) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKHttpHdrUrlSet");

  /* negative test for INKHttpHdrUrlSet */
#ifdef DEBUG
  if (INKHttpHdrUrlSet(NULL, srcHdrLoc, srcUrl) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpHdrUrlSet");
  if (INKHttpHdrUrlSet(srcBuffer, NULL, srcUrl) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpHdrUrlSet");
  if (INKHttpHdrUrlSet(srcBuffer, srcHdrLoc, NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpHdrUrlSet");
#endif

  /* (3) output the http header */
  if ((outBuf1 = INKIOBufferCreate()) == INK_ERROR_PTR || outBuf1 == NULL)
    LOG_ERROR_AND_CLEANUP("INKIOBufferCreate");
  if (INKHttpHdrPrint(srcBuffer, srcHdrLoc, outBuf1) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKHttpHdrPrint");

  printHeader(srcBuffer, srcHdrLoc, outBuf1, sect++, str3);

  /* negative test for INKHttpHdrPrint */
#ifdef DEBUG
  if (INKHttpHdrPrint(NULL, srcHdrLoc, outBuf1) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpHdrPrint");
  if (INKHttpHdrPrint(srcBuffer, NULL, outBuf1) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpHdrPrint");
  if (INKHttpHdrPrint(srcBuffer, srcHdrLoc, NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpHdrPrint");
#endif

  /* clone the http header and url */
  if ((destBuffer = INKMBufferCreate()) == INK_ERROR_PTR || destBuffer == NULL)
    LOG_ERROR_AND_CLEANUP("INKMBufferCreate");

  if ((destHdrLoc = INKHttpHdrClone(destBuffer, srcBuffer, srcHdrLoc)) == INK_ERROR_PTR || destHdrLoc == NULL)
    LOG_ERROR_AND_CLEANUP("INKHttpHdrClone");
  if ((destUrl = INKUrlClone(destBuffer, srcBuffer, srcUrl)) == INK_ERROR_PTR || destUrl == NULL)
    LOG_ERROR_AND_CLEANUP("INKUrlClone");

  /* negative test for INKHttpHdrClone and INKUrlClone */
#ifdef DEBUG
  if (INKHttpHdrClone(NULL, srcBuffer, srcHdrLoc) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKHttpHdrClone");
  }
  if (INKHttpHdrClone(destBuffer, NULL, srcHdrLoc) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKHttpHdrClone");
  }
  if (INKHttpHdrClone(destBuffer, srcBuffer, NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKHttpHdrClone");
  }

  if (INKUrlClone(NULL, srcBuffer, srcUrl) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKUrlClone");
  }
  if (INKUrlClone(destBuffer, NULL, srcUrl) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKUrlClone");
  }
  if (INKUrlClone(destBuffer, srcBuffer, NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKUrlClone");
  }
#endif

  /* (4) output the cloned url */
  if ((outBuf2 = INKIOBufferCreate()) == INK_ERROR_PTR || outBuf2 == NULL)
    LOG_ERROR_AND_CLEANUP("INKIOBufferCreate");
  if (INKUrlPrint(destBuffer, destUrl, outBuf2) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKUrlPrint");

  printHeader(destBuffer, destUrl, outBuf2, sect++, str4);

  /* negative test for INKUrlPrint */
#ifdef DEBUG
  if (INKUrlPrint(NULL, destUrl, outBuf2) != INK_ERROR)
    LOG_ERROR_NEG("INKUrlPrint");
  if (INKUrlPrint(destBuffer, NULL, outBuf2) != INK_ERROR)
    LOG_ERROR_NEG("INKUrlPrint");
  if (INKUrlPrint(destBuffer, destUrl, NULL) != INK_ERROR)
    LOG_ERROR_NEG("INKUrlPrint");
#endif

  /* clean up */
Lcleanup:

  /* negative test for cleanup functions */
#ifdef DEBUG
  if (INKHttpParserDestroy(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpParserDestroy");
  }

  if (INKUrlDestroy(srcBuffer, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKUrlDestroy");
  }

  if (INKHttpHdrDestroy(srcBuffer, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpHdrDestroy");
  }
#endif

  /* destroy the parser */
  if (VALID_POINTER(parser))
    INKHttpParserDestroy(parser);

  /* destroy the output buffers */
  if (VALID_POINTER(outBuf1))
    INKIOBufferDestroy(outBuf1);
  if (VALID_POINTER(outBuf2))
    INKIOBufferDestroy(outBuf2);

  if (VALID_POINTER(srcUrl))
    INKUrlDestroy(srcBuffer, srcUrl);
  if (VALID_POINTER(srcUrl))
    INKHandleMLocRelease(srcBuffer, srcHdrLoc, srcUrl);

  if (VALID_POINTER(destUrl))
    INKUrlDestroy(destBuffer, destUrl);
  if (VALID_POINTER(destUrl))
    INKHandleMLocRelease(destBuffer, INK_NULL_MLOC, destUrl);

  if (VALID_POINTER(srcHdrLoc))
    INKHttpHdrDestroy(srcBuffer, srcHdrLoc);
  if (VALID_POINTER(srcHdrLoc))
    INKHandleMLocRelease(srcBuffer, INK_NULL_MLOC, srcHdrLoc);

  if (VALID_POINTER(destHdrLoc))
    INKHttpHdrDestroy(destBuffer, destHdrLoc);
  if (VALID_POINTER(destHdrLoc))
    INKHandleMLocRelease(destBuffer, INK_NULL_MLOC, destHdrLoc);

  if (VALID_POINTER(srcBuffer))
    INKMBufferDestroy(srcBuffer);
  if (VALID_POINTER(destBuffer))
    INKMBufferDestroy(destBuffer);
}

/******************************************************************************
 * call INKHttpHdrReasonLookup for each status and print out the default reason
 ******************************************************************************/
static int
httpHdrReasonHandler()
{
  LOG_SET_FUNCTION_NAME("httpHdrReasonHandler");
  const char *str;

#define CHECK_REASON_LOOKUP(R) {\
	str = INKHttpHdrReasonLookup(R); \
	if (str == INK_ERROR_PTR) \
	    LOG_ERROR_AND_RETURN(#R); \
	INKDebug(REASON_DEBUG_TAG, "%s: %s", #R, str); \
    }

  INKDebug(REASON_DEBUG_TAG, "********************** INK_HTTP_STATUS Reason ********************");

  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_NONE);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_CONTINUE);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_SWITCHING_PROTOCOL);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_OK);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_CREATED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_ACCEPTED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_NO_CONTENT);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_RESET_CONTENT);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_PARTIAL_CONTENT);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_MULTIPLE_CHOICES);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_MOVED_PERMANENTLY);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_MOVED_TEMPORARILY);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_SEE_OTHER);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_NOT_MODIFIED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_USE_PROXY);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_BAD_REQUEST);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_UNAUTHORIZED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_PAYMENT_REQUIRED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_FORBIDDEN);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_NOT_FOUND);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_METHOD_NOT_ALLOWED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_NOT_ACCEPTABLE);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_REQUEST_TIMEOUT);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_CONFLICT);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_GONE);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_LENGTH_REQUIRED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_PRECONDITION_FAILED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_REQUEST_ENTITY_TOO_LARGE);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_REQUEST_URI_TOO_LONG);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_INTERNAL_SERVER_ERROR);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_NOT_IMPLEMENTED);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_BAD_GATEWAY);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_SERVICE_UNAVAILABLE);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_GATEWAY_TIMEOUT);
  CHECK_REASON_LOOKUP(INK_HTTP_STATUS_HTTPVER_NOT_SUPPORTED);
}

void
INKPluginInit(int argc, const char *argv[])
{
  mimeHdrHandler();
  httpHdrHandler();
  httpHdrReasonHandler();
}
