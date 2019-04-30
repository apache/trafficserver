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

#include "tscore/ink_defs.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#include "common.h"

/**
 * @brief PCRE matching, capturing and replacing
 */
class Pattern
{
public:
  static const int TOKENCOUNT = 10;             /**< @brief Capturing groups $0..$9 */
  static const int OVECOUNT   = TOKENCOUNT * 3; /**< @brief pcre_exec() array count, handle 10 capture groups */

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
  void pcreFree();

  pcre *_re          = nullptr; /**< @brief PCRE compiled info structure, computed during initialization */
  pcre_extra *_extra = nullptr; /**< @brief PCRE study data block, computed during initialization */

  String _pattern;     /**< @brief PCRE pattern string, containing PCRE patterns and capturing groups. */
  String _replacement; /**< @brief PCRE replacement string, containing $0..$9 to be replaced with content of the capturing groups */

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

  bool empty() const;
  void add(Pattern *pattern);
  virtual bool match(const String &subject) const;
  const String &name() const;

  bool process(const String &subject, StringVector &result) const;

protected:
  std::vector<Pattern *> _list; /**< @brief vector which dictates the order of the pattern evaluation. */
  String _name;                 /**< @brief multi-pattern name */

  // noncopyable
  MultiPattern(const MultiPattern &) = delete;            // disallow
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
  NonMatchingMultiPattern(const NonMatchingMultiPattern &) = delete;            // disallow
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
  void add(MultiPattern *pattern);

  // noncopyable
  Classifier(const Classifier &) = delete;            // disallow
  Classifier &operator=(const Classifier &) = delete; // disallow

private:
  std::vector<MultiPattern *> _list; /**< @brief vector which dictates the multi-pattern evaluation order */
};
