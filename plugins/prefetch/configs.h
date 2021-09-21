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
 * @file configs.h
 * @brief Plugin configuration (header file).
 */

#pragma once

#include <string>

#include "common.h"
#include "pattern.h"

/**
 * @brief Prefetch configuration instance
 */
class PrefetchConfig
{
public:
  PrefetchConfig()
    : _apiHeader("X-AppleCDN-Prefetch"),
      _nextHeader("X-AppleCDN-Prefetch-Next"),
      _replaceHost(),
      _namespace("default"),
      _metricsPrefix("prefetch.stats")

  {
  }

  /**
   * @brief initializes plugin configuration.
   * @param argc number of plugin parameters
   * @param argv plugin parameters
   */
  bool init(int argc, char *argv[]);

  void
  setApiHeader(const char *optarg)
  {
    _apiHeader.assign(optarg);
  }

  const std::string &
  getApiHeader() const
  {
    return _apiHeader;
  }

  void
  setNextHeader(const char *optarg)
  {
    _nextHeader.assign(optarg);
  }

  const std::string &
  getNextHeader() const
  {
    return _nextHeader;
  }

  void
  setFetchPolicy(const char *optarg)
  {
    _fetchPolicy.assign(optarg);
  }

  const std::string &
  getFetchPolicy() const
  {
    return _fetchPolicy;
  }

  void
  setReplaceHost(const char *optarg)
  {
    _replaceHost.assign(optarg);
  }

  const std::string &
  getReplaceHost() const
  {
    return _replaceHost;
  }

  bool
  isFront() const
  {
    return _front;
  }

  bool
  isExactMatch() const
  {
    return _exactMatch;
  }

  void
  setFetchCount(const char *optarg)
  {
    _fetchCount = getValue(optarg);
  }

  unsigned
  getFetchCount() const
  {
    return _fetchCount;
  }

  void
  setFetchMax(const char *optarg)
  {
    _fetchMax = getValue(optarg);
  }

  unsigned
  getFetchMax() const
  {
    return _fetchMax;
  }

  void
  setNameSpace(const char *optarg)
  {
    _namespace.assign(optarg);
  }

  const String &
  getNameSpace() const
  {
    return _namespace;
  }

  void
  setMetricsPrefix(const char *optarg)
  {
    _metricsPrefix.assign(optarg);
  }

  const String &
  getMetricsPrefix() const
  {
    return _metricsPrefix;
  }

  MultiPattern &
  getNextPath()
  {
    return _nextPaths;
  }

  void
  setLogName(const char *optarg)
  {
    _logName.assign(optarg);
  }

  const String &
  getLogName() const
  {
    return _logName;
  }

  void
  setQueryKey(const char *optarg)
  {
    _queryKey.assign(optarg);
  }

  const String &
  getQueryKeyName() const
  {
    return _queryKey;
  }

  /**
   * @brief provides means for post-processing of the plugin parameters to finalize the configuration.
   * @return true if successful, false if failure.
   */
  bool finalize();

private:
  std::string _apiHeader;
  std::string _nextHeader;
  std::string _fetchPolicy;
  std::string _replaceHost;
  std::string _namespace;
  std::string _metricsPrefix;
  std::string _logName;
  std::string _queryKey;
  unsigned _fetchCount = 1;
  unsigned _fetchMax   = 0;
  bool _front          = false;
  bool _exactMatch     = false;
  MultiPattern _nextPaths;
};
