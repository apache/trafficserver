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

#include <cstring>

#include "tscore/ink_defs.h"
#include "tscore/ink_apidefs.h"
#include "tscore/ink_platform.h"

typedef unsigned int CTypeResult;

// Set this to 0 to disable SI
// decimal multipliers
#define USE_SI_MULTILIERS 1

#define is_char_BIT (1 << 0)
#define is_upalpha_BIT (1 << 1)
#define is_loalpha_BIT (1 << 2)
#define is_alpha_BIT (1 << 3)
#define is_digit_BIT (1 << 4)
#define is_ctl_BIT (1 << 5)
#define is_ws_BIT (1 << 6)
#define is_hex_BIT (1 << 7)
#define is_pchar_BIT (1 << 8)
#define is_extra_BIT (1 << 9)
#define is_safe_BIT (1 << 10)
#define is_unsafe_BIT (1 << 11)
#define is_national_BIT (1 << 12)
#define is_reserved_BIT (1 << 13)
#define is_unreserved_BIT (1 << 14)
#define is_punct_BIT (1 << 15)
#define is_end_of_url_BIT (1 << 16)
#define is_tspecials_BIT (1 << 17)
#define is_spcr_BIT (1 << 18)
#define is_splf_BIT (1 << 19)
#define is_wslfcr_BIT (1 << 20)
#define is_eow_BIT (1 << 21)
#define is_token_BIT (1 << 22)
#define is_uri_BIT (1 << 23)
#define is_sep_BIT (1 << 24)
#define is_empty_BIT (1 << 25)
#define is_alnum_BIT (1 << 26)
#define is_space_BIT (1 << 27)
#define is_control_BIT (1 << 28)
#define is_mime_sep_BIT (1 << 29)
#define is_http_field_name_BIT (1 << 30)
/* shut up the DEC compiler */
#define is_http_field_value_BIT (((CTypeResult)1) << 31)

extern ink_undoc_liapi const CTypeResult parseRulesCType[];
inkcoreapi extern const char parseRulesCTypeToUpper[];
inkcoreapi extern const char parseRulesCTypeToLower[];

class ParseRules
{
public:
  ParseRules();

  ////////////////////////////
  // whitespace definitions //
  ////////////////////////////

  enum {
    CHAR_SP = 32, /* space           */
    CHAR_HT = 9,  /* horizontal tab  */
    CHAR_LF = 10, /* line feed       */
    CHAR_VT = 11, /* vertical tab    */
    CHAR_NP = 12, /* new page        */
    CHAR_CR = 13  /* carriage return */
  };

  /////////////////////
  // character tests //
  /////////////////////

  static CTypeResult is_type(char c, uint32_t bit);

  static CTypeResult is_char(char c);             // ASCII 0-127
  static CTypeResult is_upalpha(char c);          // A-Z
  static CTypeResult is_loalpha(char c);          // a-z
  static CTypeResult is_alpha(char c);            // A-Z,a-z
  static CTypeResult is_digit(char c);            // 0-9
  static CTypeResult is_ctl(char c);              // ASCII 0-31,127 (includes ws)
  static CTypeResult is_hex(char c);              // 0-9,A-F,a-f
  static CTypeResult is_ws(char c);               // SP,HT
  static CTypeResult is_cr(char c);               // CR
  static CTypeResult is_lf(char c);               // LF
  static CTypeResult is_spcr(char c);             // SP,CR
  static CTypeResult is_splf(char c);             // SP,LF
  static CTypeResult is_wslfcr(char c);           // SP,HT,LF,CR
  static CTypeResult is_tspecials(char c);        // HTTP chars that need quoting
  static CTypeResult is_token(char c);            // token (not CTL or specials)
  static CTypeResult is_extra(char c);            // !,*,QUOT,(,),COMMA
  static CTypeResult is_safe(char c);             // [$-_.+]
  static CTypeResult is_unsafe(char c);           // SP,DBLQUOT,#,%,<,>
  static CTypeResult is_national(char c);         // {,},|,BACKSLASH,^,~,[,],`
  static CTypeResult is_reserved(char c);         // :,/,?,:,@,&,=
  static CTypeResult is_unreserved(char c);       // alpha,digit,safe,extra,nat.
  static CTypeResult is_punct(char c);            // !"#$%&'()*+,-./:;<>=?@_{}|~
  static CTypeResult is_end_of_url(char c);       // NUL,CR,SP
  static CTypeResult is_eow(char c);              // NUL,CR,LF
  static CTypeResult is_uri(char c);              // A-Z,a-z,0-9 :/?#[]@!$&'()*+,;=-._~%
  static CTypeResult is_sep(char c);              // nullptr,COMMA,':','!',wslfcr
  static CTypeResult is_empty(char c);            // wslfcr,#
  static CTypeResult is_alnum(char c);            // 0-9,A-Z,a-z
  static CTypeResult is_space(char c);            // ' ' HT,VT,NP,CR,LF
  static CTypeResult is_control(char c);          // 0-31 127
  static CTypeResult is_mime_sep(char c);         // ()<>,;\"/[]?{} \t
  static CTypeResult is_http_field_name(char c);  // not : or mime_sep except for @
  static CTypeResult is_http_field_value(char c); // not CR, LF, comma, or "

  //////////////////
  // string tests //
  //////////////////

  static CTypeResult is_escape(const char *seq); // %<hex><hex>
  static CTypeResult is_uchar(const char *seq);  // starts unresrvd or is escape
  static CTypeResult is_pchar(const char *seq);  // uchar,:,@,&,=,+ (see code)

  ///////////////////
  // unimplemented //
  ///////////////////

  // static CTypeResult   is_comment(const char * str);
  // static CTypeResult   is_ctext(const char * str);

  ////////////////
  // operations //
  ////////////////

  static CTypeResult strncasecmp_eow(const char *s1, const char *s2, int n);
  static const char *strcasestr(const char *s1, const char *s2);
  static int strlen_eow(const char *s);
  static const char *strstr_eow(const char *s1, const char *s2);

  static char ink_toupper(char c);
  static char ink_tolower(char c);
  static const char *memchr(const char *s, char c, int max_length);
  static const char *strchr(const char *s, char c);

  // noncopyable
  ParseRules(const ParseRules &) = delete;
  ParseRules &operator=(const ParseRules &) = delete;
};

/* * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * inline functions definitions
 * * * * * * * * * * * * * * * * * * * * * * * * * * * */

inline CTypeResult
ParseRules::is_type(char c, uint32_t bitmask)
{
  return (parseRulesCType[(unsigned char)c] & bitmask);
}

inline CTypeResult
ParseRules::is_char(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_char_BIT);
#else
  return ((c & 0x80) == 0);
#endif
}

inline CTypeResult
ParseRules::is_upalpha(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_upalpha_BIT);
#else
  return (c >= 'A' && c <= 'Z');
#endif
}

inline CTypeResult
ParseRules::is_loalpha(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_loalpha_BIT);
#else
  return (c >= 'a' && c <= 'z');
#endif
}

inline CTypeResult
ParseRules::is_alpha(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_alpha_BIT);
#else
  return (is_upalpha(c) || is_loalpha(c));
#endif
}

inline CTypeResult
ParseRules::is_digit(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_digit_BIT);
#else
  return (c >= '0' && c <= '9');
#endif
}

inline CTypeResult
ParseRules::is_alnum(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_alnum_BIT);
#else
  return (is_alpha(c) || is_digit(c));
#endif
}

inline CTypeResult
ParseRules::is_ctl(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_ctl_BIT);
#else
  return ((!(c & 0x80) && c <= 31) || c == 127);
#endif
}

inline CTypeResult
ParseRules::is_ws(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_ws_BIT);
#else
  return (c == CHAR_SP || c == CHAR_HT);
#endif
}

inline CTypeResult
ParseRules::is_hex(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_hex_BIT);
#else
  return ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'));
#endif
}

inline CTypeResult
ParseRules::is_cr(char c)
{
  return (c == CHAR_CR);
}

inline CTypeResult
ParseRules::is_lf(char c)
{
  return (c == CHAR_LF);
}

inline CTypeResult
ParseRules::is_splf(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_splf_BIT);
#else
  return (c == CHAR_SP || c == CHAR_LF);
#endif
}

inline CTypeResult
ParseRules::is_spcr(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_spcr_BIT);
#else
  return (c == CHAR_SP || c == CHAR_CR);
#endif
}

inline CTypeResult
ParseRules::is_wslfcr(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_wslfcr_BIT);
#else
  return ParseRules::is_ws(c) || ParseRules::is_splf(c) || ParseRules::is_spcr(c);
#endif
}

inline CTypeResult
ParseRules::is_extra(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_extra_BIT);
#else
  switch (c) {
  case '!':
  case '*':
  case '\'':
  case '(':
  case ')':
  case ',':
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_safe(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_safe_BIT);
#else
  return (c == '$' || c == '-' || c == '_' || c == '.' || c == '+');
#endif
}

inline CTypeResult
ParseRules::is_unsafe(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_unsafe_BIT);
#else
  if (is_ctl(c))
    return (true);

  switch (c) {
  case ' ':
  case '\"':
  case '#':
  case '%':
  case '<':
  case '>':
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_reserved(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_reserved_BIT);
#else
  switch (c) {
  case ';':
  case '/':
  case '?':
  case ':':
  case '@':
  case '&':
  case '=':
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_national(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_national_BIT);
#else
  switch (c) {
  case '{':
  case '}':
  case '|':
  case '\\':
  case '^':
  case '~':
  case '[':
  case ']':
  case '`':
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_unreserved(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_unreserved_BIT);
#else
  return (is_alpha(c) || is_digit(c) || is_safe(c) || is_extra(c) || is_national(c));
#endif
}

inline CTypeResult
ParseRules::is_punct(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_punct_BIT);
#else
  switch (c) {
  case '!':
  case '"':
  case '#':
  case '%':
  case '&':
  case '\'':
  case '(':
  case ')':
  case '*':
  case '+':
  case ',':
  case '-':
  case '.':
  case '/':
  case ':':
  case ';':
  case '<':
  case '=':
  case '>':
  case '?':
  case '@':
  case '[':
  case '\\':
  case ']':
  case '^':
  case '_':
  case '`':
  case '{':
  case '|':
  case '}':
  case '~':
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_end_of_url(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_end_of_url_BIT);
#else
  return (c == '\0' || c == '\n' || c == ' ' || ParseRules::is_ctl(c));
#endif
}

inline CTypeResult
ParseRules::is_escape(const char *seq)
{
  return (seq[0] == '%' && is_hex(seq[1]) && is_hex(seq[2]));
}

inline CTypeResult
ParseRules::is_uchar(const char *seq)
{
  return (is_unreserved(seq[0]) || is_escape(seq));
}

//
// have to cheat on this one
//
inline CTypeResult
ParseRules::is_pchar(const char *seq)
{
#ifndef COMPILE_PARSE_RULES
  if (*seq != '%')
    return (parseRulesCType[(uint8_t)*seq] & is_pchar_BIT);
  else
    return is_hex(seq[1]) && is_hex(seq[2]);
#else
  if (is_unreserved(*seq))
    return (true);

  switch (seq[0]) {
  case ':':
  case '@':
  case '&':
  case '=':
  case '+':
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_tspecials(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_tspecials_BIT);
#else
  switch (c) {
  case '(':
  case ')':
  case '<':
  case '>':
  case '@':
  case ',':
  case ';':
  case ':':
  case '\\':
  case '"':
  case '/':
  case '[':
  case ']':
  case '?':
  case '=':
  case '{':
  case '}':
  case CHAR_SP:
  case CHAR_HT:
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_token(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_token_BIT);
#else
  return (is_char(c) && !(is_ctl(c) || is_tspecials(c)));
#endif
}

inline char
ParseRules::ink_toupper(char c)
{
#ifndef COMPILE_PARSE_RULES
  return parseRulesCTypeToUpper[(unsigned char)c];
#else
  int up_case            = c;
  const int up_case_diff = 'a' - 'A';

  if (c >= 'a' && c <= 'z') {
    up_case = c - up_case_diff;
  }
  return (up_case);
#endif
}

inline char
ParseRules::ink_tolower(char c)
{
#ifndef COMPILE_PARSE_RULES
  return parseRulesCTypeToLower[(unsigned char)c];
#else
  int lo_case            = c;
  const int lo_case_diff = 'a' - 'A';

  if (c >= 'A' && c <= 'Z') {
    lo_case = c + lo_case_diff;
  }
  return (lo_case);
#endif
}

inline CTypeResult
ParseRules::is_eow(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_eow_BIT);
#else
  return (c == '\0' || c == '\r' || c == '\n');
#endif
}

inline CTypeResult
ParseRules::is_uri(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_uri_BIT);
#else
  if (is_alnum(c))
    return (true);

  switch (c) {
  case ':':
  case '/':
  case '?':
  case '#':
  case '[':
  case ']':
  case '@':
  case '!':
  case '$':
  case '&':
  case '\'':
  case '(':
  case ')':
  case '*':
  case '+':
  case ',':
  case ';':
  case '=':
  case '-':
  case '.':
  case '_':
  case '~':
  case '%':
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_sep(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_sep_BIT);
#else
  return (!c || c == ',' || c == ':' || c == '!' || is_wslfcr(c));
#endif
}

inline CTypeResult
ParseRules::is_empty(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_empty_BIT);
#else
  return (c == '#' || is_wslfcr(c));
#endif
}

inline CTypeResult
ParseRules::is_space(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_space_BIT);
#else
  switch (c) {
  case CHAR_SP:
  case CHAR_HT:
  case CHAR_LF:
  case CHAR_VT:
  case CHAR_NP:
  case CHAR_CR:
    return (true);
  }
  return (false);
#endif
}

inline CTypeResult
ParseRules::is_control(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_control_BIT);
#else
  if (((unsigned char)c) < 32 || ((unsigned char)c) == 127)
    return true;
  return false;
#endif
}

inline CTypeResult
ParseRules::is_mime_sep(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_mime_sep_BIT);
#else
  if ((c == '(') || (c == ')') || (c == '<') || (c == '>') || (c == '@') || (c == ',') || (c == ';') || (c == '\\') ||
      (c == '\"') || (c == '/') || (c == '[') || (c == ']') || (c == '?') || (c == '{') || (c == '}') || (c == ' ') || (c == '\t'))
    return true;
  return false;
#endif
}

inline CTypeResult
ParseRules::is_http_field_name(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (parseRulesCType[(unsigned char)c] & is_http_field_name_BIT);
#else
  if ((c == ':') || (is_mime_sep(c) && (c != '@')))
    return false;
  return true;
#endif
}

inline CTypeResult
ParseRules::is_http_field_value(char c)
{
#ifndef COMPILE_PARSE_RULES
  return (CTypeResult)(parseRulesCType[(unsigned char)c] & is_http_field_value_BIT);
#else
  switch (c) {
  case CHAR_CR:
  case CHAR_LF:
  case '\"':
  case ',':
    return false;
  }
  return true;
#endif
}

//////////////////////////////////////////////////////////////////////////////
//
//      inline CTypeResult ParseRules::strncasecmp_eol(s1, s2, count)
//
//      This wacky little function compares if two strings <s1> and <s2> match
//      (case-insensitively) up to <count> characters long, stopping not only
//      at the end of string ('\0'), but also at end of line (CR or LF).
//
//////////////////////////////////////////////////////////////////////////////

inline CTypeResult
ParseRules::strncasecmp_eow(const char *s1, const char *s2, int count)
{
  for (int i = 0; i < count; i++) {
    const char &a = s1[i];
    const char &b = s2[i];

    ///////////////////////////////////////////////////////////////
    // if they are different; only match if both are terminators //
    ///////////////////////////////////////////////////////////////
    if (ink_tolower(a) != ink_tolower(b))
      return (is_eow(a) && is_eow(b));
  }
  return (true);
}

//////////////////////////////////////////////////////////////////////////////
//
//  strlen_eow()
//
//  return the length of a string
//////////////////////////////////////////////////////////////////////////////
inline int
ParseRules::strlen_eow(const char *s)
{
  for (int i = 0; true; i++) {
    if (is_eow(s[i]))
      return (i);
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//  strstr_eow()
//
//  This function is the same as strstr(), except that it accepts strings
//  that are terminated with '\r', '\n' or null.
//  It returns a pointer to the first occurance of s2 within s1 (or null).
//////////////////////////////////////////////////////////////////////////////
inline const char *
ParseRules::strstr_eow(const char *s1, const char *s2)
{
  int i1;

  int s2_len = strlen_eow(s2);

  for (i1 = 0; !is_eow(s1[i1]); i1++)
    if (ink_tolower(s1[i1]) == ink_tolower(s2[0]))
      if (strncasecmp_eow(&s1[i1], &s2[0], s2_len))
        return (&s1[i1]);

  return (nullptr);
}

inline const char *
ParseRules::strcasestr(const char *s1, const char *s2)
{
  int i1;

  size_t s2_len = strlen(s2);

  for (i1 = 0; s1[i1] != '\0'; i1++)
    if (ink_tolower(s1[i1]) == ink_tolower(s2[0]))
      if (strncasecmp_eow(&s1[i1], &s2[0], (int)s2_len))
        return (&s1[i1]);

  return (nullptr);
}

inline const char *
ParseRules::memchr(const char *s, char c, int max_length)
{
  for (int i = 0; i < max_length; i++)
    if (s[i] == c)
      return (&s[i]);
  return (nullptr);
}

inline const char *
ParseRules::strchr(const char *s, char c)
{
  for (int i = 0; s[i] != '\0'; i++)
    if (s[i] == c)
      return (&s[i]);
  return (nullptr);
}

static inline int
ink_get_hex(char c)
{
  if (ParseRules::is_digit(c))
    return (int)(c - '0');
  c = ParseRules::ink_tolower(c);
  return (int)((c - 'a') + 10);
}

int64_t ink_atoi64(const char *);
uint64_t ink_atoui64(const char *);
int64_t ink_atoi64(const char *, int);

static inline int
ink_atoi(const char *str)
{
  int64_t val = ink_atoi64(str);

  if (val > INT_MAX)
    return INT_MAX;
  else if (val < INT_MIN)
    return INT_MIN;
  else
    return static_cast<int>(val);
}

static inline int
ink_atoi(const char *str, int len)
{
  int64_t val = ink_atoi64(str, len);

  if (val > INT_MAX)
    return INT_MAX;
  else if (val < INT_MIN)
    return INT_MIN;
  else
    return static_cast<int>(val);
}

static inline unsigned int
ink_atoui(const char *str)
{
  uint64_t val = ink_atoui64(str);

  if (val > INT_MAX)
    return INT_MAX;
  else
    return static_cast<int>(val);
}
