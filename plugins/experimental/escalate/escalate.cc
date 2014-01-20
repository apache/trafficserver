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
#include <sstream>
#include <iterator>
#include <map>

struct EscalationState
{
  typedef std::map<unsigned, TSMLoc> urlmap_type;

  EscalationState() {
    this->mbuf = TSMBufferCreate();
  }

  ~EscalationState() {
    TSMBufferDestroy(this->mbuf);
  }

  TSCont      handler;
  urlmap_type urlmap;
  TSMBuffer   mbuf;
};

static unsigned
toint(const std::string& str)
{
  std::istringstream istr(str);
  unsigned val;

  istr >> val;
  return val;
}

static int
EscalateResponse(TSCont cont, TSEvent event, void * edata)
{
  EscalationState * es = (EscalationState *)TSContDataGet(cont);
  TSHttpTxn         txn = (TSHttpTxn)edata;
  TSMBuffer         buffer;
  TSMLoc            hdr;
  TSHttpStatus      status;

  TSDebug("escalate", "hit escalation hook with event %d", (int)event);
  TSReleaseAssert(event == TS_EVENT_HTTP_READ_RESPONSE_HDR);

  // First, we need the server response ...
  TSReleaseAssert(
    TSHttpTxnServerRespGet(txn, &buffer, &hdr) == TS_SUCCESS
  );

  // Next, the respose status ...
  status = TSHttpHdrStatusGet(buffer, hdr);

  // If we have an escalation URL for this response code, set the redirection URL and force it
  // to be followed.
  EscalationState::urlmap_type::iterator entry = es->urlmap.find((unsigned)status);
  if (entry != es->urlmap.end()) {
    TSDebug("escalate", "found an escalation entry for HTTP status %u", (unsigned)status);
    TSHttpTxnRedirectRequest(txn, es->mbuf, entry->second);
    TSHttpTxnFollowRedirect(txn, 1 /* on */);
  }

  // Set the transaction free ...
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api */, char * /* errbuf */, int /* bufsz */)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char * argv[], void ** instance, char * errbuf, int errbuf_size)
{
  EscalationState * es((EscalationState *)instance);

  es = new EscalationState();
  es->handler = TSContCreate(EscalateResponse, NULL);
  TSContDataSet(es->handler, es);

  // The first two arguments are the "from" and "to" URL string. We can just
  // skip those, since we only ever remap on the error path.
  for (int i = 2; i < argc; ++i) {
    unsigned  status;
    TSMLoc    url;
    char *    sep;

    // Each token should be a status code then a URL, separated by '='.
    sep = strchr(argv[i], '=');
    if (sep == NULL) {
      snprintf(errbuf, errbuf_size, "missing status code: %s", argv[i]);
      goto fail;
    }

    status = toint(std::string(argv[i], std::distance(argv[i], sep)));
    if (status < 100 || status > 599) {
      snprintf(errbuf, errbuf_size, "invalid status code: %.*s", (int)std::distance(argv[i], sep), argv[i]);
      goto fail;
    }

    TSReleaseAssert(TSUrlCreate(es->mbuf, &url) == TS_SUCCESS);

    ++sep; // Skip over the '='.

    TSDebug("escalate", "escalating HTTP status %u to %s", status, sep);
    if (TSUrlParse(es->mbuf, url, (const char **)&sep, argv[i] + strlen(argv[i])) != TS_PARSE_DONE) {
      snprintf(errbuf, errbuf_size, "invalid target URL: %s", sep);
      goto fail;
    }

    // OK, we have a valid status/URL pair.
    es->urlmap[status] = url;
  }

  *instance = es;
  return TS_SUCCESS;

fail:
  delete es;
  return TS_ERROR;
}

void
TSRemapDeleteInstance(void * instance)
{
  delete (EscalationState *)instance;
}

TSRemapStatus
TSRemapDoRemap(void * instance, TSHttpTxn txn, TSRemapRequestInfo * /* rri */)
{
  EscalationState * es((EscalationState *)instance);

  TSHttpTxnHookAdd(txn, TS_HTTP_READ_RESPONSE_HDR_HOOK, es->handler);
  return TSREMAP_NO_REMAP;
}
