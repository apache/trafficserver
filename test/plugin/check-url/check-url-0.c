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
  INKHttpType httpType;
  char *httpMethod;

  /* Req line URL */
  int urlFtpType;
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

  msgLine = (HttpMsgLine_T *) INKmalloc(sizeof(HttpMsgLine_T));

  msgLine->httpType = INK_HTTP_TYPE_UNKNOWN;
  msgLine->httpMethod = NULL;

  msgLine->urlFtpType = 0;
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

  if ((pHttpMsgLine1->urlFtpType != pHttpMsgLine2->urlFtpType)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "FTP type different");
    return 0;
  } else if (pHttpMsgLine1->urlHost && strcmp(pHttpMsgLine1->urlHost, pHttpMsgLine2->urlHost)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlHost different");
    return 0;
  } else if (pHttpMsgLine1->urlFragment && strcmp(pHttpMsgLine1->urlFragment, pHttpMsgLine2->urlFragment)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlFragement different");
    return 0;
  } else if (pHttpMsgLine1->urlParams && strcmp(pHttpMsgLine1->urlParams, pHttpMsgLine2->urlParams)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlParams different");
    return 0;
  } else if (pHttpMsgLine1->urlQuery && strcmp(pHttpMsgLine1->urlQuery, pHttpMsgLine2->urlQuery)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlQuery different");
    return 0;
  } else if ((pHttpMsgLine1->urlLength != pHttpMsgLine2->urlLength)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlLength type different");
    return 0;
  } else if (pHttpMsgLine1->urlPassword && strcmp(pHttpMsgLine1->urlPassword, pHttpMsgLine2->urlPassword)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlPassword different");
    return 0;
  } else if (pHttpMsgLine1->urlPath && strcmp(pHttpMsgLine1->urlPath, pHttpMsgLine2->urlPath)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlPath different");
    return 0;
  } else if ((pHttpMsgLine1->urlPort != pHttpMsgLine2->urlPort)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlLength type different");
    return 0;
  } else if (pHttpMsgLine1->urlScheme && strcmp(pHttpMsgLine1->urlScheme, pHttpMsgLine2->urlScheme)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlScheme different");
    return 0;
  } else if (pHttpMsgLine1->urlUser && strcmp(pHttpMsgLine1->urlUser, pHttpMsgLine2->urlUser)) {
    LOG_AUTO_ERROR("INKHttpUrlCopy", "urlUser different");
    return 0;
  }

  return 1;                     /* Both the headers are exactly identical */
}

static void
storeHdrInfo(HttpMsgLine_T * pHttpMsgLine, INKMBuffer hdrBuf, INKMLoc hdrLoc,
             INKMLoc urlLoc, char *debugTag, float section)
{
  LOG_SET_FUNCTION_NAME("storeHdrInfo");

  const char *sUrlHostName = NULL, *sHttpMethod = NULL, *sUrlFragment = NULL, *sUrlQuery = NULL;
  const char *sUrlPassword = NULL, *sUrlPath = NULL, *sUrlScheme = NULL;
  const char *sUrlParams = NULL, *sUrlUser = NULL;
  int iHttpMethodLength, iUrlFragmentLength, iUrlParamsLength, iUrlQueryLength;
  int iUrlHostLength, iUrlPasswordLength, iUrlPathLength, iUrlSchemeLength, iUrlUserLength;

  if (hdrLoc) {
    if ((pHttpMsgLine->hdrLength = INKHttpHdrLengthGet(hdrBuf, hdrLoc)) == INK_ERROR) {
      LOG_API_ERROR("INKHttpHdrLengthGet");
    }
    if ((pHttpMsgLine->httpVersion = INKHttpHdrVersionGet(hdrBuf, hdrLoc)) == INK_ERROR) {
      LOG_API_ERROR("INKHttpHdrVersionGet");
    }

    if ((sHttpMethod = INKHttpHdrMethodGet(hdrBuf, hdrLoc, &iHttpMethodLength)) == INK_ERROR_PTR) {
      LOG_API_ERROR("INKHttpHdrMethodGet");
    } else {
      pHttpMsgLine->httpMethod = INKstrndup(sHttpMethod, iHttpMethodLength);
      INKDebug(debugTag, "(%g) HTTP Method = %s", section, pHttpMsgLine->httpMethod);
      STR_RELEASE(hdrBuf, urlLoc, sHttpMethod);
    }
  }

  /*urlLoc = INKHttpHdrUrlGet (hdrBuf, hdrLoc); */

  if ((pHttpMsgLine->urlFtpType = INKUrlFtpTypeGet(hdrBuf, urlLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKUrlFtpTypeGet");
  } else {
    INKDebug(debugTag, "(%g) FTP type = %d (%c)", section, pHttpMsgLine->urlFtpType, pHttpMsgLine->urlFtpType);
  }

  if ((sUrlHostName = INKUrlHostGet(hdrBuf, urlLoc, &iUrlHostLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHostGet");
  } else {
    pHttpMsgLine->urlHost = INKstrndup(sUrlHostName, iUrlHostLength);
    INKDebug(debugTag, "(%g) URL Host = %s", section, pHttpMsgLine->urlHost);
  }

  if ((sUrlFragment = INKUrlHttpFragmentGet(hdrBuf, urlLoc, &iUrlFragmentLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHttpFragment");
  } else {
    pHttpMsgLine->urlFragment = INKstrndup(sUrlFragment, iUrlFragmentLength);
    INKDebug(debugTag, "(%g) URL HTTP Fragment = %s", section, pHttpMsgLine->urlFragment);
  }

  if ((sUrlParams = INKUrlHttpParamsGet(hdrBuf, urlLoc, &iUrlParamsLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHttpParmsGet");
  } else {
    pHttpMsgLine->urlParams = INKstrndup(sUrlParams, iUrlParamsLength);
    INKDebug(debugTag, "(%g) URL HTTP Params = %s", section, pHttpMsgLine->urlParams);
  }

  if ((sUrlQuery = INKUrlHttpQueryGet(hdrBuf, urlLoc, &iUrlQueryLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHttpQueryGet");
  } else {
    pHttpMsgLine->urlQuery = INKstrndup(sUrlQuery, iUrlQueryLength);
    INKDebug(debugTag, "(%g) URL HTTP Query = %s", section, pHttpMsgLine->urlQuery);
  }

  if ((pHttpMsgLine->urlLength = INKUrlLengthGet(hdrBuf, urlLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKUrlLengthGet");
  } else {
    INKDebug(debugTag, "(%g) URL Length = %d", section, pHttpMsgLine->urlLength);
  }

  if ((sUrlPassword = INKUrlPasswordGet(hdrBuf, urlLoc, &iUrlPasswordLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlPasswordGet");
  } else {
    pHttpMsgLine->urlPassword = INKstrndup(sUrlPassword, iUrlPasswordLength);
    INKDebug(debugTag, "(%g) URL Password = %s", section, pHttpMsgLine->urlPassword);
  }

  if ((sUrlPath = INKUrlPathGet(hdrBuf, urlLoc, &iUrlPathLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlPathGet");
  } else {
    pHttpMsgLine->urlPath = INKstrndup(sUrlPath, iUrlPathLength);
    INKDebug(debugTag, "(%g) URL Path = %s", section, pHttpMsgLine->urlPath);
  }

  if ((pHttpMsgLine->urlPort = INKUrlPortGet(hdrBuf, urlLoc)) == INK_ERROR) {
    LOG_API_ERROR("INKUrlPortGet");
  } else {
    INKDebug(debugTag, "(%g) URL Port = %d", section, pHttpMsgLine->urlPort);
  }

  if ((sUrlScheme = INKUrlSchemeGet(hdrBuf, urlLoc, &iUrlSchemeLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlSchemeGet");
  } else {
    pHttpMsgLine->urlScheme = INKstrndup(sUrlScheme, iUrlSchemeLength);
    INKDebug(debugTag, "(%g) URL Scheme = %s", section, pHttpMsgLine->urlScheme);
  }

  if ((sUrlUser = INKUrlUserGet(hdrBuf, urlLoc, &iUrlUserLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlUserGet");
  } else {
    pHttpMsgLine->urlUser = INKstrndup(sUrlUser, iUrlUserLength);
    INKDebug(debugTag, "(%g) URL User = %s", section, pHttpMsgLine->urlUser);
  }

  /* Clean-up */
  STR_RELEASE(hdrBuf, urlLoc, sHttpMethod);
  STR_RELEASE(hdrBuf, urlLoc, sUrlHostName);
  STR_RELEASE(hdrBuf, urlLoc, sUrlFragment);
  STR_RELEASE(hdrBuf, urlLoc, sUrlParams);
  STR_RELEASE(hdrBuf, urlLoc, sUrlQuery);
  STR_RELEASE(hdrBuf, urlLoc, sUrlPassword);
  STR_RELEASE(hdrBuf, urlLoc, sUrlPath);
  STR_RELEASE(hdrBuf, urlLoc, sUrlScheme);
  STR_RELEASE(hdrBuf, urlLoc, sUrlUser);

  HANDLE_RELEASE(hdrBuf, hdrLoc, urlLoc);
}


static void
setCustomUrl(INKMBuffer hdrBuf, INKMLoc httpHdrLoc)
{
  LOG_SET_FUNCTION_NAME("setCustomUrl");

  INKMLoc urlLoc;

  const char *sUrlHostName = NULL, *sUrlFragment = NULL, *sUrlParams = NULL, *sUrlQuery = NULL;
  const char *sUrlPassword = NULL, *sUrlPath = NULL, *sUrlScheme = NULL, *sUrlUser = NULL;
  int iHostLength, iUrlFragmentLength, iUrlParamsLength, iUrlQueryLength, iUrlPasswordLength;
  int iUrlPathLength, iUrlSchemeLength, iUrlUserLength;
  int i = 0;

  static HttpMsgLine_T custUrl[] =
    { {INK_HTTP_TYPE_REQUEST, "\0", 'a', "www.testing-host.com", "testing-fragment", "testing-params", "testing-query",
       100, "testing-password", "testing/path", 19000, "testing-scheme", "testing-user", 0, 0} };


  if ((urlLoc = INKHttpHdrUrlGet(hdrBuf, httpHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrUrlGet");
  }

  if (INKHttpHdrTypeGet(hdrBuf, httpHdrLoc) == INK_ERROR) {
    LOG_API_ERROR("INKHttpHdrTypeGet");
  } else if (INKHttpHdrTypeGet(hdrBuf, httpHdrLoc) != INK_HTTP_TYPE_REQUEST) {
    LOG_AUTO_ERROR("INKHttpHdrTypeSet", "Type not set to INK_HTTP_TYPE_REQUEST");
  }

  if (INKUrlFtpTypeSet(hdrBuf, urlLoc, custUrl[i].urlFtpType) == INK_ERROR) {
    LOG_API_ERROR("INKUrlFtpTypeSet");
  } else if (INKUrlFtpTypeGet(hdrBuf, urlLoc) == INK_ERROR) {
    LOG_API_ERROR("INKUrlFtpTypeGet");
  } else if (INKUrlFtpTypeGet(hdrBuf, urlLoc) != custUrl->urlFtpType) {
    LOG_AUTO_ERROR("INKUrlFtpTypeSet/Get", "GET different from SET");
  }

  if (INKUrlHostSet(hdrBuf, urlLoc, custUrl[i].urlHost, strlen(custUrl[i].urlHost)) == INK_ERROR) {
    LOG_API_ERROR("INKUrlHostSet");
  } else if ((sUrlHostName = INKUrlHostGet(hdrBuf, urlLoc, &iHostLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHostGet");
  } else if (strncmp(sUrlHostName, custUrl[i].urlHost, iHostLength)) {
    LOG_AUTO_ERROR("INKUrlHostSet/Get", "GET different from SET");
  }

  if (INKUrlHttpFragmentSet(hdrBuf, urlLoc, custUrl[i].urlFragment, strlen(custUrl[i].urlFragment))
      == INK_ERROR) {
    LOG_API_ERROR("INKUrlHttpFragmentSet");
  } else if ((sUrlFragment = INKUrlHttpFragmentGet(hdrBuf, urlLoc, &iUrlFragmentLength))
             == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHttpFragmentGet");
  } else if (strncmp(sUrlFragment, custUrl[i].urlFragment, iUrlFragmentLength)) {
    LOG_AUTO_ERROR("INKUrlHttpFragmentSet/Get", "GET different from SET");
  }

  if (INKUrlHttpParamsSet(hdrBuf, urlLoc, custUrl[i].urlParams, strlen(custUrl[i].urlParams))
      == INK_ERROR) {
    LOG_API_ERROR("INKUrlHttpParamsSet");
  } else if ((sUrlParams = INKUrlHttpParamsGet(hdrBuf, urlLoc, &iUrlParamsLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHttpParamsGet");
  } else if (strncmp(sUrlParams, custUrl[i].urlParams, iUrlParamsLength)) {
    LOG_AUTO_ERROR("INKUrlHttpParamsSet/Get", "GET different from SET");
  }

  if (INKUrlHttpQuerySet(hdrBuf, urlLoc, custUrl[i].urlQuery, strlen(custUrl[i].urlQuery))
      == INK_ERROR) {
    LOG_API_ERROR("INKUrlHttpQuerySet");
  } else if ((sUrlQuery = INKUrlHttpQueryGet(hdrBuf, urlLoc, &iUrlQueryLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlHttpQueryGet");
  } else if (strncmp(sUrlQuery, custUrl[i].urlQuery, iUrlQueryLength)) {
    LOG_AUTO_ERROR("INKUrlHttpQuerySet/Get", "GET different from SET");
  }

  if (INKUrlPasswordSet(hdrBuf, urlLoc, custUrl[i].urlPassword, strlen(custUrl[i].urlPassword))
      == INK_ERROR) {
    LOG_API_ERROR("INKUrlPasswordSet");
  } else if ((sUrlPassword = INKUrlPasswordGet(hdrBuf, urlLoc, &iUrlPasswordLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlPasswordGet");
  } else if (strncmp(sUrlPassword, custUrl[i].urlPassword, iUrlPasswordLength)) {
    LOG_AUTO_ERROR("INKUrlHttpPasswordSet/Get", "GET different from SET");
  }

  if (INKUrlPathSet(hdrBuf, urlLoc, custUrl[i].urlPath, strlen(custUrl[i].urlPath)) == INK_ERROR) {
    LOG_API_ERROR("INKUrlPathSet");
  } else if ((sUrlPath = INKUrlPathGet(hdrBuf, urlLoc, &iUrlPathLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlPathGet");
  } else if (strncmp(sUrlPath, custUrl[i].urlPath, iUrlPathLength)) {
    LOG_AUTO_ERROR("INKUrlHttpPathSet/Get", "GET different from SET");
  }

  if (INKUrlPortSet(hdrBuf, urlLoc, custUrl[i].urlPort) == INK_ERROR) {
    LOG_API_ERROR("INKUrlPortSet");
  } else if (INKUrlPortGet(hdrBuf, urlLoc) == INK_ERROR) {
    LOG_API_ERROR("INKUrlPortGet");
  } else if (INKUrlPortGet(hdrBuf, urlLoc) != custUrl[i].urlPort) {
    LOG_AUTO_ERROR("INKUrlHttpPortSet/Get", "GET different from SET");
  }

  if (INKUrlSchemeSet(hdrBuf, urlLoc, custUrl[i].urlScheme, strlen(custUrl[i].urlScheme)) == INK_ERROR) {
    LOG_API_ERROR("INKUrlSchemeSet");
  } else if ((sUrlScheme = INKUrlSchemeGet(hdrBuf, urlLoc, &iUrlSchemeLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlSchemeGet");
  } else if (strncmp(sUrlScheme, custUrl[i].urlScheme, iUrlSchemeLength)) {
    LOG_AUTO_ERROR("INKUrlHttpSchemeSet/Get", "GET different from SET");
  }

  if (INKUrlUserSet(hdrBuf, urlLoc, custUrl[i].urlUser, strlen(custUrl[i].urlUser)) == INK_ERROR) {
    LOG_API_ERROR("INKUrlUserSet");
  } else if ((sUrlUser = INKUrlUserGet(hdrBuf, urlLoc, &iUrlUserLength)) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKUrlUserGet");
  } else if (strncmp(sUrlUser, custUrl[i].urlUser, iUrlUserLength)) {
    LOG_AUTO_ERROR("INKUrlHttpUserSet/Get", "GET different from SET");
  }

  /* Clean-up */
  STR_RELEASE(hdrBuf, urlLoc, sUrlHostName);
  STR_RELEASE(hdrBuf, urlLoc, sUrlFragment);
  STR_RELEASE(hdrBuf, urlLoc, sUrlParams);
  STR_RELEASE(hdrBuf, urlLoc, sUrlQuery);
  STR_RELEASE(hdrBuf, urlLoc, sUrlPassword);
  STR_RELEASE(hdrBuf, urlLoc, sUrlPath);
  STR_RELEASE(hdrBuf, urlLoc, sUrlScheme);
  STR_RELEASE(hdrBuf, urlLoc, sUrlUser);

  HANDLE_RELEASE(hdrBuf, httpHdrLoc, urlLoc);
}                               /* setCustomUrl */


void
negTesting(INKMBuffer hdrBuf, INKMLoc urlLoc)
{
  LOG_SET_FUNCTION_NAME("negTesting");

  INKMBuffer negHdrBuf = NULL;
  INKMLoc negHttpHdrLoc = NULL, negUrlLoc = NULL;

  INKHttpType negType, hdrHttpType;
  INKHttpStatus httpStatus;

  const char *sHttpReason = NULL, *pUrlParseStart = NULL, *pUrlParseEnd = NULL;
  int iHttpMethodLength, iHttpHdrReasonLength, iUrlHostLength, iUrlFragmentLength, iUrlParamsLength;
  int iUrlQueryLength, iUrlPasswordLength, iUrlSchemeLength, iUrlPathLength, iUrlUserLength;
  const char *urlParseStr = "http://joe:bolts4USA@www.joes-hardware.com/cgi-bin/inventory?product=hammer43";

  static HttpMsgLine_T custUrl[] =
    { {INK_HTTP_TYPE_REQUEST, "\0", 'a', "www.testing-host.com", "testing-fragment", "testing-params", "testing-query",
       100, "testing-password", "testing/path", 19000, "testing-scheme", "testing-user", 0, 0} };

  /* valid INKMBufferCreate */
  if ((negHdrBuf = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR("INKHttpHdrCreate");
  }

  /* INKUrlCreate */
  if (INKUrlCreate(NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlCreate");
  }

  /* valid INKUrlCreate */
  if ((negUrlLoc = INKUrlCreate(negHdrBuf)) == INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlCreate");
  }

  /* INKUrlCopy */
  if (INKUrlCopy(NULL, negUrlLoc, hdrBuf, urlLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlCopy");
  }
  if (INKUrlCopy(negHdrBuf, NULL, hdrBuf, urlLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlCopy");
  }

  /* valid INKUrlCopy */
  if (INKUrlCopy(negHdrBuf, negUrlLoc, hdrBuf, urlLoc) == INK_ERROR) {
    LOG_NEG_ERROR("INKUrlCopy");
  }

  /* INKUrlFtpTypeGet(); */
  if (INKUrlFtpTypeGet(NULL, negUrlLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlFtpTypeGet");
  }
  if (INKUrlFtpTypeGet(negHdrBuf, NULL) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlFtpTypeGet");
  }

  /* INKUrlFtpTypeSet(); */
  if (INKUrlFtpTypeSet(NULL, negUrlLoc, 'a') != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlFtpTypeSet");
  }
  if (INKUrlFtpTypeSet(negHdrBuf, NULL, 'a') != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlFtpTypeSet");
  }
  /* FIXME: INKqa12722 */
  if (INKUrlFtpTypeSet(negHdrBuf, negUrlLoc, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlFtpTypeSet");
  }

  /* INKUrlHostGet */
  if (INKUrlHostGet(NULL, negUrlLoc, &iUrlHostLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHostGet");
  }
  if (INKUrlHostGet(negHdrBuf, NULL, &iUrlHostLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHostGet");
  }
  if (INKUrlHostGet(negHdrBuf, negUrlLoc, NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHostGet");
  }

  /* INKUrlHostSet */
  if (INKUrlHostSet(NULL, negUrlLoc, "www.inktomi.com", strlen("www.inktomi.com")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHostSet");
  }
  if (INKUrlHostSet(negHdrBuf, NULL, "www.inktomi.com", strlen("www.inktomi.com")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHostSet");
  }
  if (INKUrlHostSet(negHdrBuf, negUrlLoc, NULL, 0) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHostSet");
  }
  /* INKqa12722 */
  if (INKUrlHostSet(hdrBuf, urlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHostSet");
  }

  /* INKUrlHttpFragmentGet */
  if (INKUrlHttpFragmentGet(NULL, negUrlLoc, &iUrlFragmentLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpFragment");
  }
  if (INKUrlHttpFragmentGet(negHdrBuf, NULL, &iUrlFragmentLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpFragment");
  }
  if (INKUrlHttpFragmentGet(negHdrBuf, negUrlLoc, NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpFragment");
  }

  /* INKUrlHttpFragmentSet */
  if (INKUrlHttpFragmentSet(NULL, negUrlLoc, "testing-fragment", strlen("testing-fragment"))
      != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpFragmentSet");
  }
  if (INKUrlHttpFragmentSet(negHdrBuf, NULL, "testing-fragment", strlen("testing-fragment"))
      != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpFragmentSet");
  }
  if (INKUrlHttpFragmentSet(negHdrBuf, negUrlLoc, NULL, strlen("testing-fragment")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpFragmentSet");
  }
  /* INKqa12722 */
  if (INKUrlHttpFragmentSet(negHdrBuf, negUrlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpFragmentSet");
  }

  /* INKUrlHttpParamsGet */
  if (INKUrlHttpParamsGet(NULL, negUrlLoc, &iUrlParamsLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpParmsGet");
  }
  if (INKUrlHttpParamsGet(negHdrBuf, NULL, &iUrlParamsLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpParmsGet");
  }
  if (INKUrlHttpParamsGet(negHdrBuf, negUrlLoc, NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpParmsGet");
  }

  /* INKUrlHttpParamsSet */
  if (INKUrlHttpParamsSet(NULL, negUrlLoc, "test-params", strlen("test-params")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpParamsSet");
  }
  if (INKUrlHttpParamsSet(negHdrBuf, NULL, "test-params", strlen("test-params")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpParamsSet");
  }
  if (INKUrlHttpParamsSet(negHdrBuf, negUrlLoc, NULL, 0) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpParamsSet");
  }
  /* INKqa12722 */
  if (INKUrlHttpParamsSet(negHdrBuf, negUrlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpParamsSet");
  }

  /* INKUrlHttpQueryGet(); */
  if (INKUrlHttpQueryGet(NULL, negUrlLoc, &iUrlQueryLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpQueryGet");
  }
  if (INKUrlHttpQueryGet(negHdrBuf, NULL, &iUrlQueryLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlHttpQueryGet");
  }

  /* INKUrlHttpQuerySet */
  if (INKUrlHttpQuerySet(NULL, negUrlLoc, "test-query", strlen("test-query")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpQuerySet");
  }
  if (INKUrlHttpQuerySet(negHdrBuf, NULL, "test-query", strlen("test-query")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpQuerySet");
  }
  if (INKUrlHttpQuerySet(negHdrBuf, negUrlLoc, NULL, 0) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpQuerySet");
  }
  if (INKUrlHttpQuerySet(negHdrBuf, negUrlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlHttpQuerySet");
  }

  /* INKUrlLengthGet */
  if (INKUrlLengthGet(NULL, negUrlLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlLengthGet");
  }
  if (INKUrlLengthGet(negHdrBuf, NULL) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlLengthGet");
  }

  /* INKUrlPasswordGet */
  if (INKUrlPasswordGet(NULL, negUrlLoc, &iUrlPasswordLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlPasswordGet");
  }
  if (INKUrlPasswordGet(negHdrBuf, NULL, &iUrlPasswordLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlPasswordGet");
  }
  if (INKUrlPasswordGet(negHdrBuf, negUrlLoc, NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlPasswordGet");
  }

  /* INKUrlPasswordSet */
  if (INKUrlPasswordSet(NULL, negUrlLoc, "clear-text-password", strlen("clear-text-password"))
      != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPasswordSet");
  }
  if (INKUrlPasswordSet(negHdrBuf, NULL, "clear-text-password", strlen("clear-text-password"))
      != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPasswordSet");
  }
  if (INKUrlPasswordSet(negHdrBuf, negUrlLoc, NULL, 0) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPasswordSet");
  }
  if (INKUrlPasswordSet(negHdrBuf, negUrlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPasswordSet");
  }

  /* INKUrlPathGet */
  if (INKUrlPathGet(NULL, negUrlLoc, &iUrlPathLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlPathGet");
  }
  if (INKUrlPathGet(negHdrBuf, NULL, &iUrlPathLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlPathGet");
  }
  if (INKUrlPathGet(negHdrBuf, negUrlLoc, NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlPathGet");
  }

  /* INKUrlPathSet */
  if (INKUrlPathSet(NULL, negUrlLoc, "testing/sample/path", strlen("testing/sample/path")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPathSet");
  }
  if (INKUrlPathSet(negHdrBuf, NULL, "testing/sample/path", strlen("testing/sample/path")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPathSet");
  }
  if (INKUrlPathSet(negHdrBuf, negUrlLoc, NULL, 0) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPathSet");
  }
  if (INKUrlPathSet(negHdrBuf, negUrlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPathSet");
  }

  /* INKUrlPortGet */
  if (INKUrlPortGet(NULL, negUrlLoc) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPortGet");
  }
  if (INKUrlPortGet(negHdrBuf, NULL) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPortGet");
  }

  /* INKUrlPortSet */
  if (INKUrlPortSet(NULL, negUrlLoc, 13150) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPortSet");
  }
  if (INKUrlPortSet(negHdrBuf, NULL, 13150) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPortSet");
  }
  /* FIXME: INKqa12722 */
  if (INKUrlPortSet(negHdrBuf, negUrlLoc, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlPortSet");
  }

  /* INKUrlSchemeGet */
  if (INKUrlSchemeGet(NULL, negUrlLoc, &iUrlSchemeLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlSchemeGet");
  }
  if (INKUrlSchemeGet(negHdrBuf, NULL, &iUrlSchemeLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlSchemeGet");
  }
  if (INKUrlSchemeGet(negHdrBuf, negUrlLoc, NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlSchemeGet");
  }

  /* INKUrlSchemeSet */
  if (INKUrlSchemeSet(NULL, negUrlLoc, "test-scheme", strlen("test-scheme")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlSchemeSet");
  }
  if (INKUrlSchemeSet(negHdrBuf, NULL, "test-scheme", strlen("test-scheme")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlSchemeSet");
  }
  if (INKUrlSchemeSet(negHdrBuf, negUrlLoc, NULL, 0) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlSchemeSet");
  }
  if (INKUrlSchemeSet(negHdrBuf, negUrlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlSchemeSet");
  }

  /* INKUrlUserGet */
  if (INKUrlUserGet(NULL, negUrlLoc, &iUrlUserLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlUserGet");
  }
  if (INKUrlUserGet(negHdrBuf, NULL, &iUrlUserLength) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlUserGet");
  }
  if (INKUrlUserGet(negHdrBuf, negUrlLoc, NULL) != INK_ERROR_PTR) {
    LOG_NEG_ERROR("INKUrlUserGet");
  }

  /* INKUrlUserSet */
  if (INKUrlUserSet(NULL, negUrlLoc, "test-user", strlen("test-user")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlUserSet");
  }
  if (INKUrlUserSet(negHdrBuf, NULL, "test-user", strlen("test-user")) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlUserSet");
  }
  /* FIXME: INKqa12722 */
  if (INKUrlUserSet(negHdrBuf, negUrlLoc, NULL, 0) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlUserSet");
  }
  /* FIXME: INKqa12722: This cause TS crash */
  if (INKUrlUserSet(negHdrBuf, negUrlLoc, NULL, -1) != INK_ERROR) {
    LOG_NEG_ERROR("INKUrlUserSet");
  }

  /* INKUrlParse */
  pUrlParseStart = urlParseStr;
  pUrlParseEnd = pUrlParseStart + strlen(pUrlParseStart);

  if (INKUrlParse(NULL, negUrlLoc, &pUrlParseStart, pUrlParseEnd) != INK_PARSE_ERROR) {
    LOG_NEG_ERROR("INKUrlParse");
  }
  if (INKUrlParse(negHdrBuf, NULL, &pUrlParseStart, pUrlParseEnd) != INK_PARSE_ERROR) {
    LOG_NEG_ERROR("INKUrlParse");
  }

  if (INKUrlParse(negHdrBuf, negUrlLoc, NULL, pUrlParseEnd) != INK_PARSE_ERROR) {
    LOG_NEG_ERROR("INKUrlParse");
  }
  /* FIXME: INKqa12929: */
  if (INKUrlParse(negHdrBuf, negUrlLoc, &pUrlParseStart, NULL) != INK_PARSE_ERROR) {
    LOG_NEG_ERROR("INKUrlParse");
  }

  /* Clean-up */
  HANDLE_RELEASE(negHdrBuf, INK_NULL_MLOC, negUrlLoc);

  URL_DESTROY(negHdrBuf, negUrlLoc);
  BUFFER_DESTROY(negHdrBuf);
}

/****************************************************************************
 * handleReadRequest:
 *
 * Description: handler for INK_HTTP_READ_REQUEST_HDR_HOOK
 ****************************************************************************/

static void
handleReadRequest(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleReadRequest");

  INKMBuffer reqHdrBuf = NULL, parseBuffer = NULL, newHdrBuf1 = NULL, newHdrBuf2 = NULL;
  INKMLoc reqHttpHdrLoc = NULL, newHttpHdrLoc1 = NULL, parseHttpHdrLoc = NULL;
  INKMLoc reqUrlLoc = NULL, newUrlLoc1 = NULL, newUrlLoc2 = NULL, parseUrlLoc = NULL;

  INKHttpType httpType;

  HttpMsgLine_T *pReqMsgLine = NULL, *pNewReqMsgLine = NULL, *pParseReqMsgLine = NULL;

  int parseStatus, iUrlFragmentLength;
  const char *pUrlParseStart, *pUrlParseEnd, *sUrlFragment;
  const char *urlParseStr = "http://joe:bolts4USA@www.joes-hardware.com/cgi-bin/inventory?product=hammer43";


  INKDebug(REQ, "\n>>>>>> handleReadRequest <<<<<<<");

  /* Get Request Marshall Buffer */
  if (!INKHttpTxnClientReqGet(pTxn, &reqHdrBuf, &reqHttpHdrLoc)) {
    LOG_API_ERROR_COMMENT("INKHttpTxnClientReqGet", "ERROR: Can't retrieve client req hdr; abnormal exit");
    goto done;
  }


        /******** (1): Simply print the URL details of the request header **************/

  INKDebug(REQ, "--------------------------------");

  pReqMsgLine = initMsgLine();

        /*** INKHttpHdrUrlGet ***/
  if ((reqUrlLoc = INKHttpHdrUrlGet(reqHdrBuf, reqHttpHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrUrlGet", "ERROR: abnormal exit");
    goto done;
  }
  storeHdrInfo(pReqMsgLine, reqHdrBuf, reqHttpHdrLoc, reqUrlLoc, REQ, 1);

#ifdef DEBUG
  printf("=================\n");
  negTesting(reqHdrBuf, reqUrlLoc);
#endif

        /******** (2): Do a *header* copy and print URL details of the new buffer **********/

  /* Header copy also copies the URL, so we can still print URL pieces */

  INKDebug(REQ, "--------------------------------");
  pNewReqMsgLine = initMsgLine();

  if ((newHdrBuf1 = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKMBufferCreate", "skipping to section 5");
    goto section_5;             /* Skip to section (5) down the line directly; I hate GOTOs too :-) */
  }

        /*** INKHttpHdrCreate ***/
  if ((newHttpHdrLoc1 = INKHttpHdrCreate(newHdrBuf1)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrCreate", "skipping to section 5");
    goto section_5;             /* Skip to section (5) down the line directly; */
  }

  /* Make sure the newly created HTTP header has INKHttpType value of INK_HTTP_TYPE_UNKNOWN */
  if ((httpType = INKHttpHdrTypeGet(newHdrBuf1, newHttpHdrLoc1)) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrTypeGet", "still continuing");
  } else if (httpType != INK_HTTP_TYPE_UNKNOWN) {
    LOG_API_ERROR_COMMENT("INKHttpHdrTypeGet", "New created hdr not of type INK_HTTP_TYPE_UNKNOWN");
  }

  /* set the HTTP header type: a new buffer has a type INK_HTTP_TYPE_UNKNOWN by default */
  if (INKHttpHdrTypeSet(newHdrBuf1, newHttpHdrLoc1, INK_HTTP_TYPE_REQUEST) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrTypeSet", "continuing");
  }
  if (INKHttpHdrTypeGet(newHdrBuf1, newHttpHdrLoc1) != INK_HTTP_TYPE_REQUEST) {
    LOG_AUTO_ERROR("INKHttpHdrTypeGet", "Type not set to INK_HTTP_TYPE_REQUEST");
  }

        /*** INKHttpHdrCopy ***/
  /* Note: This should also copy the URL string */
  if (INKHttpHdrCopy(newHdrBuf1, newHttpHdrLoc1, reqHdrBuf, reqHttpHdrLoc) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrCopy", "continuing");
  }

        /*** INKHttpHdrUrlGet ***/
  if ((newUrlLoc1 = INKHttpHdrUrlGet(newHdrBuf1, newHttpHdrLoc1)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrUrlGet", "skipping to section 5");
    goto section_5;
  }
  storeHdrInfo(pNewReqMsgLine, newHdrBuf1, newHttpHdrLoc1, newUrlLoc1, REQ, 2);
  if (!identicalURL(pNewReqMsgLine, pReqMsgLine)) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "New req buffer not identical to the original");
  }


        /******* (3): Now tweak some of the URL components of the same new header *******/
  INKDebug(REQ, "--------------------------------");

  if ((newUrlLoc1 = INKHttpHdrUrlGet(newHdrBuf1, newHttpHdrLoc1)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrUrlGet", "skipping to section 5");
    goto section_5;
  }

  setCustomUrl(newHdrBuf1, newHttpHdrLoc1);
  /*setCustomUrl(newHdrBuf1, custUrl); */

  freeMsgLine(pNewReqMsgLine);
  storeHdrInfo(pNewReqMsgLine, newHdrBuf1, newHttpHdrLoc1, newUrlLoc1, REQ, 3);


        /********* (4): Now do a *URL* copy from request to the above buffer and print the details **********/
  INKDebug(REQ, "--------------------------------");

  if ((reqUrlLoc = INKHttpHdrUrlGet(reqHdrBuf, reqHttpHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrUrlGet", "skipping to section 5");
    goto section_5;
  }

  if ((newUrlLoc1 = INKUrlCreate(newHdrBuf1)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKUrlCreate", "skipping to section 5");
    goto section_5;
  }


        /*** INKUrlCopy ***/
  if (INKUrlCopy(newHdrBuf1, newUrlLoc1, reqHdrBuf, reqUrlLoc) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKUrlCopy", "skipping to section 5");
    goto section_5;
  }

  freeMsgLine(pNewReqMsgLine);
  storeHdrInfo(pNewReqMsgLine, newHdrBuf1, (INKMLoc) NULL, newUrlLoc1, REQ, 4);
  if (!identicalURL(pNewReqMsgLine, pReqMsgLine)) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "New req buffer not identical to the original");
  }


section_5:

        /********* (5): Create a new buffer and do a URL copy immediately from req buffer *********/
  INKDebug(REQ, "--------------------------------");

  if ((reqUrlLoc = INKHttpHdrUrlGet(reqHdrBuf, reqHttpHdrLoc)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrUrlGet", "abnormal exit");
    goto done;
  }

  if ((newHdrBuf2 = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKMBufferCreate", "abnormal exit");
    goto done;
  }
  if ((newUrlLoc2 = INKUrlCreate(newHdrBuf2)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKUrlCreate", "abnormal exit");
    goto done;
  }

        /*** INKUrlCopy ***/
  if (INKUrlCopy(newHdrBuf2, newUrlLoc2, reqHdrBuf, reqUrlLoc) == INK_ERROR) {
    LOG_API_ERROR_COMMENT("INKUrlCopy", "abnormal exit");
    goto done;
  }

  freeMsgLine(pNewReqMsgLine);
  storeHdrInfo(pNewReqMsgLine, newHdrBuf2, (INKMLoc) NULL, newUrlLoc2, REQ, 5);
  if (!identicalURL(pNewReqMsgLine, pReqMsgLine)) {
    LOG_AUTO_ERROR("INKHttpHdrCopy", "New req buffer not identical to the original");
  }


        /*********** (6): Parse Buffer *************/

  INKDebug(REQ, "--------------------------------");

  pParseReqMsgLine = initMsgLine();

  /* Create a parser Buffer and header location */
  if ((parseBuffer = INKMBufferCreate()) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKMBufferCreate", "abnormal exit");
    goto done;
  } else if ((parseHttpHdrLoc = INKHttpHdrCreate(parseBuffer)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKHttpHdrCreate", "abnormal exit");
    goto done;
  } else if ((parseUrlLoc = INKUrlCreate(parseBuffer)) == INK_ERROR_PTR) {
    LOG_API_ERROR_COMMENT("INKUrlCreate", "abnormal exit");
    goto done;
  }

  /* Set the hdr type to REQUEST */
  if (INKHttpHdrTypeSet(parseBuffer, parseHttpHdrLoc, INK_HTTP_TYPE_REQUEST) == INK_ERROR) {
    LOG_API_ERROR("INKHttHdrTypeSet");
  }

  pUrlParseStart = urlParseStr;
  pUrlParseEnd = pUrlParseStart + strlen(pUrlParseStart);

  if ((parseStatus = INKUrlParse(parseBuffer, parseUrlLoc, &pUrlParseStart, pUrlParseEnd))
      == INK_PARSE_ERROR) {
    LOG_API_ERROR_COMMENT("INKUrlParse", "abnormal exit");
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
  HANDLE_RELEASE(reqHdrBuf, INK_NULL_MLOC, reqHttpHdrLoc);

  HANDLE_RELEASE(newHdrBuf1, newHttpHdrLoc1, newUrlLoc1);
  HANDLE_RELEASE(newHdrBuf1, INK_NULL_MLOC, newHttpHdrLoc1);

  /*INKHandleMLocRelease (newHdrBuf2, newHttpHdrLoc2, newUrlLoc2); */
  /*INKHandleMLocRelease (newHdrBuf2, INK_NULL_MLOC, newHttpHdrLoc2); */
  HANDLE_RELEASE(newHdrBuf2, INK_NULL_MLOC, newUrlLoc2);

  HANDLE_RELEASE(parseBuffer, parseHttpHdrLoc, parseUrlLoc);
  HANDLE_RELEASE(parseBuffer, INK_NULL_MLOC, parseHttpHdrLoc);

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

  if (INKHttpTxnReenable(pTxn, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_API_ERROR("INKHttpTxnReenable");
  }

  INKDebug(REQ, "..... exiting handleReadRequest ......");

}                               /* handleReadReadRequest */




static void
handleTxnStart(INKCont pCont, INKHttpTxn pTxn)
{
  LOG_SET_FUNCTION_NAME("handleTxnStart");

  if (INKHttpTxnHookAdd(pTxn, INK_HTTP_READ_REQUEST_HDR_HOOK, pCont) == INK_ERROR) {
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
    break;
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    handleReadRequest(pCont, pTxn);
    break;
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
