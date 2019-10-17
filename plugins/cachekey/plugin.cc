/*
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

/**
 * @file plugin.cc
 * @brief traffic server plugin entry points.
 */

#include "ts/ts.h"
#include "ts/remap.h"
#include "cachekey.h"
#include "common.h"

/* Configuration only used by the global plugin instance. */
Configs *globalConfig = nullptr;

/**
 * @brief Set the cache key called by both global and remap instances.
 *
 * @param txn transaction handle
 * @param config cachekey configuration
 */
static void
setCacheKey(TSHttpTxn txn, Configs *config, TSRemapRequestInfo *rri = nullptr)
{
  const CacheKeyKeyTypeSet &keyTypes = config->getKeyType();

  for (auto type : keyTypes) {
    /* Initial cache key facility from the requested URL. */
    CacheKey cachekey(txn, config->getSeparator(), config->getUriType(), type, rri);

    /* Append custom prefix or the host:port */
    if (!config->prefixToBeRemoved()) {
      cachekey.appendPrefix(config->_prefix, config->_prefixCapture, config->_prefixCaptureUri, config->canonicalPrefix());
    }
    /* Classify User-Agent and append the class name to the cache key if matched. */
    cachekey.appendUaClass(config->_classifier);

    /* Capture from User-Agent header. */
    cachekey.appendUaCaptures(config->_uaCapture);

    /* Append headers to the cache key. */
    cachekey.appendHeaders(config->_headers);

    /* Append cookies to the cache key. */
    cachekey.appendCookies(config->_cookies);

    /* Append the path to the cache key. */
    if (!config->pathToBeRemoved()) {
      cachekey.appendPath(config->_pathCapture, config->_pathCaptureUri);
    }
    /* Append query parameters to the cache key. */
    cachekey.appendQuery(config->_query);

    /* Set the cache key */
    cachekey.finalize();
  }
}

static int
contSetCachekey(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txn = static_cast<TSHttpTxn>(edata);

  setCacheKey(txn, globalConfig);

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

/**
 * @brief Global plugin instance initialization.
 *
 * Processes the configuration and initializes a global plugin instance.
 * @param argc plugin arguments number
 * @param argv plugin arguments
 */
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    CacheKeyError("global plugin registration failed");
  }

  globalConfig = new Configs();
  if (nullptr != globalConfig && globalConfig->init(argc, argv, /* perRemapConfig */ false)) {
    TSCont cont = TSContCreate(contSetCachekey, nullptr);
    TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, cont);

    CacheKeyDebug("global plugin initialized");
  } else {
    globalConfig = nullptr;
    delete globalConfig;

    CacheKeyError("failed to initialize global plugin");
  }
}

/**
 * @brief Plugin initialization.
 * @param apiInfo remap interface info pointer
 * @param errBuf error message buffer
 * @param errBufSize error message buffer size
 * @return always TS_SUCCESS.
 */
TSReturnCode
TSRemapInit(TSRemapInterface *apiInfo, char *errBuf, int erroBufSize)
{
  return TS_SUCCESS;
}

/**
 * @brief Plugin new instance entry point.
 *
 * Processes the configuration and initializes the plugin instance.
 * @param argc plugin arguments number
 * @param argv plugin arguments
 * @param instance new plugin instance pointer (initialized in this function)
 * @param errBuf error message buffer
 * @param errBufSize error message buffer size
 * @return TS_SUCCES if success or TS_ERROR if failure
 */
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **instance, char *errBuf, int errBufSize)
{
  Configs *config = new Configs();
  if (nullptr != config && config->init(argc, const_cast<const char **>(argv), /* perRemapConfig */ true)) {
    *instance = config;
  } else {
    CacheKeyError("failed to initialize the remap plugin");
    *instance = nullptr;
    delete config;
    return TS_ERROR;
  }

  CacheKeyDebug("remap plugin initialized");
  return TS_SUCCESS;
}

/**
 * @brief Plugin instance deletion clean-up entry point.
 * @param plugin instance pointer.
 */
void
TSRemapDeleteInstance(void *instance)
{
  Configs *config = static_cast<Configs *>(instance);
  delete config;
}

/**
 * @brief Sets the cache key during the remap.
 *
 * Remap is never done, continue with next in chain.
 * @param instance plugin instance pointer
 * @param txn transaction handle
 * @param rri remap request info pointer
 * @return always TSREMAP_NO_REMAP
 */
TSRemapStatus
TSRemapDoRemap(void *instance, TSHttpTxn txn, TSRemapRequestInfo *rri)
{
  Configs *config = static_cast<Configs *>(instance);

  if (nullptr != config) {
    setCacheKey(txn, config, rri);
  }

  return TSREMAP_NO_REMAP;
}
