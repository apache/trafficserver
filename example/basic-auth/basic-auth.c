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

#include <ts/ts.h>


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
  const char *ptr;

  char *user, *password;
  int authval_length;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header\n");
    goto done;
  }

  field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, INK_MIME_FIELD_PROXY_AUTHORIZATION, INK_MIME_LEN_PROXY_AUTHORIZATION);
  if (!field_loc) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    INKError("no Proxy-Authorization field\n");
    goto done;
  }

  if (INKMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, 0, &val, &authval_length) != INK_SUCCESS) {
    INKError("no value in Proxy-Authorization field\n");
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }
  ptr = val;

  if (strncmp(ptr, "Basic", 5) != 0) {
    INKError("no Basic auth type in Proxy-Authorization\n");
    INKHandleStringRelease(bufp, field_loc, val);
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  ptr += 5;
  while ((*ptr == ' ') || (*ptr == '\t')) {
    ptr += 1;
  }

  user = base64_decode(ptr);
  password = strchr(user, ':');
  if (!password) {
    INKError("no password in authorization information\n");
    INKfree(user);
    INKHandleStringRelease(bufp, field_loc, val);
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }
  *password = '\0';
  password += 1;

  if (!authorized(user, password)) {
    INKError("%s:%s not authorized\n", user, password);
    INKfree(user);
    INKHandleStringRelease(bufp, field_loc, val);
    INKHandleMLocRelease(bufp, hdr_loc, field_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  INKfree(user);
  INKHandleStringRelease(bufp, field_loc, val);
  INKHandleMLocRelease(bufp, hdr_loc, field_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
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
  const char *insert = "Basic realm=\"proxy\"";
  int len = strlen(insert);

  if (!INKHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client response header\n");
    goto done;
  }

  INKHttpHdrStatusSet(bufp, hdr_loc, INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED);
  INKHttpHdrReasonSet(bufp, hdr_loc,
                      INKHttpHdrReasonLookup(INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED),
                      strlen(INKHttpHdrReasonLookup(INK_HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED)));

  field_loc = INKMimeHdrFieldCreate(bufp, hdr_loc);
  INKMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, INK_MIME_FIELD_PROXY_AUTHENTICATE, INK_MIME_LEN_PROXY_AUTHENTICATE);
  INKMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, -1,  insert, len);
  INKMimeHdrFieldAppend(bufp, hdr_loc, field_loc);

  INKHandleMLocRelease(bufp, hdr_loc, field_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

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

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 2.0 */
    if (major_ts_version >= 2) {
      result = 1;
    }

  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  int i, cc;
  INKPluginRegistrationInfo info;

  info.plugin_name = "basic-authorization";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 2.0 or later\n");
    return;
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

