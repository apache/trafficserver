/** @file

  Fuzzing HTTP compatibility parsing and Vary header matching.

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

#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/HttpCompat.h"
#include "tscore/Diags.h"
#include "tsutil/PostScript.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern int cmd_disable_pfreelist;

namespace
{
constexpr size_t  MAX_INPUT_LENGTH          = 16 * 1024;
constexpr int     OUTPUT_BUF_SIZE           = 256;
constexpr uint8_t FLAG_COPY_COMMA_LIST      = 1u << 0;
constexpr uint8_t FLAG_COPY_SEMICOLON_LIST  = 1u << 1;
constexpr uint8_t FLAG_COPY_TOKEN_LIST      = 1u << 2;
constexpr uint8_t FLAG_SEMICOLON_SEPARATOR  = 1u << 3;
constexpr uint8_t FLAG_IGNORE_WILDCARDS     = 1u << 4;
constexpr uint8_t FLAG_TRIM_TOKEN_QUOTES    = 1u << 5;
constexpr uint8_t FLAG_FIRST_FIELD_MISSING  = 1u << 6;
constexpr uint8_t FLAG_SECOND_FIELD_MISSING = 1u << 7;

void
exercise_lists(const std::string &value, const std::string &candidate, uint8_t flags, int output_length)
{
  StrList                           comma_list((flags & FLAG_COPY_COMMA_LIST) != 0);
  StrList                           semicolon_list((flags & FLAG_COPY_SEMICOLON_LIST) != 0);
  StrList                           token_list((flags & FLAG_COPY_TOKEN_LIST) != 0);
  std::array<char, OUTPUT_BUF_SIZE> output{};
  std::array<char, OUTPUT_BUF_SIZE> type{};
  std::array<char, OUTPUT_BUF_SIZE> subtype{};
  int                               matching_length  = 0;
  int                               matching_index   = 0;
  int                               value_length     = static_cast<int>(value.size());
  int                               candidate_length = static_cast<int>(candidate.size());
  char                              separator        = (flags & FLAG_SEMICOLON_SEPARATOR) != 0 ? ';' : ',';
  bool                              ignore_wildcards = (flags & FLAG_IGNORE_WILDCARDS) != 0;

  HttpCompat::parse_comma_list(&comma_list, value.c_str(), value_length);
  HttpCompat::parse_semicolon_list(&semicolon_list, value.c_str(), value_length);
  HttpCompat::parse_tok_list(&token_list, (flags & FLAG_TRIM_TOKEN_QUOTES) != 0, value.c_str(), value_length, separator);
  HttpCompat::lookup_param_in_strlist(&semicolon_list, "q", output.data(), output_length);
  HttpCompat::lookup_param_in_strlist(&semicolon_list, "charset", output.data(), output_length);
  HttpCompat::lookup_param_in_semicolon_string(value.c_str(), value_length, candidate.c_str(), output.data(), output_length);
  HttpCompat::find_Q_param_in_strlist(&semicolon_list);
  HttpCompat::match_accept_language(candidate.c_str(), candidate_length, &comma_list, &matching_length, &matching_index,
                                    ignore_wildcards);
  HttpCompat::match_accept_charset(candidate.c_str(), candidate_length, &comma_list, &matching_index, ignore_wildcards);
  HttpCompat::parse_mime_type(value.c_str(), type.data(), subtype.data(), output_length, output_length);
  HttpCompat::parse_mime_type_with_len(value.c_str(), value_length, type.data(), subtype.data(), output_length, output_length);
}

MIMEField *
add_accept_language_field(HTTPHdr &header, std::string_view value)
{
  MIMEField *field = header.field_create("Accept-Language");

  field->value_set(header.m_heap, header.m_mime, value);
  header.field_attach(field);
  return field;
}

void
exercise_vary(const std::string &first, const std::string &second, uint8_t flags)
{
  HTTPHdr        first_header;
  HTTPHdr        second_header;
  ts::PostScript cleanup([&]() {
    second_header.destroy();
    first_header.destroy();
  });

  first_header.create(HTTPType::REQUEST);
  second_header.create(HTTPType::REQUEST);
  MIMEField *first_field  = (flags & FLAG_FIRST_FIELD_MISSING) != 0 ? nullptr : add_accept_language_field(first_header, first);
  MIMEField *second_field = (flags & FLAG_SECOND_FIELD_MISSING) != 0 ? nullptr : add_accept_language_field(second_header, second);

  // Missing selecting headers are supported: both absent match, only one absent does not.
  HttpCompat::do_vary_header_values_match(first_field, second_field);
  if (first_field && second_field) {
    HttpCompat::do_vary_header_values_match(first_field, first_field);
    // Match the swapped values attached to the opposite headers.
    MIMEField *swapped_first  = add_accept_language_field(first_header, second);
    MIMEField *swapped_second = add_accept_language_field(second_header, first);
    HttpCompat::do_vary_header_values_match(swapped_first, swapped_second);
  }
}
} // namespace

extern "C" int
LLVMFuzzerInitialize(int *, char ***)
{
  cmd_disable_pfreelist = true;
  DiagsPtr::set(new Diags("fuzzing", "", "", nullptr));
  http_init();
  return 0;
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  if (size < 3 || size > MAX_INPUT_LENGTH) {
    return 0;
  }

  uint8_t     flags         = data[0];
  int         output_length = std::min<int>(static_cast<int>(data[1]) + 1, OUTPUT_BUF_SIZE - 1);
  size_t      split         = static_cast<size_t>(data[2]) * (size - 3) / 255;
  const char *payload       = reinterpret_cast<const char *>(data + 3);
  std::string first(payload, split);
  std::string second(payload + split, size - 3 - split);

  exercise_lists(first, second, flags, output_length);
  exercise_lists(second, first, flags, output_length);
  exercise_vary(first, second, flags);
  return 0;
}
