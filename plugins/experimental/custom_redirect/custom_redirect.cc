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

/* custom_redirect.cc: Allows read header set by origin for internal redirects
 */

#include <stdio.h>
#include <string>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ts/ts.h>
#include <string.h>
#include <stdlib.h>

static char *redirect_url_header   = NULL;
static int redirect_url_header_len = 0;
static int return_code             = TS_HTTP_STATUS_NONE;

static void
handle_response(TSHttpTxn txnp, TSCont /* contp ATS_UNUSED */)
{
  TSMBuffer resp_bufp;
  TSMLoc resp_loc;
  TSMBuffer req_bufp;
  TSMLoc req_loc;
  TSMLoc redirect_url_loc;
  TSHttpStatus status;
  const char *redirect_url_str;
  int redirect_url_length;

  if (TSHttpTxnServerRespGet(txnp, &resp_bufp, &resp_loc) != TS_SUCCESS) {
    TSError("[custom_redirect] Couldn't retrieve server response header");
  } else {
    if ((status = TSHttpHdrStatusGet(resp_bufp, resp_loc)) == TS_HTTP_STATUS_NONE) {
      TSError("[custom_redirect] Couldn't retrieve status from client response header");
    } else {
      if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) != TS_SUCCESS) {
        TSError("[custom_redirect] Couldn't retrieve server response header");
      } else {
        int method_len;
        const char *method = TSHttpHdrMethodGet(req_bufp, req_loc, &method_len);
        if ((return_code == TS_HTTP_STATUS_NONE || return_code == status) &&
            ((strncasecmp(method, TS_HTTP_METHOD_GET, TS_HTTP_LEN_GET) == 0))) {
          redirect_url_loc = TSMimeHdrFieldFind(resp_bufp, resp_loc, redirect_url_header, redirect_url_header_len);

          if (redirect_url_loc) {
            redirect_url_str = TSMimeHdrFieldValueStringGet(resp_bufp, resp_loc, redirect_url_loc, -1, &redirect_url_length);
            if (redirect_url_str) {
              if (redirect_url_length > 0) {
                char *url = (char *)TSmalloc(redirect_url_length + 1);

                TSstrlcpy(url, redirect_url_str, redirect_url_length + 1);
                TSHttpTxnRedirectUrlSet(txnp, url, redirect_url_length);
              }
            }
            TSHandleMLocRelease(resp_bufp, resp_loc, redirect_url_loc);
          }
        }
        // TSHandleStringRelease(req_bufp, req_loc, method);
        TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
      }
    }
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
plugin_main_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_HTTP_READ_RESPONSE_HDR: {
    TSHttpTxn txnp = (TSHttpTxn)edata;
    TSDebug("[custom_redirect1]", "MAIN_HANDLER::TS_HTTP_READ_RESPONSE_HDR_HOOK");
    handle_response(txnp, contp);
    break;
  }

  default: {
    TSDebug("[custom_redirect]", "default event");
    break;
  }
  }

  return 0;
}

bool
isNumber(const char *str)
{
  for (int i = 0; str[i] != '\0'; i++) {
    if (!isdigit(str[i])) {
      return false;
    }
  }
  return true;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"";
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";
  /* This plugin supports following types of url redirect here:
   *
   * 1. User can specify a particular redirect-url header name in the plugin command line,
   *    in which case plugin will just look for that header in response and redirect to it.
   *
   *OR:
   * 2. User can also specify a return error code, in which case if the code matches with
   *    the response, plugin will look for the standard "Location" header and redirect to it
   *
   *OR:
   * 3. If nothing specified, plugin will assume the first case and use the default redirect-url
   *    header name "x-redirect-url"
  */
  if (argc > 1) {
    if (isNumber(argv[1])) {
      return_code         = atoi(argv[1]);
      redirect_url_header = TSstrdup(TS_MIME_FIELD_LOCATION);
    } else {
      redirect_url_header = TSstrdup(argv[1]);
    }
  } else {
    // default header name is x-redirect-url
    redirect_url_header     = TSstrdup("x-redirect-url");
    redirect_url_header_len = strlen(redirect_url_header);
  }
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[custom_redirect] Plugin registration failed.");
  }
  TSError("[custom_redirect] Plugin registered successfully.");
  TSCont mainCont = TSContCreate(plugin_main_handler, NULL);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, mainCont);
}
