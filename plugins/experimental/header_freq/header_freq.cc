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

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ts/apidefs.h"
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

// A map from header name to the number of times the header was encountered.
using CountMap_t = std::unordered_map<std::string, std::atomic<unsigned int>>;
CountMap_t client_freq;
CountMap_t origin_freq;
std::shared_mutex map_mutex;

// A vector for when we want to sort the map.
using CountVector_t = std::vector<std::pair<std::string, unsigned int>>;

// for traffic_ctl, name is a convenient identifier
const char *ctl_tag              = PLUGIN_NAME;
const char CONTROL_MSG_LOG[]     = "log"; // log all data
const size_t CONTROL_MSG_LOG_LEN = sizeof(CONTROL_MSG_LOG) - 1;

void
Log_Sorted_Map(CountMap_t const &map, std::ostream &ss)
{
  CountVector_t sorted_vector;
  {
    std::shared_lock<std::shared_mutex> lock(map_mutex);
    sorted_vector = CountVector_t(map.begin(), map.end());
  }
  std::sort(sorted_vector.begin(), sorted_vector.end(), [](const auto &a, const auto &b) -> bool { return a.second > b.second; });

  for (auto const &[header_name, count] : sorted_vector) {
    ss << header_name << ": " << count << std::endl;
  }
}

void
Log_Data(std::ostream &ss)
{
  ss << std::endl << std::string(100, '+') << std::endl;

  ss << "CLIENT HEADERS" << std::endl;
  Log_Sorted_Map(client_freq, ss);

  ss << std::endl;

  ss << "ORIGIN HEADERS" << std::endl;
  Log_Sorted_Map(origin_freq, ss);

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
  if (nullptr == command) {
    TSError("[%s] Could not get the message argument from the log handler.", PLUGIN_NAME);
    return TS_ERROR;
  }

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
        TSError("[%s] Failed to open file '%s' for logging: %s", PLUGIN_NAME, path.c_str(), strerror(errno));
      }
    } else {
      TSError("[%s] Invalid (zero length) file name for logging", PLUGIN_NAME);
    }
  } else {
    // No filename provided, log to stdout (traffic.out).
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
count_all_headers(TSMBuffer &bufp, TSMLoc &hdr_loc, CountMap_t &map)
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

    { // For lock scoping.
      std::shared_lock<std::shared_mutex> reader_lock{map_mutex};
      if (map.find(str) == map.end()) {
        // Upgrade the lock to be exclusive.
        reader_lock.unlock();
        std::unique_lock<std::shared_mutex> ulock{map_mutex};
        // There's a potential race condition here such that another thread may
        // have inserted the key while we were upgrading the lock. Regardless,
        // incrementing the value here always does the right thing.
        ++map[str];
      } else {
        ++map[str];
      }
    }

    next_hdr = TSMimeHdrFieldNext(bufp, hdr_loc, hdr);
    TSHandleMLocRelease(bufp, hdr_loc, hdr);
    hdr = next_hdr;
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

/** Handle common logic between the request and response headers.
 * @param[in] txnp The transaction pointer for this HTTP message.
 * @param[in] event The event that triggered this callback.
 * @param[out] freq_map The map to update with the header counts.
 * @return TS_SUCCESS if the event was handled successfully, TS_ERROR otherwise.
 */
int
handle_header_event(TSHttpTxn txnp, TSEvent event, CountMap_t &freq_map)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSReturnCode ret;

  char const *message_type = nullptr;
  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    message_type = "request";
    ret          = TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc);
  } else { // TS_EVENT_HTTP_SEND_RESPONSE_HDR
    message_type = "response";
    ret          = TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc);
  }

  if (ret != TS_SUCCESS) {
    TSError("[%s] could not get %s headers", PLUGIN_NAME, message_type);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_ERROR;
  }

  count_all_headers(bufp, hdr_loc, freq_map);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

/** Continuation callback. Invoked to count headers on READ_REQUEST_HDR and
 * SEND_RESPONSE_HDR hooks.
 */
int
header_handle_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = reinterpret_cast<TSHttpTxn>(edata);
  int ret_val    = TS_SUCCESS;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: // count client headers
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_READ_REQUEST_HDR");
    ret_val = handle_header_event(txnp, event, client_freq);
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: // count origin headers
    TSDebug(DEBUG_TAG_HOOK, "event TS_EVENT_HTTP_SEND_RESPONSE_HDR");
    ret_val = handle_header_event(txnp, event, origin_freq);
    break;
  default:
    TSError("[%s] unexpected event in header handler: %d", PLUGIN_NAME, event);
    break;
  }

  return ret_val;
}

/**
 * Continuation callback. Invoked to handler the LIFE_CYCLE_MSG event to log
 * header stats.
 */
int
msg_handle_hook(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
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
      } else if (msgp->data_size == 0) {
        TSError("[%s] No command provided.", PLUGIN_NAME);
      } else {
        TSError("[%s] Unknown command '%.*s'", PLUGIN_NAME, static_cast<int>(msgp->data_size),
                static_cast<const char *>(msgp->data));
      }
    }
  } break;
  default:
    TSError("[%s] unexpected event in message handler: %d", PLUGIN_NAME, event);
    break;
  }

  return TS_SUCCESS;
}

} // anonymous namespace

/// Registration entry point for plugin.
void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(DEBUG_TAG_INIT, "initializing plugin");

  TSPluginRegistrationInfo info = {PLUGIN_NAME, VENDOR_NAME, SUPPORT_EMAIL};

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s](%s) Plugin registration failed. \n", PLUGIN_NAME, __FUNCTION__);
  }

  TSCont header_contp = TSContCreate(header_handle_hook, nullptr);
  if (header_contp == nullptr) {
    // Continuation initialization failed. Unrecoverable, report and exit.
    TSError("[%s](%s) could not create the header handler continuation", PLUGIN_NAME, __FUNCTION__);
    abort();
  }
  // Continuation initialization succeeded
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, header_contp);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, header_contp);

  TSCont msg_contp = TSContCreate(msg_handle_hook, nullptr);
  if (msg_contp == nullptr) {
    // Continuation initialization failed. Unrecoverable, report and exit.
    TSError("[%s](%s) could not create the message handler continuation", PLUGIN_NAME, __FUNCTION__);
    abort();
  }
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, msg_contp);
}
