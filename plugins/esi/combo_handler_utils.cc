/** @file

    Util functions for combo handler.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one
    or more contributor license agreements.  See the NOTICE file
    distributed with this work for additional information
    regarding copyright ownership.  The ASF licenses this file
    to you under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the
    License.  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "combo_handler_utils.h"

#include <limits>

namespace
{
constexpr std::string_view MaxAge{"max-age"};
constexpr std::string_view Private{"private"};
constexpr std::string_view Immutable{"immutable"};

// ASCII-only tolower. HTTP tokens are ASCII; std::tolower from <cctype>
// is locale-dependent (notably tr_TR maps 'I' away from 'i') and would
// otherwise let the C locale at plugin init silently change how
// directives like "Immutable" and "PRIVATE" match.
constexpr char
ascii_tolower(char c)
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

// ASCII-only digit test. std::isdigit from <cctype> is locale-dependent and
// can accept non-ASCII digits when the process locale is not C; HTTP directive
// syntax (e.g. max-age) is ASCII, so match '0'..'9' explicitly.
constexpr bool
ascii_isdigit(char c)
{
  return c >= '0' && c <= '9';
}

bool
starts_with_ignore_case(std::string_view value, std::string_view token)
{
  if (value.size() < token.size()) {
    return false;
  }

  for (size_t i = 0; i < token.size(); ++i) {
    if (ascii_tolower(value[i]) != ascii_tolower(token[i])) {
      return false;
    }
  }
  return true;
}

bool
is_lws(char c)
{
  return c == ' ' || c == '\t';
}

// True if `value` begins with `token` (case-insensitive) AND the token is
// terminated by a directive boundary, so a longer word that merely starts
// with the token is not misclassified. The boundary is end-of-string or
// linear whitespace; when `allow_equals` is set (e.g. private="field-name"),
// an '=' introducing a value also terminates the token. This keeps values
// like "privatee" or "immutableX" from matching "private"/"immutable".
bool
matches_directive(std::string_view value, std::string_view token, bool allow_equals)
{
  if (!starts_with_ignore_case(value, token)) {
    return false;
  }
  if (value.size() == token.size()) {
    return true;
  }
  char const next = value[token.size()];
  return is_lws(next) || (allow_equals && next == '=');
}
} // namespace

namespace combo_handler
{
CacheControlValue
parse_cache_control_value(std::string_view value)
{
  CacheControlValue parsed;

  if (starts_with_ignore_case(value, MaxAge)) {
    value.remove_prefix(MaxAge.size());
    while (!value.empty() && is_lws(value.front())) {
      value.remove_prefix(1);
    }
    if (!value.empty() && value.front() == '=') {
      value.remove_prefix(1);
      while (!value.empty() && is_lws(value.front())) {
        value.remove_prefix(1);
      }
      unsigned max_age = 0;
      bool overflow    = false;
      bool any_digit   = false;
      while (!value.empty() && ascii_isdigit(value.front())) {
        unsigned const digit = value.front() - '0';
        if (overflow || max_age > (std::numeric_limits<unsigned>::max() - digit) / 10) {
          overflow = true;
        } else {
          max_age = (max_age * 10) + digit;
        }
        any_digit = true;
        value.remove_prefix(1);
      }
      // Require at least one digit so that "max-age=" / "max-age=foo"
      // don't masquerade as max-age=0. Clamp overflow to UINT_MAX so a
      // huge upstream value can't be misread as the smallest possible
      // max-age when callers honor zero.
      if (any_digit) {
        parsed.has_max_age = true;
        parsed.max_age     = overflow ? std::numeric_limits<unsigned>::max() : max_age;
      }
    }
  } else if (matches_directive(value, Private, /* allow_equals */ true)) {
    parsed.is_private = true;
  } else if (matches_directive(value, Immutable, /* allow_equals */ false)) {
    parsed.is_immutable = true;
  }

  return parsed;
}
} // namespace combo_handler
