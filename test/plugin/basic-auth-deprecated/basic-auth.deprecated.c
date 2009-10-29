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

/* basic-auth.c: a plugin that performs basic HTTP proxy
 *               authentication
 *
 * 
 * Usage: 
 * (NT): BasicAuth.dll 
 * (Solaris): basic-auth.so
 *
 */


#include <stdio.h>
#include <string.h>

#if !defined (_WIN32)
#include <unistd.h>
#else
#include <windows.h>
#endif

#include "InkAPI.h"


static char base64_codes[256];


static char *
base64_decode(const char *input)
{
#define decode(A) ((unsigned int) base64_codes[(int) input[A]])

  char *output;
  char *obuf;
  int len;

  for (len = 0; (input[len] != '\0') && (input[len] != '='); len++);

  output = obuf = (char *) INKmalloc((len * 6) / 8 + 3);

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
   * password. Sample NT code is provided for illustration.
   * For UNIX systems, enter your own authorization code
   * here. 
   */

#if !defined (_WIN32)

#else
  // LogonUser() will work only if the account is set with 
  // SE_TCB_NAME privilege. If SE_TCB_NAME is missing,
  // Traffic server will attempt to  add this privilege to
  // the running account, but may fail depending on the access
  // levels provided to the said account. In such a case, an 
  // NT systems administrator will have to set the privilege 
  // "Act as part of the operating system" from the NT user manager.
  // 
  int nErr = 0;
  HANDLE hToken = 0;
  BOOL bRet = LogonUser(user, NULL, password, LOGON32_LOGON_NETWORK,
                        LOGON32_PROVIDER_DEFAULT, &hToken);

  if (FALSE == bRet) {
    nErr = GetLastError();
    return 0;
  }

  CloseHandle(hToken);
#endif
  return 1;
}

static void
handle_dns(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc field_loc;
  const char *val;
  char *user, *password;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header\n");
    goto done;
  }

  field_loc = INKMimeHdrFieldRetrieve(bufp, hdr_loc, INK_MIME_FIELD_PROXY_AUTHORIZATION);
  if (!field_loc) {
    INKError("no Proxy-Authorization field\n");
    goto done;
  }

  val = INKMimeFieldValueGet(bufp, field_loc, 0, NULL);

  if (!val) {
    INKError("no value in Proxy-Authorization field\n");
    goto done;
  }

  if (strncmp(val, "Basic", 5) != 0) {
    INKError("no Basic auth type in Proxy-Authorization\n");
    goto done;
  }

  val += 5;
  while ((*val == ' ') || (*val == '\t')) {
    val += 1;
  }

  user = base64_decode(val);
  password = strchr(user, ':');
  if (!password) {
    INKError("no password in authorization information\n");
    goto done;
  }
  *password = '\0';
  password += 1;

  if (!authorized(user, password)) {
    INKError("%s:%s not authorized\n", user, password);
    INKfree(user);
    goto done;
  }

  INKfree(user);
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  return;

done:
  INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_ERROR);
}

static void
handle_response(INKHttpTxn txnp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc field_loc;

  if (!INKHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client response header\n");
    goto done;
  }

  INKHttpHdrStatusSet(bufp, hdr_loc, INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  INKHttpHdrReasonSet(bufp, hdr_loc, INKHttpHdrReasonLookup(INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED), -1);

  field_loc = INKMimeFieldCreate(bufp);
  INKMimeFieldNameSet(bufp, field_loc, INK_MIME_FIELD_PROXY_AUTHENTICATE, -1);
  INKMimeFieldValueInsert(bufp, field_loc, "Basic realm=\"proxy\"", -1, -1);
  INKMimeHdrFieldInsert(bufp, hdr_loc, field_loc, -1);

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}

static int
auth_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_OS_DNS:
    handle_dns(txnp, contp);
    return 0;
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    handle_response(txnp);
    return 0;
  default:
    break;
  }

  return 0;
}


void
INKPluginInit(int argc, const char *argv[])
{
  int i, cc;


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

  INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, INKContCreate(auth_plugin, NULL));
}


#if defined (_WIN32)
BOOL APIENTRY
DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH:
  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
  case DLL_PROCESS_DETACH:
    break;
  }
  return TRUE;
}
#endif
