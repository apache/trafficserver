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
 * @file pattern.h
 * @brief PRCE related classes (header file).
 */

#pragma once

#include "common.h"
#include "tsutil/Regex.h"

/**
 * @brief PCRE2 matching, capturing and replacing
 */
class Pattern
{
public:
  static const int TOKENCOUNT = 10; /**< @brief Capturing groups $0..$9 */

  Pattern();

  bool init(const String &pattern, const String &replacement);
  bool init(const String &config);
  bool empty() const;
  bool match(const String &subject);
  bool capture(const String &subject, StringVector &result);
  bool replace(const String &subject, String &result);
  bool process(const String &subject, StringVector &result);

private:
  bool compile();
  bool failed(const String &subject) const;

  Regex _regex;

  String _pattern;     /**< @brief PCRE2 pattern string, containing PCRE2 patterns and capturing groups. */
  String _replacement; /**< @brief PCRE2 replacement string with $0..$9 placeholders for capturing groups */

  int _tokenCount = 0;          /**< @brief number of replacements $0..$9 found in the replacement string if not empty */
  int _tokens[TOKENCOUNT];      /**< @brief replacement index 0..9, since they can be used in the replacement string in any order */
  int _tokenOffset[TOKENCOUNT]; /**< @brief replacement offset inside the replacement string */
};

/**
 * @brief Named list of regular expressions.
 */
class MultiPattern
{
public:
  MultiPattern(const String &name = "") : _name(name) {}
  virtual ~MultiPattern();

  bool          empty() const;
  void          add(std::unique_ptr<Pattern> pattern);
  virtual bool  match(const String &subject) const;
  virtual bool  replace(const String &subject, String &result) const;
  const String &name() const;

protected:
  std::vector<std::unique_ptr<Pattern>> _list; /**< @brief vector which dictates the order of the pattern evaluation. */
  String                                _name; /**< @brief multi-pattern name */

private:
  MultiPattern(const MultiPattern &);            // disallow
  MultiPattern &operator=(const MultiPattern &); // disallow
};
