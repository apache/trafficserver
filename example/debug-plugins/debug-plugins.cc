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

/*
 * debug-plugins.cc:  Example implementation of http request debug feature.
 *                    Before and after plugin execution at every hook, logging headers
 *                    information to a text log file.
 */

#include <ts/ts.h>
#include <atscppapi/Headers.h>
#include <pthread.h>
#include <string>
#include <dlfcn.h>

static const char *LOG_FILE_NAME = "plugin_debug";
static const char *DEBUG_TAG     = "http_plugin_hook";

// Sensitive fields in header will not be logged.
static bool
isSensitiveField(const std::string &name)
{
  // Todo: black list and white list.
  return false;
}

static std::string
wireHeadersToStr(void *bufp, void *mloc)
{
  std::string retVal          = "";
  atscppapi::Headers *headers = new atscppapi::Headers(bufp, mloc);

  for (atscppapi::Headers::iterator header_iter = headers->begin(); header_iter != headers->end(); ++header_iter) {
    atscppapi::HeaderField hf = *header_iter;
    std::string name          = hf.name().str();
    std::string value         = hf.values(", ");

    retVal += name;
    retVal += ": ";

    if (isSensitiveField(name)) {
      retVal += "******";
    } else {
      retVal += value;
    }

    retVal += "\\r\\n";
  }

  delete headers;
  return retVal;
}

static std::string
getPluginName(const void *addr)
{
  Dl_info dl_info;
  int ret = 0;
  if ((ret = dladdr(addr, &dl_info)) != 0) {
    // the whole path of .so file
    std::string path(dl_info.dli_fname);

    // file name begin index
    std::size_t first = path.find_last_of("/\\");
    if (first == std::string::npos) {
      first = 0;
    } else {
      first++;
    }

    // .so begin index
    std::size_t end = path.rfind(".so");
    if (end == std::string::npos) {
      end = path.length();
    }

    return path.substr(first, end - first);
  } else {
    // we could not get the plugin file name
    return "unknown";
  }
}

static std::string
createRecord(bool isBeforePlugin, TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp, TSCont contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  std::string clientRequest  = "";
  std::string serverRequest  = "";
  std::string serverResponse = "";
  std::string clientResponse = "";

  std::string recordJson = "{";
  recordJson += "\"hook_id\" : \"";
  recordJson += TSHttpHookNameLookup(id);
  recordJson += "\"";

  recordJson += ", ";
  recordJson += "\"plugin_name\" : \"";
  recordJson += getPluginName((void *)funcp);
  recordJson += "\"";

  recordJson += ", ";
  recordJson += "\"tag\" : \"";
  recordJson += isBeforePlugin ? "beforePlugin" : "afterPlugin";
  recordJson += "\"";

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(DEBUG_TAG, "couldn't retrieve client request header");
  } else {
    TSDebug(DEBUG_TAG, "Retrieve client request header");
    clientRequest = wireHeadersToStr(bufp, hdr_loc);
  }
  recordJson += ", ";
  recordJson += "\"client_request\" : \"";
  recordJson += clientRequest;
  recordJson += "\"";

  if (TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(DEBUG_TAG, "couldn't retrieve server request header");
  } else {
    TSDebug(DEBUG_TAG, "Retrieve server request header");
    serverRequest = wireHeadersToStr(bufp, hdr_loc);
  }
  recordJson += ", ";
  recordJson += "\"server_request\" : \"";
  recordJson += serverRequest;
  recordJson += "\"";

  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(DEBUG_TAG, "couldn't retrieve server response header");
  } else {
    TSDebug(DEBUG_TAG, "Retrieve server response header");
    serverResponse = wireHeadersToStr(bufp, hdr_loc);
  }
  recordJson += ", ";
  recordJson += "\"server_response\" : \"";
  recordJson += serverResponse;
  recordJson += "\"";

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(DEBUG_TAG, "couldn't retrieve client response header");
  } else {
    TSDebug(DEBUG_TAG, "Retrieve client response header");
    clientResponse = wireHeadersToStr(bufp, hdr_loc);
  }
  recordJson += ", ";
  recordJson += "\"client_response\" : \"";
  recordJson += clientResponse;
  recordJson += "\"";

  recordJson += "}";

  return recordJson;
}

// Use this TSTextLogObject to write debug messages to log file.
static TSTextLogObject *txtLogObj_;
static pthread_once_t txtLogObjIsinitialized = PTHREAD_ONCE_INIT;

// Init txtLogObj_, use pthread_once to make sure it is only executes once.
static void
initTxtLogObj()
{
  txtLogObj_          = new TSTextLogObject();
  TSReturnCode result = TSTextLogObjectCreate(LOG_FILE_NAME, 0, txtLogObj_);
  if (result != TS_SUCCESS) {
    delete txtLogObj_;
    txtLogObj_ = NULL;
    TSDebug(DEBUG_TAG, "initTxtLogObj(): failed to create log object");
  } else {
    TSDebug(DEBUG_TAG, "initTxtLogObj(): successfully create log object");
  }
}

// Record debug message before and after execution of plugin.
static void
record(bool isBeforePlugin, TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp, TSCont contp)
{
  // init txtLogObj_;
  (void)pthread_once(&txtLogObjIsinitialized, initTxtLogObj);

  if (txtLogObj_ != NULL) {
    std::string recordJson = createRecord(isBeforePlugin, txnp, id, funcp, contp);
    TSTextLogObjectWrite(*txtLogObj_, "%s", recordJson.c_str());
  }
}

// Only debug a request specified by a cookie __ts_debug=on.
// For security reason, only allow request from certain IPs to use debug function.
static bool
shouldDebugRequest(const TSHttpTxn txnp)
{
  // Todo: check cookie and IPs.
  return true;
}

// Function to use before plugin execution.
extern "C" void
TSHttpTxnPrePluginHook(TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp, TSCont contp)
{
  if (shouldDebugRequest(txnp)) {
    record(true, txnp, id, funcp, contp);
  }
}

// Function to use after plugin execution.
extern "C" void
TSHttpTxnPostPluginHook(TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp, TSCont contp)
{
  if (shouldDebugRequest(txnp)) {
    record(false, txnp, id, funcp, contp);
  }
}

extern "C" void
TSHttpTxnBegin(TSHttpTxn txnp)
{
  // Todo: initialize this debug session.
  TSDebug(DEBUG_TAG, "TSHttpTxnBegin()");
}

extern "C" void
TSHttpTxnEnd(TSHttpTxn txnp)
{
  // Todo: this debug session expires.
  TSDebug(DEBUG_TAG, "TSHttpTxnEnd()");
}
