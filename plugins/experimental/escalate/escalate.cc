/** @file

  Escalation plugin.

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

#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/experimental.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <string>
#include <iterator>
#include <map>


// Constants
const char PLUGIN_NAME[] = "escalate";


static int EscalateResponse(TSCont, TSEvent, void *);

struct EscalationState
{
  typedef std::map<unsigned, std::string> hostmap_type;

  EscalationState()
  {
    handler = TSContCreate(EscalateResponse, NULL);
    TSContDataSet(handler, this);
  }

  ~EscalationState()
  {
    TSContDestroy(handler);
  }

  TSCont       handler;
  hostmap_type hostmap;
};


static int
EscalateResponse(TSCont cont, TSEvent event, void* edata)
{
  EscalationState* es = static_cast<EscalationState*>(TSContDataGet(cont));
  TSHttpTxn        txn = (TSHttpTxn)edata;
  TSMBuffer        response;
  TSMLoc           resp_hdr;

  TSReleaseAssert(event == TS_EVENT_HTTP_READ_RESPONSE_HDR);

  // First, we need the server response ...
  if (TS_SUCCESS == TSHttpTxnServerRespGet(txn, &response, &resp_hdr)) {
    int tries = TSHttpTxnRedirectRetries(txn);

    TSDebug(PLUGIN_NAME, "This is try %d", tries);
    if (0 == tries) { // ToDo: Future support for more than one retry-URL
      // Next, the response status ...
      TSHttpStatus status = TSHttpHdrStatusGet(response, resp_hdr);

      // If we have an escalation URL for this response code, set the redirection URL and force it
      // to be followed.
      EscalationState::hostmap_type::iterator entry = es->hostmap.find((unsigned)status);

      if (entry != es->hostmap.end()) {
        TSMBuffer request;
        TSMLoc    req_hdr;

        TSDebug(PLUGIN_NAME, "Found an entry for HTTP status %u", (unsigned)status);
        if (TS_SUCCESS == TSHttpTxnClientReqGet(txn, &request, &req_hdr)) {
          TSMLoc url;

          if (TS_SUCCESS == TSHttpHdrUrlGet(request, req_hdr, &url)) {
            char* url_str;
            int url_len;

            // Update the request URL with the new Host to try.
            TSUrlHostSet(request, url, entry->second.c_str(), entry->second.size());
            url_str = TSUrlStringGet(request, url, &url_len);

            TSDebug(PLUGIN_NAME, "Setting new URL to %.*s", url_len, url_str);
            TSHttpTxnRedirectUrlSet(txn, url_str, url_len); // Transfers ownership
          }
          // Release the request MLoc
        TSHandleMLocRelease(request, TS_NULL_MLOC, req_hdr);
        }
      }
    }
    // Release the response MLoc
    TSHandleMLocRelease(response, TS_NULL_MLOC, resp_hdr);
  }

  // Set the transaction free ...
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

TSReturnCode
TSRemapInit(TSRemapInterface* /* api */, char* /* errbuf */, int /* bufsz */)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char* argv[], void** instance, char* errbuf, int errbuf_size)
{
  EscalationState* es = new EscalationState();

  // The first two arguments are the "from" and "to" URL string. We can just
  // skip those, since we only ever remap on the error path.
  for (int i = 2; i < argc; ++i) {
    unsigned status;
    char*    sep;

    // Each token should be a status code then a URL, separated by ':'.
    sep = strchr(argv[i], ':');
    if (sep == NULL) {
      snprintf(errbuf, errbuf_size, "missing status code: %s", argv[i]);
      goto fail;
    }

    *sep = '\0';
    status = strtol(argv[i], NULL, 10);

    if (status < 100 || status > 599) {
      snprintf(errbuf, errbuf_size, "invalid status code: %.*s", (int)std::distance(argv[i], sep), argv[i]);
      goto fail;
    }

    ++sep; // Skip over the ':'

    // OK, we have a valid status/URL pair.
    TSDebug(PLUGIN_NAME, "Redirect of HTTP status %u to %s", status, sep);
    es->hostmap[status] = sep;
  }

  *instance = es;
  return TS_SUCCESS;

fail:
  delete es;
  return TS_ERROR;
}

void
TSRemapDeleteInstance(void* instance)
{
  delete (EscalationState *)instance;
}

TSRemapStatus
TSRemapDoRemap(void* instance, TSHttpTxn txn, TSRemapRequestInfo* /* rri */)
{
  EscalationState* es = static_cast<EscalationState *>(instance);

  TSHttpTxnHookAdd(txn, TS_HTTP_READ_RESPONSE_HDR_HOOK, es->handler);
  return TSREMAP_NO_REMAP;
}
