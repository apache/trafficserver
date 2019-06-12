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
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ts/ts.h>

namespace
{
// plugin registration info
char PLUGIN_NAME[]   = "header_freq";
char VENDOR_NAME[]   = "Apache Software Foundation";
char SUPPORT_EMAIL[] = "dev@trafficserver.apache.org";

// debug messages during one-time initialization
const char DEBUG_TAG_INIT[] = "header_freq.init";

// debug messages in continuation callbacks
const char DEBUG_TAG_HOOK[] = "header_freq.hook";

// maps from header name to # of times encountered
std::map<std::string, unsigned int> client_freq;
std::map<std::string, unsigned int> origin_freq;

// for traffic_ctl, name is a convenient identifier
const char *ctl_tag              = PLUGIN_NAME;
const char CONTROL_MSG_LOG[]     = "log"; // log all data
const size_t CONTROL_MSG_LOG_LEN = sizeof(CONTROL_MSG_LOG) - 1;

void
Log_Data(std::ostream &ss)
{
  ss << std::endl << std::string(100, '+') << std::endl;

  ss << "CLIENT HEADERS" << std::endl;
  for (auto &elem : client_freq) {
    ss << elem.first << ": " << elem.second << std::endl;
  }

  ss << std::endl;

  ss << "ORIGIN HEADERS" << std::endl;
  for (auto &elem : origin_freq) {
    ss << elem.first << ": " << elem.second << std::endl;
  }

  ss << std::string(100, '+') << std::endl;
}

/**
 * Logs the data collected, first the client, and then
 * the origin headers.
 */
int
CB_Command_Log(TSCont contp, TSEvent event, void *edata)
{
  std::string *command = static_cast<std::string *>(TSContDataGet(contp));
  std::string::size_type colon_idx;

  if (std::string::npos != (colon_idx = command->find(':'))) {
    std::string path = command->substr(colon_idx + 1);
    // The length of the data can include a trailing null, clip it.
    if (path.length() > 0 && path.back() == '\0') {
      path.pop_back();
    }
    if (path.length() > 0) {
      std::ofstream out;
      out.open(path, std::ios::out | std::ios::app);
      if (out.is_open()) {
        Log_Data(out);
      } else {
        TSError("[%s] Failed to open file '%s' for logging", PLUGIN_NAME, path.c_str());
      }
    } else {
      TSError("[%s] Invalid (zero length) file name for logging", PLUGIN_NAME);
    }
  } else {
    Log_Data(std::cout);
  }

  // cleanup.
  delete command;
  TSContDestroy(contp);
  return TS_SUCCESS;
}

/**
 * Records all headers found in the buffer in the map provided. Comparison
 * against existing entries is case-insensitive.
 */
static void
count_all_headers(TSMBuffer &bufp, TSMLoc &hdr_loc, std::map<std::string, unsigned int> &map)
{
  TSMLoc hdr, next_hdr;
  hdr           = TSMimeHdrFieldGet(bufp, hdr_loc, 0);
  int n_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
  TSDebug(DEBUG_TAG_HOOK, "%d headers found", n_headers);

  // iterate through all headers
  for (int i = 0; i < n_headers && nullptr != hdr; ++i) {
    int hdr_len;
    const char *hdr_name = TSMimeHdrFieldNameGet(bufp, hdr_loc, hdr, &hdr_len);
    std::string str      = std::string(hdr_name, hdr_len);

    // make case-insensitive by converting to lowercase
    for (auto &c : str) {
      c = tolower(c);
    }

    ++map[str];

    next_hdr = TSMimeHdrFieldNext(bufp, hdr_loc, hdr);
    TSHandleMLocRelease(bufp, hdr_loc, hdr);
    hdr = next_hdr;
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

/**
 * Continuation callback. Invoked to count headers on READ_REQUEST_HDR and
 * SEND_RESPONSE_HDR hooks and to log through traffic_ctl's LIFECYCLE_MSG.
 */
int
handle_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  int ret_val = 0;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: // count client headers
  {
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_READ_REQUEST_HDR");
    txnp = reinterpret_cast<TSHttpTxn>(edata);
    // get the client request so we can loop through the headers
    if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      TSError("[%s] could not get request headers", PLUGIN_NAME);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
      ret_val = -1;
      break;
    }
    count_all_headers(bufp, hdr_loc, client_freq);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: // count origin headers
  {
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_SEND_RESPONSE_HDR");
    // get the response so we can loop through the headers
    txnp = reinterpret_cast<TSHttpTxn>(edata);
    if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      TSError("[%s] could not get response headers", PLUGIN_NAME);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
      ret_val = -2;
      break;
    }
    count_all_headers(bufp, hdr_loc, origin_freq);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } break;
  case TS_EVENT_LIFECYCLE_MSG: // Handle external command
  {
    TSPluginMsg *msgp = static_cast<TSPluginMsg *>(edata);

    if (0 == strcasecmp(ctl_tag, msgp->tag)) {
      // identify the command
      if (msgp->data_size >= CONTROL_MSG_LOG_LEN &&
          0 == strncasecmp(CONTROL_MSG_LOG, static_cast<const char *>(msgp->data), CONTROL_MSG_LOG_LEN)) {
        TSDebug(DEBUG_TAG_HOOK, "Scheduled execution of '%s' command", CONTROL_MSG_LOG);
        TSCont c = TSContCreate(CB_Command_Log, TSMutexCreate());
        TSContDataSet(c, new std::string(static_cast<const char *>(msgp->data), msgp->data_size));
        TSContScheduleOnPool(c, 0, TS_THREAD_POOL_TASK);
      } else {
        TSError("[%s] Unknown command '%.*s'", PLUGIN_NAME, static_cast<int>(msgp->data_size),
                static_cast<const char *>(msgp->data));
      }
    }
  } break;
  // do nothing in any of the other states
  default:
    break;
  }

  return ret_val;
}

} // namespace

/// Registration entry point for plugin.
void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(DEBUG_TAG_INIT, "initializing plugin");

  TSPluginRegistrationInfo info = {PLUGIN_NAME, VENDOR_NAME, SUPPORT_EMAIL};

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s](%s) Plugin registration failed. \n", PLUGIN_NAME, __FUNCTION__);
  }

  TSCont contp = TSContCreate(handle_hook, TSMutexCreate());
  if (contp == nullptr) {
    // Continuation initialization failed. Unrecoverable, report and exit.
    TSError("[%s](%s) could not create continuation", PLUGIN_NAME, __FUNCTION__);
    abort();
  } else {
    // Continuation initialization succeeded
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
    TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
    TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, contp);
  }
}
