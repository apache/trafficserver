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
 * - TSHttpHdrLengthGet
 * - TSHttpHdrMethodGet/Set
 * - TSHttpHdrReasonGet/Set
 * - TSHttpHdrStatusGet/Set
 * - TSHttpHdrTypeGet/Set
 * - TSHttpHdrVersionGet/Set
 *
 * APIs not covered -
 * - TSHttpHdrUrlGet/Set (covered in check-url-0)
 * - TSHttpHdrReasonLookup
 * - TSHttpHdrPrint (covered in output-hdr.c)
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
  TSHttpType httpType;
  int hdrLength;
  int httpVersion;

  /* REQUEST HDR */
  char *httpMethod;
  char *hostName;

  /* RESPONSE HDR */
  TSHttpStatus httpStatus;
  char *hdrReason;
} HdrInfo_T;


HdrInfo_T *
initHdr()
{
  HdrInfo_T *tmpHdr = NULL;

  tmpHdr = (HdrInfo_T *) TSmalloc(sizeof(HdrInfo_T));

  tmpHdr->httpType = TS_HTTP_TYPE_UNKNOWN;
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
negTesting(TSMBuffer hdrBuf, TSMLoc httpHdrLoc)
{
  LOG_SET_FUNCTION_NAME("negTesting");

  TSMBuffer negHdrBuf = NULL;
  TSMLoc negHttpHdrLoc = NULL;

  TSHttpType negType, hdrHttpType;
  TSHttpStatus httpStatus;

  const char *sHttpReason = NULL;
  int iHttpMethodLength, iHttpHdrReasonLength;

  /* TSMBufferCreate: Nothing to neg test */

  /* TSMBufferDestroy */
  if (TSMBufferDestroy(NULL) != TS_ERROR) {
    LOG_NEG_ERROR("TSMBufferDestroy");
  }

  /* TSHttpHdrCreate */
  if (TSHttpHdrCreate(NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSHttpHdrCreate");
  }

  /* TSHttpHdrCopy */
  /* Copy w/o creating the hdrBuf and httpHdrLoc */
  if (TSHttpHdrCopy(negHdrBuf, negHttpHdrLoc, hdrBuf, httpHdrLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrCopy");
  }

  /* valid create */
  if ((negHdrBuf = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrCreate");
  }
  if ((negHttpHdrLoc = TSHttpHdrCreate(negHdrBuf)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSMHttpHdrCreate");
  }

  if (TSHttpHdrCopy(NULL, negHttpHdrLoc, hdrBuf, httpHdrLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrCopy");
  }
  if (TSHttpHdrCopy(negHdrBuf, NULL, hdrBuf, httpHdrLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrCopy");
  }
  if (TSHttpHdrCopy(negHdrBuf, negHttpHdrLoc, NULL, httpHdrLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrCopy");
  }
  if (TSHttpHdrCopy(negHdrBuf, negHttpHdrLoc, hdrBuf, NULL) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrCopy");
  }

  /* TSHttpHdrTypeSet */
  /* Docs - TSHttpHdrTypeSet should NOT be called after TSHttpHdrCopy */
  /* Try some incorrect (but valid int type) arguments */
  if (TSHttpHdrTypeSet(negHdrBuf, negHttpHdrLoc, 10) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrTypeSet");
  }
  if (TSHttpHdrTypeSet(negHdrBuf, negHttpHdrLoc, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrTypeSet");
  }

  if (TSHttpHdrTypeSet(NULL, negHttpHdrLoc, TS_HTTP_TYPE_RESPONSE) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrTypeSet");
  }
  if (TSHttpHdrTypeSet(negHdrBuf, NULL, TS_HTTP_TYPE_RESPONSE) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrTypeSet");
  }
  /* TSqa12708 */
  if (TSHttpHdrTypeSet(negHdrBuf, negHttpHdrLoc, 100) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrTypeSet");
  }


  /* TSHtttpHdrTypeGet */
  if ((negType = TSHttpHdrTypeGet(NULL, negHttpHdrLoc)) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrTypeGet");
  }
  if ((negType = TSHttpHdrTypeGet(negHdrBuf, NULL)) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrTypeGet");
  }

  /* TSHttpHdrVersionGet */
  if (TSHttpHdrVersionGet(NULL, negHttpHdrLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrVersionGet");
  }
  if (TSHttpHdrVersionGet(negHdrBuf, NULL) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrVersionGet");
  }

  /* TSHttpHdrVersionSet */
  if (TSHttpHdrVersionSet(NULL, negHttpHdrLoc, TS_HTTP_VERSION(1, 1)) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrVersionSet");
  }
  if (TSHttpHdrVersionSet(negHdrBuf, NULL, TS_HTTP_VERSION(1, 1)) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrVersionSet");
  }
  /* Try some incorrect (but valid int type) arguments */
  if (TSHttpHdrVersionSet(negHdrBuf, negHttpHdrLoc, 0) == TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrVersionSet");
  }
  if (TSHttpHdrVersionSet(negHdrBuf, negHttpHdrLoc, -1) == TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrVersionSet");
  }

  /* TSHttpHdrLengthGet */
  if (TSHttpHdrLengthGet(NULL, negHttpHdrLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrLengthGet");
  }
  if (TSHttpHdrLengthGet(negHdrBuf, NULL) != TS_ERROR) {
    LOG_NEG_ERROR("TSHttpHdrLengthGet");
  }

  /* valid copy */
  if (TSHttpHdrCopy(negHdrBuf, negHttpHdrLoc, hdrBuf, httpHdrLoc) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrCopy");
  }

  if ((hdrHttpType = TSHttpHdrTypeGet(negHdrBuf, negHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrTypeGet");
  }

  if (hdrHttpType == TS_HTTP_TYPE_REQUEST) {
    /* TSHttpHdrUrlGet */
    if (TSHttpHdrUrlGet(NULL, negHttpHdrLoc) != TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrUrlGet");
    }
    if (TSHttpHdrUrlGet(negHdrBuf, NULL) != TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrUrlGet");
    }

    /* TSHttpHdrMethodGet */
    if (TSHttpHdrMethodGet(NULL, negHttpHdrLoc, &iHttpMethodLength) != TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrMethodGet");
    }
    if (TSHttpHdrMethodGet(negHdrBuf, NULL, &iHttpMethodLength) != TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrMethodGet");
    }
    if (TSHttpHdrMethodGet(negHdrBuf, negHttpHdrLoc, NULL) == TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrMethodGet");
    }

    /* TSHttpHdrMethodSet */
    if (TSHttpHdrMethodSet(NULL, negHttpHdrLoc, "FOOBAR", strlen("FOOBAR")) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrMethodSet");
    }
    if (TSHttpHdrMethodSet(negHdrBuf, NULL, "FOOBAR", strlen("FOOBAR")) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrMethodSet");
    }
    /* TSqa12722 */
    if (TSHttpHdrMethodSet(negHdrBuf, negHttpHdrLoc, NULL, -1) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrMethodSet");
    }
    /* FIXME:  This neg test would crash TS */
    /* NOTE: This is a valid (corner) test case */
    if (TSHttpHdrMethodSet(negHdrBuf, negHttpHdrLoc, "", -1) == TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrMethodSet");
    }

  } else if (hdrHttpType == TS_HTTP_TYPE_RESPONSE) {

    /* TSHttpHdrStatusGet */
    if (TSHttpHdrStatusGet(NULL, negHttpHdrLoc) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrStatusGet");
    }
    if (TSHttpHdrStatusGet(negHdrBuf, NULL) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrStatusGet");
    }

    /* TSHttpHdrStatusSet */
    /* valid status get */
    if ((httpStatus = TSHttpHdrStatusGet(negHdrBuf, negHttpHdrLoc)) == TS_ERROR) {
      LOG_API_ERROR("TSHttpHdrStatusGet");
    }

    if (TSHttpHdrStatusSet(NULL, negHttpHdrLoc, httpStatus) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrStatusSet");
    }
    if (TSHttpHdrStatusSet(negHdrBuf, NULL, httpStatus) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrStatusSet");
    }
    /* setting status = NULL is NOT an error */
    if (TSHttpHdrStatusSet(negHdrBuf, negHttpHdrLoc, -1) == TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrStatusSet");
    }

    /* TSHttpHdrReasonGet */
    /* valid reason get */
    if ((sHttpReason = TSHttpHdrReasonGet(negHdrBuf, negHttpHdrLoc, &iHttpHdrReasonLength))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrReasonGet");
    }

    if (TSHttpHdrReasonGet(NULL, negHttpHdrLoc, &iHttpHdrReasonLength) != TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrReasonGet");
    }
    if (TSHttpHdrReasonGet(negHdrBuf, NULL, &iHttpHdrReasonLength) != TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrReasonGet");
    }
    /* NULL is a valid length arg */
    if (TSHttpHdrReasonGet(negHdrBuf, negHttpHdrLoc, NULL) == TS_ERROR_PTR) {
      LOG_NEG_ERROR("TSHttpHdrReasonGet");
    }

    /* TSHttpHdrReasonSet */
    if (TSHttpHdrReasonSet(NULL, negHttpHdrLoc, sHttpReason, iHttpHdrReasonLength) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrReasonSet");
    }
    if (TSHttpHdrReasonSet(negHdrBuf, NULL, sHttpReason, iHttpHdrReasonLength) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrReasonSet");
    }

    /* NOTE: TSqa12722: NULL reason arg fixed now */
    if (TSHttpHdrReasonSet(negHdrBuf, negHttpHdrLoc, NULL, iHttpHdrReasonLength) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrReasonSet");
    }
    /* FIXME: TSqa12722 - This neg test would crash TS */
    if (TSHttpHdrReasonSet(negHdrBuf, negHttpHdrLoc, NULL, -1) != TS_ERROR) {
      LOG_NEG_ERROR("TSHttpHdrReasonSet");
    }
  }

  /* Clean-up */
  HANDLE_RELEASE(negHdrBuf, TS_NULL_MLOC, negHttpHdrLoc);
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
    LOG_AUTO_ERROR("TSHttpHdrCopy", "httpType different");
    return 0;
  } else if (pHdrInfo1->hdrLength != pHdrInfo2->hdrLength) {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "hdrLength different");
    return 0;
  } else if (pHdrInfo1->httpVersion != pHdrInfo2->httpVersion) {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "httpVersion different");
    return 0;
  } else if (pHdrInfo1->httpType == TS_HTTP_TYPE_REQUEST) {
    if (strcmp(pHdrInfo1->httpMethod, pHdrInfo2->httpMethod)) {
      LOG_AUTO_ERROR("TSHttpHdrCopy", "httpMethod different");
      return 0;
    } else if (strcmp(pHdrInfo1->hostName, pHdrInfo2->hostName)) {
      LOG_AUTO_ERROR("TSHttpHdrCopy", "hostName different");
      return 0;
    }
  } else if (pHdrInfo1->httpType == TS_HTTP_TYPE_RESPONSE) {
    if (pHdrInfo1->httpStatus != pHdrInfo2->httpStatus) {
      LOG_AUTO_ERROR("TSHttpHdrCopy", "httpStatus different");
      return 0;
    } else if (pHdrInfo1->hdrReason && strcmp(pHdrInfo1->hdrReason, pHdrInfo2->hdrReason)) {
      LOG_AUTO_ERROR("TSHttpHdrCopy", "hdrReason different");
      return 0;
    }
  } else {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "httpType still TS_HTTP_TYPE_UNKNOWN");
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
getHdrInfo(HdrInfo_T * pHdrInfo, TSMBuffer hdrBuf, TSMLoc hdrLoc)
{
  LOG_SET_FUNCTION_NAME("getHdrInfo");
  TSMLoc urlLoc = NULL;

  const char *sHostName = NULL, *sHttpMethod = NULL, *sHttpHdrReason = NULL;
  int iHttpHdrReasonLength, iHttpMethodLength, iHostLength;

  if ((pHdrInfo->httpType = TSHttpHdrTypeGet(hdrBuf, hdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrTypeGet");
  }
  if ((pHdrInfo->hdrLength = TSHttpHdrLengthGet(hdrBuf, hdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrLengthGet");
  }
  if ((pHdrInfo->httpVersion = TSHttpHdrVersionGet(hdrBuf, hdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  }

  if (pHdrInfo->httpType == TS_HTTP_TYPE_REQUEST) {
    if ((sHttpMethod = TSHttpHdrMethodGet(hdrBuf, hdrLoc, &iHttpMethodLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrMethodGet");
    } else {
      pHdrInfo->httpMethod = TSstrndup(sHttpMethod, iHttpMethodLength);
    }

    if ((urlLoc = TSHttpHdrUrlGet(hdrBuf, hdrLoc)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrUrlGet");
    } else if ((sHostName = TSUrlHostGet(hdrBuf, urlLoc, &iHostLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSUrlHostGet");
    } else {
      pHdrInfo->hostName = TSstrndup(sHostName, iHostLength);
    }

    /* Clean-up */
    HANDLE_RELEASE(hdrBuf, hdrLoc, urlLoc);

  } else if (pHdrInfo->httpType == TS_HTTP_TYPE_RESPONSE) {
    if ((pHdrInfo->httpStatus = TSHttpHdrStatusGet(hdrBuf, hdrLoc)) == TS_ERROR) {
      LOG_API_ERROR("TSHttpHdrStatusGet");
    }

    if ((sHttpHdrReason = TSHttpHdrReasonGet(hdrBuf, hdrLoc, &iHttpHdrReasonLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrReasonGet");
    } else if (sHttpHdrReason) {
      pHdrInfo->hdrReason = TSstrndup(sHttpHdrReason, iHttpHdrReasonLength);
    }

    /* clean-up */
  } else {
    LOG_AUTO_ERROR("getHdrInfo", "httpType unknown");
  }
}

static void
printHttpHeader(TSMBuffer hdrBuf, TSMLoc hdrLoc, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("printHttpHeader");

  TSMLoc urlLoc = NULL;

  TSHttpStatus httpStatus;
  TSHttpType httpType;

  int iHostLength, iHttpHdrLength, iHttpMethodLength, iHttpHdrReasonLength, iHttpVersion;
  const char *sHostName = NULL, *sHttpMethod = NULL, *sHttpHdrReason = NULL;
  char *outputString = NULL;


    /*** TSHttpHdrTypeGet ***/
  if ((httpType = TSHttpHdrTypeGet(hdrBuf, hdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrTypeGet");
  }
  TSDebug(debugTag, "(%g) HTTP Header Type = %d", section, httpType);

    /*** TSHttpHdrLengthGet ***/
  if ((iHttpHdrLength = TSHttpHdrLengthGet(hdrBuf, hdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrLengthGet");
  }
  TSDebug(debugTag, "(%g) HTTP Header Length = %d", section, iHttpHdrLength);

    /*** TSHttpVersionGet ***/
  if ((iHttpVersion = TSHttpHdrVersionGet(hdrBuf, hdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  }
  TSDebug(debugTag, "(%g) HTTP Header Version = %d", section, iHttpVersion);
  TSDebug(debugTag, "(%g) Major Version = %d, Minor Version = %d", section,
           TS_HTTP_MAJOR(iHttpVersion), TS_HTTP_MINOR(iHttpVersion));


  if (httpType == TS_HTTP_TYPE_REQUEST) {
        /*** TSHttpHdrMethodGet ***/
    if ((sHttpMethod = TSHttpHdrMethodGet(hdrBuf, hdrLoc, &iHttpMethodLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrMethodGet");
    } else {
      outputString = TSstrndup(sHttpMethod, iHttpMethodLength);
      TSDebug(debugTag, "(%g) HTTP Header Method = %s", section, outputString);
      FREE(outputString);
    }

        /*** TSHttpHdrUrlGet ***/
    if ((urlLoc = TSHttpHdrUrlGet(hdrBuf, hdrLoc)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrUrlGet");
    } else if ((sHostName = TSUrlHostGet(hdrBuf, urlLoc, &iHostLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSUrlHostGet");
    } else if (sHostName) {
      outputString = TSstrndup(sHostName, iHostLength);
      TSDebug(debugTag, "(%g) HTTP Host = %s", section, outputString);
      FREE(outputString);
    }

    /* Clean-up */
    HANDLE_RELEASE(hdrBuf, hdrLoc, urlLoc);

  } else if (httpType == TS_HTTP_TYPE_RESPONSE) {

        /*** TSHttpHdrReasonGet ***/
    /* Try getting reason phrase from the request header - this is an error */
    if ((sHttpHdrReason = TSHttpHdrReasonGet(hdrBuf, hdrLoc, &iHttpHdrReasonLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrReasonGet");
    } else {
      outputString = TSstrndup(sHttpHdrReason, iHttpHdrReasonLength);
      TSDebug(debugTag, "(%g) HTTP Header Reason = %s", section, outputString);
      FREE(outputString);
    }

        /*** TSHttpHdrStatusGet ***/
    /* Try getting status phrase from the request header - this is an error */
    if ((httpStatus = TSHttpHdrStatusGet(hdrBuf, hdrLoc)) == TS_ERROR) {
      LOG_API_ERROR("TSHttpHdrStatusGet");
    } else {
      TSDebug(debugTag, "(%g) HTTP Header Status = %d", section, httpStatus);
    }
  }
}



/************************************************************************
 * handleSendResponse:
 *
 * Description:	handler for TS_HTTP_SEND_RESPONSE_HOOK
 ************************************************************************/

static void
handleSendResponse(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleSendResponse");

  TSMBuffer respHdrBuf = NULL, newHttpHdrBuf = NULL, parseBuffer = NULL;
  TSMLoc respHttpHdrLoc = NULL, newHttpHdrLoc = NULL, parseHttpHdrLoc = NULL;

  TSHttpStatus oldHttpStatus, tmpHttpStatus;
  TSHttpType httpType;
  TSHttpParser httpRespParser = NULL;

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

  TSDebug(RESP, ">>> handleSendResponse <<<<\n");

  /* Get Response Marshall Buffer */
  if (!TSHttpTxnClientRespGet(pTxn, &respHdrBuf, &respHttpHdrLoc)) {
    LOG_API_ERROR_COMMENT("TSHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr");
    goto done;
  }

#ifdef DEBUG
  negTesting(respHdrBuf, respHttpHdrLoc);
#endif

    /******* (1): Exercise all possible TS*GET and print the values **********/

  TSDebug(RESP, "--------------------------------");
  getHdrInfo(pRespHdrInfo, respHdrBuf, respHttpHdrLoc);
  printHttpHeader(respHdrBuf, respHttpHdrLoc, RESP, 1);

    /******* (2): Create a new header and check everything is copied correctly *********/

  TSDebug(RESP, "--------------------------------");

  if ((newHttpHdrBuf = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSMBufferCreate", "skipping to section(4)");
    goto resp_4;
  }

    /*** TSHttpHdrCreate ***/
  if ((newHttpHdrLoc = TSHttpHdrCreate(newHttpHdrBuf)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSMHTTPHdrCreate", "skipping to section(4)");
    goto resp_4;
  }

  /* Make sure the newly created HTTP header has TSHttpType value of TS_HTTP_TYPE_UNKNOWN */
  if ((httpType = TSHttpHdrTypeGet(newHttpHdrBuf, newHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSMHTTPHdrCreate", "continuing");
  } else if (httpType != TS_HTTP_TYPE_UNKNOWN) {
    LOG_AUTO_ERROR("TSHttpHdrCreate", "Newly created hdr not of type TS_HTTP_TYPE_UNKNOWN");
  }


    /*** TSHttpHdrCopy ***/
  if (TSHttpHdrCopy(newHttpHdrBuf, newHttpHdrLoc, respHdrBuf, respHttpHdrLoc) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrCopy");
  }

  getHdrInfo(pNewRespHdrInfo, newHttpHdrBuf, newHttpHdrLoc);
  printHttpHeader(newHttpHdrBuf, newHttpHdrLoc, RESP, 2);

  if (!identical_hdr(pRespHdrInfo, pNewRespHdrInfo)) {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "copy of the resp header not identical to the original");
  }

  /* Reuse:
   * newHttpHdrBuf, newHttHdrLoc */

    /******* (3): Now excercise some TS..SETs on the new header ********/
  TSDebug(RESP, "--------------------------------");

    /*** TSHttpHdrTypeSet ***/
  /* ERROR:
   * 1. Setting type other than TS_HTTP_TYPE_UNKNOWN, TS_HTTP_TYPE_REQUEST,
   * TS_HTTP_TYPE_RESPONSE, and,
   * 2. Setting the type twice.  The hdr type has been already set during TSHttpHdrCopy
   * above, so setting it again is incorrect */
  if (TSHttpHdrTypeSet(newHttpHdrBuf, newHttpHdrLoc, TS_HTTP_TYPE_RESPONSE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrTypeSet");
  }

    /*** TSHttpHdrReasonSet ***/
  /* save the original reason */
  if ((sHttpHdrReason = TSHttpHdrReasonGet(newHttpHdrBuf, newHttpHdrLoc, &iHttpHdrReasonLength))
      == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrReasonGet");
  } else {
    sOldHttpReason = TSstrndup(sHttpHdrReason, iHttpHdrReasonLength);
  }

  /* Note:
   * TSHttpHdrReasonGet may return a NULL reason string (for e.g. I tried www.eyesong.8m.com).
   * Do NOT assume that TSstrndup always returns a null terminated string.  TSstrndup does
   * not returns a NULL terminated string for a NULL ptr as i/p parameter.  It simply returns
   * it backs. So functions like strlen() on the return value might cause TS to crash */


  if (TSHttpHdrReasonSet(newHttpHdrBuf, newHttpHdrLoc, "dummy reason", strlen("dummy reason")) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrReasonGet");
  } else {
    if ((sTmpHttpHdrReason = TSHttpHdrReasonGet(newHttpHdrBuf, newHttpHdrLoc, &iTmpHttpHdrReasonLength))
        == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrReasonGet");
    } else if (sTmpHttpHdrReason && strncmp(sTmpHttpHdrReason, "dummy reason", iTmpHttpHdrReasonLength)) {
      LOG_AUTO_ERROR("TSHttpHdrReasonSet/Get", "GET reason different from the SET reason");
    }
  }

    /*** TSHttpStatusSet ***/
  /* save the original value */
  if ((oldHttpStatus = TSHttpHdrStatusGet(newHttpHdrBuf, newHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusGet");
  }

  /* change it to some unknown value */
  if (TSHttpHdrStatusSet(newHttpHdrBuf, newHttpHdrLoc, TS_HTTP_STATUS_NONE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusSet");
  } else if ((tmpHttpStatus = TSHttpHdrStatusGet(newHttpHdrBuf, newHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusGet");
  } else if (tmpHttpStatus != TS_HTTP_STATUS_NONE) {
    LOG_AUTO_ERROR("TSHttpHdrStatusGet/Set", "GET status different from the SET status");
  }


    /*** TSHttpHdrVersionSet ***/
  /* get the original version */
  if ((iOldHttpVersion = TSHttpHdrVersionGet(newHttpHdrBuf, newHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  }

  /* change it to some unknown version */
  if (TSHttpHdrVersionSet(newHttpHdrBuf, newHttpHdrLoc, TS_HTTP_VERSION(10, 10)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionSet");
  } else if ((iTmpHttpVersion = TSHttpHdrVersionGet(newHttpHdrBuf, newHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  } else if (TS_HTTP_MAJOR(iTmpHttpVersion) != 10 && TS_HTTP_MINOR(iTmpHttpVersion) != 10) {
    LOG_AUTO_ERROR("TSHttpHdrVersionSet", "GET version different from SET version");
  }

  printHttpHeader(newHttpHdrBuf, newHttpHdrLoc, RESP, 3);

  /* Restore the original values */

  /* Here we can't use strlen(sOldHttpReason) to set the length.  This would crash TS if
   * sOldHttpReason happens to be NULL */
  if (TSHttpHdrReasonSet(newHttpHdrBuf, newHttpHdrLoc, sOldHttpReason, iHttpHdrReasonLength) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrReasonSet");
  }
  /*TSHttpHdrReasonSet (newHttpHdrBuf, newHttpHdrLoc, sOldHttpReason, strlen(sOldHttpReason)); */
  if (TSHttpHdrStatusSet(newHttpHdrBuf, newHttpHdrLoc, oldHttpStatus) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusSet");
  }
  if (TSHttpHdrVersionSet(newHttpHdrBuf, newHttpHdrLoc, iOldHttpVersion) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusSet");
  }

  if (!identical_hdr(pRespHdrInfo, pNewRespHdrInfo)) {
    LOG_AUTO_ERROR("TS..SET", "Hdr values not properly restored");
  }

  /* (3): clean-up */
  FREE(sOldHttpReason);

resp_4:
    /******* (4): Now excercise some SETs on the response header ********/
  TSDebug(RESP, "--------------------------------");

    /*** TSHttpHdrReasonSet ***/
  /* save the original reason */
  if ((sHttpHdrReason = TSHttpHdrReasonGet(respHdrBuf, respHttpHdrLoc, &iHttpHdrReasonLength))
      == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrReasonGet");
  } else {
    sOldHttpReason = TSstrndup(sHttpHdrReason, iHttpHdrReasonLength);
  }

  /* change the reason phrase */
  if (TSHttpHdrReasonSet(respHdrBuf, respHttpHdrLoc, "dummy reason", strlen("dummy reason")) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrReasonSet");
  }

  if ((sTmpHttpHdrReason = TSHttpHdrReasonGet(respHdrBuf, respHttpHdrLoc, &iTmpHttpHdrReasonLength))
      == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrReasonGet");
  } else if (sTmpHttpHdrReason && strncmp(sTmpHttpHdrReason, "dummy reason", iTmpHttpHdrReasonLength)) {
    LOG_AUTO_ERROR("TSHttpHdrReasonSet/Get", "GET reason string different from SET reason");
  }

    /*** TSHttpStatusSet ***/
  /* save the original value */
  if ((oldHttpStatus = TSHttpHdrStatusGet(respHdrBuf, respHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusGet");
  }

  /* change it to some unknown value */
  if (TSHttpHdrStatusSet(respHdrBuf, respHttpHdrLoc, TS_HTTP_STATUS_NONE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusSet");
  } else if (TSHttpHdrStatusGet(respHdrBuf, respHttpHdrLoc) != TS_HTTP_STATUS_NONE) {
    LOG_AUTO_ERROR("TSHttpHdrStatusSet/GET", "GET status value different from SET status");
  }


    /*** TSHttpHdrTypeSet ***/
  /* ERROR:
   * 1. Setting type other than TS_HTTP_TYPE_UNKNOWN, TS_HTTP_TYPE_REQUEST,
   * TS_HTTP_TYPE_RESPONSE and,
   * 2. Setting the type twice.  The hdr type has been already set during TSHttpTxnClientRespGet
   * above, so setting it again should fail */
  if (TSHttpHdrTypeSet(respHdrBuf, respHttpHdrLoc, TS_HTTP_TYPE_RESPONSE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrTypeSet");
  }
  if (TSHttpHdrTypeGet(respHdrBuf, respHttpHdrLoc) == TS_HTTP_TYPE_UNKNOWN) {
    LOG_AUTO_ERROR("TSHttpHdrTypeSet/Get", "respHdrBuf CAN be set to TS_HTTP_TYPE_UNKNOWN");
  }

    /*** TSHttpHdrVersionSet ***/
  /* get the original version */
  if ((iOldHttpVersion = TSHttpHdrVersionGet(respHdrBuf, respHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  }

  /* change it to some unknown version */
  if (TSHttpHdrVersionSet(respHdrBuf, respHttpHdrLoc, TS_HTTP_VERSION(10, 10)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionSet");
  } else if ((iTmpHttpVersion = TSHttpHdrVersionGet(respHdrBuf, respHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  } else if (TS_HTTP_MAJOR(iTmpHttpVersion) != 10 && TS_HTTP_MINOR(iTmpHttpVersion) != 10) {
    LOG_AUTO_ERROR("TSHttpHdrVersionGet/Set", "GET HTTP version different from SET version");
  }

  printHttpHeader(respHdrBuf, respHttpHdrLoc, RESP, 4);

  /* restore the original values */

  /* For TSHttpHdrReasonSet, do NOT use strlen(sOldHttpReason) to set the length.
   * This would crash TS if sOldHttpReason happened to be NULL */
  if (TSHttpHdrReasonSet(respHdrBuf, respHttpHdrLoc, sOldHttpReason, iHttpHdrReasonLength) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrReasonSet");
  }
  /*TSHttpHdrReasonSet (respHdrBuf, respHttpHdrLoc, sOldHttpReason, strlen(sOldHttpReason)); */
  if (TSHttpHdrStatusSet(respHdrBuf, respHttpHdrLoc, oldHttpStatus) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrStatusSet");
  }
  if (TSHttpHdrVersionSet(respHdrBuf, respHttpHdrLoc, iOldHttpVersion) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionSet");
  }

  FREE(pNewRespHdrInfo->hdrReason);
  getHdrInfo(pNewRespHdrInfo, respHdrBuf, respHttpHdrLoc);

  if (!identical_hdr(pRespHdrInfo, pNewRespHdrInfo)) {
    LOG_AUTO_ERROR("TS..SET", "Hdr values not properly restored");
  }

  /* (4): clean-up */
  FREE(sOldHttpReason);

    /********************************/
    /** (5): TSHttpHdrParseResp   **/
    /********************************/

  TSDebug(RESP, "--------------------------------");

  /* Create a parser Buffer and header location */
  if ((parseBuffer = TSMBufferCreate()) == TS_ERROR_PTR || parseBuffer == NULL) {
    LOG_API_ERROR_COMMENT("TSMBufferCreate", "abnormal exit");
    goto done;
  } else if ((parseHttpHdrLoc = TSHttpHdrCreate(parseBuffer)) == TS_ERROR_PTR || parseHttpHdrLoc == NULL) {
    LOG_API_ERROR_COMMENT("TSHttpHdrCreate", "abnormal exit");
    goto done;
  }

  pHttpParseStart = sRespHdrStr1;
  pHttpParseEnd = pHttpParseStart + strlen(pHttpParseStart);

  httpRespParser = TSHttpParserCreate();

  if (TSHttpHdrParseResp(httpRespParser, parseBuffer, parseHttpHdrLoc, &pHttpParseStart, pHttpParseEnd)
      == TS_PARSE_ERROR) {
    LOG_API_ERROR("TSHttpHdrParseResp");
  }

  printHttpHeader(parseBuffer, parseHttpHdrLoc, RESP, 5.1);

  if (TSHttpParserClear(httpRespParser) == TS_ERROR) {
    LOG_API_ERROR("TSHttpParseClear");
  }

  TSDebug(RESP, "--------------------------------");

  pHttpParseStart = sRespHdrStr2;
  pHttpParseEnd = pHttpParseStart + strlen(pHttpParseStart);

  /* httpRespParser = TSHttpParserCreate(); */

  if (TSHttpHdrParseResp(httpRespParser, parseBuffer, parseHttpHdrLoc, &pHttpParseStart, pHttpParseEnd)
      == TS_PARSE_ERROR) {
    LOG_API_ERROR("TSHttpHdrParseResp");
  }

  printHttpHeader(parseBuffer, parseHttpHdrLoc, RESP, 5.2);

  if (TSHttpParserClear(httpRespParser) == TS_ERROR) {
    LOG_API_ERROR("TSHttpParseClear");
  }

  TSDebug(RESP, "--------------------------------");

  pHttpParseStart = sRespHdrStr3;
  pHttpParseEnd = pHttpParseStart + strlen(pHttpParseStart);

  /* httpRespParser = TSHttpParserCreate(); */

  if (TSHttpHdrParseResp(httpRespParser, parseBuffer, parseHttpHdrLoc, &pHttpParseStart, pHttpParseEnd)
      == TS_PARSE_ERROR) {
    LOG_API_ERROR("TSHttpHdrParseResp");
  }

  printHttpHeader(parseBuffer, parseHttpHdrLoc, RESP, 5.3);


done:
  /* Clean-up */
  freeHdr(pRespHdrInfo);
  freeHdr(pNewRespHdrInfo);

  /* release hdrLoc */
  HANDLE_RELEASE(respHdrBuf, TS_NULL_MLOC, respHttpHdrLoc);
  HANDLE_RELEASE(newHttpHdrBuf, TS_NULL_MLOC, newHttpHdrLoc);
  HANDLE_RELEASE(parseBuffer, TS_NULL_MLOC, parseHttpHdrLoc);

  /* destroy hdrLoc */
  HDR_DESTROY(respHdrBuf, respHttpHdrLoc);
  HDR_DESTROY(parseBuffer, parseHttpHdrLoc);

  /* destroy mbuffer */
  BUFFER_DESTROY(newHttpHdrBuf);
  BUFFER_DESTROY(parseBuffer);

  /* destroy the parser */
  if (TSHttpParserDestroy(httpRespParser) == TS_ERROR) {
    LOG_API_ERROR("TSHttpParserDestroy");
  }

  if (TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpTxnReenable");
  }

  TSDebug(RESP, "......... exiting handleRespResponse .............\n");

}                               /* handleSendResponse */




/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for TS_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleReadRequest");

  TSMBuffer reqHdrBuf = NULL, newHttpHdrBuf = NULL;
  TSMLoc reqHdrLoc = NULL, newHttpHdrLoc = NULL;

  TSHttpType httpType;

  int iOldHttpVersion, iHttpMethodLength, iHttpVersion;
  const char *sHttpMethod = NULL;
  char *outputString = NULL, *sOldHttpMethod = NULL;

  HdrInfo_T *pReqHdrInfo = NULL, *pNewReqHdrInfo = NULL;



  pReqHdrInfo = initHdr();
  pNewReqHdrInfo = initHdr();

  TSDebug(REQ, "\n>>>>>> handleReadRequest <<<<<<<\n");

  /* Get Request Marshall Buffer */
  if (!TSHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHdrLoc)) {
    LOG_API_ERROR_COMMENT("TSHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr");
    goto done;
  }


    /******* (1): Get every specifics from the HTTP header *********/

  TSDebug(REQ, "--------------------------------");

  getHdrInfo(pReqHdrInfo, reqHdrBuf, reqHdrLoc);
  printHttpHeader(reqHdrBuf, reqHdrLoc, REQ, 1);

#ifdef DEBUG
  negTesting(reqHdrBuf, reqHdrLoc);
#endif

    /*********** (2): Create/Copy/Destroy **********/
  /* For every request, create, copy and destroy a new HTTP header and
   * print the details */

  TSDebug(REQ, "--------------------------------");
  if ((newHttpHdrBuf = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSMBufferCreate", "skipping to section 3");
    goto section_3;             /* Skip to section (3) down the line directly; I hate GOTOs */
  }

    /*** TSHttpHdrCreate ***/
  if ((newHttpHdrLoc = TSHttpHdrCreate(newHttpHdrBuf)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrCreate", "skipping to section 3");
    goto section_3;             /* Skip to section (3) down the line directly; I hate GOTOs */
  }

  /* Make sure the newly created HTTP header has TSHttpType value of TS_HTTP_TYPE_UNKNOWN */
  if ((httpType = TSHttpHdrTypeGet(newHttpHdrBuf, newHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrTypeGet", "but still continuing...");
  } else if (httpType != TS_HTTP_TYPE_UNKNOWN) {
    LOG_AUTO_ERROR("TSHttpHdrCreate", "Newly created hdr not of type TS_HTTP_TYPE_UNKNOWN");
  }

  /* set the HTTP header type: a new buffer has a type TS_HTTP_TYPE_UNKNOWN by default */
  if (TSHttpHdrTypeSet(newHttpHdrBuf, newHttpHdrLoc, TS_HTTP_TYPE_REQUEST) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrTypeSet", "unable to set it to TS_HTTP_TYPE_REQUEST");
  } else if ((httpType = TSHttpHdrTypeGet(newHttpHdrBuf, newHttpHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrTypeGet", "still continuing");
  } else if (httpType != TS_HTTP_TYPE_REQUEST) {
    LOG_AUTO_ERROR("TSHttpHdrTypeSet", "Type not set to TS_HTTP_TYPE_REQUEST");
  }

    /*** TSHttpHdrCopy ***/
  if (TSHttpHdrCopy(newHttpHdrBuf, newHttpHdrLoc, reqHdrBuf, reqHdrLoc) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrCopy");
  }
  getHdrInfo(pNewReqHdrInfo, newHttpHdrBuf, newHttpHdrLoc);

  if (!identical_hdr(pNewReqHdrInfo, pReqHdrInfo)) {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "New req buffer not identical to the original");
  }

  printHttpHeader(newHttpHdrBuf, newHttpHdrLoc, REQ, 2);

  FREE(pNewReqHdrInfo->httpMethod);
  FREE(pNewReqHdrInfo->hostName);

section_3:
    /********* (3): Excercise all the TS__Set on ReqBuf *********/
  TSDebug(REQ, "--------------------------------");

    /*** TSHttpHdrMethodSet ***/
  /* save the original method */
  if ((sHttpMethod = TSHttpHdrMethodGet(reqHdrBuf, reqHdrLoc, &iHttpMethodLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrMethodGet");
  } else {
    sOldHttpMethod = TSstrndup(sHttpMethod, iHttpMethodLength);
  }
  /* change it to some unknown method */
  if (TSHttpHdrMethodSet(reqHdrBuf, reqHdrLoc, "FOOBAR", strlen("FOOBAR")) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrMethodSet");
  } else {
    if ((sHttpMethod = TSHttpHdrMethodGet(reqHdrBuf, reqHdrLoc, &iHttpMethodLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrMethodGet");
    } else if (strncmp(sHttpMethod, "FOOBAR", iHttpMethodLength)) {
      LOG_AUTO_ERROR("TSHttpHdrMethodSet/Get", "GET method different from SET method");
    }
  }

  outputString = TSstrndup(sHttpMethod, iHttpMethodLength);
  TSDebug(REQ, "(3): new HTTP Header Method = %s", outputString);
  FREE(outputString);

  printHttpHeader(reqHdrBuf, reqHdrLoc, REQ, 3);

  /* set it back to the original method */
  /*TSHttpHdrMethodSet (reqHdrBuf, reqHdrLoc, sOldHttpMethod, iHttpMethodLength); */
  if (TSHttpHdrMethodSet(reqHdrBuf, reqHdrLoc, sOldHttpMethod, strlen(sOldHttpMethod)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrMethodSet");
  } else if ((sHttpMethod = TSHttpHdrMethodGet(reqHdrBuf, reqHdrLoc, &iHttpMethodLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrMethodGet");
  } else if (strncmp(sHttpMethod, sOldHttpMethod, iHttpMethodLength)) {
    LOG_AUTO_ERROR("TSHttpHdrMethodSet/Get", "GET method different from SET method");
  }

  outputString = TSstrndup(sHttpMethod, iHttpMethodLength);
  TSDebug(REQ, "(3): original HTTP Header Method = %s", outputString);
  FREE(outputString);


    /*** TSHttpHdrVersionSet ***/
  /* get the original version */
  if ((iOldHttpVersion = TSHttpHdrVersionGet(reqHdrBuf, reqHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  }

  /* change it to some unknown version */
  if (TSHttpHdrVersionSet(reqHdrBuf, reqHdrLoc, TS_HTTP_VERSION(10, 10)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionSet");
  } else if ((iHttpVersion = TSHttpHdrVersionGet(reqHdrBuf, reqHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  } else if ((TS_HTTP_MAJOR(iHttpVersion) != 10) || (TS_HTTP_MINOR(iHttpVersion) != 10)) {
    LOG_AUTO_ERROR("TSHttpHdrVersionSet/Get", "SET HTTP version different from GET version");
  }

  TSDebug(REQ, "(3): new HTTP version; Major = %d   Minor = %d",
           TS_HTTP_MAJOR(iHttpVersion), TS_HTTP_MINOR(iHttpVersion));

  /* change it back to the original version */
  if (TSHttpHdrVersionSet(reqHdrBuf, reqHdrLoc, iOldHttpVersion) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionSet");
  } else if ((iHttpVersion = TSHttpHdrVersionGet(reqHdrBuf, reqHdrLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrVersionGet");
  } else if (iHttpVersion != iOldHttpVersion) {
    LOG_AUTO_ERROR("TSHttpHdrVersionSet/Get", "SET HTTP version different from GET version");
  }

  getHdrInfo(pNewReqHdrInfo, reqHdrBuf, reqHdrLoc);
  if (!identical_hdr(pNewReqHdrInfo, pReqHdrInfo)) {
    LOG_AUTO_ERROR("TS..Set", "ReqBuf: Values not restored properly");
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
  HANDLE_RELEASE(reqHdrBuf, TS_NULL_MLOC, reqHdrLoc);
  HANDLE_RELEASE(newHttpHdrBuf, TS_NULL_MLOC, newHttpHdrLoc);

  /* destroy hdr */
  HDR_DESTROY(newHttpHdrBuf, newHttpHdrLoc);

  /* destroy mbuffer */
  BUFFER_DESTROY(newHttpHdrBuf);


  if (TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpTxnReenable");
  }
  TSDebug(REQ, "..... exiting handleReadRequest ......\n");

}                               /* handleReadReadRequest */



static void
handleTxnStart(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleTxnStart");

  if (TSHttpTxnHookAdd(pTxn, TS_HTTP_READ_REQUEST_HDR_HOOK, pCont) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHookAdd");
  }

  if (TSHttpTxnHookAdd(pTxn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, pCont) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHookAdd");
  }

  if (TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpTxnReenable");
  }
}


static int
contHandler(TSCont pCont, TSEvent event, void *edata)
{
  TSHttpTxn pTxn = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    handleTxnStart(pCont, pTxn);
    return 0;
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    handleReadRequest(pCont, pTxn);
    return 0;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    handleSendResponse(pCont, pTxn);
    return 0;
  default:
    break;
  }

  return 0;
}



void
TSPluginInit(int argc, const char *argv[])
{
  TSCont pCont;

  LOG_SET_FUNCTION_NAME("TSPluginInit");

  if ((pCont = TSContCreate(contHandler, NULL)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSContCreate")
  } else if (TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, pCont) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHookAdd");
  }
}
