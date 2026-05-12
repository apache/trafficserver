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
 * @file path.cc
 * @brief Prefetch path construction helpers.
 */

#include "path.h"

#include <cctype>

namespace
{
String
getDirectoryPrefix(const String &path)
{
  const String::size_type lastSlash = path.find_last_of('/');

  if (String::npos == lastSlash) {
    return {};
  }
  return path.substr(0, lastSlash + 1);
}

bool
normalizePath(StringView path, String &normalized)
{
  const bool absolute = !path.empty() && '/' == path.front();
  StringList segments;

  normalized.clear();

  for (String::size_type offset = 0; offset <= path.size();) {
    const String::size_type nextSeparator = path.find('/', offset);
    const StringView        segment       = path.substr(offset, nextSeparator - offset);

    if (segment.empty() || "." == segment) {
      // Skip empty and current-directory segments.
    } else if (".." == segment) {
      if (segments.empty()) {
        return false;
      }
      segments.pop_back();
    } else {
      segments.emplace_back(segment);
    }

    if (String::npos == nextSeparator) {
      break;
    }
    offset = nextSeparator + 1;
  }

  if (absolute) {
    normalized.push_back('/');
  }

  for (const auto &segment : segments) {
    if (!normalized.empty() && '/' != normalized.back()) {
      normalized.push_back('/');
    }
    normalized.append(segment);
  }

  if (!path.empty() && '/' == path.back() && (normalized.empty() || '/' != normalized.back())) {
    normalized.push_back('/');
  }

  return true;
}

int
hexDigitValue(char c)
{
  if ('0' <= c && c <= '9') {
    return c - '0';
  }
  c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if ('a' <= c && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

String
decodePathSeparatorsAndDots(StringView path)
{
  String decoded;

  decoded.reserve(path.size());
  for (String::size_type i = 0; i < path.size(); ++i) {
    if ('\\' == path[i]) {
      decoded.push_back('/');
    } else if ('%' == path[i] && i + 2 < path.size()) {
      const int high = hexDigitValue(path[i + 1]);
      const int low  = hexDigitValue(path[i + 2]);
      if (0 <= high && 0 <= low) {
        const char c = static_cast<char>((high << 4) | low);
        if ('.' == c || '/' == c || '\\' == c) {
          decoded.push_back('\\' == c ? '/' : c);
        } else {
          decoded.append(path.substr(i, 3));
        }
        i += 2;
      } else {
        decoded.push_back(path[i]);
      }
    } else {
      decoded.push_back(path[i]);
    }
  }

  return decoded;
}

bool
isUnderPrefix(const String &path, const String &prefix)
{
  return prefix.empty() || path.rfind(prefix, 0) == 0;
}

bool
startsWithSeparator(StringView path)
{
  return !path.empty() && ('/' == path.front() || '\\' == path.front());
}
} // namespace

bool
makeSafeRelativeFetchPath(const String &currentPath, const String &relativePath, SafeRelativeFetchPath &fetchPath)
{
  fetchPath = {};

  if (String::npos != relativePath.find('#')) {
    return false;
  }

  const String::size_type queryStart = relativePath.find('?');
  const StringView        pathPart =
    String::npos == queryStart ? StringView{relativePath} : StringView{relativePath}.substr(0, queryStart);
  const String decodedPathPart = decodePathSeparatorsAndDots(pathPart);

  if (pathPart.empty() || startsWithSeparator(pathPart) || startsWithSeparator(decodedPathPart)) {
    return false;
  }

  const String basePrefix = getDirectoryPrefix(currentPath);
  const String candidatePath{basePrefix + String{pathPart}};

  String normalizedBasePrefix;
  String normalizedCandidatePath;
  if (!normalizePath(basePrefix, normalizedBasePrefix) || !normalizePath(candidatePath, normalizedCandidatePath)) {
    return false;
  }

  String validationBasePrefix;
  String validationCandidatePath;
  if (!normalizePath(decodePathSeparatorsAndDots(basePrefix), validationBasePrefix) ||
      !normalizePath(decodePathSeparatorsAndDots(candidatePath), validationCandidatePath)) {
    return false;
  }

  if (!isUnderPrefix(normalizedCandidatePath, normalizedBasePrefix) ||
      !isUnderPrefix(validationCandidatePath, validationBasePrefix)) {
    return false;
  }

  fetchPath.path = normalizedCandidatePath;
  if (String::npos != queryStart) {
    fetchPath.hasQuery = true;
    fetchPath.query    = relativePath.substr(queryStart + 1);
  }
  return true;
}
