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


#include <InkAPI.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

const char *EXPECTED_REDIRECT_CNT = "x-expected_redirect_cnt";
int EXPECTED_REDIRECT_CNT_LEN;
const char *ACTUAL_REDIRECT_CNT = "x-actual_redirect_cnt";
int ACTUAL_REDIRECT_CNT_LEN;
const char *ACTUAL_REDIRECT_ATTEMPT = "x-actual_redirect_attempt";
int ACTUAL_REDIRECT_ATTEMPT_LEN;


const char *EXPECTED_REDIRECT_URL = "x-expected_redirect_url";
int EXPECTED_REDIRECT_URL_LEN;

const char *EXPECTED_CALL_RESULT = "x-expected_call_result";
int EXPECTED_CALL_RESULT_LEN;
const char *ACTUAL_CALL_RESULT = "x-actual_call_result";
int ACTUAL_CALL_RESULT_LEN;

const char *EXPECTED_TEST_RESULT = "x-expected_test_result";
int EXPECTED_TEST_RESULT_LEN;
const char *ACTUAL_TEST_RESULT = "x-actual_test_result";
int ACTUAL_TEST_RESULT_LEN;

const char *INTERFACE_NAME = "x-api_interface_name";
int INTERFACE_NAME_LEN;

const char *TEST_PASS = "pass";
const char *TEST_FAIL = "fail";

int SEPARATOR_TOKEN = '=';

static void
constDataInit()
{
  EXPECTED_REDIRECT_CNT_LEN = strlen(EXPECTED_REDIRECT_CNT);
  ACTUAL_REDIRECT_CNT_LEN = strlen(ACTUAL_REDIRECT_CNT);

  EXPECTED_REDIRECT_URL_LEN = strlen(EXPECTED_REDIRECT_URL);
  EXPECTED_CALL_RESULT_LEN = strlen(EXPECTED_CALL_RESULT);

  ACTUAL_CALL_RESULT_LEN = strlen(ACTUAL_CALL_RESULT);

  ACTUAL_TEST_RESULT_LEN = strlen(ACTUAL_TEST_RESULT);

  EXPECTED_TEST_RESULT_LEN = strlen(EXPECTED_TEST_RESULT);
  INTERFACE_NAME_LEN = strlen(INTERFACE_NAME);

  ACTUAL_REDIRECT_ATTEMPT_LEN = strlen(ACTUAL_REDIRECT_ATTEMPT);
}


char *
mimeValueGet(const char *pval, int separator_token)
{
  char *ptr = NULL;
  if (!pval)
    return NULL;
  ptr = strchr(pval, separator_token);
  if (!ptr) {
    INKDebug("INKHttpTxnRedirectRequest", "\n mimeValueGet: in [%s], separator [%c] !found \n", pval, separator_token);
    return NULL;
  }
  ptr++;
  if (!ptr) {
    INKDebug("INKHttpTxnRedirectRequest", "\n mimeValueGet: in [%s], value after separator !found \n", pval);
    return NULL;
  }
  return INKstrndup(ptr, strlen(ptr));
}

/* 
 *	incoming: mimeHdr  (must be null terminated)
 * 	incoming: lenHdr
 *	outgoing: mimeValue 
 * 
 * If mimeValue is not found, mimeValue pointer is not set.
*/
static int
getTestParam(INKMBuffer buff, INKMLoc loc, const char *mimeHdr, int lenHdr, void **mimeValue)
{
  INKMLoc field_loc;
  const char *ptr, *nptr;
  int len;

#ifdef DEBUG_DUMP
  dump_field_mloc(buff, loc);
#endif

  field_loc = INKMimeHdrFieldFind(buff, loc, mimeHdr, lenHdr);
  if (!field_loc) {
    INKDebug("INKHttpTxnRedirectRequest", "\n getTestParam: INKMimeHdrFieldFind did not find %s\n", mimeHdr);
    return 0;
  }

  ptr = INKMimeHdrFieldValueGet(buff, loc, field_loc, -1, &len);
  if (!ptr || (len == 0)) {
    INKDebug("INKHttpTxnRedirectRequest", "\n getTestParam: INKMimeHdrFieldValueGet did not find %s \n", mimeHdr);
    return 0;
  }

  *mimeValue = (void *) INKstrndup(ptr, strlen(ptr));
  INKDebug("INKHttpTxnRedirectRequest", "\n getTestParam: hdr = [%s], val = [%s]\n", mimeHdr, *(char **) mimeValue);

  INKHandleStringRelease(buff, field_loc, ptr);
  INKHandleMLocRelease(buff, loc, field_loc);

  return 1;
}


/*	
 *		incoming: mimeEntry
 * 		outgoing: none
 *      	txn outgoing: values from mimeValue
*/
static int
setTestResult(INKMBuffer buff, INKMLoc loc, const char *mimeHdr, int mimeHdrLen, char *mimeValues)
{
  INKMLoc field_offset, dupLoc;
  char *action = NULL;

  /* Walk the list of MIME entries and append values to first duplicate
   *       found in list. Other duplicates will not be affected.
   */
  dupLoc = INKMimeHdrFieldFind(buff, loc, mimeHdr, mimeHdrLen);
  if (dupLoc) {
    int re = 0;

    action = " appended ";

    INKMimeHdrFieldDelete(buff, loc, dupLoc);
    INKHandleMLocRelease(buff, loc, dupLoc);
  } else {
    action = " new insert ";
  }

  /* Insert mimeHdr */
  field_offset = INKMimeHdrFieldCreate(buff, loc);
  if (!field_offset) {
    INKDebug("INKHttpTxnRedirectRequest", "\n setTestResult: INKMimeHdrFieldCreate failed ");
    return 0;
  }

  /* insert append, position is not significant */
  INKMimeHdrFieldInsert(buff, loc, field_offset, -1);
  INKMimeHdrFieldNameSet(buff, loc, field_offset, mimeHdr, strlen(mimeHdr));

  /* insert append */
  INKMimeHdrFieldValueInsert(buff, loc, field_offset, mimeValues, strlen(mimeValues), -1);

  INKHandleMLocRelease(buff, loc, field_offset);
  INKHandleMLocRelease(buff, INK_NULL_MLOC, loc);

  INKDebug("INKHttpTxnRedirectRequest", "\n setTestResult: %s  [%s: %s] \n", action, mimeHdr, mimeValues);
  return 1;
}

static int
TxnInit(INKHttpTxn txn)
{
  int re = 1;
  INKMBuffer reqBuf;
  INKMLoc reqLoc;
  char buf[BUFSIZ];

  if (!(re = INKHttpTxnClientReqGet(txn, &reqBuf, &reqLoc))) {
    INKDebug("INKHttpTxnRedirectRequest", "\n TxnInit: INKHttpTxnClientReqGet failed \n");
    return re;
  }
  sprintf(buf, " value=%d", 0);
  setTestResult(reqBuf, reqLoc, ACTUAL_REDIRECT_ATTEMPT, ACTUAL_REDIRECT_ATTEMPT_LEN, buf);
  setTestResult(reqBuf, reqLoc, ACTUAL_REDIRECT_CNT, ACTUAL_REDIRECT_CNT_LEN, buf);

  INKHandleMLocRelease(reqBuf, reqLoc, INK_NULL_MLOC);
  INKDebug("INKHttpTxnRedirectRequest", "\n TxnInit: done\n");
  return re;
}

static int
CountRedirects(INKHttpTxn txn)
{
  void *pval = NULL, *pfield = NULL;
  INKMBuffer reqBuf, reqLoc;
  char buf[BUFSIZ];
  int actualRedirAttempted;

  if (!INKHttpTxnClientReqGet(txn, &reqBuf, &reqLoc))
    return 0;

  getTestParam(reqBuf, reqLoc, ACTUAL_REDIRECT_ATTEMPT, ACTUAL_REDIRECT_ATTEMPT_LEN, &pfield);
  pval = mimeValueGet((const char *) pfield, SEPARATOR_TOKEN);
  INKfree(pfield);
  pfield = NULL;

  actualRedirAttempted = atoi((const char *) pval);
  INKfree(pval);

  if (actualRedirAttempted > 0) {
    void *pval2 = NULL;
    int actualRedirCnt, expectedRedirCnt;

    getTestParam(reqBuf, reqLoc, ACTUAL_REDIRECT_CNT, ACTUAL_REDIRECT_CNT_LEN, &pfield);
    pval2 = mimeValueGet((const char *) pfield, SEPARATOR_TOKEN);
    INKfree(pfield);
    pfield = NULL;
    actualRedirCnt = atoi((const char *) pval2);
    INKfree(pval2);
    actualRedirCnt++;

    sprintf(buf, " value=%d", actualRedirCnt);
    setTestResult(reqBuf, reqLoc, ACTUAL_REDIRECT_CNT, ACTUAL_REDIRECT_CNT_LEN, buf);


    getTestParam(reqBuf, reqLoc, EXPECTED_REDIRECT_CNT, EXPECTED_REDIRECT_CNT_LEN, &pfield);
    pval2 = mimeValueGet((const char *) pfield, SEPARATOR_TOKEN);
    INKfree(pfield);
    pfield = NULL;
    expectedRedirCnt = atoi((const char *) pval2);
    INKfree(pval2);

    INKDebug("INKHttpTxnRedirectRequest",
             "\n CountRedirects: txn [0x%08x] attempted=%d actual=%d expected=%d\n",
             txn, actualRedirAttempted, actualRedirCnt, expectedRedirCnt);
  }
  INKHandleMLocRelease(reqBuf, reqLoc, INK_NULL_MLOC);
  return 1;
}


static int
Request2Response(INKHttpTxn txn)
{
  int re = 1;
  void *pField = NULL, *pVal = NULL;
  INKMBuffer reqBuf;
  INKMLoc reqLoc;
  INKMBuffer respBuf;
  INKMLoc respLoc;
  char buf[BUFSIZ];

  if (!(re = INKHttpTxnClientReqGet(txn, &reqBuf, &reqLoc))) {
    return re;
  }
  if (!(re = INKHttpTxnClientRespGet(txn, &respBuf, &respLoc))) {
    return re;
  }

  getTestParam(reqBuf, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, &pField);
  setTestResult(respBuf, respLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, (char *) pField);
  INKfree(pField);
  pField = NULL;

  getTestParam(reqBuf, reqLoc, ACTUAL_REDIRECT_CNT, ACTUAL_REDIRECT_CNT_LEN, &pField);
  setTestResult(respBuf, respLoc, ACTUAL_REDIRECT_CNT, ACTUAL_REDIRECT_CNT_LEN, (char *) pField);
  INKfree(pField);
  pField = NULL;

  getTestParam(reqBuf, reqLoc, ACTUAL_REDIRECT_ATTEMPT, ACTUAL_REDIRECT_ATTEMPT_LEN, &pField);
  setTestResult(respBuf, respLoc, ACTUAL_REDIRECT_ATTEMPT, ACTUAL_REDIRECT_ATTEMPT_LEN, (char *) pField);
  INKfree(pField);
  pField = NULL;


  getTestParam(reqBuf, reqLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, &pField);
  setTestResult(respBuf, respLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, (char *) pField);
  INKfree(pField);
  pField = NULL;

  sprintf(buf, "%s", "INKHttpTxnRedirectRequest");
  setTestResult(respBuf, respLoc, INTERFACE_NAME, INTERFACE_NAME_LEN, buf);

  INKDebug("INKHttpTxnRedirectRequest", "\n Request2Response: completed \n");

  INKHandleMLocRelease(reqBuf, reqLoc, INK_NULL_MLOC);
  INKHandleMLocRelease(respBuf, respLoc, INK_NULL_MLOC);
  return re;
}

static int
RedirectRequest(INKHttpTxn txn)
{
  int re = 0;
  INKMBuffer reqBuf;
  INKMLoc reqLoc;
  INKMBuffer redirBuf;
  INKMLoc redirLoc;
  void *pField = NULL;
  char *pVal = NULL;
  int actualRedirCnt = 0, expectedRedirCnt = 0;

  if (!INKHttpTxnClientReqGet(txn, &reqBuf, &reqLoc)) {
    INKDebug("INKHttpTxnRedirectRequest", "RedirectRequest: INKHttpTxnClientReqGet: failed");
    return 0;
  }

  getTestParam(reqBuf, reqLoc, EXPECTED_REDIRECT_CNT, EXPECTED_REDIRECT_CNT_LEN, &pField);
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  INKfree(pField);
  pField = NULL;
  expectedRedirCnt = atoi((char *) pVal);
  INKfree(pVal);

  getTestParam(reqBuf, reqLoc, ACTUAL_REDIRECT_CNT, ACTUAL_REDIRECT_CNT_LEN, &pField);
  pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
  INKfree(pField);
  pField = NULL;
  actualRedirCnt = atoi((char *) pVal);
  INKfree(pVal);

  if (actualRedirCnt < expectedRedirCnt) {

    const char *redirUrl, *startRedirUrl;
    int redirectAttempted = 0, expectedCallResult = 0;
    redirBuf = INKMBufferCreate();
    redirLoc = INKUrlCreate(redirBuf);
    char buf[BUFSIZ];

    getTestParam(reqBuf, reqLoc, EXPECTED_REDIRECT_URL, EXPECTED_REDIRECT_URL_LEN, &pField);
    pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
    INKfree(pField);
    pField = NULL;
    redirUrl = (char *) pVal;
    startRedirUrl = redirUrl;

    getTestParam(reqBuf, reqLoc, ACTUAL_REDIRECT_ATTEMPT, ACTUAL_REDIRECT_ATTEMPT_LEN, &pField);
    pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
    INKfree(pField);
    pField = NULL;
    redirectAttempted = atoi((char *) pVal);
    INKfree(pVal);

    re = INKUrlParse(redirBuf, redirLoc, &redirUrl, (redirUrl + (strlen(redirUrl) + 1)));
    if (re != INK_PARSE_DONE) {
      INKDebug("INKHttpTxnRedirectRequest", "\n RedirectRequest: INKParse failed ");
      return 0;
    }

    re = INKHttpTxnRedirectRequest(txn, redirBuf, redirLoc);

    redirectAttempted++;
    sprintf(buf, " value=%d", redirectAttempted);
    setTestResult(reqBuf, reqLoc, ACTUAL_REDIRECT_ATTEMPT, ACTUAL_REDIRECT_ATTEMPT_LEN, buf);

    INKDebug("INKHttpTxnRedirectRequest", "\n attempt redirect %d to [%s]\n", redirectAttempted, startRedirUrl);

    sprintf(buf, " got=%d", re);
    setTestResult(reqBuf, reqLoc, ACTUAL_CALL_RESULT, ACTUAL_CALL_RESULT_LEN, buf);

    getTestParam(reqBuf, reqLoc, EXPECTED_CALL_RESULT, EXPECTED_CALL_RESULT_LEN, &pField);
    pVal = mimeValueGet((const char *) pField, SEPARATOR_TOKEN);
    INKfree(pField);
    pField = NULL;
    expectedCallResult = atoi((char *) pVal);
    INKfree(pVal);

    if (re != expectedCallResult)
      sprintf(buf, " result=fail");
    else
      sprintf(buf, " result=pass");
    setTestResult(reqBuf, reqLoc, ACTUAL_TEST_RESULT, ACTUAL_TEST_RESULT_LEN, buf);

    INKfree((void *) startRedirUrl);
    INKUrlDestroy(redirBuf, redirLoc);
    INKMBufferDestroy(redirBuf);
  } else
    INKDebug("INKHttpTxnRedirectRequest",
             "\n completed redirects actual=%d, expected=%d  \n", actualRedirCnt, expectedRedirCnt);

  INKHandleMLocRelease(reqBuf, reqLoc, INK_NULL_MLOC);
  return re;
}

static int
handle_event(INKCont contp, INKEvent event, void *eData)
{
  int re = 1;
  INKHttpTxn txn = (INKHttpTxn) eData;

  switch (event) {

    /* case INK_EVENT_HTTP_TXN_START: */
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    re = TxnInit(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    re = RedirectRequest(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    re = Request2Response(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_SEND_REQUEST_HDR:
    re = CountRedirects(txn);
    INKHttpTxnReenable(txn, INK_EVENT_HTTP_CONTINUE);
    break;

  default:
    re = 0;
  }
  return re;
}

void
INKPluginInit(int argc, const char **argv)
{
  INKCont continuation = INKContCreate(handle_event, INKMutexCreate());
  constDataInit();

  /* INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, continuation); */

  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, continuation);

  INKHttpHookAdd(INK_HTTP_SEND_REQUEST_HDR_HOOK, continuation);
  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, continuation);
  INKHttpHookAdd(INK_HTTP_SEND_RESPONSE_HDR_HOOK, continuation);
}
