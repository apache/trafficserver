/** @file

  Helpers for parsing log field fallback expressions.

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

#include "LogFieldFallback.h"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include "swoc/TextView.h"

namespace
{
constexpr std::string_view FIELD_FALLBACK_SEPARATOR{"??"};

/** Find the next fallback separator outside of quoted default literals.
 *
 * This keeps the chain tokenizer from splitting on separator text that is
 * part of a quoted default value.
 *
 * @param[in] text Candidate fallback expression text.
 * @return Pointer to the first separator occurrence, or @c nullptr if none.
 */
constexpr char const *
find_field_fallback_separator(std::string_view text)
{
  char quote  = '\0';
  bool escape = false;

  // The following logic assumes a 2 character separator. If that changes,
  // adjust the logic accordingly.
  static_assert(FIELD_FALLBACK_SEPARATOR.size() == 2, "FIELD_FALLBACK_SEPARATOR must be exactly two characters long");

  for (auto const *spot = text.data(), *limit = text.data() + text.size(); spot < limit; ++spot) {
    char c = *spot;

    if (quote != '\0') {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == quote) {
        quote = '\0';
      }
      continue;
    }

    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }

    if (*spot == FIELD_FALLBACK_SEPARATOR[0] && (spot + 1) < limit && *(spot + 1) == FIELD_FALLBACK_SEPARATOR[1]) {
      return spot;
    }
  }

  return nullptr;
}

constexpr bool
test_find_field_fallback_separator()
{
  static_assert(find_field_fallback_separator("") == nullptr);
  constexpr char const *text1 = "{field}??default";
  static_assert(find_field_fallback_separator(text1) == text1 + 7);
  constexpr char const *text2 = "??default";
  static_assert(find_field_fallback_separator(text2) == text2);
  constexpr char const *text3 = "{field}??def??ault";
  static_assert(find_field_fallback_separator(text3) == text3 + 7);
  constexpr char const *text4 = "{field}??\"def??ault\"";
  static_assert(find_field_fallback_separator(text4) == text4 + 7);
  return true;
}

static_assert(test_find_field_fallback_separator(), "find_field_fallback_separator failed its tests");

void
set_parse_error(std::string &error, std::string_view symbol, std::string_view detail)
{
  error.assign("Invalid log field fallback specification: ");
  error.append(detail.data(), detail.size());
  error.append(" in ");
  error.append(symbol.data(), symbol.size());
}

bool
parse_header_fallback_candidate(swoc::TextView term, LogField::HeaderField &field, std::string_view original_symbol,
                                std::string &error)
{
  term.trim_if(isspace);
  std::string term_text{term};

  if (term_text.empty()) {
    set_parse_error(error, original_symbol, "empty candidate");
    return false;
  }

  if (term_text.front() != '{') {
    set_parse_error(error, original_symbol, term_text + " is not a header field candidate");
    return false;
  }

  term.remove_prefix(1);
  size_t name_end = term.find('}');
  if (name_end == swoc::TextView::npos) {
    set_parse_error(error, original_symbol, "no trailing '}'");
    return false;
  }

  swoc::TextView field_name = term.substr(0, name_end);
  if (field_name.empty()) {
    set_parse_error(error, original_symbol, "empty header name");
    return false;
  }

  swoc::TextView container_text = term.substr(name_end + 1);
  container_text.trim_if(isspace);
  std::string container_spec{container_text};
  LogSlice    slice(container_spec.data());
  auto        container = LogField::valid_container_name(container_spec.data());

  if (!LogField::isHeaderContainer(container)) {
    set_parse_error(error, original_symbol, container_spec + " is not a supported header container");
    return false;
  }

  field.name.assign(field_name.data(), field_name.size());
  field.container = container;
  field.slice     = slice;

  return true;
}

bool
parse_field_fallback_symbol(swoc::TextView term, std::string &field_symbol, std::string_view original_symbol, std::string &error)
{
  term.trim_if(isspace);
  if (term.empty()) {
    set_parse_error(error, original_symbol, "empty candidate");
    return false;
  }

  if (term.find('(') != swoc::TextView::npos || term.find(')') != swoc::TextView::npos) {
    set_parse_error(error, original_symbol, "aggregate expressions are not supported in fallback chains");
    return false;
  }

  if (term.find('{') != swoc::TextView::npos || term.find('}') != swoc::TextView::npos) {
    set_parse_error(error, original_symbol, std::string{term} + " is not a supported fallback term");
    return false;
  }

  field_symbol.assign(term.data(), term.size());
  return true;
}

bool
parse_field_fallback_default(swoc::TextView term, std::string &default_value, std::string_view original_symbol, std::string &error)
{
  term.trim_if(isspace);
  if (term.empty()) {
    set_parse_error(error, original_symbol, "empty default");
    return false;
  }

  char quote = term.front();
  if (quote != '\'' && quote != '"') {
    set_parse_error(error, original_symbol, std::string{term} + " is not a quoted default literal");
    return false;
  }

  default_value.clear();
  bool escaped = false;

  for (auto const *spot = term.data() + 1, *limit = term.data_end(); spot < limit; ++spot) {
    char c = *spot;

    if (escaped) {
      if (c == quote || c == '\\') {
        default_value.push_back(c);
      } else {
        default_value.push_back('\\');
        default_value.push_back(c);
      }
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }

    if (c == quote) {
      if (spot + 1 != limit) {
        set_parse_error(error, original_symbol, "trailing characters after default literal");
        return false;
      }
      return true;
    }

    default_value.push_back(c);
  }

  set_parse_error(error, original_symbol, "unterminated default literal");
  return false;
}
} // namespace

namespace LogFieldFallback
{
bool
has_fallback(std::string_view symbol)
{
  return find_field_fallback_separator(symbol) != nullptr;
}

std::optional<ParseResult>
parse(std::string_view symbol, std::string &error)
{
  error.clear();

  ParseResult    result;
  swoc::TextView remaining{symbol};

  while (true) {
    swoc::TextView term;
    bool           has_more_terms = false;

    if (auto const *separator = find_field_fallback_separator(remaining); separator != nullptr) {
      term = swoc::TextView{remaining.data(), static_cast<size_t>(separator - remaining.data())};
      remaining.remove_prefix((separator - remaining.data()) + FIELD_FALLBACK_SEPARATOR.size());
      has_more_terms = true;
    } else {
      term      = remaining;
      remaining = swoc::TextView{};
    }

    term.trim_if(isspace);
    if (term.empty()) {
      set_parse_error(error, symbol, "empty candidate");
      return std::nullopt;
    }

    if (term.starts_with('"') || term.starts_with('\'')) {
      if (has_more_terms) {
        set_parse_error(error, symbol, "default literal must be the final term");
        return std::nullopt;
      }

      std::string fallback_default;
      if (!parse_field_fallback_default(term, fallback_default, symbol, error)) {
        return std::nullopt;
      }
      result.fallback_default = std::move(fallback_default);
      break;
    }

    if (term.starts_with('{')) {
      LogField::HeaderField field;
      if (!parse_header_fallback_candidate(term, field, symbol, error)) {
        return std::nullopt;
      }

      result.header_fields.push_back(std::move(field));

      if (!has_more_terms) {
        break;
      }
    } else {
      if (has_more_terms) {
        set_parse_error(error, symbol, "plain field symbols must be the final term");
        return std::nullopt;
      }

      std::string fallback_symbol;
      if (!parse_field_fallback_symbol(term, fallback_symbol, symbol, error)) {
        return std::nullopt;
      }

      result.fallback_symbol = std::move(fallback_symbol);
      break;
    }
  }

  if (result.header_fields.empty()) {
    set_parse_error(error, symbol, "must start with a header field candidate");
    return std::nullopt;
  }

  return result;
}
} // namespace LogFieldFallback
