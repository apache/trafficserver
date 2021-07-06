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
 * @file config.cc
 * @brief Access Control Plug-in Configuration.
 * @see config.h
 */

#include <algorithm> /* find_if */
#include <fstream>   /* std::ifstream */
#include <getopt.h>  /* getopt_long() */

#include "common.h"
#include "config.h"

static bool
isTrue(const char *arg)
{
  return (nullptr == arg || 0 == strncasecmp("true", arg, 4) || 0 == strncasecmp("1", arg, 1) || 0 == strncasecmp("yes", arg, 3));
}

/**
 * trim from start
 */
static inline std::string &
ltrim(std::string &s)
{
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
  return s;
}

/**
 *  trim from end
 */
static inline std::string &
rtrim(std::string &s)
{
  s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
  return s;
}

/**
 * trim from both ends
 */
static inline std::string &
trim(std::string &s)
{
  return ltrim(rtrim(s));
}

/**
 * @brief Rebase a relative path onto the configuration directory.
 */
static String
makeConfigPath(const String &path)
{
  if (path.empty() || path[0] == '/') {
    return path;
  }

  return String(TSConfigDirGet()) + "/" + path;
}

/**
 * @brief parse and load a single line (base template - does nothing, see specializations for maps and vectors below)
 */
template <class T>
void
loadLine(T &container, const String &line)
{
}

/**
 * @brief parse and load a single line into a map.
 */
template <>
void
loadLine<StringMap>(StringMap &map, const String &line)
{
  String key;
  String value;
  std::istringstream ss(line);
  std::getline(ss, key, '=');
  std::getline(ss, value, '=');
  trim(key);
  trim(value);
  map[key] = value;

#ifdef ACCESS_CONTROL_LOG_SECRETS
  AccessControlDebug("Adding secrets[%s]='%s'", key.c_str(), value.c_str());
#endif
}

/**
 * @brief parse and load a single line into a vector.
 */
template <>
void
loadLine<StringVector>(StringVector &vector, const String &line)
{
  String trimmedLine(line);
  trim(trimmedLine);
  vector.push_back(trimmedLine);

#ifdef ACCESS_CONTROL_LOG_SECRETS
  AccessControlDebug("Adding secrets[%d]='%s'", (int)(vector.size() - 1), trimmedLine.c_str());
#endif
}

/**
 * @brief parse and load a secrets into a container (i.e. map or vector).
 */
template <typename T>
static bool
load(T &container, const String &filename)
{
  String line;
  String::size_type pos;

  String path(makeConfigPath(filename));

  AccessControlDebug("reading file %s", path.c_str());

  std::ifstream infile;
  infile.open(path.c_str());
  if (!infile.is_open()) {
    AccessControlError("failed to load file '%s'", path.c_str());
    return false;
  }

  while (std::getline(infile, line)) {
    // Allow #-prefixed comments.
    pos = line.find_first_of('#');
    if (pos != String::npos) {
      line.resize(pos);
    }
    if (line.empty()) {
      continue;
    }

    loadLine(container, line);
  }
  infile.close();

  return true;
}

/**
 * @brief initializes plugin configuration.
 * @param argc number of plugin parameters
 * @param argv plugin parameters
 */
bool
AccessControlConfig::init(int argc, char *argv[])
{
  static const struct option longopt[] = {{const_cast<char *>("invalid-syntax-status-code"), optional_argument, nullptr, 'a'},
                                          {const_cast<char *>("invalid-signature-status-code"), optional_argument, nullptr, 'b'},
                                          {const_cast<char *>("invalid-timing-status-code"), optional_argument, nullptr, 'c'},
                                          {const_cast<char *>("invalid-scope-status-code"), optional_argument, nullptr, 'd'},
                                          {const_cast<char *>("invalid-origin-response"), optional_argument, nullptr, 'e'},
                                          {const_cast<char *>("internal-error-status-code"), optional_argument, nullptr, 'f'},
                                          {const_cast<char *>("check-cookie"), optional_argument, nullptr, 'g'},
                                          {const_cast<char *>("symmetric-keys-map"), optional_argument, nullptr, 'h'},
                                          {const_cast<char *>("reject-invalid-token-requests"), optional_argument, nullptr, 'i'},
                                          {const_cast<char *>("extract-subject-to-header"), optional_argument, nullptr, 'j'},
                                          {const_cast<char *>("extract-tokenid-to-header"), optional_argument, nullptr, 'k'},
                                          {const_cast<char *>("extract-status-to-header"), optional_argument, nullptr, 'l'},
                                          {const_cast<char *>("token-response-header"), optional_argument, nullptr, 'm'},
                                          {const_cast<char *>("use-redirects"), optional_argument, nullptr, 'n'},
                                          {const_cast<char *>("include-uri-paths-file"), optional_argument, nullptr, 'o'},
                                          {const_cast<char *>("exclude-uri-paths-file"), optional_argument, nullptr, 'p'},
                                          {nullptr, 0, nullptr, 0}};

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
    AccessControlDebug("processing %s", argv[optind - 1]);

    switch (opt) {
    case 'a': /* invalid-syntax-status-code */
    {
      _invalidSignature = static_cast<TSHttpStatus>(string2int(optarg));
    } break;
    case 'b': /* invalid-signature-status-code */
    {
      _invalidSignature = static_cast<TSHttpStatus>(string2int(optarg));
    } break;
    case 'c': /* invalid-timing-status-code */
    {
      _invalidTiming = static_cast<TSHttpStatus>(string2int(optarg));
    } break;
    case 'd': /* invalid-scope-status-code */
    {
      _invalidScope = static_cast<TSHttpStatus>(string2int(optarg));
    } break;
    case 'e': /* invalid-origin-response */
    {
      _invalidOriginResponse = static_cast<TSHttpStatus>(string2int(optarg));
    } break;
    case 'f': /* internal-error-status-code */
    {
      _internalError = static_cast<TSHttpStatus>(string2int(optarg));
    } break;
    case 'g': /* check-cookie */
    {
      _cookieName.assign(optarg);
    } break;
    case 'h': /* symmetric-keys-map */
    {
      load(_symmetricKeysMap, optarg);
    } break;
    case 'i': /* reject-invalid-token-requests */
    {
      _rejectRequestsWithInvalidTokens = ::isTrue(optarg);
    } break;
    case 'j': /* extract-subject-to-header */
    {
      _extrSubHdrName.assign(optarg);
    } break;
    case 'k': /* extract-tokenid-to-header */
    {
      _extrTokenIdHdrName.assign(optarg);
    } break;
    case 'l': /* extract-status-to-header */
    {
      _extrValidationHdrName.assign(optarg);
    } break;
    case 'm': /* token-response-header */
    {
      _respTokenHeaderName.assign(optarg);
    } break;
    case 'n': /* use-redirects */
    {
      _useRedirects = ::isTrue(optarg);
    } break;
    case 'o': /* include-uri-paths-file */
      if (!loadMultiPatternsFromFile(optarg, /* denylist = */ false)) {
        AccessControlError("failed to load uri-path multi-pattern allow-list '%s'", optarg);
        status = false;
      }
      break;
    case 'p': /* exclude-uri-paths-file */
      if (!loadMultiPatternsFromFile(optarg, /* denylist = */ true)) {
        AccessControlError("failed to load uri-path multi-pattern deny-list '%s'", optarg);
        status = false;
      }
      break;

    default: {
      status = false;
    }
    }
  }

  /* Make sure at least 1 secret source is specified */
  if (_symmetricKeysMap.empty()) {
    AccessControlDebug("no secrets' source provided");
    return false;
  }

  /* Support only KeyValuePair syntax for now */
  _tokenFactory = new AccessTokenFactory(_kvpAccessTokenConfig, _symmetricKeysMap, _debugLevel);
  if (nullptr == _tokenFactory) {
    AccessControlDebug("failed to initialize the access token factory");
    return false;
  }
  return status;
}

/**
 * @brief a helper function which loads the classifier from files.
 * @param filename file name
 * @param denylist true - load as a denylist of patterns, false - allow-list of patterns
 * @return true if successful, false otherwise.
 */
bool
AccessControlConfig::loadMultiPatternsFromFile(const String &filename, bool denylist)
{
  if (filename.empty()) {
    AccessControlError("filename cannot be empty");
    return false;
  }

  String path(makeConfigPath(filename));

  std::ifstream ifstr;
  String regex;
  unsigned lineno = 0;

  ifstr.open(path.c_str());
  if (!ifstr) {
    AccessControlError("failed to load uri-path multi-pattern from '%s'", path.c_str());
    return false;
  }

  /* Have the multiplattern be named as same as the filename, would be used only for debugging. */
  MultiPattern *multiPattern;
  if (denylist) {
    multiPattern = new NonMatchingMultiPattern(filename);
    AccessControlDebug("NonMatchingMultiPattern('%s')", filename.c_str());
  } else {
    multiPattern = new MultiPattern(filename);
    AccessControlDebug("MultiPattern('%s')", filename.c_str());
  }
  if (nullptr == multiPattern) {
    AccessControlError("failed to allocate multi-pattern from '%s'", filename.c_str());
    return false;
  }

  AccessControlDebug("loading multi-pattern '%s' from '%s'", filename.c_str(), path.c_str());

  while (std::getline(ifstr, regex)) {
    Pattern *p;
    String::size_type pos;

    ++lineno;

    // Allow #-prefixed comments.
    pos = regex.find_first_of('#');
    if (pos != String::npos) {
      regex.resize(pos);
    }

    if (regex.empty()) {
      continue;
    }

    p = new Pattern();

    if (nullptr != p && p->init(regex)) {
      if (denylist) {
        AccessControlDebug("Added pattern '%s' to deny list uri-path multi-pattern '%s'", regex.c_str(), filename.c_str());
        multiPattern->add(p);
      } else {
        AccessControlDebug("Added pattern '%s' to allow list uri-path multi-pattern '%s'", regex.c_str(), filename.c_str());
        multiPattern->add(p);
      }
    } else {
      AccessControlError("%s:%u: failed to parse regex '%s'", path.c_str(), lineno, regex.c_str());
      delete p;
    }
  }

  ifstr.close();

  if (!multiPattern->empty()) {
    _uriPathScope.add(multiPattern);
  } else {
    delete multiPattern;
  }

  return true;
}
