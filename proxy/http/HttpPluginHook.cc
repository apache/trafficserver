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

/****************************************************************************

   HttpPluginHook.cc

   Description:
       Http transaction debugging interfaces.


 ****************************************************************************/
#include "HttpConfig.h"
#include "HttpPluginHook.h"
#include <dlfcn.h>
#include <cstddef>

TxnSession_t txnBegin       = NULL;
TxnSession_t txnEnd         = NULL;
PluginHook_t prePluginHook  = NULL;
PluginHook_t postPluginHook = NULL;
static const char *DBG_TAG  = "http_plugin_hook";

void
initHttpTxnPluginHook()
{
  // Get http configuration
  static HttpConfigParams *httpConfig = HttpConfig::acquire();

  // If user does not configure http plugin hook library path, this feature is disabled.
  // prePluginHook and postPluginHook will be checked before using.
  if (!httpConfig->http_plugin_hook_library_path) {
    Debug(DBG_TAG, "Http transaction plugin hook library path is not specified");
    return;
  }

  // Open http plugin hook library
  void *handle = dlopen(httpConfig->http_plugin_hook_library_path, RTLD_LAZY);
  if (!handle) {
    Debug(DBG_TAG, "Cannot open http transaction plugin hook library: %s", dlerror());
    return;
  }
  Debug(DBG_TAG, "Successfully open http transaction plugin hook library");

  // Clear any existing error
  dlerror();

  // For TSHttpTxnBegin/TSHttpTxnEnd, both are required;
  // For prePluginHook/postPluginHook, only require one;

  // Set up txnBegin
  txnBegin                = (TxnSession_t)dlsym(handle, "TSHttpTxnBegin");
  const char *dlsym_error = dlerror();
  if (dlsym_error) {
    txnBegin = NULL;
    Error(DBG_TAG, "Cannot load symbol 'TSHttpTxnBegin': %s", dlsym_error);
    // Close library
    dlclose(handle);
    return;
  } else {
    Debug(DBG_TAG, "Successfully load symbol 'TSHttpTxnBegin'");
  }

  // Set up txnEnd
  txnEnd      = (TxnSession_t)dlsym(handle, "TSHttpTxnEnd");
  dlsym_error = dlerror();
  if (dlsym_error) {
    txnEnd = NULL;
    // Both of txnBegin and txnEnd are required.
    txnBegin = NULL;
    // Close library
    dlclose(handle);
    Error(DBG_TAG, "Cannot load symbol 'TSHttpTxnEnd': %s", dlsym_error);
    return;
  } else {
    Debug(DBG_TAG, "Successfully load symbol 'TSHttpTxnEnd");
  }

  // Set up prePluginHook
  prePluginHook = (PluginHook_t)dlsym(handle, "TSHttpTxnPrePluginHook");
  dlsym_error   = dlerror();
  if (dlsym_error) {
    prePluginHook = NULL;
    Debug(DBG_TAG, "Cannot load symbol 'TSHttpTxnPrePluginHook': %s", dlsym_error);
  } else {
    Debug(DBG_TAG, "Successfully load symbol 'TSHttpTxnPrePluginHook");
  }

  // Set up postPluginHook
  postPluginHook = (PluginHook_t)dlsym(handle, "TSHttpTxnPostPluginHook");
  dlsym_error    = dlerror();
  if (dlsym_error) {
    postPluginHook = NULL;
    Debug(DBG_TAG, "Cannot load symbol 'TSHttpTxnPostPluginHook': %s", dlsym_error);
  } else {
    Debug(DBG_TAG, "Successfully load symbol 'TSHttpTxnPostPluginHook");
  }

  // Neither of the hooks is defined, so close the library.
  if (!prePluginHook && !postPluginHook) {
    txnBegin = NULL;
    txnEnd   = NULL;
    dlclose(handle);
  }
}
