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

#ifndef __HTTP_COMPAT_H__
#define __HTTP_COMPAT_H__

#include "ts/ink_string++.h"
#include "MIME.h"
#include "ts/RawHashTable.h"
#include "ts/Diags.h"

struct HttpBodySetRawData {
  unsigned int magic;
  char *set_name;
  char *content_language;
  char *content_charset;
  RawHashTable *table_of_pages;
};

class HttpCompat
{
public:
  static void parse_tok_list(StrList *list, int trim_quotes, const char *comma_list_str, int comma_list_len, char tok);

  static void parse_tok_list(StrList *list, int trim_quotes, const char *comma_list_str, char tok);

  static bool lookup_param_in_strlist(StrList *param_list, const char *param_name, char *param_val, int param_val_length);

  static bool lookup_param_in_semicolon_string(const char *semicolon_string, int semicolon_string_len, const char *param_name,
                                               char *param_val, int param_val_length);

  static void parse_mime_type(const char *mime_string, char *type, char *subtype, int type_len, int subtype_len);

  static void parse_mime_type_with_len(const char *mime_string, int mime_string_len, char *type, char *subtype, int type_len,
                                       int subtype_len);

  static bool do_header_values_rfc2068_14_43_match(MIMEField *hv1, MIMEField *hv2);

  static float find_Q_param_in_strlist(StrList *strlist);

  static float match_accept_language(const char *lang_str, int lang_len, StrList *acpt_lang_list, int *matching_length,
                                     int *matching_index, bool ignore_wildcards = false);

  static float match_accept_charset(const char *charset_str, int charset_len, StrList *acpt_charset_list, int *matching_index,
                                    bool ignore_wildcards = false);

  static const char *determine_set_by_language(RawHashTable *table_of_sets, StrList *acpt_language_list, StrList *acpt_charset_list,
                                               float *Q_best_ptr, int *La_best_ptr, int *Lc_best_ptr, int *I_best_ptr);

  static void
  parse_comma_list(StrList *list, const char *comma_list_str)
  {
    parse_tok_list(list, 1, comma_list_str, ',');
  }

  static void
  parse_comma_list(StrList *list, const char *comma_list_str, int comma_list_len)
  {
    parse_tok_list(list, 1, comma_list_str, comma_list_len, ',');
  }

  static void
  parse_semicolon_list(StrList *list, const char *comma_list_str)
  {
    parse_tok_list(list, 1, comma_list_str, ';');
  }

  static void
  parse_semicolon_list(StrList *list, const char *comma_list_str, int comma_list_len)
  {
    parse_tok_list(list, 1, comma_list_str, comma_list_len, ';');
  }
};

#endif /* __HTTP_COMPAT_H__ */
