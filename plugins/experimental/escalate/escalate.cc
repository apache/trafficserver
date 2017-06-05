/** @file

  This plugin allows retrying requests against different destinations.

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
#include <cstdlib>
#include <cstdio>
#include <getopt.h>
#include <cstring>
#include <string>
#include <iterator>
#include <map>

// Constants and some declarations
const char PLUGIN_NAME[] = "escalate";
static int EscalateResponse(TSCont, TSEvent, void *);

//////////////////////////////////////////////////////////////////////////////////////////
// Hold information about the escalation / retry states for a remap rule.
//
struct EscalationState {
  enum RetryType {
    RETRY_URL,
    RETRY_HOST,
  };

  struct RetryInfo {
    RetryType type;
    std::string target;
  };

  typedef std::map<unsigned, RetryInfo> StatusMapType;

  EscalationState() : use_pristine(false)
  {
    cont = TSContCreate(EscalateResponse, nullptr);
    TSContDataSet(cont, this);
  }

  ~EscalationState() { TSContDestroy(cont); }
  TSCont cont;
  StatusMapType status_map;
  bool use_pristine;
};

// Little helper function, to update the Host portion of a URL, and stringify the result.
// Returns the URL string, and updates url_len with the length.
char *
MakeEscalateUrl(TSMBuffer mbuf, TSMLoc url, const char *host, size_t host_len, int &url_len)
{
  char *url_str = nullptr;

  // Update the request URL with the new Host to try.
  TSUrlHostSet(mbuf, url, host, host_len);
  url_str = TSUrlStringGet(mbuf, url, &url_len);
  TSDebug(PLUGIN_NAME, "Setting new URL to %.*s", url_len, url_str);

  return url_str;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Main continuation for the plugin, examining an origin response for a potential retry.
//
static int
EscalateResponse(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn txn       = (TSHttpTxn)edata;
  EscalationState *es = static_cast<EscalationState *>(TSContDataGet(cont));
  EscalationState::StatusMapType::const_iterator entry;
  TSMBuffer mbuf;
  TSMLoc hdrp, url;
  TSHttpStatus status;
  char *url_str = nullptr;
  int url_len, tries;

  TSAssert(event == TS_EVENT_HTTP_READ_RESPONSE_HDR);

  // First, we need the server response ...
  if (TS_SUCCESS != TSHttpTxnServerRespGet(txn, &mbuf, &hdrp)) {
    goto no_action;
  }

  tries = TSHttpTxnRedirectRetries(txn);
  if (0 != tries) { // ToDo: Future support for more than one retry-URL
    goto no_action;
  }
  TSDebug(PLUGIN_NAME, "This is try %d, proceeding", tries);

  // Next, the response status ...
  status = TSHttpHdrStatusGet(mbuf, hdrp);
  TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdrp); // Don't need this any more

  // See if we have an escalation retry config for this response code
  entry = es->status_map.find((unsigned)status);
  if (entry == es->status_map.end()) {
    goto no_action;
  }

  TSDebug(PLUGIN_NAME, "Found an entry for HTTP status %u", (unsigned)status);
  if (EscalationState::RETRY_URL == entry->second.type) {
    url_str = TSstrdup(entry->second.target.c_str());
    url_len = entry->second.target.size();
    TSDebug(PLUGIN_NAME, "Setting new URL to %.*s", url_len, url_str);
  } else if (EscalationState::RETRY_HOST == entry->second.type) {
    if (es->use_pristine) {
      if (TS_SUCCESS == TSHttpTxnPristineUrlGet(txn, &mbuf, &url)) {
        url_str = MakeEscalateUrl(mbuf, url, entry->second.target.c_str(), entry->second.target.size(), url_len);
        TSHandleMLocRelease(mbuf, TS_NULL_MLOC, url);
      }
    } else {
      if (TS_SUCCESS == TSHttpTxnClientReqGet(txn, &mbuf, &hdrp)) {
        if (TS_SUCCESS == TSHttpHdrUrlGet(mbuf, hdrp, &url)) {
          url_str = MakeEscalateUrl(mbuf, url, entry->second.target.c_str(), entry->second.target.size(), url_len);
        }
        // Release the request MLoc
        TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdrp);
      }
    }
  }

  // Now update the Redirect URL, if set
  if (url_str) {
    TSHttpTxnRedirectUrlSet(txn, url_str, url_len); // Transfers ownership
  }

// Set the transaction free ...
no_action:
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_EVENT_NONE;
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api */, char * /* errbuf */, int /* bufsz */)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errbuf, int errbuf_size)
{
  EscalationState *es = new EscalationState();

  // The first two arguments are the "from" and "to" URL string. We can just
  // skip those, since we only ever remap on the error path.
  for (int i = 2; i < argc; ++i) {
    char *sep, *token, *save;

    // Ugly, but we set the precedence before with non-command line parsing of args
    if (0 == strncasecmp(argv[i], "--pristine", 10)) {
      es->use_pristine = true;
    } else {
      // Each token should be a status code then a URL, separated by ':'.
      sep = strchr(argv[i], ':');
      if (sep == nullptr) {
        snprintf(errbuf, errbuf_size, "malformed status:target config: %s", argv[i]);
        goto fail;
      }

      *sep = '\0';
      ++sep; // Skip over the ':' (which is now \0)

      // OK, we have a valid status/URL pair.
      EscalationState::RetryInfo info;

      info.target = sep;
      if (std::string::npos != info.target.find('/')) {
        info.type = EscalationState::RETRY_URL;
        TSDebug(PLUGIN_NAME, "Creating Redirect rule with URL = %s", sep);
      } else {
        info.type = EscalationState::RETRY_HOST;
        TSDebug(PLUGIN_NAME, "Creating Redirect rule with Host = %s", sep);
      }

      for (token = strtok_r(argv[i], ",", &save); token; token = strtok_r(nullptr, ",", &save)) {
        unsigned status = strtol(token, nullptr, 10);

        if (status < 100 || status > 599) {
          snprintf(errbuf, errbuf_size, "invalid status code: %.*s", (int)std::distance(argv[i], sep), argv[i]);
          goto fail;
        }

        TSDebug(PLUGIN_NAME, "      added status = %d to rule", status);
        es->status_map[status] = info;
      }
    }
  }

  *instance = es;
  return TS_SUCCESS;

fail:
  delete es;
  return TS_ERROR;
}

void
TSRemapDeleteInstance(void *instance)
{
  delete static_cast<EscalationState *>(instance);
}

TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txn, TSRemapRequestInfo * /* rri */)
{
  EscalationState *es = static_cast<EscalationState *>(instance);

  TSHttpTxnHookAdd(txn, TS_HTTP_READ_RESPONSE_HDR_HOOK, es->cont);
  return TSREMAP_NO_REMAP;
}
