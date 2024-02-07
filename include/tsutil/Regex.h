/** @file

  A brief file description

  @section license License

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

#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <memory>

#define PCRE2_CODE_UNIT_WIDTH 8
#if __has_include(<pcre2/pcre2.h>)
#include <pcre2/pcre2.h>
#else
#include <pcre2.h>
#endif

/// Match flags for regular expression evaluation.
enum REFlags {
  RE_CASE_INSENSITIVE = 0x0001, ///< Ignore case (default: case sensitive).
  RE_UNANCHORED       = 0x0002, ///< Unanchored (DFA defaults to anchored).
  RE_ANCHORED         = 0x0004, ///< Anchored (Regex defaults to unanchored).
};

//----------------------------------------------------------------------------
class RegexMatches
{
public:
  RegexMatches(uint32_t size = 20);
  ~RegexMatches();

  pcre2_match_data *get_match_data();
  void set_subject(std::string_view subject);
  std::string_view operator[](size_t index) const;

private:
  pcre2_match_data *_match_data = nullptr;
  std::string_view _subject;
};

/** Wrapper for PCRE evaluation.
 *
 */
class Regex
{
public:
  /// Default number of capture groups.
  static constexpr size_t DEFAULT_GROUP_COUNT = 10;

  Regex()              = default;
  Regex(Regex const &) = delete; // No copying.
  Regex(Regex &&that) noexcept;
  ~Regex();

  /** Compile the @a pattern into a regular expression.
   *
   * @param pattern Source pattern for regular expression (null terminated).
   * @param flags Compilation flags.
   * @return @a true if compiled successfully, @a false otherwise.
   *
   * @a flags should be the bitwise @c or of @c REFlags values.
   */
  bool compile(std::string_view, uint32_t flags = 0);

  /** Compile the @a pattern into a regular expression.
   *
   * @param pattern Source pattern for regular expression (null terminated).
   * @param flags Compilation flags.
   * @param error Pointer to string to receive error message.
   * @param erroffset Pointer to integer to receive error offset.
   * @return @a true if compiled successfully, @a false otherwise.
   *
   * @a flags should be the bitwise @c or of @c REFlags values.
   */
  bool compile(std::string_view pattern, std::string &error, int &erroffset, unsigned flags = 0);

  /** Execute the regular expression.
   *
   * @param str String to match against.
   * @return @c true if the pattern matched, @a false if not.
   *
   * It is safe to call this method concurrently on the same instance of @a this.
   */
  bool exec(const std::string_view &subject) const;

  /** Execute the regular expression.
   *
   * @param str String to match against.
   * @param ovector Capture results.
   * @param ovecsize Number of elements in @a ovector.
   * @return @c true if the pattern matched, @a false if not.
   *
   * It is safe to call this method concurrently on the same instance of @a this.
   *
   * Each capture group takes 3 elements of @a ovector, therefore @a ovecsize must
   * be a multiple of 3 and at least three times the number of desired capture groups.
   */
  int exec(const std::string_view &subject, RegexMatches &matches) const;

  /// @return The number of groups captured in the last call to @c exec.
  int get_capture_count();

private:
  // @internal - Because the PCRE header is badly done, we can't forward declare the PCRE
  // enough to use as pointers. For some reason the header defines in name only a struct and
  // then aliases it to the standard name, rather than simply declare the latter in name only.
  // The goal is completely wrap PCRE and not include that header in client code.
  pcre2_code *_code = nullptr;
};

/** Deterministic Finite state Automata container.
 *
 * This contains a set of patterns (which may be of size 1) and matches if any of the patterns
 * match.
 */
class DFA
{
public:
  DFA() = default;
  ~DFA();

  /// @return The number of patterns successfully compiled.
  int compile(std::string_view const &pattern, unsigned flags = 0);
  /// @return The number of patterns successfully compiled.
  int compile(std::string_view *patterns, int npatterns, unsigned flags = 0);
  /// @return The number of patterns successfully compiled.
  int compile(const char **patterns, int npatterns, unsigned flags = 0);

  /** Match @a str against the internal patterns.
   *
   * @param str String to match.
   * @return Index of the matched pattern, -1 if no match.
   */
  int match(std::string_view const &str) const;

private:
  struct Pattern {
    Pattern(Regex &&rxp, std::string &&s) : _re(std::move(rxp)), _p(std::move(s)) {}
    Regex _re;      ///< The compile pattern.
    std::string _p; ///< The original pattern.
  };

  /** Compile @a pattern and add it to the pattern set.
   *
   * @param pattern Regular expression to compile.
   * @param flags Regular expression compilation flags.
   * @return @c true if @a pattern was successfully compiled, @c false if not.
   */
  bool build(std::string_view const &pattern, unsigned flags = 0);

  std::vector<Pattern> _patterns;
};
