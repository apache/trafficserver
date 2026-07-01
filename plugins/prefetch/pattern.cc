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
 * @file pattern.cc
 * @brief PRCE related classes.
 * @see pattern.h
 */

#include "pattern.h"
#include "tsutil/Regex.h"

static void
replaceString(String &str, const String &from, const String &to)
{
  if (from.empty()) {
    return;
  }

  String::size_type start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != String::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

Pattern::Pattern() : _pattern(""), _replacement("") {}

/**
 * @brief Initializes PCRE2 pattern by providing the subject and replacement strings.
 * @param pattern PCRE2 pattern, a string containing PCRE2 patterns, capturing groups.
 * @param replacement PCRE2 replacement, a string where $0 ... $9 will be replaced with the corresponding capturing groups
 * @return true if successful, false if failure
 */
bool
Pattern::init(const String &pattern, const String &replacement)
{
  _pattern.assign(pattern);
  _replacement.assign(replacement);

  _tokenCount = 0;

  if (!compile()) {
    PrefetchDebug("failed to initialize pattern:'%s', replacement:'%s'", pattern.c_str(), replacement.c_str());
    return false;
  }

  return true;
}

/**
 * @brief Initializes PCRE2 pattern by providing the pattern only or pattern+replacement in a single configuration string.
 * @see init()
 * @param config PCRE2 pattern <pattern> or PCRE2 pattern + replacement in format /<pattern>/<replacement>/
 * @return true if successful, false if failure
 */
bool
Pattern::init(const String &config)
{
  if (config[0] == '/') {
    /* This is a config in format /regex/replacement/ */
    String pattern;
    String replacement;

    size_t start   = 1;
    size_t current = 0;
    size_t next    = 1;
    do {
      current = next + 1;
      next    = config.find_first_of('/', current);
    } while (next != String::npos && '\\' == config[next - 1]);

    if (next != String::npos) {
      pattern = config.substr(start, next - start);
    } else {
      /* Error, no closing '/' */
      PrefetchError("failed to parse the pattern in '%s'", config.c_str());
      return false;
    }

    start = next + 1;
    do {
      current = next + 1;
      next    = config.find_first_of('/', current);
    } while (next != String::npos && '\\' == config[next - 1]);

    if (next != String::npos) {
      replacement = config.substr(start, next - start);
    } else {
      /* Error, no closing '/' */
      PrefetchError("failed to parse the replacement in '%s'", config.c_str());
      return false;
    }

    // Remove '\' which escaped '/' inside the pattern and replacement strings.
    ::replaceString(pattern, "\\/", "/");
    ::replaceString(replacement, "\\/", "/");

    return this->init(pattern, replacement);
  } else {
    return this->init(config, "");
  }

  /* Should never get here. */
  return false;
}

/**
 * @brief Checks if the pattern object was initialized with a meaningful regex pattern.
 * @return true if initialized, false if not.
 */
bool
Pattern::empty() const
{
  return _pattern.empty() || _regex.empty();
}

/**
 * @brief PCRE2 matches a subject string against the regex pattern.
 * @param subject PCRE2 subject
 * @return true - matched, false - did not.
 */
bool
Pattern::match(const String &subject)
{
  PrefetchDebug("matching '%s' to '%s'", _pattern.c_str(), subject.c_str());

  if (_regex.empty()) {
    return false;
  }

  RegexMatches matches;
  int          matchCount = _regex.exec(subject, matches, RE_NOTEMPTY);
  if (matchCount < 0) {
    if (matchCount != RE_ERROR_NOMATCH) {
      PrefetchError("matching error %d", matchCount);
    }
    return false;
  }

  return true;
}

/**
 * @brief Replaces all replacements found in the replacement string with what matched in the PCRE2 capturing groups.
 * @param subject PCRE2 subject string
 * @param result reference to A string where the result of the replacement will be stored
 * @return true - success, false - nothing matched or failure.
 */
bool
Pattern::replace(const String &subject, String &result)
{
  PrefetchDebug("matching '%s' to '%s'", _pattern.c_str(), subject.c_str());

  if (_regex.empty()) {
    return false;
  }

  RegexMatches matches;
  int          matchCount = _regex.exec(subject, matches, RE_NOTEMPTY);

  if (matchCount <= 0) {
    if (matchCount != RE_ERROR_NOMATCH) {
      PrefetchError("matching error %d", matchCount);
    }
    return false;
  }

  int previous = 0;
  for (int i = 0; i < _tokenCount; i++) {
    int replIndex = _tokens[i];

    /* $replIndex was validated at config-load time against the number of groups the pattern defines, but
     * the group may still not have participated in *this* match (e.g. a trailing optional group such as
     * "(\?.*)?" when the subject has no query string).  pcre2_match() returns one past the highest
     * participating group, so substitute an empty string for a group at or beyond that -- the documented
     * PCRE2 semantics for an unmatched group -- rather than failing the whole replacement. */
    std::string_view dst = (replIndex < matchCount) ? matches[replIndex] : std::string_view{};

    PrefetchDebug("replacing '$%d' with '%.*s'", replIndex, static_cast<int>(dst.length()), dst.data());

    result.append(_replacement, previous, _tokenOffset[i] - previous);
    result.append(dst.data(), dst.length());

    previous = _tokenOffset[i] + 2; /* 2 is the size of $0 or $1 or $2, ... or $9 */
  }

  result.append(_replacement, previous, _replacement.length() - previous);

  PrefetchDebug("replacing '%s' resulted in '%s'", _replacement.c_str(), result.c_str());

  return true;
}

/**
 * @brief PCRE2 compiles the regex, called only during initialization.
 * @return true if successful, false if not.
 */
bool
Pattern::compile()
{
  PrefetchDebug("compiling pattern:'%s', replacement:'%s'", _pattern.c_str(), _replacement.c_str());

  std::string error;
  int         erroffset;
  if (!_regex.compile(_pattern, error, erroffset)) {
    PrefetchError("compile of regex '%s' at char %d: %s", _pattern.c_str(), erroffset, error.c_str());
    return false;
  }

  if (_replacement.empty()) {
    /* No replacement necessary - we are done. */
    return true;
  }

  _tokenCount  = 0;
  bool success = true;

  for (unsigned i = 0; i < _replacement.length(); i++) {
    if (_replacement[i] == '$') {
      if (_tokenCount >= TOKENCOUNT) {
        PrefetchError("too many tokens in replacement string: %s", _replacement.c_str());

        success = false;
        break;
      } else if (_replacement[i + 1] < '0' || _replacement[i + 1] > '9') {
        PrefetchError("invalid replacement token $%c in %s: should be $0 - $9", _replacement[i + 1], _replacement.c_str());

        success = false;
        break;
      } else {
        /* Store the location of the replacement */
        /* Convert '0' to 0 */
        _tokens[_tokenCount]      = _replacement[i + 1] - '0';
        _tokenOffset[_tokenCount] = i;
        _tokenCount++;
        /* Skip the next char */
        i++;
      }
    }
  }

  /* Validate replacement references against the number of capture groups the pattern actually defines
   * (not how many happen to participate in any given match) at config-load time.  This catches a
   * genuinely out-of-range reference such as $5 against a 3-group pattern, and a pattern that defines
   * more groups than can be captured -- RegexMatches holds the whole match plus TOKENCOUNT-1 groups. */
  if (success) {
    int32_t captureCount = _regex.get_capture_count();
    if (captureCount < 0) {
      PrefetchError("failed to get capture count for regex '%s'", _pattern.c_str());
      success = false;
    } else if (captureCount > TOKENCOUNT - 1) {
      PrefetchError("regex '%s' defines %d capture groups; the prefetch plugin supports at most %d (references $0..$%d)",
                    _pattern.c_str(), captureCount, TOKENCOUNT - 1, TOKENCOUNT - 1);
      success = false;
    } else {
      for (int i = 0; i < _tokenCount; i++) {
        if (_tokens[i] > captureCount) {
          PrefetchError("invalid reference $%d in replacement '%s': pattern defines only %d group(s)", _tokens[i],
                        _replacement.c_str(), captureCount);
          success = false;
          break;
        }
      }
    }
  }

  return success;
}

/**
 * @brief Destructor, deletes all patterns.
 */
MultiPattern::~MultiPattern() {}

/**
 * @brief Check if empty.
 * @return true if the classification contains any patterns, false otherwise
 */
bool
MultiPattern::empty() const
{
  return _list.empty();
}

/**
 * @brief Adds a pattern to the multi-pattern
 *
 * The order of addition matters during the classification
 * @param pattern pattern pointer
 */
void
MultiPattern::add(std::unique_ptr<Pattern> pattern)
{
  this->_list.push_back(std::move(pattern));
}

/**
 * @brief Matches the subject string against all patterns.
 * @param subject subject string.
 * @return true if any matches, false if nothing matches.
 */
bool
MultiPattern::match(const String &subject) const
{
  for (auto &p : this->_list) {
    if (nullptr != p && p->match(subject)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Calls Pattern::replace() on all patterns in the multi-pattern one by one until the first match.
 * @param subject subject string.
 * @param result vector of the result.
 * @return true if any matches, false if nothing matches.
 */
bool
MultiPattern::replace(const String &subject, String &result) const
{
  for (auto &p : this->_list) {
    if (nullptr != p && p->replace(subject, result)) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Returns the name of the multi-pattern (set during the instantiation only).
 */
const String &
MultiPattern::name() const
{
  return _name;
}
