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
#include <cstdlib>
#include <cstdio>
#include <getopt.h>
#include <cstring>
#include <string>
#include <iterator>
#include <map>

#include "swoc/IPEndpoint.h"
#include "swoc/TextView.h"

// Constants and some declarations

const char PLUGIN_NAME[] = "escalate";

static DbgCtl dbg_ctl{PLUGIN_NAME};

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
    RetryType   type;
    std::string target;
  };

  using StatusMapType = std::map<unsigned int, RetryInfo>;

  EscalationState()
  {
    cont = TSContCreate(EscalateResponse, nullptr);

    TSContDataSet(cont, this);
  }

  ~EscalationState() { TSContDestroy(cont); }
  TSCont        cont;
  StatusMapType status_map;
  bool          use_pristine = false;
};

// Little helper function, to update the Host portion of a URL, and stringify the result.
// Returns the URL string, and updates url_len with the length.
char *
MakeEscalateUrl(TSMBuffer mbuf, TSMLoc url, const char *host, size_t host_len, int &url_len)
{
  swoc::TextView   input_host_view{host, host_len};
  std::string_view host_view;
  std::string_view port_view;
  swoc::IPEndpoint::tokenize(input_host_view, &host_view, &port_view);
  // Update the request URL with the new Host to try.
  TSUrlHostSet(mbuf, url, host_view.data(), host_view.size());
  if (port_view.size()) {
    int const port_int = swoc::svtou(port_view);
    TSUrlPortSet(mbuf, url, port_int);
    Dbg(dbg_ctl, "Setting port to %d", port_int);
  }
  char *url_str = TSUrlStringGet(mbuf, url, &url_len);
  Dbg(dbg_ctl, "Setting new URL from configured %.*s to %.*s", (int)host_len, host, url_len, url_str);

  return url_str;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Main continuation for the plugin, examining an origin response for a potential retry.
//
static int
EscalateResponse(TSCont cont, TSEvent event, void *edata)
{
  TSHttpTxn        txn = static_cast<TSHttpTxn>(edata);
  EscalationState *es  = static_cast<EscalationState *>(TSContDataGet(cont));
  TSMBuffer        mbuf;
  TSMLoc           hdrp, url;

  TSAssert(event == TS_EVENT_HTTP_READ_RESPONSE_HDR || event == TS_EVENT_HTTP_SEND_RESPONSE_HDR);
  bool const processing_connection_error = (event == TS_EVENT_HTTP_SEND_RESPONSE_HDR);

  if (processing_connection_error) {
    TSServerState const state = TSHttpTxnServerStateGet(txn);
    if (state == TS_SRVSTATE_CONNECTION_ALIVE) {
      // There is no connection error, so nothing to do.
      TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
      return TS_EVENT_NONE;
    }
  }

  int const tries = TSHttpTxnRedirectRetries(txn);
  if (0 != tries) { // ToDo: Future support for more than one retry-URL
    Dbg(dbg_ctl, "Not pursuing failover due previous redirect already, num tries: %d", tries);
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_NONE;
  }

  int ret = 0;
  if (processing_connection_error) {
    ret = TSHttpTxnClientRespGet(txn, &mbuf, &hdrp);
  } else {
    ret = TSHttpTxnServerRespGet(txn, &mbuf, &hdrp);
  }
  if (TS_SUCCESS != ret) {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_NONE;
  }

  // Next, the response status ...
  TSHttpStatus const status = TSHttpHdrStatusGet(mbuf, hdrp);
  TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdrp);

  // See if we have an escalation retry config for this response code.
  auto const entry = es->status_map.find(status);
  if (entry == es->status_map.end()) {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return TS_EVENT_NONE;
  }
  EscalationState::RetryInfo const &retry_info = entry->second;

  Dbg(dbg_ctl, "Handling failover redirect for HTTP status %d", status);
  char const *url_str = nullptr;
  int         url_len = 0;
  if (EscalationState::RETRY_URL == retry_info.type) {
    url_str = TSstrdup(retry_info.target.c_str());
    url_len = retry_info.target.size();
    Dbg(dbg_ctl, "Setting new URL to %.*s", url_len, url_str);
  } else if (EscalationState::RETRY_HOST == retry_info.type) {
    if (es->use_pristine) {
      if (TS_SUCCESS == TSHttpTxnPristineUrlGet(txn, &mbuf, &url)) {
        url_str = MakeEscalateUrl(mbuf, url, retry_info.target.c_str(), retry_info.target.size(), url_len);
        TSHandleMLocRelease(mbuf, TS_NULL_MLOC, url);
      }
    } else {
      if (TS_SUCCESS == TSHttpTxnClientReqGet(txn, &mbuf, &hdrp)) {
        if (TS_SUCCESS == TSHttpHdrUrlGet(mbuf, hdrp, &url)) {
          url_str = MakeEscalateUrl(mbuf, url, retry_info.target.c_str(), retry_info.target.size(), url_len);
        }
        // Release the request MLoc
        TSHandleMLocRelease(mbuf, TS_NULL_MLOC, hdrp);
      }
    }
    Dbg(dbg_ctl, "Setting host URL to %.*s", url_len, url_str);
  }

  // Now update the Redirect URL, if set
  if (url_str) {
    TSHttpTxnRedirectUrlSet(txn, url_str, url_len); // Transfers ownership
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
        Dbg(dbg_ctl, "Creating Redirect rule with URL = %s", sep);
      } else {
        info.type = EscalationState::RETRY_HOST;
        Dbg(dbg_ctl, "Creating Redirect rule with Host = %s", sep);
      }

      for (token = strtok_r(argv[i], ",", &save); token; token = strtok_r(nullptr, ",", &save)) {
        unsigned status = strtol(token, nullptr, 10);

        if (status < 100 || status > 599) {
          snprintf(errbuf, errbuf_size, "invalid status code: %.*s", static_cast<int>(std::distance(argv[i], sep)), argv[i]);
          goto fail;
        }

        Dbg(dbg_ctl, "      added status = %d to rule", status);
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
  TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, es->cont);
  return TSREMAP_NO_REMAP;
}
