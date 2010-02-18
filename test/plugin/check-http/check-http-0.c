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

/***************************************************************************************
 * check-http-0:
 *
 * Description: Covers HTTP section of Chap 7.
 *
 * APIs covered - 
 * - INKHttpHdrLengthGet
 * - INKHttpHdrMethodGet/Set
 * - INKHttpHdrReasonGet/Set
 * - INKHttpHdrStatusGet/Set
 * - INKHttpHdrTypeGet/Set
 * - INKHttpHdrVersionGet/Set
 * 
 * APIs not covered - 
 * - INKHttpHdrUrlGet/Set (covered in check-url-0)
 * - INKHttpHdrReasonLookup
 * - INKHttpHdrPrint (covered in output-hdr.c)
 ****************************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>

#if !defined (_WIN32)
#	include <unistd.h>
#else
#	include <windows.h>
#endif

#include "ts.h"
#include "macro.h"

#define STRING_SIZE 100
#define REQ "request"
#define RESP "response"
#define GENERAL "general"

#define AUTO_TAG "AUTO_ERROR"
#define DEBUG_TAG "ERROR"
#define NEG_ERROR_TAG "NEG_ERROR"
#define PLUGIN_NAME "check-http-0"


typedef struct
{
  INKHttpType httpType;
  int hdrLength;
  int httpVersion;

  /* REQUEST HDR */
  char *httpMethod;
  char *hostName;

  /* RESPONSE HDR */
  INKHttpStatus httpStatus;
  char *hdrReason;
} HdrInfo_T;


HdrInfo_T *
initHdr()
{
  HdrInfo_T *tmpHdr = NULL;

  tmpHdr = (HdrInfo_T *) INKmalloc(sizeof(HdrInfo_T));

  tmpHdr->httpType = INK_HTTP_TYPE_UNKNOWN;
  tmpHdr->hdrLength = 0;
  tmpHdr->httpVersion = 0;

  tmpHdr->httpMethod = NULL;
  tmpHdr->hostName = NULL;

  tmpHdr->hdrReason = NULL;

  return tmpHdr;
}

void
freeHdr(HdrInfo_T * hdrInfo)
{
  FREE(hdrInfo->httpMethod);
  FREE(hdrInfo->hostName);
  FREE(hdrInfo->hdrReason);

  FREE(hdrInfo);
}

void
negTesting(INKMBuffer hdrBuf, INKMLoc httpHdrLoc)
{
  LOG_SET_FUNCTION_NAME("negTesting");

  INKMBuffer negHdrBuf = NULL;
  INKMLoc negHttpHdrLoc = NULL;

  INKHttpType negType, hdrHttpType;
  INKHttpStatus httpStatus;

  const char *sHttpReason = NULL;
  int iHttpMethodLength, iHttpHdrReasonLength;

  /* INKMBufferCreate: Nothing to neg test */

  /* INKMBufferDestroy */
  if (INKMBufferDestroy(NULL) != INK_ERROR) {
    LOG_NEG_ERROR("INKMBufferDestroy");
  }

  /* INKHttpHdrCreate */
  if (INKHttpHdrCreate(NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKHttpHdrCreate");
  }

  /* INKHttpHdrCopy */
  /* Copy w/o creating the hdrBuf and httpHdrLoc */
  if (INKHttpHdrCopy(negHdrBuf, negHttpHdrLoc, hdrBuf, httpHdrLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrCopy");
  }

  /* valid create */
  if ((negHdrBuf = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrCreate");
  }
  if ((negHttpHdrLoc = INKHttpHdrCreate(negHdrBuf)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKMHttpHdrCreate");
  }

  if (INKHttpHdrCopy(NULL, negHttpHdrLoc, hdrBuf, httpHdrLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrCopy");
  }
  if (INKHttpHdrCopy(negHdrBuf, NULL, hdrBuf, httpHdrLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrCopy");
  }
  if (INKHttpHdrCopy(negHdrBuf, negHttpHdrLoc, NULL, httpHdrLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrCopy");
  }
  if (INKHttpHdrCopy(negHdrBuf, negHttpHdrLoc, hdrBuf, NULL) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrCopy");
  }

  /* INKHttpHdrTypeSet */
  /* Docs - INKHttpHdrTypeSet should NOT be called after INKHttpHdrCopy */
  /* Try some incorrect (but valid int type) arguments */
  if (INKHttpHdrTypeSet(negHdrBuf, negHttpHdrLoc, 10) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrTypeSet");
  }
  if (INKHttpHdrTypeSet(negHdrBuf, negHttpHdrLoc, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrTypeSet");
  }

  if (INKHttpHdrTypeSet(NULL, negHttpHdrLoc, INK_HTTP_TYPE_RESPONSE) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrTypeSet");
  }
  if (INKHttpHdrTypeSet(negHdrBuf, NULL, INK_HTTP_TYPE_RESPONSE) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrTypeSet");
  }
  /* INKqa12708 */
  if (INKHttpHdrTypeSet(negHdrBuf, negHttpHdrLoc, 100) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrTypeSet");
  }


  /* INKHtttpHdrTypeGet */
  if ((negType = INKHttpHdrTypeGet(NULL, negHttpHdrLoc)) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrTypeGet");
  }
  if ((negType = INKHttpHdrTypeGet(negHdrBuf, NULL)) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrTypeGet");
  }

  /* INKHttpHdrVersionGet */
  if (INKHttpHdrVersionGet(NULL, negHttpHdrLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrVersionGet");
  }
  if (INKHttpHdrVersionGet(negHdrBuf, NULL) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrVersionGet");
  }

  /* INKHttpHdrVersionSet */
  if (INKHttpHdrVersionSet(NULL, negHttpHdrLoc, INK_HTTP_VERSION(1, 1)) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrVersionSet");
  }
  if (INKHttpHdrVersionSet(negHdrBuf, NULL, INK_HTTP_VERSION(1, 1)) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrVersionSet");
  }
  /* Try some incorrect (but valid int type) arguments */
  if (INKHttpHdrVersionSet(negHdrBuf, negHttpHdrLoc, 0) == INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrVersionSet");
  }
  if (INKHttpHdrVersionSet(negHdrBuf, negHttpHdrLoc, -1) == INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrVersionSet");
  }

  /* INKHttpHdrLengthGet */
  if (INKHttpHdrLengthGet(NULL, negHttpHdrLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrLengthGet");
  }
  if (INKHttpHdrLengthGet(negHdrBuf, NULL) != INK_ERROR) {
    LOG_NEG_ERROR("INKHttpHdrLengthGet");
  }

  /* valid copy */
  if (INKHttpHdrCopy(negHdrBuf, negHttpHdrLoc, hdrBuf, httpHdrLoc) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrCopy");
  }

  if ((hdrHttpType = INKHttpHdrTypeGet(negHdrBuf, negHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrTypeGet");
  }

  if (hdrHttpType == INK_HTTP_TYPE_REQUEST) {
    /* INKHttpHdrUrlGet */
    if (INKHttpHdrUrlGet(NULL, negHttpHdrLoc) != INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrUrlGet");
    }
    if (INKHttpHdrUrlGet(negHdrBuf, NULL) != INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrUrlGet");
    }

    /* INKHttpHdrMethodGet */
    if (INKHttpHdrMethodGet(NULL, negHttpHdrLoc, &iHttpMethodLength) != INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrMethodGet");
    }
    if (INKHttpHdrMethodGet(negHdrBuf, NULL, &iHttpMethodLength) != INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrMethodGet");
    }
    if (INKHttpHdrMethodGet(negHdrBuf, negHttpHdrLoc, NULL) == INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrMethodGet");
    }

    /* INKHttpHdrMethodSet */
    if (INKHttpHdrMethodSet(NULL, negHttpHdrLoc, "FOOBAR", strlen("FOOBAR")) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrMethodSet");
    }
    if (INKHttpHdrMethodSet(negHdrBuf, NULL, "FOOBAR", strlen("FOOBAR")) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrMethodSet");
    }
    /* INKqa12722 */
    if (INKHttpHdrMethodSet(negHdrBuf, negHttpHdrLoc, NULL, -1) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrMethodSet");
    }
    /* FIXME:  This neg test would crash TS */
    /* NOTE: This is a valid (corner) test case */
    if (INKHttpHdrMethodSet(negHdrBuf, negHttpHdrLoc, "", -1) == INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrMethodSet");
    }

  } else if (hdrHttpType == INK_HTTP_TYPE_RESPONSE) {

    /* INKHttpHdrStatusGet */
    if (INKHttpHdrStatusGet(NULL, negHttpHdrLoc) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrStatusGet");
    }
    if (INKHttpHdrStatusGet(negHdrBuf, NULL) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrStatusGet");
    }

    /* INKHttpHdrStatusSet */
    /* valid status get */
    if ((httpStatus = INKHttpHdrStatusGet(negHdrBuf, negHttpHdrLoc)) == INK_ERROR) {
      LOG_API_ERROR("INKHttpHdrStatusGet");
    }

    if (INKHttpHdrStatusSet(NULL, negHttpHdrLoc, httpStatus) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrStatusSet");
    }
    if (INKHttpHdrStatusSet(negHdrBuf, NULL, httpStatus) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrStatusSet");
    }
    /* setting status = NULL is NOT an error */
    if (INKHttpHdrStatusSet(negHdrBuf, negHttpHdrLoc, -1) == INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrStatusSet");
    }

    /* INKHttpHdrReasonGet */
    /* valid reason get */
    if ((sHttpReason = INKHttpHdrReasonGet(negHdrBuf, negHttpHdrLoc, &iHttpHdrReasonLength))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrReasonGet");
    }

    if (INKHttpHdrReasonGet(NULL, negHttpHdrLoc, &iHttpHdrReasonLength) != INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrReasonGet");
    }
    if (INKHttpHdrReasonGet(negHdrBuf, NULL, &iHttpHdrReasonLength) != INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrReasonGet");
    }
    /* NULL is a valid length arg */
    if (INKHttpHdrReasonGet(negHdrBuf, negHttpHdrLoc, NULL) == INK_ERROR_PTR) {
      LOG_NEG_ERROR("INKHttpHdrReasonGet");
    }

    /* INKHttpHdrReasonSet */
    if (INKHttpHdrReasonSet(NULL, negHttpHdrLoc, sHttpReason, iHttpHdrReasonLength) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrReasonSet");
    }
    if (INKHttpHdrReasonSet(negHdrBuf, NULL, sHttpReason, iHttpHdrReasonLength) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrReasonSet");
    }

    /* NOTE: INKqa12722: NULL reason arg fixed now */
    if (INKHttpHdrReasonSet(negHdrBuf, negHttpHdrLoc, NULL, iHttpHdrReasonLength) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrReasonSet");
    }
    /* FIXME: INKqa12722 - This neg test would crash TS */
    if (INKHttpHdrReasonSet(negHdrBuf, negHttpHdrLoc, NULL, -1) != INK_ERROR) {
      LOG_NEG_ERROR("INKHttpHdrReasonSet");
    }

    STR_RELEASE(negHdrBuf, negHttpHdrLoc, sHttpReason);
  }

  /* Clean-up */
  HANDLE_RELEASE(negHdrBuf, INK_NULL_MLOC, negHttpHdrLoc);
  BUFFER_DESTROY(negHdrBuf);
}

/************************************************************************************
 * identical_hdr:
 *
 * DESCRIPTION: 
 *  Function to check whether 2 hdrInfos are identical (member to member)
 *
 * RETURN:
 *  0: If not identical
 *  1: If identical
 ************************************************************************************/
int
identical_hdr(HdrInfo_T * pHdrInfo1, HdrInfo_T * pHdrInfo2)
{
  LOG_SET_FUNCTION_NAME("identical_hdr");

  if (pHdrInfo1->httpType != pHdrInfo2->httpType) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "httpType different");
    return 0;
  } else if (pHdrInfo1->hdrLength != pHdrInfo2->hdrLength) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "hdrLength different");
    return 0;
  } else if (pHdrInfo1->httpVersion != pHdrInfo2->httpVersion) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "httpVersion different");
    return 0;
  } else if (pHdrInfo1->httpType == INK_HTTP_TYPE_REQUEST) {
    if (strcmp(pHdrInfo1->httpMethod, pHdrInfo2->httpMethod)) {
      LOG_AUTO_ERROR("INKHttpHdrCopy", "httpMethod different");
      return 0;
    } else if (strcmp(pHdrInfo1->hostName, pHdrInfo2->hostName)) {
      LOG_AUTO_ERROR("INKHttpHdrCopy", "hostName different");
      return 0;
    }
  } else if (pHdrInfo1->httpType == INK_HTTP_TYPE_RESPONSE) {
    if (pHdrInfo1->httpStatus != pHdrInfo2->httpStatus) {
      LOG_AUTO_ERROR("INKHttpHdrCopy", "httpStatus different");
      return 0;
    } else if (pHdrInfo1->hdrReason && strcmp(pHdrInfo1->hdrReason, pHdrInfo2->hdrReason)) {
      LOG_AUTO_ERROR("INKHttpHdrCopy", "hdrReason different");
      return 0;
    }
  } else {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "httpType still INK_HTTP_TYPE_UNKNOWN");
    return 0;
  }

  return 1;                     /* Both the headers are exactly identical */
}


/************************************************************************************
 * getHdrInfo:
 *
 * DESCRIPTION: 
 *  Function to store httpHdrBuffer (pointed by hdrBuf) information into pHdrInfo
 *
 * RETURN:
 *  void
 ************************************************************************************/
static void
getHdrInfo(HdrInfo_T * pHdrInfo, INKMBuffer hdrBuf, INKMLoc hdrLoc)
{
  LOG_SET_FUNCTION_NAME("getHdrInfo");
  INKMLoc urlLoc = NULL;

  const char *sHostName = NULL, *sHttpMethod = NULL, *sHttpHdrReason = NULL;
  int iHttpHdrReasonLength, iHttpMethodLength, iHostLength;

  if ((pHdrInfo->httpType = INKHttpHdrTypeGet(hdrBuf, hdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrTypeGet");
  }
  if ((pHdrInfo->hdrLength = INKHttpHdrLengthGet(hdrBuf, hdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrLengthGet");
  }
  if ((pHdrInfo->httpVersion = INKHttpHdrVersionGet(hdrBuf, hdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  }

  if (pHdrInfo->httpType == INK_HTTP_TYPE_REQUEST) {
    if ((sHttpMethod = INKHttpHdrMethodGet(hdrBuf, hdrLoc, &iHttpMethodLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrMethodGet");
    } else {
      pHdrInfo->httpMethod = INKstrndup(sHttpMethod, iHttpMethodLength);
    }

    if ((urlLoc = INKHttpHdrUrlGet(hdrBuf, hdrLoc)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrUrlGet");
    } else if ((sHostName = INKUrlHostGet(hdrBuf, urlLoc, &iHostLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKUrlHostGet");
    } else {
      pHdrInfo->hostName = INKstrndup(sHostName, iHostLength);
    }

    /* Clean-up */
    STR_RELEASE(hdrBuf, urlLoc, sHostName);
    STR_RELEASE(hdrBuf, urlLoc, sHttpMethod);
    HANDLE_RELEASE(hdrBuf, hdrLoc, urlLoc);

  } else if (pHdrInfo->httpType == INK_HTTP_TYPE_RESPONSE) {
    if ((pHdrInfo->httpStatus = INKHttpHdrStatusGet(hdrBuf, hdrLoc)) == INK_ERROR) {
      LOG_API_ERROR("INKHttpHdrStatusGet");
    }

    if ((sHttpHdrReason = INKHttpHdrReasonGet(hdrBuf, hdrLoc, &iHttpHdrReasonLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrReasonGet");
    } else if (sHttpHdrReason) {
      pHdrInfo->hdrReason = INKstrndup(sHttpHdrReason, iHttpHdrReasonLength);
    }

    /* clean-up */
    STR_RELEASE(hdrBuf, hdrLoc, sHttpHdrReason);
  } else {
    LOG_AUTO_ERROR("getHdrInfo", "httpType unknown");
  }
}

static void
printHttpHeader(INKMBuffer hdrBuf, INKMLoc hdrLoc, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printHttpHeader");

  INKMLoc urlLoc = NULL;

  INKHttpStatus httpStatus;
  INKHttpType httpType;

  int iHostLength, iHttpHdrLength, iHttpMethodLength, iHttpHdrReasonLength, iHttpVersion;
  const char *sHostName = NULL, *sHttpMethod = NULL, *sHttpHdrReason = NULL;
  char *outputString = NULL;


    /*** INKHttpHdrTypeGet ***/
  if ((httpType = INKHttpHdrTypeGet(hdrBuf, hdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrTypeGet");
  }
  INKDebug(debugTag, "(%g) HTTP Header Type = %d", section, httpType);

    /*** INKHttpHdrLengthGet ***/
  if ((iHttpHdrLength = INKHttpHdrLengthGet(hdrBuf, hdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrLengthGet");
  }
  INKDebug(debugTag, "(%g) HTTP Header Length = %d", section, iHttpHdrLength);

    /*** INKHttpVersionGet ***/
  if ((iHttpVersion = INKHttpHdrVersionGet(hdrBuf, hdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  }
  INKDebug(debugTag, "(%g) HTTP Header Version = %d", section, iHttpVersion);
  INKDebug(debugTag, "(%g) Major Version = %d, Minor Version = %d", section,
           INK_HTTP_MAJOR(iHttpVersion), INK_HTTP_MINOR(iHttpVersion));


  if (httpType == INK_HTTP_TYPE_REQUEST) {
        /*** INKHttpHdrMethodGet ***/
    if ((sHttpMethod = INKHttpHdrMethodGet(hdrBuf, hdrLoc, &iHttpMethodLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrMethodGet");
    } else {
      outputString = INKstrndup(sHttpMethod, iHttpMethodLength);
      INKDebug(debugTag, "(%g) HTTP Header Method = %s", section, outputString);
      FREE(outputString);
      STR_RELEASE(hdrBuf, urlLoc, sHttpMethod);
    }

        /*** INKHttpHdrUrlGet ***/
    if ((urlLoc = INKHttpHdrUrlGet(hdrBuf, hdrLoc)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrUrlGet");
    } else if ((sHostName = INKUrlHostGet(hdrBuf, urlLoc, &iHostLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKUrlHostGet");
    } else if (sHostName) {
      outputString = INKstrndup(sHostName, iHostLength);
      INKDebug(debugTag, "(%g) HTTP Host = %s", section, outputString);
      FREE(outputString);
      STR_RELEASE(hdrBuf, urlLoc, sHostName);
    }

    /* Clean-up */
    HANDLE_RELEASE(hdrBuf, hdrLoc, urlLoc);

  } else if (httpType == INK_HTTP_TYPE_RESPONSE) {

        /*** INKHttpHdrReasonGet ***/
    /* Try getting reason phrase from the request header - this is an error */
    if ((sHttpHdrReason = INKHttpHdrReasonGet(hdrBuf, hdrLoc, &iHttpHdrReasonLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrReasonGet");
    } else {
      outputString = INKstrndup(sHttpHdrReason, iHttpHdrReasonLength);
      INKDebug(debugTag, "(%g) HTTP Header Reason = %s", section, outputString);
      FREE(outputString);
      STR_RELEASE(hdrBuf, hdrLoc, sHttpHdrReason);
    }

        /*** INKHttpHdrStatusGet ***/
    /* Try getting status phrase from the request header - this is an error */
    if ((httpStatus = INKHttpHdrStatusGet(hdrBuf, hdrLoc)) == INK_ERROR) {
      LOG_API_ERROR("INKHttpHdrStatusGet");
    } else {
      INKDebug(debugTag, "(%g) HTTP Header Status = %d", section, httpStatus);
    }
  }
}



/************************************************************************
 * handleSendResponse:
 *
 * Description:	handler for INK_HTTP_SEND_RESPONSE_HOOK
 ************************************************************************/

static void
handleSendResponse(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleSendResponse");

  INKMBuffer respHdrBuf = NULL, newHttpHdrBuf = NULL, parseBuffer = NULL;
  INKMLoc respHttpHdrLoc = NULL, newHttpHdrLoc = NULL, parseHttpHdrLoc = NULL;

  INKHttpStatus oldHttpStatus, tmpHttpStatus;
  INKHttpType httpType;
  INKHttpParser httpRespParser = NULL;

  HdrInfo_T *pRespHdrInfo = NULL, *pNewRespHdrInfo = NULL;

  int iHttpHdrReasonLength, iOldHttpVersion, iTmpHttpVersion, iTmpHttpHdrReasonLength;
  const char *sHttpHdrReason = NULL, *sTmpHttpHdrReason = NULL, *pHttpParseStart = NULL, *pHttpParseEnd = NULL;
  char *sOldHttpReason = NULL;

  const char *sRespHdrStr1 =
    "HTTP/1.1 200 OK\r\nServer: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 12440\r\nVia: HTTP/1.1 ts-sun14 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *sRespHdrStr2 =
    "HTTP/1.1 404 Not Found \r\nServer: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 12440\r\nVia: HTTP/1.1 ts-sun24 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";

  const char *sRespHdrStr3 =
    "HTTP/1.1 505 HTTP Version Not Supported \r\nServer: Netscape-Enterprise/4.1\r\nDate: Tue, 31 Oct 2000 03:38:19 GMT\r\nContent-type: text/html\r\nAge: 3476\r\nContent-Length: 12440\r\nVia: HTTP/1.1 ts-sun34 (Traffic-Server/4.0.0 [cHs f ])\r\n\r\n";



  pRespHdrInfo = initHdr();
  pNewRespHdrInfo = initHdr();

  INKDebug(RESP, ">>> handleSendResponse <<<<\n");

  /* Get Response Marshall Buffer */
  if (!INKHttpTxnClientRespGet(pTxn, &respHdrBuf, &respHttpHdrLoc)) {
    LOG_API_ERROR_COMMENT("INKHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr");
    goto done;
  }

#ifdef DEBUG
  negTesting(respHdrBuf, respHttpHdrLoc);
#endif

    /******* (1): Exercise all possible INK*GET and print the values **********/

  INKDebug(RESP, "--------------------------------");
  getHdrInfo(pRespHdrInfo, respHdrBuf, respHttpHdrLoc);
  printHttpHeader(respHdrBuf, respHttpHdrLoc, RESP, 1);

    /******* (2): Create a new header and check everything is copied correctly *********/

  INKDebug(RESP, "--------------------------------");

  if ((newHttpHdrBuf = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKMBufferCreate", "skipping to section(4)");
    goto resp_4;
  }

    /*** INKHttpHdrCreate ***/
  if ((newHttpHdrLoc = INKHttpHdrCreate(newHttpHdrBuf)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKMHTTPHdrCreate", "skipping to section(4)");
    goto resp_4;
  }

  /* Make sure the newly created HTTP header has INKHttpType value of INK_HTTP_TYPE_UNKNOWN */
  if ((httpType = INKHttpHdrTypeGet(newHttpHdrBuf, newHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKMHTTPHdrCreate", "continuing");
  } else if (httpType != INK_HTTP_TYPE_UNKNOWN) {
    LOG_AUTO_ERROR("INKHttpHdrCreate", "Newly created hdr not of type INK_HTTP_TYPE_UNKNOWN");
  }


    /*** INKHttpHdrCopy ***/
  if (INKHttpHdrCopy(newHttpHdrBuf, newHttpHdrLoc, respHdrBuf, respHttpHdrLoc) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrCopy");
  }

  getHdrInfo(pNewRespHdrInfo, newHttpHdrBuf, newHttpHdrLoc);
  printHttpHeader(newHttpHdrBuf, newHttpHdrLoc, RESP, 2);

  if (!identical_hdr(pRespHdrInfo, pNewRespHdrInfo)) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "copy of the resp header not identical to the original");
  }

  /* Reuse:
   * newHttpHdrBuf, newHttHdrLoc */

    /******* (3): Now excercise some INK..SETs on the new header ********/
  INKDebug(RESP, "--------------------------------");

    /*** INKHttpHdrTypeSet ***/
  /* ERROR: 
   * 1. Setting type other than INK_HTTP_TYPE_UNKNOWN, INK_HTTP_TYPE_REQUEST, 
   * INK_HTTP_TYPE_RESPONSE, and,
   * 2. Setting the type twice.  The hdr type has been already set during INKHttpHdrCopy 
   * above, so setting it again is incorrect */
  if (INKHttpHdrTypeSet(newHttpHdrBuf, newHttpHdrLoc, INK_HTTP_TYPE_RESPONSE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrTypeSet");
  }

    /*** INKHttpHdrReasonSet ***/
  /* save the original reason */
  if ((sHttpHdrReason = INKHttpHdrReasonGet(newHttpHdrBuf, newHttpHdrLoc, &iHttpHdrReasonLength))
      == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrReasonGet");
  } else {
    sOldHttpReason = INKstrndup(sHttpHdrReason, iHttpHdrReasonLength);
  }

  /* Note: 
   * INKHttpHdrReasonGet may return a NULL reason string (for e.g. I tried www.eyesong.8m.com).
   * Do NOT assume that INKstrndup always returns a null terminated string.  INKstrndup does 
   * not returns a NULL terminated string for a NULL ptr as i/p parameter.  It simply returns 
   * it backs. So functions like strlen() on the return value might cause TS to crash */


  if (INKHttpHdrReasonSet(newHttpHdrBuf, newHttpHdrLoc, "dummy reason", strlen("dummy reason")) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrReasonGet");
  } else {
    if ((sTmpHttpHdrReason = INKHttpHdrReasonGet(newHttpHdrBuf, newHttpHdrLoc, &iTmpHttpHdrReasonLength))
        == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrReasonGet");
    } else if (sTmpHttpHdrReason && strncmp(sTmpHttpHdrReason, "dummy reason", iTmpHttpHdrReasonLength)) {
      LOG_AUTO_ERROR("INKHttpHdrReasonSet/Get", "GET reason different from the SET reason");
    }
    STR_RELEASE(newHttpHdrBuf, newHttpHdrLoc, sTmpHttpHdrReason);
  }

    /*** INKHttpStatusSet ***/
  /* save the original value */
  if ((oldHttpStatus = INKHttpHdrStatusGet(newHttpHdrBuf, newHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusGet");
  }

  /* change it to some unknown value */
  if (INKHttpHdrStatusSet(newHttpHdrBuf, newHttpHdrLoc, INK_HTTP_STATUS_NONE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusSet");
  } else if ((tmpHttpStatus = INKHttpHdrStatusGet(newHttpHdrBuf, newHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusGet");
  } else if (tmpHttpStatus != INK_HTTP_STATUS_NONE) {
    LOG_AUTO_ERROR("INKHttpHdrStatusGet/Set", "GET status different from the SET status");
  }


    /*** INKHttpHdrVersionSet ***/
  /* get the original version */
  if ((iOldHttpVersion = INKHttpHdrVersionGet(newHttpHdrBuf, newHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  }

  /* change it to some unknown version */
  if (INKHttpHdrVersionSet(newHttpHdrBuf, newHttpHdrLoc, INK_HTTP_VERSION(10, 10)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionSet");
  } else if ((iTmpHttpVersion = INKHttpHdrVersionGet(newHttpHdrBuf, newHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  } else if (INK_HTTP_MAJOR(iTmpHttpVersion) != 10 && INK_HTTP_MINOR(iTmpHttpVersion) != 10) {
    LOG_AUTO_ERROR("INKHttpHdrVersionSet", "GET version different from SET version");
  }

  printHttpHeader(newHttpHdrBuf, newHttpHdrLoc, RESP, 3);

  /* Restore the original values */

  /* Here we can't use strlen(sOldHttpReason) to set the length.  This would crash TS if 
   * sOldHttpReason happens to be NULL */
  if (INKHttpHdrReasonSet(newHttpHdrBuf, newHttpHdrLoc, sOldHttpReason, iHttpHdrReasonLength) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrReasonSet");
  }
  /*INKHttpHdrReasonSet (newHttpHdrBuf, newHttpHdrLoc, sOldHttpReason, strlen(sOldHttpReason)); */
  if (INKHttpHdrStatusSet(newHttpHdrBuf, newHttpHdrLoc, oldHttpStatus) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusSet");
  }
  if (INKHttpHdrVersionSet(newHttpHdrBuf, newHttpHdrLoc, iOldHttpVersion) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusSet");
  }

  if (!identical_hdr(pRespHdrInfo, pNewRespHdrInfo)) {
    LOG_AUTO_ERROR("INK..SET", "Hdr values not properly restored");
  }

  /* (3): clean-up */
  STR_RELEASE(newHttpHdrBuf, newHttpHdrLoc, sHttpHdrReason);
  FREE(sOldHttpReason);

resp_4:
    /******* (4): Now excercise some SETs on the response header ********/
  INKDebug(RESP, "--------------------------------");

    /*** INKHttpHdrReasonSet ***/
  /* save the original reason */
  if ((sHttpHdrReason = INKHttpHdrReasonGet(respHdrBuf, respHttpHdrLoc, &iHttpHdrReasonLength))
      == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrReasonGet");
  } else {
    sOldHttpReason = INKstrndup(sHttpHdrReason, iHttpHdrReasonLength);
  }

  /* change the reason phrase */
  if (INKHttpHdrReasonSet(respHdrBuf, respHttpHdrLoc, "dummy reason", strlen("dummy reason")) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrReasonSet");
  }

  if ((sTmpHttpHdrReason = INKHttpHdrReasonGet(respHdrBuf, respHttpHdrLoc, &iTmpHttpHdrReasonLength))
      == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrReasonGet");
  } else if (sTmpHttpHdrReason && strncmp(sTmpHttpHdrReason, "dummy reason", iTmpHttpHdrReasonLength)) {
    LOG_AUTO_ERROR("INKHttpHdrReasonSet/Get", "GET reason string different from SET reason");
  }
  STR_RELEASE(respHdrBuf, respHttpHdrLoc, sTmpHttpHdrReason);

    /*** INKHttpStatusSet ***/
  /* save the original value */
  if ((oldHttpStatus = INKHttpHdrStatusGet(respHdrBuf, respHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusGet");
  }

  /* change it to some unknown value */
  if (INKHttpHdrStatusSet(respHdrBuf, respHttpHdrLoc, INK_HTTP_STATUS_NONE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusSet");
  } else if (INKHttpHdrStatusGet(respHdrBuf, respHttpHdrLoc) != INK_HTTP_STATUS_NONE) {
    LOG_AUTO_ERROR("INKHttpHdrStatusSet/GET", "GET status value different from SET status");
  }


    /*** INKHttpHdrTypeSet ***/
  /* ERROR: 
   * 1. Setting type other than INK_HTTP_TYPE_UNKNOWN, INK_HTTP_TYPE_REQUEST, 
   * INK_HTTP_TYPE_RESPONSE and,
   * 2. Setting the type twice.  The hdr type has been already set during INKHttpTxnClientRespGet
   * above, so setting it again should fail */
  if (INKHttpHdrTypeSet(respHdrBuf, respHttpHdrLoc, INK_HTTP_TYPE_RESPONSE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrTypeSet");
  }
  if (INKHttpHdrTypeGet(respHdrBuf, respHttpHdrLoc) == INK_HTTP_TYPE_UNKNOWN) {
    LOG_AUTO_ERROR("INKHttpHdrTypeSet/Get", "respHdrBuf CAN be set to INK_HTTP_TYPE_UNKNOWN");
  }

    /*** INKHttpHdrVersionSet ***/
  /* get the original version */
  if ((iOldHttpVersion = INKHttpHdrVersionGet(respHdrBuf, respHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  }

  /* change it to some unknown version */
  if (INKHttpHdrVersionSet(respHdrBuf, respHttpHdrLoc, INK_HTTP_VERSION(10, 10)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionSet");
  } else if ((iTmpHttpVersion = INKHttpHdrVersionGet(respHdrBuf, respHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  } else if (INK_HTTP_MAJOR(iTmpHttpVersion) != 10 && INK_HTTP_MINOR(iTmpHttpVersion) != 10) {
    LOG_AUTO_ERROR("INKHttpHdrVersionGet/Set", "GET HTTP version different from SET version");
  }

  printHttpHeader(respHdrBuf, respHttpHdrLoc, RESP, 4);

  /* restore the original values */

  /* For INKHttpHdrReasonSet, do NOT use strlen(sOldHttpReason) to set the length.  
   * This would crash TS if sOldHttpReason happened to be NULL */
  if (INKHttpHdrReasonSet(respHdrBuf, respHttpHdrLoc, sOldHttpReason, iHttpHdrReasonLength) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrReasonSet");
  }
  /*INKHttpHdrReasonSet (respHdrBuf, respHttpHdrLoc, sOldHttpReason, strlen(sOldHttpReason)); */
  if (INKHttpHdrStatusSet(respHdrBuf, respHttpHdrLoc, oldHttpStatus) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrStatusSet");
  }
  if (INKHttpHdrVersionSet(respHdrBuf, respHttpHdrLoc, iOldHttpVersion) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionSet");
  }

  FREE(pNewRespHdrInfo->hdrReason);
  getHdrInfo(pNewRespHdrInfo, respHdrBuf, respHttpHdrLoc);

  if (!identical_hdr(pRespHdrInfo, pNewRespHdrInfo)) {
    LOG_AUTO_ERROR("INK..SET", "Hdr values not properly restored");
  }

  /* (4): clean-up */
  STR_RELEASE(respHdrBuf, respHttpHdrLoc, sHttpHdrReason);
  FREE(sOldHttpReason);

    /********************************/
    /** (5): INKHttpHdrParseResp   **/
    /********************************/

  INKDebug(RESP, "--------------------------------");

  /* Create a parser Buffer and header location */
  if ((parseBuffer = INKMBufferCreate()) == INK_ERROR_PTR || parseBuffer == NULL) {
    LOG_API_ERROR_COMMENT("INKMBufferCreate", "abnormal exit");
    goto done;
  } else if ((parseHttpHdrLoc = INKHttpHdrCreate(parseBuffer)) == INK_ERROR_PTR || parseHttpHdrLoc == NULL) {
    LOG_API_ERROR_COMMENT("INKHttpHdrCreate", "abnormal exit");
    goto done;
  }

  pHttpParseStart = sRespHdrStr1;
  pHttpParseEnd = pHttpParseStart + strlen(pHttpParseStart);

  httpRespParser = INKHttpParserCreate();

  if (INKHttpHdrParseResp(httpRespParser, parseBuffer, parseHttpHdrLoc, &pHttpParseStart, pHttpParseEnd)
      == INK_PARSE_ERROR) {
    LOG_API_ERROR("INKHttpHdrParseResp");
  }

  printHttpHeader(parseBuffer, parseHttpHdrLoc, RESP, 5.1);

  if (INKHttpParserClear(httpRespParser) == INK_ERROR) {
    LOG_API_ERROR("INKHttpParseClear");
  }

  INKDebug(RESP, "--------------------------------");

  pHttpParseStart = sRespHdrStr2;
  pHttpParseEnd = pHttpParseStart + strlen(pHttpParseStart);

  /* httpRespParser = INKHttpParserCreate(); */

  if (INKHttpHdrParseResp(httpRespParser, parseBuffer, parseHttpHdrLoc, &pHttpParseStart, pHttpParseEnd)
      == INK_PARSE_ERROR) {
    LOG_API_ERROR("INKHttpHdrParseResp");
  }

  printHttpHeader(parseBuffer, parseHttpHdrLoc, RESP, 5.2);

  if (INKHttpParserClear(httpRespParser) == INK_ERROR) {
    LOG_API_ERROR("INKHttpParseClear");
  }

  INKDebug(RESP, "--------------------------------");

  pHttpParseStart = sRespHdrStr3;
  pHttpParseEnd = pHttpParseStart + strlen(pHttpParseStart);

  /* httpRespParser = INKHttpParserCreate(); */

  if (INKHttpHdrParseResp(httpRespParser, parseBuffer, parseHttpHdrLoc, &pHttpParseStart, pHttpParseEnd)
      == INK_PARSE_ERROR) {
    LOG_API_ERROR("INKHttpHdrParseResp");
  }

  printHttpHeader(parseBuffer, parseHttpHdrLoc, RESP, 5.3);


done:
  /* Clean-up */
  freeHdr(pRespHdrInfo);
  freeHdr(pNewRespHdrInfo);

  /* release hdrLoc */
  HANDLE_RELEASE(respHdrBuf, INK_NULL_MLOC, respHttpHdrLoc);
  HANDLE_RELEASE(newHttpHdrBuf, INK_NULL_MLOC, newHttpHdrLoc);
  HANDLE_RELEASE(parseBuffer, INK_NULL_MLOC, parseHttpHdrLoc);

  /* destroy hdrLoc */
  HDR_DESTROY(respHdrBuf, respHttpHdrLoc);
  HDR_DESTROY(parseBuffer, parseHttpHdrLoc);

  /* destroy mbuffer */
  BUFFER_DESTROY(newHttpHdrBuf);
  BUFFER_DESTROY(parseBuffer);

  /* destroy the parser */
  if (INKHttpParserDestroy(httpRespParser) == INK_ERROR) {
    LOG_API_ERROR("INKHttpParserDestroy");
  }

  if (INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpTxnReenable");
  }

  INKDebug(RESP, "......... exiting handleRespResponse .............\n");

}                               /* handleSendResponse */




/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for INK_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleReadRequest");

  INKMBuffer reqHdrBuf = NULL, newHttpHdrBuf = NULL;
  INKMLoc reqHdrLoc = NULL, newHttpHdrLoc = NULL;

  INKHttpType httpType;

  int iOldHttpVersion, iHttpMethodLength, iHttpVersion;
  const char *sHttpMethod = NULL;
  char *outputString = NULL, *sOldHttpMethod = NULL;

  HdrInfo_T *pReqHdrInfo = NULL, *pNewReqHdrInfo = NULL;

#if 0
  const char *constant_request_header_str =
    "GET http://www.joes-hardware.com/ HTTP/1.0\r\nDate: Wed, 05 Jul 2000 22:12:26 GMT\r\nConnection: Keep-Alive\r\nUser-Agent: Mozilla/4.51 [en] (X11; U; IRIX 6.2 IP22)\r\nHost: www.joes-hardware.com\r\nCache-Control: no-cache\r\nAccept: image/gif, image/x-xbitmap, image/jpeg, image/pjpeg, image/png, */*\r\nAccept-Charset: iso-8859-1,*,utf-8\r\nAccept-Encoding: gzip\r\nAccept-Language: en\r\nX-Number-Header: 12345\r\nX-Silly-Header: frobnichek grobbledegook\r\nAccept-Charset: windows-1250, koi8-r\r\nX-Silly-Header: wawaaa\r\n\r\n";

#endif


  pReqHdrInfo = initHdr();
  pNewReqHdrInfo = initHdr();

  INKDebug(REQ, "\n>>>>>> handleReadRequest <<<<<<<\n");

  /* Get Request Marshall Buffer */
  if (!INKHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHdrLoc)) {
    LOG_API_ERROR_COMMENT("INKHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr");
    goto done;
  }


    /******* (1): Get every specifics from the HTTP header *********/

  INKDebug(REQ, "--------------------------------");

  getHdrInfo(pReqHdrInfo, reqHdrBuf, reqHdrLoc);
  printHttpHeader(reqHdrBuf, reqHdrLoc, REQ, 1);

#ifdef DEBUG
  negTesting(reqHdrBuf, reqHdrLoc);
#endif

    /*********** (2): Create/Copy/Destroy **********/
  /* For every request, create, copy and destroy a new HTTP header and 
   * print the details */

  INKDebug(REQ, "--------------------------------");
  if ((newHttpHdrBuf = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKMBufferCreate", "skipping to section 3");
    goto section_3;             /* Skip to section (3) down the line directly; I hate GOTOs */
  }

    /*** INKHttpHdrCreate ***/
  if ((newHttpHdrLoc = INKHttpHdrCreate(newHttpHdrBuf)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrCreate", "skipping to section 3");
    goto section_3;             /* Skip to section (3) down the line directly; I hate GOTOs */
  }

  /* Make sure the newly created HTTP header has INKHttpType value of INK_HTTP_TYPE_UNKNOWN */
  if ((httpType = INKHttpHdrTypeGet(newHttpHdrBuf, newHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrTypeGet", "but still continuing...");
  } else if (httpType != INK_HTTP_TYPE_UNKNOWN) {
    LOG_AUTO_ERROR("INKHttpHdrCreate", "Newly created hdr not of type INK_HTTP_TYPE_UNKNOWN");
  }

  /* set the HTTP header type: a new buffer has a type INK_HTTP_TYPE_UNKNOWN by default */
  if (INKHttpHdrTypeSet(newHttpHdrBuf, newHttpHdrLoc, INK_HTTP_TYPE_REQUEST) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrTypeSet", "unable to set it to INK_HTTP_TYPE_REQUEST");
  } else if ((httpType = INKHttpHdrTypeGet(newHttpHdrBuf, newHttpHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrTypeGet", "still continuing");
  } else if (httpType != INK_HTTP_TYPE_REQUEST) {
    LOG_AUTO_ERROR("INKHttpHdrTypeSet", "Type not set to INK_HTTP_TYPE_REQUEST");
  }

    /*** INKHttpHdrCopy ***/
  if (INKHttpHdrCopy(newHttpHdrBuf, newHttpHdrLoc, reqHdrBuf, reqHdrLoc) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrCopy");
  }
  getHdrInfo(pNewReqHdrInfo, newHttpHdrBuf, newHttpHdrLoc);

  if (!identical_hdr(pNewReqHdrInfo, pReqHdrInfo)) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "New req buffer not identical to the original");
  }

  printHttpHeader(newHttpHdrBuf, newHttpHdrLoc, REQ, 2);

  FREE(pNewReqHdrInfo->httpMethod);
  FREE(pNewReqHdrInfo->hostName);

section_3:
    /********* (3): Excercise all the INK__Set on ReqBuf *********/
  INKDebug(REQ, "--------------------------------");

    /*** INKHttpHdrMethodSet ***/
  /* save the original method */
  if ((sHttpMethod = INKHttpHdrMethodGet(reqHdrBuf, reqHdrLoc, &iHttpMethodLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrMethodGet");
  } else {
    sOldHttpMethod = INKstrndup(sHttpMethod, iHttpMethodLength);
  }
  /* change it to some unknown method */
  if (INKHttpHdrMethodSet(reqHdrBuf, reqHdrLoc, "FOOBAR", strlen("FOOBAR")) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrMethodSet");
  } else {
    if ((sHttpMethod = INKHttpHdrMethodGet(reqHdrBuf, reqHdrLoc, &iHttpMethodLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrMethodGet");
    } else if (strncmp(sHttpMethod, "FOOBAR", iHttpMethodLength)) {
      LOG_AUTO_ERROR("INKHttpHdrMethodSet/Get", "GET method different from SET method");
    }
  }

  outputString = INKstrndup(sHttpMethod, iHttpMethodLength);
  INKDebug(REQ, "(3): new HTTP Header Method = %s", outputString);
  FREE(outputString);
  STR_RELEASE(reqHdrBuf, reqHdrLoc, sHttpMethod);

  printHttpHeader(reqHdrBuf, reqHdrLoc, REQ, 3);

  /* set it back to the original method */
  /*INKHttpHdrMethodSet (reqHdrBuf, reqHdrLoc, sOldHttpMethod, iHttpMethodLength); */
  if (INKHttpHdrMethodSet(reqHdrBuf, reqHdrLoc, sOldHttpMethod, strlen(sOldHttpMethod)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrMethodSet");
  } else if ((sHttpMethod = INKHttpHdrMethodGet(reqHdrBuf, reqHdrLoc, &iHttpMethodLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrMethodGet");
  } else if (strncmp(sHttpMethod, sOldHttpMethod, iHttpMethodLength)) {
    LOG_AUTO_ERROR("INKHttpHdrMethodSet/Get", "GET method different from SET method");
  }

  outputString = INKstrndup(sHttpMethod, iHttpMethodLength);
  INKDebug(REQ, "(3): original HTTP Header Method = %s", outputString);
  FREE(outputString);
  STR_RELEASE(reqHdrBuf, reqHdrLoc, sHttpMethod);


    /*** INKHttpHdrVersionSet ***/
  /* get the original version */
  if ((iOldHttpVersion = INKHttpHdrVersionGet(reqHdrBuf, reqHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  }

  /* change it to some unknown version */
  if (INKHttpHdrVersionSet(reqHdrBuf, reqHdrLoc, INK_HTTP_VERSION(10, 10)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionSet");
  } else if ((iHttpVersion = INKHttpHdrVersionGet(reqHdrBuf, reqHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  } else if ((INK_HTTP_MAJOR(iHttpVersion) != 10) || (INK_HTTP_MINOR(iHttpVersion) != 10)) {
    LOG_AUTO_ERROR("INKHttpHdrVersionSet/Get", "SET HTTP version different from GET version");
  }

  INKDebug(REQ, "(3): new HTTP version; Major = %d   Minor = %d",
           INK_HTTP_MAJOR(iHttpVersion), INK_HTTP_MINOR(iHttpVersion));

  /* change it back to the original version */
  if (INKHttpHdrVersionSet(reqHdrBuf, reqHdrLoc, iOldHttpVersion) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionSet");
  } else if ((iHttpVersion = INKHttpHdrVersionGet(reqHdrBuf, reqHdrLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrVersionGet");
  } else if (iHttpVersion != iOldHttpVersion) {
    LOG_AUTO_ERROR("INKHttpHdrVersionSet/Get", "SET HTTP version different from GET version");
  }

  getHdrInfo(pNewReqHdrInfo, reqHdrBuf, reqHdrLoc);
  if (!identical_hdr(pNewReqHdrInfo, pReqHdrInfo)) {
    LOG_AUTO_ERROR("INK..Set", "ReqBuf: Values not restored properly");
  }

  /* (3): clean-up */
  FREE(sOldHttpMethod);

done:
    /*************** Clean-up ***********************/
  /*
     FREE(pReqHdrInfo->httpMethod);
     FREE(pReqHdrInfo->hostName);

     FREE(pNewReqHdrInfo->httpMethod);
     FREE(pNewReqHdrInfo->hostName);

     FREE(pReqHdrInfo);
     FREE(pNewReqHdrInfo);
   */
  freeHdr(pReqHdrInfo);
  freeHdr(pNewReqHdrInfo);

  /* release hdrLoc */
  HANDLE_RELEASE(reqHdrBuf, INK_NULL_MLOC, reqHdrLoc);
  HANDLE_RELEASE(newHttpHdrBuf, INK_NULL_MLOC, newHttpHdrLoc);

  /* destroy hdr */
  HDR_DESTROY(newHttpHdrBuf, newHttpHdrLoc);

  /* destroy mbuffer */
  BUFFER_DESTROY(newHttpHdrBuf);


  if (INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpTxnReenable");
  }
  INKDebug(REQ, "..... exiting handleReadRequest ......\n");

}                               /* handleReadReadRequest */



static void
handleTxnStart(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleTxnStart");

  if (INKHttpTxnHookAdd(pTxn, INK_HTTP_READ_REQUEST_HDR_HOOK, pCont) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHookAdd");
  }

  if (INKHttpTxnHookAdd(pTxn, INK_HTTP_SEND_RESPONSE_HDR_HOOK, pCont) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHookAdd");
  }

  if (INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpTxnReenable");
  }
}


static int
contHandler(INKCont pCont, INKEvent event, void *edata)
{
  INKHttpTxn pTxn = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    handleTxnStart(pCont, pTxn);
    return 0;
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    handleReadRequest(pCont, pTxn);
    return 0;
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    handleSendResponse(pCont, pTxn);
    return 0;
  default:
    break;
  }

  return 0;
}



void
INKPluginInit(int argc, const char *argv[])
{
  INKCont pCont;

  LOG_SET_FUNCTION_NAME("INKPluginInit");

  if ((pCont = INKContCreate(contHandler, NULL)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKContCreate")
  } else if (INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, pCont) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHookAdd");
  }
}
