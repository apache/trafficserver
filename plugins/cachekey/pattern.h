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
 * @brief Regex related classes (header file).
 */

#pragma once

#include "tscore/ink_defs.h"
#include "tsutil/Regex.h"

#include "common.h"

/**
 * @brief Regex matching, capturing and replacing
 */
class Pattern
{
public:
  static const int TOKENCOUNT = 10; /**< @brief Capturing groups $0..$9 */

  Pattern();
  virtual ~Pattern();

  bool init(const String &pattern, const String &replacement, bool replace);
  bool init(const String &config);
  bool empty() const;
  bool match(const String &subject);
  bool capture(const String &subject, StringVector &result);
  bool replace(const String &subject, String &result);
  bool process(const String &subject, StringVector &result);

private:
  bool compile();

  Regex _re; /**< @brief Regex compiled object */

  String _pattern; /**< @brief Regex pattern string, containing regex patterns and capturing groups. */
  String
    _replacement; /**< @brief Regex replacement string, containing $0..$9 to be replaced with content of the capturing groups */

  bool _replace = false; /**< @brief true if a replacement is needed, false if not, this is to distinguish between an empty
                    replacement string and no replacement needed case */

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
  const String &name() const;

  bool process(const String &subject, StringVector &result) const;

protected:
  std::vector<std::unique_ptr<Pattern>> _list; /**< @brief vector which dictates the order of the pattern evaluation. */
  String                                _name; /**< @brief multi-pattern name */

  // noncopyable
  MultiPattern(const MultiPattern &)            = delete; // disallow
  MultiPattern &operator=(const MultiPattern &) = delete; // disallow
};

/**
 * @brief Named list of non-matching regular expressions.
 */
class NonMatchingMultiPattern : public MultiPattern
{
public:
  NonMatchingMultiPattern(const String &name) { _name = name; }
  /*
   * @brief Matches the subject string against all patterns.
   * @param subject subject string
   * @return return false if any of the patterns matches, true otherwise.
   */
  bool
  match(const String &subject) const override
  {
    return !MultiPattern::match(subject);
  }

  // noncopyable
  NonMatchingMultiPattern(const NonMatchingMultiPattern &)            = delete; // disallow
  NonMatchingMultiPattern &operator=(const NonMatchingMultiPattern &) = delete; // disallow
};

/**
 * @brief Simple classifier which classifies a subject string using a list of named multi-patterns.
 */
class Classifier
{
public:
  Classifier() {}
  ~Classifier();

  bool classify(const String &subject, String &name) const;
  void add(std::unique_ptr<MultiPattern> pattern);

  // noncopyable
  Classifier(const Classifier &)            = delete; // disallow
  Classifier &operator=(const Classifier &) = delete; // disallow

private:
  std::vector<std::unique_ptr<MultiPattern>> _list; /**< @brief vector which dictates the multi-pattern evaluation order */
};
