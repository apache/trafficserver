/** @file

  This plugin counts the number of times every header has appeared.
  Maintains separate counts for client and origin headers.

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

#include <iostream>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <ts/ts.h>


// plugin registration info
static char plugin_name[]   = "header_freq";
static char vendor_name[]   = "Apache Software Foundation";
static char support_email[] = "dev@trafficserver.apache.org";

// debug messages during one-time initialization
static const char DEBUG_TAG_INIT[] = "header_freq.init";

// debug messages in continuation callbacks
static const char DEBUG_TAG_HOOK[] = "header_freq.hook";

// maps from header name to # of times encountered
static std::map<std::string, unsigned int> client_freq;
static std::map<std::string, unsigned int> origin_freq;

// for traffic_ctl, name is a convenient identifier
static const char *ctl_tag = plugin_name;
static const char *ctl_log = "log"; // log all data

/**
 * Logs the data collected, first the client, and then
 * the origin headers.
 */
void
log()
{
  std::stringstream ss("");

  ss << std::endl << std::string(100, '+') << std::endl;

  ss << "CLIENT HEADERS" << std::endl;
  for (auto &elem: client_freq) {
    ss << elem.first << ": " << elem.second << std::endl;
  }

  ss << std::endl;

  ss << "ORIGIN HEADERS" << std::endl;
  for (auto &elem: origin_freq) {
    ss << elem.first << ": " << elem.second << std::endl;
  }

  ss << std::string(100, '+') << std::endl;
  std::cout << ss.str() << std::endl; 
}

/**
 * Records all headers found in the buffer in the map provided. Comparison
 * against existing entries is case-insensitive.
 */
void
count_all_headers(TSMBuffer &bufp, TSMLoc &hdr_loc, std::map<std::string, unsigned int> &map)
{
  TSMLoc hdr, next_hdr;
  hdr = TSMimeHdrFieldGet(bufp, hdr_loc, 0);
  int n_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
  TSDebug(DEBUG_TAG_HOOK, "%d headers found", n_headers);

  // iterate through all headers
  for (int i = 0; i < n_headers; ++i) {
    if (hdr == NULL)
      break;
    next_hdr = TSMimeHdrFieldNext(bufp, hdr_loc, hdr);
    int hdr_len;
    const char *hdr_name = TSMimeHdrFieldNameGet(bufp, hdr_loc, hdr, &hdr_len);

    std::string str = std::string(hdr_name, hdr_len);

    // make case-insensitive by converting to lowercase
    for (auto &c: str) {
      c = tolower(c);
    }

    // count the header
    if (map.find(str) == map.end()) {
      // Not found.
      map[str] = 1;
     } else {
      // Found.
      ++map[str];
    }

    TSHandleMLocRelease(bufp, hdr_loc, hdr);
    hdr = next_hdr;
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

/**
 * Continuation callback. Invoked to count headers on READ_REQUEST_HDR and
 * SEND_RESPONSE_HDR hooks and to log through traffic_ctl's LIFECYCLE_MSG.
 */
static int
handle_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  int ret_val = 0;
  switch(event){
  case TS_EVENT_HTTP_READ_REQUEST_HDR: // count client headers
    {
      TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_READ_REQUEST_HDR");
      txnp = reinterpret_cast<TSHttpTxn>(edata);
      // get the client request so we can loop through the headers
      if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        TSError("(%s) could not get request headers", plugin_name);
        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
        ret_val = -1;
        break;
      }
      count_all_headers(bufp, hdr_loc, client_freq);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: // count origin headers
    {
      TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_SEND_RESPONSE_HDR");
      // get the response so we can loop through the headers
      txnp = reinterpret_cast<TSHttpTxn>(edata);
      if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        TSError("(%s) could not get response headers", plugin_name);
        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
        ret_val = -2;
        break;
      }
      count_all_headers(bufp, hdr_loc, origin_freq);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    }
    break;
  case TS_EVENT_LIFECYCLE_MSG:
    {
      TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_LIFECYCLE_MSG");
      TSPluginMsg* msgp = reinterpret_cast<TSPluginMsg*>(edata);

      if (strcmp(ctl_tag, msgp->tag))
        {
          TSDebug(DEBUG_TAG_HOOK, "tag %s does not concern us", msgp->tag);
          return 0;
        }

      // identify the command
      if (strncmp(ctl_log, reinterpret_cast<const char*>(msgp->data),
          strlen(ctl_log)) == 0) {
        log();
      }

    }
    break;
  // do nothing in any of the other states
  default:
    break;
  }
  return ret_val;
}

/**
 * Entry point for the plugin.
 */
void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(DEBUG_TAG_INIT, "initializing plugin");

  TSPluginRegistrationInfo info;

  info.plugin_name = plugin_name;
  info.vendor_name = vendor_name;
  info.support_email = support_email;

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s](%s) Plugin registration failed. \n", plugin_name,
    __FUNCTION__);
  }

  TSCont contp = TSContCreate(handle_hook, NULL);
  if (contp == NULL) {
    // Continuation initialization failed. Unrecoverable, report and exit.
    TSError("(%s)[%s] could not create continuation", plugin_name,
            __FUNCTION__);
    abort();
  } else {
    // Continuation initialization succeeded
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
    TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, contp);
  }
}
