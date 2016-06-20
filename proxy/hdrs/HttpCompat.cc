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

#include "ts/ink_platform.h"
#include "HttpCompat.h"
#include "HdrUtils.h" /* MAGIC_EDITING_TAG */

//////////////////////////////////////////////////////////////////////////////
//
//      HttpCompat::parse_tok_list
//
//      Takes a string containing an HTTP list broken on the separator
//      character <sep>, and returns a StrList object containing a
//      dynamically allocated list of elements.  This is essentially a
//      fancy strtok that runs to completion and hands you back all tokens.
//
//      The routine either allocates and copies each string token, or
//      just maintains the point to the raw text token, depending on the
//      mode of the StrList object.
//
//////////////////////////////////////////////////////////////////////////////

void
HttpCompat::parse_tok_list(StrList *list, int trim_quotes, const char *string, char sep)
{
  if (string == NULL)
    return;
  HttpCompat::parse_tok_list(list, trim_quotes, string, (int)strlen(string), sep);
}

void
HttpCompat::parse_tok_list(StrList *list, int trim_quotes, const char *string, int len, char sep)
{
  int in_quote;
  const char quot = '\"';
  const char *s, *e, *l, *s_before_skipping_ws;
  int index, byte_length, hit_sep;

  if ((string == NULL) || (list == NULL) || (sep == NUL))
    return;

  s     = string;
  l     = s + len - 1;
  index = 0;

  hit_sep              = 0;
  s_before_skipping_ws = s;

  while (s <= l) {
    //////////////////////////////////////////////////////////
    // find the start of the first token, skipping over any //
    // whitespace or empty tokens, to leave <s> pointing at //
    // a NUL, a character, or a double quote.               //
    //////////////////////////////////////////////////////////

    while ((s <= l) && ParseRules::is_ws(*s))
      ++s; // skip whitespace

    //////////////////////////////////////////////////////////
    // if we are pointing at a separator, this was an empty //
    // token, so add the empty token, and continue parsing. //
    //////////////////////////////////////////////////////////

    if ((s <= l) && (*s == sep)) {
      list->append_string(s_before_skipping_ws, 0);
      ++index;
      s_before_skipping_ws = s + 1;
      s                    = s_before_skipping_ws;
      hit_sep              = 1;
      continue;
    }
    //////////////////////////////////////////////////////////////////
    // at this point, <s> points to EOS, a double quote, or another //
    // character --- if EOS, then break out of the loop, and either //
    // tack on a final empty token if we had a trailing separator,  //
    // or just exit.                                                //
    //////////////////////////////////////////////////////////////////

    if (s > l)
      break;

///////////////////////////////////////////////////////////////////
// we are pointing to the first character of a token now, either //
// a character, or a double quote --- the next step is to scan   //
// for the next separator or end of string, being careful not to //
// include separators inside quotes.                             //
///////////////////////////////////////////////////////////////////

#define is_unquoted_separator(c) ((c == sep) && !in_quote)

    if (*s == quot) {
      in_quote = 1;
      e        = s + 1; // start after quote
      if (trim_quotes)
        ++s; // trim starting quote
    } else {
      in_quote = 0;
      e        = s;
    }

    while ((e <= l) && !is_unquoted_separator(*e)) {
      if (*e == quot) {
        in_quote = !in_quote;
      }
      e++;
    }

    ///////////////////////////////////////////////////////////////////////
    // we point one char past the last character of string, or an        //
    // unquoted separator --- so back up into any previous whitespace or //
    // quote, leaving <e> pointed 1 char after the last token character. //
    ///////////////////////////////////////////////////////////////////////

    hit_sep = (e <= l); // must have hit a separator if still inside string

    s_before_skipping_ws = e + 1; // where to start next time
    while ((e > s) && ParseRules::is_ws(*(e - 1)))
      --e; // eat trailing ws
    if ((e > s) && (*(e - 1) == quot) && trim_quotes)
      --e; // eat trailing quote

    /////////////////////////////////////////////////////////////////////
    // now <e> points to the character AFTER the last character of the //
    // field, either a separator, a quote, or a NUL (other other char  //
    // after the last char in the string.                              //
    /////////////////////////////////////////////////////////////////////

    byte_length = (int)(e - s);
    ink_assert(byte_length >= 0);

    ///////////////////////////////////////////
    // add the text to the list, and move on //
    ///////////////////////////////////////////

    list->append_string(s, byte_length);
    s = s_before_skipping_ws; // where to start next time
    ++index;
  }

  ////////////////////////////////////////////////////////////////////////////
  // fall out of loop when at end of string --- three possibilities:        //
  //   (1) at end of string after final token ("a,b,c" or "a,b,c   ")       //
  //   (2) at end of string after final separator ("a,b,c," or "a,b,c,   ") //
  //   (3) at end of string before any tokens ("" or "   ")                 //
  // for cases (2) & (3), we want to return an empty token                  //
  ////////////////////////////////////////////////////////////////////////////

  if (hit_sep || (index == 0)) {
    ink_assert(s == l + 1);
    list->append_string(s_before_skipping_ws, 0);
    ++index;
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      bool HttpCompat::lookup_param_in_strlist(
//          StrList *param_list, char *param_name,
//          char *param_val, int param_val_length)
//
//      Takes a list of parameter strings, and searches each parameter list
//      element for the name <param_name>, and if followed by '=' and a value,
//      the value string is stored in <param_val> up to <param_val_length>
//      bytes minus 1 character for trailing NUL.
//
//      This routine can be used to search for charset=XXX, Q=XXX, and other
//      kinds of parameters.  The param list can be constructed using the
//      parse_comma_list and parse_semicolon_list functions.
//
//      The routine returns true if there was a match, false otherwise.
//
//////////////////////////////////////////////////////////////////////////////

bool
HttpCompat::lookup_param_in_strlist(StrList *param_list, const char *param_name, char *param_val, int param_val_length)
{
  int cnt;
  const char *s, *t;
  Str *param;
  bool is_match;

  for (param = param_list->head; param != NULL; param = param->next) {
    /////////////////////////////////////////////////////
    // compare this parameter to the target param_name //
    /////////////////////////////////////////////////////

    s = param->str; // source str
    t = param_name; // target str
    while (*s && *t && (ParseRules::ink_tolower(*s) == ParseRules::ink_tolower(*t))) {
      ++s;
      ++t;
    }

    ////////////////////////////////////////////////////////////////
    // match if target string empty, and if current string empty,  //
    // or points to space or '=' character.                       //
    ////////////////////////////////////////////////////////////////

    is_match = ((!*t) && ((!*s) || ParseRules::is_ws(*s) || (*s == '=')));

    /////////////////////////////////////////////////////////////
    // copy text after '=' into param_val, up to length limits //
    /////////////////////////////////////////////////////////////

    if (is_match) {
      param_val[0] = '\0';

      while (*s && ParseRules::is_ws(*s))
        s++; // skip white
      if (*s == '=') {
        ++s; // skip '='
        while (*s && ParseRules::is_ws(*s))
          s++; // skip white

        for (cnt         = 0; *s && (cnt < param_val_length - 1); s++, cnt++)
          param_val[cnt] = *s;
        if (cnt < param_val_length)
          param_val[cnt++] = '\0';
      }
      return (true);
    }
  }

  return (false);
}

//////////////////////////////////////////////////////////////////////////////
//
//      bool HttpCompat::lookup_param_in_semicolon_string(
//          char *semicolon_string, int semicolon_string_len,
//          char *param_name, char *param_val, int param_val_length)
//
//      Takes a semicolon-separated string of parameters, and searches
//      for a parameter named <param_name>, as in lookup_param_in_strlist.
//
//      The routine returns true if there was a match, false otherwise.
//      If multiple parameters will be searched for in the same string,
//      use lookup_param_in_strlist(), so the string is not tokenized
//      multiple times.
//
//////////////////////////////////////////////////////////////////////////////

bool
HttpCompat::lookup_param_in_semicolon_string(const char *semicolon_string, int semicolon_string_len, const char *param_name,
                                             char *param_val, int param_val_length)
{
  StrList l;
  bool result;

  parse_semicolon_list(&l, semicolon_string, semicolon_string_len);
  result = lookup_param_in_strlist(&l, param_name, param_val, param_val_length);
  return (result);
}

//////////////////////////////////////////////////////////////////////////////
//
//      void HttpCompat::parse_mime_type(
//          char *mime_string, char *type, char *subtype,
//          int type_len, int subtype_len)
//
//      This routine takes a pointer to a MIME type, and decomposes it
//      into type and subtype fields, skipping over spaces, and placing
//      the decomposed values into <type> and <subtype>.  The length
//      fields describe the lengths of the type and subtype buffers,
//      including the trailing NUL characters.
//
//////////////////////////////////////////////////////////////////////////////

void
HttpCompat::parse_mime_type(const char *mime_string, char *type, char *subtype, int type_len, int subtype_len)
{
  const char *s, *e;
  char *d;

  *type = *subtype = '\0';

  /////////////////////
  // skip whitespace //
  /////////////////////

  for (s = mime_string; *s && ParseRules::is_ws(*s); s++)
    ;

  ///////////////////////////////////////////////////////////////////////
  // scan type (until NUL, out of room, comma/semicolon, space, slash) //
  ///////////////////////////////////////////////////////////////////////

  d = type;
  e = type + type_len;
  while (*s && (d < e - 1) && (!ParseRules::is_ws(*s)) && (*s != ';') && (*s != ',') && (*s != '/')) {
    *d++ = *s++;
  }
  *d++ = '\0';

  //////////////////////////////////////////////////////////////
  // skip remainder of text and space, then slash, then space //
  //////////////////////////////////////////////////////////////

  while (*s && (*s != ';') && (*s != ',') && (*s != '/'))
    ++s;
  if (*s == '/')
    ++s;
  while (*s && ParseRules::is_ws(*s))
    ++s;

  //////////////////////////////////////////////////////////////////////////
  // scan subtype (until NUL, out of room, comma/semicolon, space, slash) //
  //////////////////////////////////////////////////////////////////////////

  d = subtype;
  e = subtype + subtype_len;
  while (*s && (d < e - 1) && (!ParseRules::is_ws(*s)) && (*s != ';') && (*s != ',') && (*s != '/')) {
    *d++ = *s++;
  }
  *d++ = '\0';
}

void
HttpCompat::parse_mime_type_with_len(const char *mime_string, int mime_string_len, char *type, char *subtype, int type_len,
                                     int subtype_len)
{
  const char *s, *s_toofar, *e;
  char *d;

  *type = *subtype = '\0';
  s_toofar         = mime_string + mime_string_len;

  /////////////////////
  // skip whitespace //
  /////////////////////

  for (s = mime_string; (s < s_toofar) && ParseRules::is_ws(*s); s++)
    ;

  ///////////////////////////////////////////////////////////////////////
  // scan type (until NUL, out of room, comma/semicolon, space, slash) //
  ///////////////////////////////////////////////////////////////////////

  d = type;
  e = type + type_len;
  while ((s < s_toofar) && (d < e - 1) && (!ParseRules::is_ws(*s)) && (*s != ';') && (*s != ',') && (*s != '/')) {
    *d++ = *s++;
  }
  *d++ = '\0';

  //////////////////////////////////////////////////////////////
  // skip remainder of text and space, then slash, then space //
  //////////////////////////////////////////////////////////////

  while ((s < s_toofar) && (*s != ';') && (*s != ',') && (*s != '/'))
    ++s;
  if ((s < s_toofar) && (*s == '/'))
    ++s;
  while ((s < s_toofar) && ParseRules::is_ws(*s))
    ++s;

  //////////////////////////////////////////////////////////////////////////
  // scan subtype (until NUL, out of room, comma/semicolon, space, slash) //
  //////////////////////////////////////////////////////////////////////////

  d = subtype;
  e = subtype + subtype_len;
  while ((s < s_toofar) && (d < e - 1) && (!ParseRules::is_ws(*s)) && (*s != ';') && (*s != ',') && (*s != '/')) {
    *d++ = *s++;
  }
  *d++ = '\0';
}

//////////////////////////////////////////////////////////////////////////////
//
//      bool HttpCompat::do_header_values_match(MIMEField *hv1, MIMEField *hv2)
//
//      This routine takes two HTTP header fields and determines
//      if their values "match", as in section 14.43 of RFC2068:
//
//        "When the cache receives a subsequent request whose Request-URI
//         specifies one or more cache entries including a Vary header, the
//         cache MUST NOT use such a cache entry to construct a response to
//         the new request unless all of the headers named in the cached
//         Vary header are present in the new request, and all of the stored
//         selecting request-headers from the previous request match the
//         corresponding headers in the new request.
//
//         The selecting request-headers from two requests are defined to
//         match if and only if the selecting request-headers in the first
//         request can be transformed to the selecting request-headers in
//         the second request by adding or removing linear whitespace (LWS)
//         at places where this is allowed by the corresponding BNF, and/or
//         combining multiple message-header fields with the same field
//         name following the rules about message headers in section 4.2."
//
//////////////////////////////////////////////////////////////////////////////
bool
HttpCompat::do_header_values_rfc2068_14_43_match(MIMEField *hdr1, MIMEField *hdr2)
{
  // If both headers are missing, the headers match.
  if (!hdr1 && !hdr2)
    return true;

  // If one header is missing, the headers do not match.
  if (!hdr1 || !hdr2)
    return false;

  // Make sure both headers have the same number of comma-
  // separated elements.
  HdrCsvIter iter1, iter2;
  if (iter1.count_values(hdr1) != iter2.count_values(hdr2))
    return false;

  int hdr1_val_len, hdr2_val_len;
  const char *hdr1_val = iter1.get_first(hdr1, &hdr1_val_len);
  const char *hdr2_val = iter2.get_first(hdr2, &hdr2_val_len);

  while (hdr1_val || hdr2_val) {
    if (hdr1_val_len != hdr2_val_len || ParseRules::strncasecmp_eow(hdr1_val, hdr2_val, hdr1_val_len) == false)
      return false;

    hdr1_val = iter1.get_next(&hdr1_val_len);
    hdr2_val = iter2.get_next(&hdr2_val_len);
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////
//
//      float HttpCompat::find_Q_param_in_strlist(StrList *strlist);
//
//      Takes a StrList formed from semicolon-parsing a value, and returns
//      the value of the Q directive, or 1.0 by default.
//
//////////////////////////////////////////////////////////////////////////////

float
HttpCompat::find_Q_param_in_strlist(StrList *strlist)
{
  float f, this_q;
  char q_string[8];

  this_q = 1.0;
  if (HttpCompat::lookup_param_in_strlist(strlist, (char *)"q", q_string, sizeof(q_string))) {
    // coverity[secure_coding]
    if (sscanf(q_string, "%f", &f) == 1) // parse q
      this_q = (f < 0 ? 0 : (f > 1 ? 1 : f));
  }

  return (this_q);
}

//////////////////////////////////////////////////////////////////////////////
//
//      float HttpCompat::match_accept_language
//
//      This routine returns the resulting Q factor from matching the
//      content language tag <lang_str> against the Accept-Language value
//      string <acpt_str>.
//
//      It also returns the index of the particular accept list piece
//      that matches, and the length of the accept list piece that matches,
//      in case you later want to resolve quality ties by position in the
//      list, or by length of match.  In general, you want to sort the
//      results of this call first by chosen Q, then by matching_length
//      (longer is better), then by matching_index (lower is better).
//      The first matching_index value is index 1.
//
//////////////////////////////////////////////////////////////////////////////

static inline bool
does_language_range_match(const char *pattern, int pattern_len, const char *tag, int tag_len)
{
  bool match;

  while (pattern_len && tag_len && (ParseRules::ink_tolower(*pattern) == ParseRules::ink_tolower(*tag))) {
    ++pattern;
    ++tag;
    --pattern_len;
    --tag_len;
  }

  // matches if range equals tag, or if range is a lang prefix of tag
  if ((((pattern_len == 0) && (tag_len == 0)) || ((pattern_len == 0) && (*tag == '-'))))
    match = true;
  else
    match = false;

  return (match);
}

float
HttpCompat::match_accept_language(const char *lang_str, int lang_len, StrList *acpt_lang_list, int *matching_length,
                                  int *matching_index, bool ignore_wildcards)
{
  float Q, Q_wild;
  Str *a_value;

  Q                     = -1; // will never be returned as -1
  Q_wild                = -1; // will never be returned as -1
  int match_count       = 0;
  int wild_match_count  = 0;
  int longest_match_len = 0;

  int index        = 0;
  int Q_index      = 0;
  int Q_wild_index = 0;

  *matching_index  = 0;
  *matching_length = 0;

  ///////////////////////////////////////////////////////
  // rip the accept string into comma-separated values //
  ///////////////////////////////////////////////////////
  if (acpt_lang_list->count == 0)
    return (0.0);

  ////////////////////////////////////////
  // loop over each Accept-Language tag //
  ////////////////////////////////////////
  for (a_value = acpt_lang_list->head; a_value; a_value = a_value->next) {
    ++index;

    if (a_value->len == 0)
      continue; // blank tag

    ///////////////////////////////////////////////////////////
    // now rip the Accept-Language tag into head and Q parts //
    ///////////////////////////////////////////////////////////
    StrList a_param_list(false);
    HttpCompat::parse_semicolon_list(&a_param_list, a_value->str, (int)a_value->len);
    if (!a_param_list.head)
      continue;

    /////////////////////////////////////////////////////////////////////
    // This algorithm is a bit wierd --- the resulting Q factor is     //
    // the Q value corresponding to the LONGEST range field that       //
    // matched, or if none matched, then the Q value of any asterisk.  //
    // Also, if the lang value is "", meaning that no Content-Language //
    // was specified, this document matches all accept headers.        //
    /////////////////////////////////////////////////////////////////////
    const char *atag_str = a_param_list.head->str;
    int atag_len         = (int)a_param_list.head->len;

    float tq = HttpCompat::find_Q_param_in_strlist(&a_param_list);

    if ((atag_len == 1) && (atag_str[0] == '*')) // wildcard
    {
      ++wild_match_count;
      if (tq > Q_wild) {
        Q_wild       = tq;
        Q_wild_index = index;
      }
    } else if (does_language_range_match(atag_str, atag_len, lang_str, lang_len)) {
      ++match_count;
      if (atag_len > longest_match_len) {
        longest_match_len = atag_len;
        Q                 = tq;
        Q_index           = index;
      } else if (atag_len == longest_match_len) // if tie, pick higher Q
      {
        if (tq > Q) {
          Q       = tq;
          Q_index = index;
        }
      }
    }
  }

  if ((ignore_wildcards == false) && wild_match_count && !match_count) {
    *matching_index  = Q_wild_index;
    *matching_length = 1;
    return (Q_wild);
  } else if (match_count > 0) // real match
  {
    *matching_index  = Q_index;
    *matching_length = longest_match_len;
    return (Q);
  } else // no match
  {
    *matching_index  = 0;
    *matching_length = 0;
    return (0.0);
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      float HttpCompat::match_accept_charset
//
//      This routine returns the resulting Q factor from matching the
//      content language tag <lang_str> against the Accept-Language value
//      string <acpt_str>.
//
//      It also returns the index of the particular accept list piece
//      that matches, and the length of the accept list piece that matches,
//      in case you later want to resolve quality ties by position in the
//      list, or by length of match.  In general, you want to sort the
//      results of this call first by chosen Q, then by matching_length
//      (longer is better), then by matching_index (lower is better).
//      The first matching_index value is index 1.
//
//////////////////////////////////////////////////////////////////////////////

// FIX: not implemented!

float
HttpCompat::match_accept_charset(const char *charset_str, int charset_len, StrList *acpt_charset_list, int *matching_index,
                                 bool ignore_wildcards)
{
  float Q, Q_wild;
  Str *a_value;

  Q                    = -1; // will never be returned as -1
  Q_wild               = -1; // will never be returned as -1
  int match_count      = 0;
  int wild_match_count = 0;

  int index        = 0;
  int Q_index      = 0;
  int Q_wild_index = 0;

  *matching_index = 0;

  ///////////////////////////////////////////////////////
  // rip the accept string into comma-separated values //
  ///////////////////////////////////////////////////////
  if (acpt_charset_list->count == 0)
    return (0.0);

  ///////////////////////////////////////
  // loop over each Accept-Charset tag //
  ///////////////////////////////////////
  for (a_value = acpt_charset_list->head; a_value; a_value = a_value->next) {
    ++index;
    if (a_value->len == 0)
      continue; // blank tag

    //////////////////////////////////////////////////////////
    // now rip the Accept-Charset tag into head and Q parts //
    //////////////////////////////////////////////////////////
    StrList a_param_list(false);
    HttpCompat::parse_semicolon_list(&a_param_list, a_value->str, (int)a_value->len);
    if (!a_param_list.head)
      continue;

    ///////////////////////////////////////////////////////////////
    // see if the Accept-Charset tag matches the current charset //
    ///////////////////////////////////////////////////////////////
    const char *atag_str = a_param_list.head->str;
    int atag_len         = (int)a_param_list.head->len;
    float tq             = HttpCompat::find_Q_param_in_strlist(&a_param_list);

    if ((atag_len == 1) && (atag_str[0] == '*')) // wildcard
    {
      ++wild_match_count;
      if (tq > Q_wild) {
        Q_wild       = tq;
        Q_wild_index = index;
      }
    } else if ((atag_len == charset_len) && (strncasecmp(atag_str, charset_str, charset_len) == 0)) {
      ++match_count;
      if (tq > Q) {
        Q       = tq;
        Q_index = index;
      }
    }
  }

  if ((ignore_wildcards == false) && wild_match_count && !match_count) {
    *matching_index = Q_wild_index;
    return (Q_wild);
  } else if (match_count > 0) // real match
  {
    *matching_index = Q_index;
    return (Q);
  } else // no match
  {
    *matching_index = 0;
    return (0.0);
  }
}

const char *
HttpCompat::determine_set_by_language(RawHashTable *table_of_sets, StrList *acpt_language_list, StrList *acpt_charset_list,
                                      float *Q_best_ptr, int *La_best_ptr, int *Lc_best_ptr, int *I_best_ptr)
{
  float Q, Ql, Qc, Q_best;
  int I, Idummy, I_best;
  int La, Lc, La_best, Lc_best;
  int is_the_default_set;
  const char *set_best;

  RawHashTable_Key k1;
  RawHashTable_Value v1;
  RawHashTable_Binding *b1;
  RawHashTable_IteratorState i1;
  RawHashTable *table_of_pages;
  HttpBodySetRawData *body_set;

  set_best = "default";
  Q_best   = 0.00001;
  La_best  = 0;
  Lc_best  = INT_MAX;
  I_best   = INT_MAX;

  Debug("body_factory_determine_set", "  INITIAL: [ set_best='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best, Lc_best,
        I_best);

  // FIX: eliminate this special case (which doesn't work anyway), by properly
  //      handling empty lists and empty pieces in match_accept_XXX

  // if no Accept-Language or Accept-Charset, just return default
  if ((acpt_language_list->count == 0) && (acpt_charset_list->count == 0)) {
    Q_best = 1;
    Debug("body_factory_determine_set", "  no constraints => returning '%s'", set_best);
    goto done;
  }

  if (table_of_sets != NULL) {
    ///////////////////////////////////////////
    // loop over set->body-types hash table //
    ///////////////////////////////////////////

    for (b1 = table_of_sets->firstBinding(&i1); b1 != NULL; b1 = table_of_sets->nextBinding(&i1)) {
      k1                   = table_of_sets->getKeyFromBinding(b1);
      v1                   = table_of_sets->getValueFromBinding(b1);
      const char *set_name = (const char *)k1;

      body_set       = (HttpBodySetRawData *)v1;
      table_of_pages = body_set->table_of_pages;

      if ((set_name == NULL) || (table_of_pages == NULL))
        continue;

      //////////////////////////////////////////////////////////////////////
      // Take this error page language and match it against the           //
      // Accept-Language string passed in, to evaluate the match          //
      // quality.  Disable wildcard processing so we use "default"        //
      // if no set explicitly matches.  We also get back the index        //
      // of the match and the length of the match.                        //
      //                                                                  //
      // We optimize the match in a couple of ways:                       //
      //   (a) if Q is better ==> wins, else if tie,                      //
      //   (b) if accept tag length La is bigger ==> wins, else if tie,   //
      //   (c) if content tag length Lc is smaller ==> wins, else if tie, //
      //   (d) if index position I is smaller ==> wins                    //
      //////////////////////////////////////////////////////////////////////

      is_the_default_set = (strcmp(set_name, "default") == 0);

      Debug("body_factory_determine_set", "  --- SET: %-8s (Content-Language '%s', Content-Charset '%s')", set_name,
            body_set->content_language, body_set->content_charset);

      // if no Accept-Language hdr at all, treat as a wildcard that
      // slightly prefers "default".
      if (acpt_language_list->count == 0) {
        Ql = (is_the_default_set ? 1.0001 : 1.000);
        La = 0;
        Lc = INT_MAX;
        I  = 1;
        Debug("body_factory_determine_set", "      SET: [%-8s] A-L not present => [ Ql=%g, La=%d, Lc=%d, I=%d ]", set_name, Ql, La,
              Lc, I);
      } else {
        Lc = strlen(body_set->content_language);
        Ql = HttpCompat::match_accept_language(body_set->content_language, Lc, acpt_language_list, &La, &I, true);
        Debug("body_factory_determine_set", "      SET: [%-8s] A-L match value => [ Ql=%g, La=%d, Lc=%d, I=%d ]", set_name, Ql, La,
              Lc, I);
      }

      /////////////////////////////////////////////////////////////
      // Take this error page language and match it against the  //
      // Accept-Charset string passed in, to evaluate the match  //
      // quality.  Disable wildcard processing so that only      //
      // explicit values match.  (Many browsers will send along  //
      // "*" with all lists, and we really don't want to send    //
      // strange character sets for these people --- we'd rather //
      // use a more portable "default" set.  The index value we  //
      // get back isn't used, because it's a little hard to know //
      // how to tradeoff language indices vs. charset indices.   //
      // If someone cares, we could surely work charset indices  //
      // into the sorting computation below.                     //
      /////////////////////////////////////////////////////////////

      // if no Accept-Charset hdr at all, treat as a wildcard that
      // slightly prefers "default".
      if (acpt_charset_list->count == 0) {
        Qc     = (is_the_default_set ? 1.0001 : 1.000);
        Idummy = 1;
        Debug("body_factory_determine_set", "      SET: [%-8s] A-C not present => [ Qc=%g ]", set_name, Qc);
      } else {
        Qc = HttpCompat::match_accept_charset(body_set->content_charset, strlen(body_set->content_charset), acpt_charset_list,
                                              &Idummy, true);
        Debug("body_factory_determine_set", "      SET: [%-8s] A-C match value => [ Qc=%g ]", set_name, Qc);
      }

      /////////////////////////////////////////////////////////////////
      // We get back the Q value, the matching field length, and the //
      // matching field index.  We sort by largest Q value, but if   //
      // there is a Q tie, we sub sort on longer matching length,    //
      // and if there is a tie on Q and L, we sub sort on position   //
      // index, preferring values earlier in Accept-Language list.   //
      /////////////////////////////////////////////////////////////////

      Q = min(Ql, Qc);

      //////////////////////////////////////////////////////////
      // normally the Q for default pages should be slightly  //
      // less than for normal pages, but default pages should //
      // always match to a slight level, in case everything   //
      // else doesn't match (matches with Q=0).               //
      //////////////////////////////////////////////////////////

      if (is_the_default_set) {
        Q = Q + -0.00005;
        if (Q < 0.00001)
          Q = 0.00001;
      }

      Debug("body_factory_determine_set", "      NEW: [ set='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_name, Q, La, Lc, I);
      Debug("body_factory_determine_set", "      OLD: [ set='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best, Lc_best,
            I_best);

      if (((Q > Q_best)) || ((Q == Q_best) && (La > La_best)) || ((Q == Q_best) && (La == La_best) && (Lc < Lc_best)) ||
          ((Q == Q_best) && (La == La_best) && (Lc == Lc_best) && (I < I_best))) {
        Q_best   = Q;
        La_best  = La;
        Lc_best  = Lc;
        I_best   = I;
        set_best = set_name;

        Debug("body_factory_determine_set", "   WINNER: [ set_best='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best,
              Lc_best, I_best);
      } else {
        Debug("body_factory_determine_set", "    LOSER: [ set_best='%s', Q=%g, La=%d, Lc=%d, I=%d ]", set_best, Q_best, La_best,
              Lc_best, I_best);
      }
    }
  }

done:

  *Q_best_ptr  = Q_best;
  *La_best_ptr = La_best;
  *Lc_best_ptr = Lc_best;
  *I_best_ptr  = I_best;
  return (set_best);
}
