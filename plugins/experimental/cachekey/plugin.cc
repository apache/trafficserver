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
  if (NULL != config && config->init(argc, argv)) {
    *instance = config;
  } else {
    CacheKeyError("failed to initialize the " PLUGIN_NAME " plugin");
    *instance = NULL;
    delete config;
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

/**
 * @brief Plugin instance deletion clean-up entry point.
 * @param plugin instance pointer.
 */
void
TSRemapDeleteInstance(void *instance)
{
  Configs *config = (Configs *)instance;
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
  Configs *config = (Configs *)instance;

  if (NULL != config) {
    /* Initial cache key facility from the requested URL. */
    CacheKey cachekey(txn, rri->requestBufp, rri->requestUrl, rri->requestHdrp, config->getSeparator());

    /* Append custom prefix or the host:port */
    if (!config->prefixToBeRemoved()) {
      cachekey.appendPrefix(config->_prefix, config->_prefixCapture, config->_prefixCaptureUri);
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
    if (!cachekey.finalize()) {
      char *url;
      int len;

      url = TSHttpTxnEffectiveUrlStringGet(txn, &len);
      CacheKeyError("failed to set cache key for url %.*s", len, url);
      TSfree(url);
    }
  }

  return TSREMAP_NO_REMAP;
}
