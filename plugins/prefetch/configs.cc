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
 * @file configs.cc
 * @brief Plugin configuration.
 */

#include <fstream>   /* std::ifstream */
#include <getopt.h>  /* getopt_long() */
#include <sstream>   /* std::istringstream */
#include <strings.h> /* strncasecmp() */

#include "configs.h"

DbgCtl Bg_dbg_ctl{PLUGIN_NAME};

template <typename ContainerType>
static void
commaSeparateString(ContainerType &c, const String &input)
{
  std::istringstream istr(input);
  String             token;

  while (std::getline(istr, token, ',')) {
    c.insert(c.end(), token);
  }
}

static bool
isTrue(const char *arg)
{
  return (0 == strncasecmp("true", arg, 4) || 0 == strncasecmp("1", arg, 1) || 0 == strncasecmp("yes", arg, 3));
}

static String
fetchOverflowString(const EvalPolicy policy)
{
  switch (policy) {
  case EvalPolicy::Overflow64:
    return "64";
    break;
  case EvalPolicy::Bignum:
    return "Bignum";
    break;
  default:
    return "32";
    break;
  }
}

static bool
iequals(const StringView lhs, const StringView rhs)
{
  return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                    [](const char a, const char b) { return tolower(a) == tolower(b); });
}

void
PrefetchConfig::setFetchOverflow(const char *optarg)
{
  if (StringView("64") == optarg) {
    _fetchOverflow = EvalPolicy::Overflow64;
  } else if (iequals("bignum", optarg)) {
    _fetchOverflow = EvalPolicy::Bignum;
  }
}

/**
 * @brief initializes plugin configuration.
 * @param argc number of plugin parameters
 * @param argv plugin parameters
 */
bool
PrefetchConfig::init(int argc, char *argv[])
{
  static const struct option longopt[] = {
    {const_cast<char *>("front"),              optional_argument, nullptr, 'f'},
    {const_cast<char *>("api-header"),         optional_argument, nullptr, 'h'},
    {const_cast<char *>("cmcd-nor"),           optional_argument, nullptr, 'd'},
    {const_cast<char *>("next-header"),        optional_argument, nullptr, 'n'},
    {const_cast<char *>("fetch-policy"),       optional_argument, nullptr, 'p'},
    {const_cast<char *>("fetch-count"),        optional_argument, nullptr, 'c'},
    {const_cast<char *>("fetch-path-pattern"), optional_argument, nullptr, 'e'},
    {const_cast<char *>("fetch-query"),        optional_argument, nullptr, 'q'},
    {const_cast<char *>("fetch-max"),          optional_argument, nullptr, 'x'},
    {const_cast<char *>("replace-host"),       optional_argument, nullptr, 'r'},
    {const_cast<char *>("name-space"),         optional_argument, nullptr, 's'},
    {const_cast<char *>("metrics-prefix"),     optional_argument, nullptr, 'm'},
    {const_cast<char *>("exact-match"),        optional_argument, nullptr, 'y'},
    {const_cast<char *>("log-name"),           optional_argument, nullptr, 'l'},
    {const_cast<char *>("fetch-overflow"),     optional_argument, nullptr, 'o'},
    {nullptr,                                  0,                 nullptr, 0  },
  };

  bool status = true;
  optind      = 0;

  /* argv contains the "to" and "from" URLs. Skip the first so that the second one poses as the program name. */
  --argc;
  ++argv;

  for (;;) {
    int opt;
    opt = getopt_long(argc, (char *const *)argv, "", longopt, nullptr);

    if (opt == -1) {
      break;
    }

    PrefetchDebug("processing %s", argv[optind - 1]);

    switch (opt) {
    case 'f': /* --front */
      _front = ::isTrue(optarg);
      break;

    case 'h': /* --api-header */
      setApiHeader(optarg);
      break;

    case 'd': /* --cmcd-nor */
      _cmcd_nor = ::isTrue(optarg);
      break;

    case 'n': /* --next-header */
      setNextHeader(optarg);
      break;

    case 'p': /* --fetch-policy */
      setFetchPolicy(optarg);
      break;

    case 'c': /* --fetch-count */
      setFetchCount(optarg);
      break;

    case 'e': /* --fetch-path-pattern */ {
      Pattern *pattern = new Pattern();
      if (nullptr != pattern) {
        if (pattern->init(optarg)) {
          _nextPaths.add(pattern);
        } else {
          PrefetchError("failed to initialize next object pattern");
          delete pattern;
        }
      }
    } break;

    case 'q': /* --fetch-query */ {
      setQueryKey(optarg);
    } break;

    case 'x': /* --fetch-max */
      setFetchMax(optarg);
      break;

    case 'o': /* --fetch-overflow */
      setFetchOverflow(optarg);
      break;

    case 'r': /* --replace-host */
      setReplaceHost(optarg);
      break;

    case 's': /* --name-space */
      setNameSpace(optarg);
      break;

    case 'm': /* --metrics-prefix */
      setMetricsPrefix(optarg);
      break;

    case 'y': /* --exact-match */
      _exactMatch = ::isTrue(optarg);
      break;

    case 'l': /* --log-name */
      setLogName(optarg);
      break;
    }
  }

  status &= finalize();

  return status;
}

/**
 * @brief provides means for post-processing of the plugin parameters to finalize the configuration or to "cache" some of the
 * decisions for later use.
 * @return true if successful, false if failure.
 */
bool
PrefetchConfig::finalize()
{
  PrefetchDebug("front-end: %s", (_front ? "true" : "false"));
  PrefetchDebug("exact match: %s", (_exactMatch ? "true" : "false"));
  PrefetchDebug("query key: %s", _queryKey.c_str());
  PrefetchDebug("cncd-nor: %s", (_front ? "true" : "false"));
  PrefetchDebug("API header name: %s", _apiHeader.c_str());
  PrefetchDebug("next object header name: %s", _nextHeader.c_str());
  PrefetchDebug("fetch policy parameters: %s", _fetchPolicy.c_str());
  PrefetchDebug("fetch count: %d", _fetchCount);
  PrefetchDebug("fetch concurrently max: %d", _fetchMax);
  String fostr = fetchOverflowString(_fetchOverflow);
  PrefetchDebug("fetch overflow: %.*s", (int)fostr.length(), fostr.data());
  PrefetchDebug("replace host name: %s", _replaceHost.c_str());
  PrefetchDebug("name space: %s", _namespace.c_str());
  PrefetchDebug("log name: %s", _logName.c_str());

  return true;
}
