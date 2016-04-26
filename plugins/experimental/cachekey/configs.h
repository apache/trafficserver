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

#ifndef PLUGINS_EXPERIMENTAL_CACHEKEY_CONFIGS_H_
#define PLUGINS_EXPERIMENTAL_CACHEKEY_CONFIGS_H_

#include "pattern.h"
#include "common.h"

/**
 * @brief Plug-in configuration elements (query / headers / cookies).
 *
 * Query parameters, cookies and headers can be handle in a similar way, through a similar set of rules (methods and properties).
 */
class ConfigElements
{
public:
  ConfigElements() : _sort(false), _remove(false), _skip(false) {}
  virtual ~ConfigElements() {}
  void setExclude(const char *arg);
  void setInclude(const char *arg);
  void setExcludePatterns(const char *arg);
  void setIncludePatterns(const char *arg);
  void setRemove(const char *arg);
  void setSort(const char *arg);

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

  StringSet _exclude;
  StringSet _include;

  MultiPattern _includePatterns;
  MultiPattern _excludePatterns;

  bool _sort;
  bool _remove;
  bool _skip;
};

/**
 * @brief Query configuration class.
 */
class ConfigQuery : public ConfigElements
{
public:
  bool finalize();

private:
  const String &name() const;
  static const String _NAME;
};

/**
 * @brief Headers configuration class.
 */
class ConfigHeaders : public ConfigElements
{
public:
  bool finalize();

  const StringSet &getInclude() const;

private:
  const String &name() const;
  static const String _NAME;
};

/**
 * @brief Cookies configuration class.
 */
class ConfigCookies : public ConfigElements
{
public:
  bool finalize();

private:
  const String &name() const;
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
   */
  bool init(int argc, char *argv[]);

  /**
   * @brief provides means for post-processing of the plugin parameters to finalize the configuration or to "cache" some of the
   * decisions for later use.
   * @return true if succesful, false if failure.
   */
  bool finalize();

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
};

#endif // PLUGINS_EXPERIMENTAL_CACHEKEY_CONFIGS_H_
