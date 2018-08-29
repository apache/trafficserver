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

template <typename ContainerType>
static void
commaSeparateString(ContainerType &c, const String &input)
{
  std::istringstream istr(input);
  String token;

  while (std::getline(istr, token, ',')) {
    c.insert(c.end(), token);
  }
}

static bool
isTrue(const char *arg)
{
  return (0 == strncasecmp("true", arg, 4) || 0 == strncasecmp("1", arg, 1) || 0 == strncasecmp("yes", arg, 3));
}

/**
 * @brief initializes plugin configuration.
 * @param argc number of plugin parameters
 * @param argv plugin parameters
 */
bool
PrefetchConfig::init(int argc, char *argv[])
{
  static const struct option longopt[] = {{const_cast<char *>("front"), optional_argument, 0, 'f'},
                                          {const_cast<char *>("api-header"), optional_argument, 0, 'h'},
                                          {const_cast<char *>("next-header"), optional_argument, 0, 'n'},
                                          {const_cast<char *>("fetch-policy"), optional_argument, 0, 'p'},
                                          {const_cast<char *>("fetch-count"), optional_argument, 0, 'c'},
                                          {const_cast<char *>("fetch-path-pattern"), optional_argument, 0, 'e'},
                                          {const_cast<char *>("fetch-max"), optional_argument, 0, 'x'},
                                          {const_cast<char *>("replace-host"), optional_argument, 0, 'r'},
                                          {const_cast<char *>("name-space"), optional_argument, 0, 's'},
                                          {const_cast<char *>("metrics-prefix"), optional_argument, 0, 'm'},
                                          {const_cast<char *>("exact-match"), optional_argument, 0, 'y'},
                                          {const_cast<char *>("log-name"), optional_argument, 0, 'l'},
                                          {0, 0, 0, 0}};

  bool status = true;
  optind      = 0;

  /* argv contains the "to" and "from" URLs. Skip the first so that the second one poses as the program name. */
  argc--;
  argv++;

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

    case 'x': /* --fetch-max */
      setFetchMax(optarg);
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
  PrefetchDebug("API header name: %s", _apiHeader.c_str());
  PrefetchDebug("next object header name: %s", _nextHeader.c_str());
  PrefetchDebug("fetch policy parameters: %s", _fetchPolicy.c_str());
  PrefetchDebug("fetch count: %d", _fetchCount);
  PrefetchDebug("fetch concurrently max: %d", _fetchMax);
  PrefetchDebug("replace host name: %s", _replaceHost.c_str());
  PrefetchDebug("name space: %s", _namespace.c_str());
  PrefetchDebug("log name: %s", _logName.c_str());

  return true;
}
