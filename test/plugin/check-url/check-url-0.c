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
#define REQ             "request"
#define AUTO_TAG        "AUTO_ERROR"
#define NEG_ERROR_TAG   "NEG_ERROR"
#define DEBUG_TAG       "ERROR"
#define PLUGIN_NAME     "check-url-0"


typedef struct
{
  /* Req line Method */
  TSHttpType httpType;
  char *httpMethod;

  /* Req line URL */
  char *urlHost;
  char *urlFragment;
  char *urlParams;
  char *urlQuery;
  int urlLength;
  char *urlPassword;
  char *urlPath;
  int urlPort;
  char *urlScheme;
  char *urlUser;

  /* Req line HTTP version */
  int hdrLength;
  int httpVersion;
} HttpMsgLine_T;


HttpMsgLine_T *
initMsgLine()
{
  HttpMsgLine_T *msgLine = NULL;

  msgLine = (HttpMsgLine_T *) TSmalloc(sizeof(HttpMsgLine_T));

  msgLine->httpType = TS_HTTP_TYPE_UNKNOWN;
  msgLine->httpMethod = NULL;
  msgLine->urlHost = NULL;
  msgLine->urlFragment = NULL;
  msgLine->urlParams = NULL;
  msgLine->urlQuery = NULL;
  msgLine->urlLength = 0;
  msgLine->urlPassword = NULL;
  msgLine->urlPath = NULL;
  msgLine->urlPort = 0;
  msgLine->urlScheme = NULL;
  msgLine->urlUser = NULL;

  msgLine->hdrLength = 0;
  msgLine->httpVersion = 0;

  return msgLine;
}

static void
freeMsgLine(HttpMsgLine_T * httpMsgLine)
{
  if (VALID_PTR(httpMsgLine)) {
    FREE(httpMsgLine->httpMethod);
    FREE(httpMsgLine->urlHost);
    FREE(httpMsgLine->urlFragment);
    FREE(httpMsgLine->urlParams);
    FREE(httpMsgLine->urlQuery);
    FREE(httpMsgLine->urlPassword);
    FREE(httpMsgLine->urlPath);
    FREE(httpMsgLine->urlScheme);
    FREE(httpMsgLine->urlUser);
  }
}


int
identicalURL(HttpMsgLine_T * pHttpMsgLine1, HttpMsgLine_T * pHttpMsgLine2)
{
  LOG_SET_FUNCTION_NAME("identicalURL");

if (pHttpMsgLine1->urlHost && strcmp(pHttpMsgLine1->urlHost, pHttpMsgLine2->urlHost)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlHost different");
    return 0;
  } else if (pHttpMsgLine1->urlFragment && strcmp(pHttpMsgLine1->urlFragment, pHttpMsgLine2->urlFragment)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlFragement different");
    return 0;
  } else if (pHttpMsgLine1->urlParams && strcmp(pHttpMsgLine1->urlParams, pHttpMsgLine2->urlParams)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlParams different");
    return 0;
  } else if (pHttpMsgLine1->urlQuery && strcmp(pHttpMsgLine1->urlQuery, pHttpMsgLine2->urlQuery)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlQuery different");
    return 0;
  } else if ((pHttpMsgLine1->urlLength != pHttpMsgLine2->urlLength)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlLength type different");
    return 0;
  } else if (pHttpMsgLine1->urlPassword && strcmp(pHttpMsgLine1->urlPassword, pHttpMsgLine2->urlPassword)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlPassword different");
    return 0;
  } else if (pHttpMsgLine1->urlPath && strcmp(pHttpMsgLine1->urlPath, pHttpMsgLine2->urlPath)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlPath different");
    return 0;
  } else if ((pHttpMsgLine1->urlPort != pHttpMsgLine2->urlPort)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlLength type different");
    return 0;
  } else if (pHttpMsgLine1->urlScheme && strcmp(pHttpMsgLine1->urlScheme, pHttpMsgLine2->urlScheme)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlScheme different");
    return 0;
  } else if (pHttpMsgLine1->urlUser && strcmp(pHttpMsgLine1->urlUser, pHttpMsgLine2->urlUser)) {
    LOG_AUTO_ERROR("TSHttpUrlCopy", "urlUser different");
    return 0;
  }

  return 1;                     /* Both the headers are exactly identical */
}

static void
storeHdrInfo(HttpMsgLine_T * pHttpMsgLine, TSMBuffer hdrBuf, TSMLoc hdrLoc,
             TSMLoc urlLoc, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("storeHdrInfo");

  const char *sUrlHostName = NULL, *sHttpMethod = NULL, *sUrlFragment = NULL, *sUrlQuery = NULL;
  const char *sUrlPassword = NULL, *sUrlPath = NULL, *sUrlScheme = NULL;
  const char *sUrlParams = NULL, *sUrlUser = NULL;
  int iHttpMethodLength, iUrlFragmentLength, iUrlParamsLength, iUrlQueryLength;
  int iUrlHostLength, iUrlPasswordLength, iUrlPathLength, iUrlSchemeLength, iUrlUserLength;

  if (hdrLoc) {
    if ((pHttpMsgLine->hdrLength = TSHttpHdrLengthGet(hdrBuf, hdrLoc)) == TS_ERROR) {
      LOG_API_ERROR("TSHttpHdrLengthGet");
    }
    if ((pHttpMsgLine->httpVersion = TSHttpHdrVersionGet(hdrBuf, hdrLoc)) == TS_ERROR) {
      LOG_API_ERROR("TSHttpHdrVersionGet");
    }

    if ((sHttpMethod = TSHttpHdrMethodGet(hdrBuf, hdrLoc, &iHttpMethodLength)) == TS_ERROR_PTR) {
      LOG_API_ERROR("TSHttpHdrMethodGet");
    } else {
      pHttpMsgLine->httpMethod = TSstrndup(sHttpMethod, iHttpMethodLength);
      TSDebug(debugTag, "(%g) HTTP Method = %s", section, pHttpMsgLine->httpMethod);
    }
  }

  /*urlLoc = TSHttpHdrUrlGet (hdrBuf, hdrLoc); */
  if ((sUrlHostName = TSUrlHostGet(hdrBuf, urlLoc, &iUrlHostLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHostGet");
  } else {
    pHttpMsgLine->urlHost = TSstrndup(sUrlHostName, iUrlHostLength);
    TSDebug(debugTag, "(%g) URL Host = %s", section, pHttpMsgLine->urlHost);
  }

  if ((sUrlFragment = TSUrlHttpFragmentGet(hdrBuf, urlLoc, &iUrlFragmentLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHttpFragment");
  } else {
    pHttpMsgLine->urlFragment = TSstrndup(sUrlFragment, iUrlFragmentLength);
    TSDebug(debugTag, "(%g) URL HTTP Fragment = %s", section, pHttpMsgLine->urlFragment);
  }

  if ((sUrlParams = TSUrlHttpParamsGet(hdrBuf, urlLoc, &iUrlParamsLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHttpParmsGet");
  } else {
    pHttpMsgLine->urlParams = TSstrndup(sUrlParams, iUrlParamsLength);
    TSDebug(debugTag, "(%g) URL HTTP Params = %s", section, pHttpMsgLine->urlParams);
  }

  if ((sUrlQuery = TSUrlHttpQueryGet(hdrBuf, urlLoc, &iUrlQueryLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHttpQueryGet");
  } else {
    pHttpMsgLine->urlQuery = TSstrndup(sUrlQuery, iUrlQueryLength);
    TSDebug(debugTag, "(%g) URL HTTP Query = %s", section, pHttpMsgLine->urlQuery);
  }

  if ((pHttpMsgLine->urlLength = TSUrlLengthGet(hdrBuf, urlLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSUrlLengthGet");
  } else {
    TSDebug(debugTag, "(%g) URL Length = %d", section, pHttpMsgLine->urlLength);
  }

  if ((sUrlPassword = TSUrlPasswordGet(hdrBuf, urlLoc, &iUrlPasswordLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlPasswordGet");
  } else {
    pHttpMsgLine->urlPassword = TSstrndup(sUrlPassword, iUrlPasswordLength);
    TSDebug(debugTag, "(%g) URL Password = %s", section, pHttpMsgLine->urlPassword);
  }

  if ((sUrlPath = TSUrlPathGet(hdrBuf, urlLoc, &iUrlPathLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlPathGet");
  } else {
    pHttpMsgLine->urlPath = TSstrndup(sUrlPath, iUrlPathLength);
    TSDebug(debugTag, "(%g) URL Path = %s", section, pHttpMsgLine->urlPath);
  }

  if ((pHttpMsgLine->urlPort = TSUrlPortGet(hdrBuf, urlLoc)) == TS_ERROR) {
    LOG_API_ERROR("TSUrlPortGet");
  } else {
    TSDebug(debugTag, "(%g) URL Port = %d", section, pHttpMsgLine->urlPort);
  }

  if ((sUrlScheme = TSUrlSchemeGet(hdrBuf, urlLoc, &iUrlSchemeLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlSchemeGet");
  } else {
    pHttpMsgLine->urlScheme = TSstrndup(sUrlScheme, iUrlSchemeLength);
    TSDebug(debugTag, "(%g) URL Scheme = %s", section, pHttpMsgLine->urlScheme);
  }

  if ((sUrlUser = TSUrlUserGet(hdrBuf, urlLoc, &iUrlUserLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlUserGet");
  } else {
    pHttpMsgLine->urlUser = TSstrndup(sUrlUser, iUrlUserLength);
    TSDebug(debugTag, "(%g) URL User = %s", section, pHttpMsgLine->urlUser);
  }

  /* Clean-up */
  HANDLE_RELEASE(hdrBuf, hdrLoc, urlLoc);
}


static void
setCustomUrl(TSMBuffer hdrBuf, TSMLoc httpHdrLoc)
{
  LOG_SET_FUNCTION_NAME("setCustomUrl");

  TSMLoc urlLoc;

  const char *sUrlHostName = NULL, *sUrlFragment = NULL, *sUrlParams = NULL, *sUrlQuery = NULL;
  const char *sUrlPassword = NULL, *sUrlPath = NULL, *sUrlScheme = NULL, *sUrlUser = NULL;
  int iHostLength, iUrlFragmentLength, iUrlParamsLength, iUrlQueryLength, iUrlPasswordLength;
  int iUrlPathLength, iUrlSchemeLength, iUrlUserLength;
  int i = 0;

  static HttpMsgLine_T custUrl[] =
    { {TS_HTTP_TYPE_REQUEST, "\0", 'a', "www.testing-host.com", "testing-fragment", "testing-params", "testing-query",
       100, "testing-password", "testing/path", 19000, "testing-scheme", "testing-user", 0, 0} };


  if ((urlLoc = TSHttpHdrUrlGet(hdrBuf, httpHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrUrlGet");
  }

  if (TSHttpHdrTypeGet(hdrBuf, httpHdrLoc) == TS_ERROR) {
    LOG_API_ERROR("TSHttpHdrTypeGet");
  } else if (TSHttpHdrTypeGet(hdrBuf, httpHdrLoc) != TS_HTTP_TYPE_REQUEST) {
    LOG_AUTO_ERROR("TSHttpHdrTypeSet", "Type not set to TS_HTTP_TYPE_REQUEST");
  }

  if (TSUrlHostSet(hdrBuf, urlLoc, custUrl[i].urlHost, strlen(custUrl[i].urlHost)) == TS_ERROR) {
    LOG_API_ERROR("TSUrlHostSet");
  } else if ((sUrlHostName = TSUrlHostGet(hdrBuf, urlLoc, &iHostLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHostGet");
  } else if (strncmp(sUrlHostName, custUrl[i].urlHost, iHostLength)) {
    LOG_AUTO_ERROR("TSUrlHostSet/Get", "GET different from SET");
  }

  if (TSUrlHttpFragmentSet(hdrBuf, urlLoc, custUrl[i].urlFragment, strlen(custUrl[i].urlFragment))
      == TS_ERROR) {
    LOG_API_ERROR("TSUrlHttpFragmentSet");
  } else if ((sUrlFragment = TSUrlHttpFragmentGet(hdrBuf, urlLoc, &iUrlFragmentLength))
             == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHttpFragmentGet");
  } else if (strncmp(sUrlFragment, custUrl[i].urlFragment, iUrlFragmentLength)) {
    LOG_AUTO_ERROR("TSUrlHttpFragmentSet/Get", "GET different from SET");
  }

  if (TSUrlHttpParamsSet(hdrBuf, urlLoc, custUrl[i].urlParams, strlen(custUrl[i].urlParams))
      == TS_ERROR) {
    LOG_API_ERROR("TSUrlHttpParamsSet");
  } else if ((sUrlParams = TSUrlHttpParamsGet(hdrBuf, urlLoc, &iUrlParamsLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHttpParamsGet");
  } else if (strncmp(sUrlParams, custUrl[i].urlParams, iUrlParamsLength)) {
    LOG_AUTO_ERROR("TSUrlHttpParamsSet/Get", "GET different from SET");
  }

  if (TSUrlHttpQuerySet(hdrBuf, urlLoc, custUrl[i].urlQuery, strlen(custUrl[i].urlQuery))
      == TS_ERROR) {
    LOG_API_ERROR("TSUrlHttpQuerySet");
  } else if ((sUrlQuery = TSUrlHttpQueryGet(hdrBuf, urlLoc, &iUrlQueryLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlHttpQueryGet");
  } else if (strncmp(sUrlQuery, custUrl[i].urlQuery, iUrlQueryLength)) {
    LOG_AUTO_ERROR("TSUrlHttpQuerySet/Get", "GET different from SET");
  }

  if (TSUrlPasswordSet(hdrBuf, urlLoc, custUrl[i].urlPassword, strlen(custUrl[i].urlPassword))
      == TS_ERROR) {
    LOG_API_ERROR("TSUrlPasswordSet");
  } else if ((sUrlPassword = TSUrlPasswordGet(hdrBuf, urlLoc, &iUrlPasswordLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlPasswordGet");
  } else if (strncmp(sUrlPassword, custUrl[i].urlPassword, iUrlPasswordLength)) {
    LOG_AUTO_ERROR("TSUrlHttpPasswordSet/Get", "GET different from SET");
  }

  if (TSUrlPathSet(hdrBuf, urlLoc, custUrl[i].urlPath, strlen(custUrl[i].urlPath)) == TS_ERROR) {
    LOG_API_ERROR("TSUrlPathSet");
  } else if ((sUrlPath = TSUrlPathGet(hdrBuf, urlLoc, &iUrlPathLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlPathGet");
  } else if (strncmp(sUrlPath, custUrl[i].urlPath, iUrlPathLength)) {
    LOG_AUTO_ERROR("TSUrlHttpPathSet/Get", "GET different from SET");
  }

  if (TSUrlPortSet(hdrBuf, urlLoc, custUrl[i].urlPort) == TS_ERROR) {
    LOG_API_ERROR("TSUrlPortSet");
  } else if (TSUrlPortGet(hdrBuf, urlLoc) == TS_ERROR) {
    LOG_API_ERROR("TSUrlPortGet");
  } else if (TSUrlPortGet(hdrBuf, urlLoc) != custUrl[i].urlPort) {
    LOG_AUTO_ERROR("TSUrlHttpPortSet/Get", "GET different from SET");
  }

  if (TSUrlSchemeSet(hdrBuf, urlLoc, custUrl[i].urlScheme, strlen(custUrl[i].urlScheme)) == TS_ERROR) {
    LOG_API_ERROR("TSUrlSchemeSet");
  } else if ((sUrlScheme = TSUrlSchemeGet(hdrBuf, urlLoc, &iUrlSchemeLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlSchemeGet");
  } else if (strncmp(sUrlScheme, custUrl[i].urlScheme, iUrlSchemeLength)) {
    LOG_AUTO_ERROR("TSUrlHttpSchemeSet/Get", "GET different from SET");
  }

  if (TSUrlUserSet(hdrBuf, urlLoc, custUrl[i].urlUser, strlen(custUrl[i].urlUser)) == TS_ERROR) {
    LOG_API_ERROR("TSUrlUserSet");
  } else if ((sUrlUser = TSUrlUserGet(hdrBuf, urlLoc, &iUrlUserLength)) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSUrlUserGet");
  } else if (strncmp(sUrlUser, custUrl[i].urlUser, iUrlUserLength)) {
    LOG_AUTO_ERROR("TSUrlHttpUserSet/Get", "GET different from SET");
  }

  /* Clean-up */
  HANDLE_RELEASE(hdrBuf, httpHdrLoc, urlLoc);
}                               /* setCustomUrl */


void
negTesting(TSMBuffer hdrBuf, TSMLoc urlLoc)
{
  LOG_SET_FUNCTION_NAME("negTesting");

  TSMBuffer negHdrBuf = NULL;
  TSMLoc negHttpHdrLoc = NULL, negUrlLoc = NULL;

  TSHttpType negType, hdrHttpType;
  TSHttpStatus httpStatus;

  const char *sHttpReason = NULL, *pUrlParseStart = NULL, *pUrlParseEnd = NULL;
  int iHttpMethodLength, iHttpHdrReasonLength, iUrlHostLength, iUrlFragmentLength, iUrlParamsLength;
  int iUrlQueryLength, iUrlPasswordLength, iUrlSchemeLength, iUrlPathLength, iUrlUserLength;
  const char *urlParseStr = "http://joe:bolts4USA@www.joes-hardware.com/cgi-bin/inventory?product=hammer43";

  static HttpMsgLine_T custUrl[] =
    { {TS_HTTP_TYPE_REQUEST, "\0", 'a', "www.testing-host.com", "testing-fragment", "testing-params", "testing-query",
       100, "testing-password", "testing/path", 19000, "testing-scheme", "testing-user", 0, 0} };

  /* valid TSMBufferCreate */
  if ((negHdrBuf = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR("TSHttpHdrCreate");
  }

  /* TSUrlCreate */
  if (TSUrlCreate(NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlCreate");
  }

  /* valid TSUrlCreate */
  if ((negUrlLoc = TSUrlCreate(negHdrBuf)) == TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlCreate");
  }

  /* TSUrlCopy */
  if (TSUrlCopy(NULL, negUrlLoc, hdrBuf, urlLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlCopy");
  }
  if (TSUrlCopy(negHdrBuf, NULL, hdrBuf, urlLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlCopy");
  }

  /* valid TSUrlCopy */
  if (TSUrlCopy(negHdrBuf, negUrlLoc, hdrBuf, urlLoc) == TS_ERROR) {
    LOG_NEG_ERROR("TSUrlCopy");
  }

  /* TSUrlHostGet */
  if (TSUrlHostGet(NULL, negUrlLoc, &iUrlHostLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHostGet");
  }
  if (TSUrlHostGet(negHdrBuf, NULL, &iUrlHostLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHostGet");
  }
  if (TSUrlHostGet(negHdrBuf, negUrlLoc, NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHostGet");
  }

  /* TSUrlHostSet */
  if (TSUrlHostSet(NULL, negUrlLoc, "www.inktomi.com", strlen("www.inktomi.com")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHostSet");
  }
  if (TSUrlHostSet(negHdrBuf, NULL, "www.inktomi.com", strlen("www.inktomi.com")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHostSet");
  }
  if (TSUrlHostSet(negHdrBuf, negUrlLoc, NULL, 0) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHostSet");
  }
  /* TSqa12722 */
  if (TSUrlHostSet(hdrBuf, urlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHostSet");
  }

  /* TSUrlHttpFragmentGet */
  if (TSUrlHttpFragmentGet(NULL, negUrlLoc, &iUrlFragmentLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpFragment");
  }
  if (TSUrlHttpFragmentGet(negHdrBuf, NULL, &iUrlFragmentLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpFragment");
  }
  if (TSUrlHttpFragmentGet(negHdrBuf, negUrlLoc, NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpFragment");
  }

  /* TSUrlHttpFragmentSet */
  if (TSUrlHttpFragmentSet(NULL, negUrlLoc, "testing-fragment", strlen("testing-fragment"))
      != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpFragmentSet");
  }
  if (TSUrlHttpFragmentSet(negHdrBuf, NULL, "testing-fragment", strlen("testing-fragment"))
      != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpFragmentSet");
  }
  if (TSUrlHttpFragmentSet(negHdrBuf, negUrlLoc, NULL, strlen("testing-fragment")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpFragmentSet");
  }
  /* TSqa12722 */
  if (TSUrlHttpFragmentSet(negHdrBuf, negUrlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpFragmentSet");
  }

  /* TSUrlHttpParamsGet */
  if (TSUrlHttpParamsGet(NULL, negUrlLoc, &iUrlParamsLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpParmsGet");
  }
  if (TSUrlHttpParamsGet(negHdrBuf, NULL, &iUrlParamsLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpParmsGet");
  }
  if (TSUrlHttpParamsGet(negHdrBuf, negUrlLoc, NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpParmsGet");
  }

  /* TSUrlHttpParamsSet */
  if (TSUrlHttpParamsSet(NULL, negUrlLoc, "test-params", strlen("test-params")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpParamsSet");
  }
  if (TSUrlHttpParamsSet(negHdrBuf, NULL, "test-params", strlen("test-params")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpParamsSet");
  }
  if (TSUrlHttpParamsSet(negHdrBuf, negUrlLoc, NULL, 0) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpParamsSet");
  }
  /* TSqa12722 */
  if (TSUrlHttpParamsSet(negHdrBuf, negUrlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpParamsSet");
  }

  /* TSUrlHttpQueryGet(); */
  if (TSUrlHttpQueryGet(NULL, negUrlLoc, &iUrlQueryLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpQueryGet");
  }
  if (TSUrlHttpQueryGet(negHdrBuf, NULL, &iUrlQueryLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlHttpQueryGet");
  }

  /* TSUrlHttpQuerySet */
  if (TSUrlHttpQuerySet(NULL, negUrlLoc, "test-query", strlen("test-query")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpQuerySet");
  }
  if (TSUrlHttpQuerySet(negHdrBuf, NULL, "test-query", strlen("test-query")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpQuerySet");
  }
  if (TSUrlHttpQuerySet(negHdrBuf, negUrlLoc, NULL, 0) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpQuerySet");
  }
  if (TSUrlHttpQuerySet(negHdrBuf, negUrlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlHttpQuerySet");
  }

  /* TSUrlLengthGet */
  if (TSUrlLengthGet(NULL, negUrlLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlLengthGet");
  }
  if (TSUrlLengthGet(negHdrBuf, NULL) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlLengthGet");
  }

  /* TSUrlPasswordGet */
  if (TSUrlPasswordGet(NULL, negUrlLoc, &iUrlPasswordLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlPasswordGet");
  }
  if (TSUrlPasswordGet(negHdrBuf, NULL, &iUrlPasswordLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlPasswordGet");
  }
  if (TSUrlPasswordGet(negHdrBuf, negUrlLoc, NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlPasswordGet");
  }

  /* TSUrlPasswordSet */
  if (TSUrlPasswordSet(NULL, negUrlLoc, "clear-text-password", strlen("clear-text-password"))
      != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPasswordSet");
  }
  if (TSUrlPasswordSet(negHdrBuf, NULL, "clear-text-password", strlen("clear-text-password"))
      != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPasswordSet");
  }
  if (TSUrlPasswordSet(negHdrBuf, negUrlLoc, NULL, 0) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPasswordSet");
  }
  if (TSUrlPasswordSet(negHdrBuf, negUrlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPasswordSet");
  }

  /* TSUrlPathGet */
  if (TSUrlPathGet(NULL, negUrlLoc, &iUrlPathLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlPathGet");
  }
  if (TSUrlPathGet(negHdrBuf, NULL, &iUrlPathLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlPathGet");
  }
  if (TSUrlPathGet(negHdrBuf, negUrlLoc, NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlPathGet");
  }

  /* TSUrlPathSet */
  if (TSUrlPathSet(NULL, negUrlLoc, "testing/sample/path", strlen("testing/sample/path")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPathSet");
  }
  if (TSUrlPathSet(negHdrBuf, NULL, "testing/sample/path", strlen("testing/sample/path")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPathSet");
  }
  if (TSUrlPathSet(negHdrBuf, negUrlLoc, NULL, 0) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPathSet");
  }
  if (TSUrlPathSet(negHdrBuf, negUrlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPathSet");
  }

  /* TSUrlPortGet */
  if (TSUrlPortGet(NULL, negUrlLoc) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPortGet");
  }
  if (TSUrlPortGet(negHdrBuf, NULL) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPortGet");
  }

  /* TSUrlPortSet */
  if (TSUrlPortSet(NULL, negUrlLoc, 13150) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPortSet");
  }
  if (TSUrlPortSet(negHdrBuf, NULL, 13150) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPortSet");
  }
  /* FIXME: TSqa12722 */
  if (TSUrlPortSet(negHdrBuf, negUrlLoc, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlPortSet");
  }

  /* TSUrlSchemeGet */
  if (TSUrlSchemeGet(NULL, negUrlLoc, &iUrlSchemeLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlSchemeGet");
  }
  if (TSUrlSchemeGet(negHdrBuf, NULL, &iUrlSchemeLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlSchemeGet");
  }
  if (TSUrlSchemeGet(negHdrBuf, negUrlLoc, NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlSchemeGet");
  }

  /* TSUrlSchemeSet */
  if (TSUrlSchemeSet(NULL, negUrlLoc, "test-scheme", strlen("test-scheme")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlSchemeSet");
  }
  if (TSUrlSchemeSet(negHdrBuf, NULL, "test-scheme", strlen("test-scheme")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlSchemeSet");
  }
  if (TSUrlSchemeSet(negHdrBuf, negUrlLoc, NULL, 0) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlSchemeSet");
  }
  if (TSUrlSchemeSet(negHdrBuf, negUrlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlSchemeSet");
  }

  /* TSUrlUserGet */
  if (TSUrlUserGet(NULL, negUrlLoc, &iUrlUserLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlUserGet");
  }
  if (TSUrlUserGet(negHdrBuf, NULL, &iUrlUserLength) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlUserGet");
  }
  if (TSUrlUserGet(negHdrBuf, negUrlLoc, NULL) != TS_ERROR_PTR) {
    LOG_NEG_ERROR("TSUrlUserGet");
  }

  /* TSUrlUserSet */
  if (TSUrlUserSet(NULL, negUrlLoc, "test-user", strlen("test-user")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlUserSet");
  }
  if (TSUrlUserSet(negHdrBuf, NULL, "test-user", strlen("test-user")) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlUserSet");
  }
  /* FIXME: TSqa12722 */
  if (TSUrlUserSet(negHdrBuf, negUrlLoc, NULL, 0) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlUserSet");
  }
  /* FIXME: TSqa12722: This cause TS crash */
  if (TSUrlUserSet(negHdrBuf, negUrlLoc, NULL, -1) != TS_ERROR) {
    LOG_NEG_ERROR("TSUrlUserSet");
  }

  /* TSUrlParse */
  pUrlParseStart = urlParseStr;
  pUrlParseEnd = pUrlParseStart + strlen(pUrlParseStart);

  if (TSUrlParse(NULL, negUrlLoc, &pUrlParseStart, pUrlParseEnd) != TS_PARSE_ERROR) {
    LOG_NEG_ERROR("TSUrlParse");
  }
  if (TSUrlParse(negHdrBuf, NULL, &pUrlParseStart, pUrlParseEnd) != TS_PARSE_ERROR) {
    LOG_NEG_ERROR("TSUrlParse");
  }

  if (TSUrlParse(negHdrBuf, negUrlLoc, NULL, pUrlParseEnd) != TS_PARSE_ERROR) {
    LOG_NEG_ERROR("TSUrlParse");
  }
  /* FIXME: TSqa12929: */
  if (TSUrlParse(negHdrBuf, negUrlLoc, &pUrlParseStart, NULL) != TS_PARSE_ERROR) {
    LOG_NEG_ERROR("TSUrlParse");
  }

  /* Clean-up */
  HANDLE_RELEASE(negHdrBuf, TS_NULL_MLOC, negUrlLoc);

  URL_DESTROY(negHdrBuf, negUrlLoc);
  BUFFER_DESTROY(negHdrBuf);
}

/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for TS_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleReadRequest");

  TSMBuffer reqHdrBuf = NULL, parseBuffer = NULL, newHdrBuf1 = NULL, newHdrBuf2 = NULL;
  TSMLoc reqHttpHdrLoc = NULL, newHttpHdrLoc1 = NULL, parseHttpHdrLoc = NULL;
  TSMLoc reqUrlLoc = NULL, newUrlLoc1 = NULL, newUrlLoc2 = NULL, parseUrlLoc = NULL;

  TSHttpType httpType;

  HttpMsgLine_T *pReqMsgLine = NULL, *pNewReqMsgLine = NULL, *pParseReqMsgLine = NULL;

  int parseStatus, iUrlFragmentLength;
  const char *pUrlParseStart, *pUrlParseEnd, *sUrlFragment;
  const char *urlParseStr = "http://joe:bolts4USA@www.joes-hardware.com/cgi-bin/inventory?product=hammer43";


  TSDebug(REQ, "\n>>>>>> handleReadRequest <<<<<<<");

  /* Get Request Marshall Buffer */
  if (!TSHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHttpHdrLoc)) {
    LOG_API_ERROR_COMMENT("TSHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr; abnormal exit");
    goto done;
  }


        /******** (1): Simply print the URL details of the request header **************/

  TSDebug(REQ, "--------------------------------");

  pReqMsgLine = initMsgLine();

        /*** TSHttpHdrUrlGet ***/
  if ((reqUrlLoc = TSHttpHdrUrlGet(reqHdrBuf, reqHttpHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrUrlGet", "ERROR: abnormal exit");
    goto done;
  }
  storeHdrInfo(pReqMsgLine, reqHdrBuf, reqHttpHdrLoc, reqUrlLoc, REQ, 1);

#ifdef DEBUG
  printf("=================\n");
  negTesting(reqHdrBuf, reqUrlLoc);
#endif

        /******** (2): Do a *header* copy and print URL details of the new buffer **********/

  /* Header copy also copies the URL, so we can still print URL pieces */

  TSDebug(REQ, "--------------------------------");
  pNewReqMsgLine = initMsgLine();

  if ((newHdrBuf1 = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSMBufferCreate", "skipping to section 5");
    goto section_5;             /* Skip to section (5) down the line directly; I hate GOTOs too :-) */
  }

        /*** TSHttpHdrCreate ***/
  if ((newHttpHdrLoc1 = TSHttpHdrCreate(newHdrBuf1)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrCreate", "skipping to section 5");
    goto section_5;             /* Skip to section (5) down the line directly; */
  }

  /* Make sure the newly created HTTP header has TSHttpType value of TS_HTTP_TYPE_UNKNOWN */
  if ((httpType = TSHttpHdrTypeGet(newHdrBuf1, newHttpHdrLoc1)) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrTypeGet", "still continuing");
  } else if (httpType != TS_HTTP_TYPE_UNKNOWN) {
    LOG_API_ERROR_COMMENT("TSHttpHdrTypeGet", "New created hdr not of type TS_HTTP_TYPE_UNKNOWN");
  }

  /* set the HTTP header type: a new buffer has a type TS_HTTP_TYPE_UNKNOWN by default */
  if (TSHttpHdrTypeSet(newHdrBuf1, newHttpHdrLoc1, TS_HTTP_TYPE_REQUEST) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrTypeSet", "continuing");
  }
  if (TSHttpHdrTypeGet(newHdrBuf1, newHttpHdrLoc1) != TS_HTTP_TYPE_REQUEST) {
    LOG_AUTO_ERROR("TSHttpHdrTypeGet", "Type not set to TS_HTTP_TYPE_REQUEST");
  }

        /*** TSHttpHdrCopy ***/
  /* Note: This should also copy the URL string */
  if (TSHttpHdrCopy(newHdrBuf1, newHttpHdrLoc1, reqHdrBuf, reqHttpHdrLoc) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrCopy", "continuing");
  }

        /*** TSHttpHdrUrlGet ***/
  if ((newUrlLoc1 = TSHttpHdrUrlGet(newHdrBuf1, newHttpHdrLoc1)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrUrlGet", "skipping to section 5");
    goto section_5;
  }
  storeHdrInfo(pNewReqMsgLine, newHdrBuf1, newHttpHdrLoc1, newUrlLoc1, REQ, 2);
  if (!identicalURL(pNewReqMsgLine, pReqMsgLine)) {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "New req buffer not identical to the original");
  }


        /******* (3): Now tweak some of the URL components of the same new header *******/
  TSDebug(REQ, "--------------------------------");

  if ((newUrlLoc1 = TSHttpHdrUrlGet(newHdrBuf1, newHttpHdrLoc1)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrUrlGet", "skipping to section 5");
    goto section_5;
  }

  setCustomUrl(newHdrBuf1, newHttpHdrLoc1);
  /*setCustomUrl(newHdrBuf1, custUrl); */

  freeMsgLine(pNewReqMsgLine);
  storeHdrInfo(pNewReqMsgLine, newHdrBuf1, newHttpHdrLoc1, newUrlLoc1, REQ, 3);


        /********* (4): Now do a *URL* copy from request to the above buffer and print the details **********/
  TSDebug(REQ, "--------------------------------");

  if ((reqUrlLoc = TSHttpHdrUrlGet(reqHdrBuf, reqHttpHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrUrlGet", "skipping to section 5");
    goto section_5;
  }

  if ((newUrlLoc1 = TSUrlCreate(newHdrBuf1)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSUrlCreate", "skipping to section 5");
    goto section_5;
  }


        /*** TSUrlCopy ***/
  if (TSUrlCopy(newHdrBuf1, newUrlLoc1, reqHdrBuf, reqUrlLoc) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSUrlCopy", "skipping to section 5");
    goto section_5;
  }

  freeMsgLine(pNewReqMsgLine);
  storeHdrInfo(pNewReqMsgLine, newHdrBuf1, (TSMLoc) NULL, newUrlLoc1, REQ, 4);
  if (!identicalURL(pNewReqMsgLine, pReqMsgLine)) {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "New req buffer not identical to the original");
  }


section_5:

        /********* (5): Create a new buffer and do a URL copy immediately from req buffer *********/
  TSDebug(REQ, "--------------------------------");

  if ((reqUrlLoc = TSHttpHdrUrlGet(reqHdrBuf, reqHttpHdrLoc)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrUrlGet", "abnormal exit");
    goto done;
  }

  if ((newHdrBuf2 = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSMBufferCreate", "abnormal exit");
    goto done;
  }
  if ((newUrlLoc2 = TSUrlCreate(newHdrBuf2)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSUrlCreate", "abnormal exit");
    goto done;
  }

        /*** TSUrlCopy ***/
  if (TSUrlCopy(newHdrBuf2, newUrlLoc2, reqHdrBuf, reqUrlLoc) == TS_ERROR) {
    LOG_API_ERROR_COMMENT("TSUrlCopy", "abnormal exit");
    goto done;
  }

  freeMsgLine(pNewReqMsgLine);
  storeHdrInfo(pNewReqMsgLine, newHdrBuf2, (TSMLoc) NULL, newUrlLoc2, REQ, 5);
  if (!identicalURL(pNewReqMsgLine, pReqMsgLine)) {
    LOG_AUTO_ERROR("TSHttpHdrCopy", "New req buffer not identical to the original");
  }


        /*********** (6): Parse Buffer *************/

  TSDebug(REQ, "--------------------------------");

  pParseReqMsgLine = initMsgLine();

  /* Create a parser Buffer and header location */
  if ((parseBuffer = TSMBufferCreate()) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSMBufferCreate", "abnormal exit");
    goto done;
  } else if ((parseHttpHdrLoc = TSHttpHdrCreate(parseBuffer)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSHttpHdrCreate", "abnormal exit");
    goto done;
  } else if ((parseUrlLoc = TSUrlCreate(parseBuffer)) == TS_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("TSUrlCreate", "abnormal exit");
    goto done;
  }

  /* Set the hdr type to REQUEST */
  if (TSHttpHdrTypeSet(parseBuffer, parseHttpHdrLoc, TS_HTTP_TYPE_REQUEST) == TS_ERROR) {
    LOG_API_ERROR("TSHttHdrTypeSet");
  }

  pUrlParseStart = urlParseStr;
  pUrlParseEnd = pUrlParseStart + strlen(pUrlParseStart);

  if ((parseStatus = TSUrlParse(parseBuffer, parseUrlLoc, &pUrlParseStart, pUrlParseEnd))
      == TS_PARSE_ERROR) {
    LOG_API_ERROR_COMMENT("TSUrlParse", "abnormal exit");
    goto done;
  }

  storeHdrInfo(pParseReqMsgLine, parseBuffer, parseHttpHdrLoc, parseUrlLoc, REQ, 6);


done:
        /*************** Clean-up ***********************/
  freeMsgLine(pReqMsgLine);
  freeMsgLine(pNewReqMsgLine);
  freeMsgLine(pParseReqMsgLine);

  FREE(pReqMsgLine);
  FREE(pNewReqMsgLine);
  FREE(pParseReqMsgLine);

  /* release */
  HANDLE_RELEASE(reqHdrBuf, reqHttpHdrLoc, reqUrlLoc);
  HANDLE_RELEASE(reqHdrBuf, TS_NULL_MLOC, reqHttpHdrLoc);

  HANDLE_RELEASE(newHdrBuf1, newHttpHdrLoc1, newUrlLoc1);
  HANDLE_RELEASE(newHdrBuf1, TS_NULL_MLOC, newHttpHdrLoc1);

  /*TSHandleMLocRelease (newHdrBuf2, newHttpHdrLoc2, newUrlLoc2); */
  /*TSHandleMLocRelease (newHdrBuf2, TS_NULL_MLOC, newHttpHdrLoc2); */
  HANDLE_RELEASE(newHdrBuf2, TS_NULL_MLOC, newUrlLoc2);

  HANDLE_RELEASE(parseBuffer, parseHttpHdrLoc, parseUrlLoc);
  HANDLE_RELEASE(parseBuffer, TS_NULL_MLOC, parseHttpHdrLoc);

  /* urlLoc destroy */
  URL_DESTROY(reqHdrBuf, reqUrlLoc);
  URL_DESTROY(newHdrBuf1, newUrlLoc1);
  URL_DESTROY(newHdrBuf2, newUrlLoc2);
  URL_DESTROY(parseBuffer, parseUrlLoc);

  /* httpLoc destroy */
  HDR_DESTROY(reqHdrBuf, reqHttpHdrLoc);
  HDR_DESTROY(newHdrBuf1, newHttpHdrLoc1);
  HDR_DESTROY(parseBuffer, parseHttpHdrLoc);

  /* buffer destroy */
  BUFFER_DESTROY(newHdrBuf1);
  BUFFER_DESTROY(newHdrBuf2);
  BUFFER_DESTROY(parseBuffer);

  if (TSHttpTxnReenable(pTxn, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_API_ERROR("TSHttpTxnReenable");
  }

  TSDebug(REQ, "..... exiting handleReadRequest ......");

}                               /* handleReadReadRequest */




static void
handleTxnStart(TSCont pCont, TSHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleTxnStart");

  if (TSHttpTxnHookAdd(pTxn, TS_HTTP_READ_REQUEST_HDR_HOOK, pCont) == TS_ERROR) {
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
    break;
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    handleReadRequest(pCont, pTxn);
    break;
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
