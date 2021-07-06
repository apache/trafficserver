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

#define COMPILE_PARSE_RULES

#include "tscore/ParseRules.h"

const unsigned int parseRulesCType[256] = {0};
const char parseRulesCTypeToUpper[256]  = {0};
const char parseRulesCTypeToLower[256]  = {0};

unsigned int tparseRulesCType[256];
char tparseRulesCTypeToUpper[256];
char tparseRulesCTypeToLower[256];

#include <cstdio>
#include <cctype>
#include "tscore/ink_string.h"

static char *
uint_to_binary(unsigned int u)
{
  int i;
  static char buf[33];
  for (i = 0; i < 32; i++) {
    buf[i] = ((u & (1 << (31 - i))) ? '1' : '0');
  }
  buf[32] = '\0';
  return (buf);
}

int
main()
{
  int c;
  for (c = 0; c < 256; c++) {
    tparseRulesCType[c]        = 0;
    tparseRulesCTypeToLower[c] = ParseRules::ink_tolower(c);
    tparseRulesCTypeToUpper[c] = ParseRules::ink_toupper(c);

    if (ParseRules::is_char(c)) {
      tparseRulesCType[c] |= is_char_BIT;
    }
    if (ParseRules::is_upalpha(c)) {
      tparseRulesCType[c] |= is_upalpha_BIT;
    }
    if (ParseRules::is_loalpha(c)) {
      tparseRulesCType[c] |= is_loalpha_BIT;
    }
    if (ParseRules::is_alpha(c)) {
      tparseRulesCType[c] |= is_alpha_BIT;
    }
    if (ParseRules::is_digit(c)) {
      tparseRulesCType[c] |= is_digit_BIT;
    }
    if (ParseRules::is_ctl(c)) {
      tparseRulesCType[c] |= is_ctl_BIT;
    }
    if (ParseRules::is_ws(c)) {
      tparseRulesCType[c] |= is_ws_BIT;
    }
    if (ParseRules::is_hex(c)) {
      tparseRulesCType[c] |= is_hex_BIT;
    }
    char cc = c;
    if (ParseRules::is_pchar(&cc)) {
      tparseRulesCType[c] |= is_pchar_BIT;
    }
    if (ParseRules::is_extra(c)) {
      tparseRulesCType[c] |= is_extra_BIT;
    }
    if (ParseRules::is_safe(c)) {
      tparseRulesCType[c] |= is_safe_BIT;
    }
    if (ParseRules::is_unsafe(c)) {
      tparseRulesCType[c] |= is_unsafe_BIT;
    }
    if (ParseRules::is_national(c)) {
      tparseRulesCType[c] |= is_national_BIT;
    }
    if (ParseRules::is_reserved(c)) {
      tparseRulesCType[c] |= is_reserved_BIT;
    }
    if (ParseRules::is_unreserved(c)) {
      tparseRulesCType[c] |= is_unreserved_BIT;
    }
    if (ParseRules::is_punct(c)) {
      tparseRulesCType[c] |= is_punct_BIT;
    }
    if (ParseRules::is_end_of_url(c)) {
      tparseRulesCType[c] |= is_end_of_url_BIT;
    }
    if (ParseRules::is_tspecials(c)) {
      tparseRulesCType[c] |= is_tspecials_BIT;
    }
    if (ParseRules::is_spcr(c)) {
      tparseRulesCType[c] |= is_spcr_BIT;
    }
    if (ParseRules::is_splf(c)) {
      tparseRulesCType[c] |= is_splf_BIT;
    }
    if (ParseRules::is_wslfcr(c)) {
      tparseRulesCType[c] |= is_wslfcr_BIT;
    }
    if (ParseRules::is_eow(c)) {
      tparseRulesCType[c] |= is_eow_BIT;
    }
    if (ParseRules::is_token(c)) {
      tparseRulesCType[c] |= is_token_BIT;
    }
    if (ParseRules::is_uri(c)) {
      tparseRulesCType[c] |= is_uri_BIT;
    }
    if (ParseRules::is_sep(c)) {
      tparseRulesCType[c] |= is_sep_BIT;
    }
    if (ParseRules::is_empty(c)) {
      tparseRulesCType[c] |= is_empty_BIT;
    }
    if (ParseRules::is_alnum(c)) {
      tparseRulesCType[c] |= is_alnum_BIT;
    }
    if (ParseRules::is_space(c)) {
      tparseRulesCType[c] |= is_space_BIT;
    }
    if (ParseRules::is_control(c)) {
      tparseRulesCType[c] |= is_control_BIT;
    }
    if (ParseRules::is_mime_sep(c)) {
      tparseRulesCType[c] |= is_mime_sep_BIT;
    }
    if (ParseRules::is_http_field_name(c)) {
      tparseRulesCType[c] |= is_http_field_name_BIT;
    }
    if (ParseRules::is_http_field_value(c)) {
      tparseRulesCType[c] |= is_http_field_value_BIT;
    }
  }

  FILE *fp = fopen("ParseRulesCType", "w");
  for (c = 0; c < 256; c++) {
    fprintf(fp, "/* %3d (%c) */\t", c, (isprint(c) ? c : '?'));
    fprintf(fp, "0x%08X%c\t\t", tparseRulesCType[c], (c != 255 ? ',' : ' '));
    fprintf(fp, "/* [%s] */\n", uint_to_binary((tparseRulesCType[c])));
  }
  fclose(fp);
  fp = fopen("ParseRulesCTypeToUpper", "w");
  for (c = 0; c < 256; c++) {
    fprintf(fp, "%d%c\n", tparseRulesCTypeToUpper[c], c != 255 ? ',' : ' ');
  }
  fclose(fp);
  fp = fopen("ParseRulesCTypeToLower", "w");
  for (c = 0; c < 256; c++) {
    fprintf(fp, "%d%c\n", tparseRulesCTypeToLower[c], c != 255 ? ',' : ' ');
  }
  fclose(fp);

  return (0);
}
