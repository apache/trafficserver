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

#include "pattern.h"
#include "common.h"

#include <map>

enum CacheKeyUriType {
  REMAP,
  PRISTINE,
};

enum CacheKeyKeyType {
  CACHE_KEY,
  PARENT_SELECTION_URL,
};

const char *getCacheKeyUriTypeName(CacheKeyUriType type);
const char *getCacheKeyKeyTypeName(CacheKeyKeyType type);

typedef std::set<CacheKeyKeyType> CacheKeyKeyTypeSet;

/**
 * @brief Plug-in configuration elements (query / headers / cookies).
 *
 * Query parameters, cookies and headers can be handle in a similar way, through a similar set of rules (methods and properties).
 */
class ConfigElements
{
public:
  ConfigElements() {}
  virtual ~ConfigElements();
  void setExclude(const char *arg);
  void setInclude(const char *arg);
  void setExcludePatterns(const char *arg);
  void setIncludePatterns(const char *arg);
  void setRemove(const char *arg);
  void setSort(const char *arg);
  void addCapture(const char *arg);
  const auto &
  getCaptures() const
  {
    return _captures;
  }
  /** @brief shows if the elements are to be sorted in the result */
  bool toBeSorted() const;
  /** @brief shows if the elements are to be removed from the result */
  bool toBeRemoved() const;
  /** @brief shows if the processing of elements is to be skipped */
  bool toBeSkipped() const;
  /** @brief shows if the element is to be included in the result */
  bool toBeAdded(const String &element) const;
  /** @brief returns the configuration element name for debug logging */
  virtual const String &name() const = 0;

  /**
   * @brief provides means for post-processing of the configuration after all of parameters are available.
   * @return true if successful, false if failure.
   */
  virtual bool finalize() = 0;

protected:
  bool noIncludeExcludeRules() const;
  bool setCapture(const String &name, const String &pattern);

  StringSet _exclude;
  StringSet _include;

  MultiPattern _includePatterns;
  MultiPattern _excludePatterns;

  bool _sort   = false;
  bool _remove = false;
  bool _skip   = false;

  std::map<String, MultiPattern *> _captures;
};

/**
 * @brief Query configuration class.
 */
class ConfigQuery : public ConfigElements
{
public:
  bool finalize() override;

private:
  const String &name() const override;
  static const String _NAME;
};

/**
 * @brief Headers configuration class.
 */
class ConfigHeaders : public ConfigElements
{
public:
  bool finalize() override;

  const StringSet &getInclude() const;

private:
  const String &name() const override;
  static const String _NAME;
};

/**
 * @brief Cookies configuration class.
 */
class ConfigCookies : public ConfigElements
{
public:
  bool finalize() override;

private:
  const String &name() const override;
  static const String _NAME;
};

/**
 * @brief Class holding all configurable rules on how the cache key need to be constructed.
 */
class Configs
{
public:
  Configs() {}
  /**
   * @brief initializes plugin configuration.
   * @param argc number of plugin parameters
   * @param argv plugin parameters
   * @param perRemapConfig boolean showing if this is per-remap config (vs global config).
   */
  bool init(int argc, const char *argv[], bool perRemapConfig);

  /**
   * @brief provides means for post-processing of the plugin parameters to finalize the configuration or to "cache" some of the
   * decisions for later use.
   * @return true if successful, false if failure.
   */
  bool finalize();

  /**
   * @brief Tells the caller if the prefix is to be removed (not processed at all).
   */
  bool prefixToBeRemoved();

  /**
   * @brief Tells the caller if the path is to be removed (not processed at all).
   */
  bool pathToBeRemoved();

  /**
   * @brief keep URI scheme and authority elements.
   */
  bool canonicalPrefix();

  /**
   * @brief set the cache key elements separator string.
   */
  void setSeparator(const char *arg);

  /**
   * @brief get the cache key elements separator string.
   */
  const String &getSeparator();

  /**
   * @brief sets the URI Type.
   */
  void setUriType(const char *arg);

  /**
   * @brief sets the target URI Type.
   */
  void setKeyType(const char *arg);

  /**
   * @brief get URI type.
   */
  CacheKeyUriType getUriType();

  /**
   * @brief get target URI type.
   */
  CacheKeyKeyTypeSet &getKeyType();

  /* Make the following members public to avoid unnecessary accessors */
  ConfigQuery _query;        /**< @brief query parameter related configuration */
  ConfigHeaders _headers;    /**< @brief headers related configuration */
  ConfigCookies _cookies;    /**< @brief cookies related configuration */
  Pattern _uaCapture;        /**< @brief the capture groups and the replacement string used for the User-Agent header capture */
  String _prefix;            /**< @brief cache key prefix string */
  Pattern _prefixCapture;    /**< @brief cache key prefix captured from the URI host:port */
  Pattern _prefixCaptureUri; /**< @brief cache key prefix captured from the URI as a whole */
  Pattern _pathCapture;      /**< @brief cache key element captured from the URI path */
  Pattern _pathCaptureUri;   /**< @brief cache key element captured from the URI as a whole */
  Classifier _classifier;    /**< @brief blacklist and white-list classifier used to classify User-Agent header */

private:
  /**
   * @brief a helper function which loads the classifier from files.
   * @param args classname + filename in '<classname>:<filename>' format.
   * @param blacklist true - load as a blacklist classifier, false - white-list.
   * @return true if successful, false otherwise.
   */
  bool loadClassifiers(const String &args, bool blacklist = true);

  bool _prefixToBeRemoved  = false; /**< @brief instructs the prefix (i.e. host:port) not to added to the cache key */
  bool _pathToBeRemoved    = false; /**< @brief instructs the path not to added to the cache key */
  bool _canonicalPrefix    = false; /**< @brief keep the URI scheme and authority element used as input to transforming into key */
  String _separator        = "/";   /**< @brief a separator used to separate the cache key elements extracted from the URI */
  CacheKeyUriType _uriType = REMAP; /**< @brief shows which URI the cache key will be based on */
  CacheKeyKeyTypeSet _keyTypes;     /**< @brief target URI to be modified, cache key or paren selection */
};
