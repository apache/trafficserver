/** @file

  Test plugin that re-sets the redirect URL on every response hop.

  On TS_HTTP_READ_RESPONSE_HDR_HOOK, for every response with a Location
  header (i.e. every redirect hop), call TSHttpTxnRedirectUrlSet with
  that Location value. This exercises a plugin that calls
  TSHttpTxnRedirectUrlSet on every response hook, and verifies that such
  a plugin cannot follow more redirects than
  proxy.config.http.number_of_redirections allows.

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

#include <string.h>
#include <ts/ts.h>

#define PLUGIN_TAG "redirect_rearm"

namespace
{
int
handle_read_response_hdr(TSCont /* contp */, TSEvent event, void *edata)
{
  if (event != TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    TSError("[" PLUGIN_TAG "] unexpected event %d", event);
    TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSMBuffer bufp;
  TSMLoc hdr;

  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr) != TS_SUCCESS) {
    TSDebug(PLUGIN_TAG, "no server response header, skipping hop");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSHttpStatus status = TSHttpHdrStatusGet(bufp, hdr);
  if (status < 300 || status >= 400) {
    TSDebug(PLUGIN_TAG, "status %d is not a redirect, skipping hop", status);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSMLoc field = TSMimeHdrFieldFind(bufp, hdr, "Location", 8);
  if (field == TS_NULL_MLOC) {
    TSDebug(PLUGIN_TAG, "no Location header, skipping hop");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  int vlen         = 0;
  const char *vstr = TSMimeHdrFieldValueStringGet(bufp, hdr, field, 0, &vlen);
  if (vstr != nullptr && vlen > 0) {
    char *url = static_cast<char *>(TSmalloc(vlen + 1));
    memcpy(url, vstr, vlen);
    url[vlen] = '\0';
    TSDebug(PLUGIN_TAG, "setting redirect URL from Location: %.*s", vlen, vstr);
    // TSHttpTxnRedirectUrlSet takes ownership of the buffer, which the core
    // later frees with ats_free, so it must come from the ATS allocator
    // (TSmalloc / ats_malloc), not plain malloc or a stack buffer.
    TSHttpTxnRedirectUrlSet(txnp, url, vlen);
  } else {
    TSDebug(PLUGIN_TAG, "empty Location value, skipping hop");
  }

  TSHandleMLocRelease(bufp, hdr, field);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}
} // namespace

void
TSPluginInit(int /* argc */, const char * /* argv */[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>(PLUGIN_TAG);
  info.vendor_name   = const_cast<char *>("Apache");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[" PLUGIN_TAG "] registration failed");
    return;
  }

  TSCont c = TSContCreate(handle_read_response_hdr, nullptr);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, c);
}
