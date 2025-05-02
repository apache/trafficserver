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

/// @brief Match flags for regular expression evaluation.
///
/// @internal These values are copied from pcre2.h, to avoid having to include it.  The values are checked (with
/// static_assert) in Regex.cc against PCRE2 named constants, in case they change in future PCRE2 releases.
enum REFlags {
  RE_CASE_INSENSITIVE = 0x00000008u, ///< Ignore case (default: case sensitive).
  RE_UNANCHORED       = 0x00000400u, ///< Unanchored (DFA defaults to anchored).
  RE_ANCHORED         = 0x80000000u, ///< Anchored (Regex defaults to unanchored).
};

/// @brief Wrapper for PCRE2 match data.
class RegexMatches
{
  friend class Regex;

public:
  /** Construct a new RegexMatches object.
   *
   * @param size The number of matches to allocate space for.
   */
  RegexMatches(uint32_t size = DEFAULT_MATCHES);
  ~RegexMatches();

  /** Get the match at the given index.
   *
   * @return The match at the given index.
   */
  std::string_view operator[](size_t index) const;
  /** Get the ovector pointer for the capture groups.  Don't use this unless you know what you are doing.
   *
   * @return ovector pointer.
   */
  size_t *get_ovector_pointer();
  int32_t size() const;

private:
  constexpr static uint32_t DEFAULT_MATCHES = 10;
  static void              *malloc(size_t size, void *caller);
  std::string_view          _subject;
  char    _buffer[24 + 96 + 28 * DEFAULT_MATCHES]; // 24 bytes for the general context, 96 bytes overhead, 28 bytes per match.
  size_t  _buffer_bytes_used = 0;
  int32_t _size              = 0;

  /// @internal This effectively wraps a void* so that we can avoid requiring the pcre2.h include for the user of the Regex
  /// API (see Regex.cc).
  struct _MatchData;
  class _MatchDataPtr
  {
    friend struct _MatchData;

  private:
    void *_ptr = nullptr;
  };
  _MatchDataPtr _match_data;
};

/// @brief Wrapper for PCRE2 regular expression.
class Regex
{
public:
  Regex()              = default;
  Regex(Regex const &) = delete; // No copying.
  Regex(Regex &&that) noexcept;
  Regex &operator=(Regex &&other);
  ~Regex();

  /** Compile the @a pattern into a regular expression.
   *
   * @param pattern Source pattern for regular expression (null terminated).
   * @param flags Compilation flags.
   * @return @a true if compiled successfully, @a false otherwise.
   *
   * @a flags should be the bitwise @c or of @c REFlags values.
   */
  bool compile(std::string_view pattern, uint32_t flags = 0);

  /** Compile the @a pattern into a regular expression.
   *
   * @param pattern Source pattern for regular expression (null terminated).
   * @param error String to receive error message.
   * @param erroffset Pointer to integer to receive error offset.
   * @param flags Compilation flags.
   * @return @a true if compiled successfully, @a false otherwise.
   *
   * @a flags should be the bitwise @c or of @c REFlags values.
   */
  bool compile(std::string_view pattern, std::string &error, int &erroffset, unsigned flags = 0);

  /** Execute the regular expression.
   *
   * @param subject String to match against.
   * @return @c true if the pattern matched, @a false if not.
   *
   * It is safe to call this method concurrently on the same instance of @a this.
   */
  bool exec(std::string_view subject) const;

  /** Execute the regular expression.
   *
   * @param subject String to match against.
   * @param matches Place to store the capture groups.
   * @return @c The number of capture groups. < 0 if an error occurred. 0 if the number of Matches is too small.
   *
   * It is safe to call this method concurrently on the same instance of @a this.
   *
   * Each capture group takes 3 elements of @a ovector, therefore @a ovecsize must
   * be a multiple of 3 and at least three times the number of desired capture groups.
   */
  int exec(std::string_view subject, RegexMatches &matches) const;

  /// @return The number of capture groups in the compiled pattern.
  int get_capture_count();

  /// @return Is the compiled pattern empty?
  bool empty() const;

private:
  /// @internal This effectively wraps a void* so that we can avoid requiring the pcre2.h include for the user of the Regex
  /// API (see Regex.cc).
  struct _Code;
  class _CodePtr
  {
    friend struct _Code;

  private:
    void *_ptr = nullptr;
  };
  _CodePtr _code;
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
  int32_t compile(const std::string_view pattern, unsigned flags = 0);
  /// @return The number of patterns successfully compiled.
  int32_t compile(const std::string_view *const patterns, int npatterns, unsigned flags = 0);
  /// @return The number of patterns successfully compiled.
  int32_t compile(const char *const *patterns, int npatterns, unsigned flags = 0);

  /** Match @a str against the internal patterns.
   *
   * @param str String to match.
   * @return Index of the matched pattern, -1 if no match.
   */
  int32_t match(std::string_view str) const;

private:
  struct Pattern {
    Pattern(Regex &&rxp, std::string &&s) : _re(std::move(rxp)), _p(std::move(s)) {}
    Regex       _re; ///< The compile pattern.
    std::string _p;  ///< The original pattern.
  };

  /** Compile @a pattern and add it to the pattern set.
   *
   * @param pattern Regular expression to compile.
   * @param flags Regular expression compilation flags.
   * @return @c true if @a pattern was successfully compiled, @c false if not.
   */
  bool build(std::string_view pattern, unsigned flags = 0);

  std::vector<Pattern> _patterns;
};
