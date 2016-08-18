/** @file

  A plugin that performs basic HTTP proxy authentication

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

#include <unistd.h>

#include "ts/ts.h"
#include "ts/ink_defs.h"

static char base64_codes[256];

static char *
base64_decode(const char *input)
{
#define decode(A) ((unsigned int)base64_codes[(int)input[A]])

  char *output;
  char *obuf;
  int len;

  for (len = 0; (input[len] != '\0') && (input[len] != '='); len++)
    ;

  output = obuf = (char *)TSmalloc((len * 6) / 8 + 3);

  while (len > 0) {
    *output++ = decode(0) << 2 | decode(1) >> 4;
    *output++ = decode(1) << 4 | decode(2) >> 2;
    *output++ = decode(2) << 6 | decode(3);
    len -= 4;
    input += 4;
  }

  /*
   * We don't need to worry about leftover bits because
   * we've allocated a few extra characters and if there
   * are leftover bits they will be zeros because the extra
   * inputs will be '='s and '=' decodes to 0.
   */

  *output = '\0';
  return obuf;

#undef decode
}

static int
authorized(char *user, char *password)
{
  /*
   * This routine checks the validity of the user name and
   * password. UNIX systems, enter your own authorization code
   * here. ToDO: This doesn't do anything useful now.
   */
  if (user && password) {
    return 1;
  } else {
    return 0;
  }
}

static void
handle_dns(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc field_loc;
  const char *val;
  const char *ptr;

  char *user, *password;
  int authval_length;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[basic_auth] Couldn't retrieve client request header");
    goto done;
  }

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_PROXY_AUTHORIZATION, TS_MIME_LEN_PROXY_AUTHORIZATION);
  if (!field_loc) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSError("[basic_auth] No Proxy-Authorization field");
    goto done;
  }

  val = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &authval_length);
  if (NULL == val) {
    TSError("[basic_auth] No value in Proxy-Authorization field");
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  ptr = val;
  if (strncmp(ptr, "Basic", 5) != 0) {
    TSError("[basic_auth] No Basic auth type in Proxy-Authorization");
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  ptr += 5;
  while ((*ptr == ' ') || (*ptr == '\t')) {
    ptr += 1;
  }

  user     = base64_decode(ptr);
  password = strchr(user, ':');
  if (!password) {
    TSError("[basic_auth] No password in authorization information");
    TSfree(user);
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }
  *password = '\0';
  password += 1;

  if (!authorized(user, password)) {
    TSError("[basic_auth] %s:%s not authorized", user, password);
    TSfree(user);
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  TSfree(user);
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return;

done:
  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
}

static void
handle_response(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc field_loc;
  const char *insert = "Basic realm=\"proxy\"";
  int len            = strlen(insert);

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[basic_auth] Couldn't retrieve client response header");
    goto done;
  }

  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  TSHttpHdrReasonSet(bufp, hdr_loc, TSHttpHdrReasonLookup(TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED),
                     strlen(TSHttpHdrReasonLookup(TS_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED)));

  TSMimeHdrFieldCreate(bufp, hdr_loc, &field_loc); // Probably should check for errors
  TSMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, TS_MIME_FIELD_PROXY_AUTHENTICATE, TS_MIME_LEN_PROXY_AUTHENTICATE);
  TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, -1, insert, len);
  TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
auth_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_OS_DNS:
    handle_dns(txnp, contp);
    return 0;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_response(txnp);
    return 0;
  default:
    break;
  }

  return 0;
}

void
TSPluginInit(int argc ATS_UNUSED, const char *argv[] ATS_UNUSED)
{
  int i, cc;
  TSPluginRegistrationInfo info;

  info.plugin_name   = "basic-authorization";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[basic_auth] Plugin registration failed.");
  }

  /* Build translation table */
  for (i = 0, cc = 0; i < 256; i++) {
    base64_codes[i] = 0;
  }
  for (i = 'A'; i <= 'Z'; i++) {
    base64_codes[i] = cc++;
  }
  for (i = 'a'; i <= 'z'; i++) {
    base64_codes[i] = cc++;
  }
  for (i = '0'; i <= '9'; i++) {
    base64_codes[i] = cc++;
  }
  base64_codes['+'] = cc++;
  base64_codes['/'] = cc++;

  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, TSContCreate(auth_plugin, NULL));
}
