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

#include "tscore/ink_platform.h"

#include "HttpTransact.h"
#include "HttpTransactHeaders.h"
#include "HttpTransactCache.h"
#include <ctime>
#include "HTTP.h"
#include "HttpCompat.h"
#include "tscore/InkErrno.h"

/**
  Find the pointer and length of an etag, after stripping off any leading
  "W/" prefix, and surrounding double quotes.

*/
inline static const char *
find_etag(const char *raw_tag_field, int raw_tag_field_len, int *length)
{
  const char *quote;
  int etag_length        = 0;
  const char *etag_start = raw_tag_field;
  const char *etag_end   = raw_tag_field + raw_tag_field_len;

  if ((raw_tag_field_len >= 2) && (etag_start[0] == 'W' && etag_start[1] == '/')) {
    etag_start += 2;
  }

  etag_length = etag_end - etag_start;

  if ((etag_start < etag_end) && (*etag_start == '"')) {
    ++etag_start;
    --etag_length;
    quote = (const char *)memchr(etag_start, '"', etag_length);
    if (quote) {
      etag_length = quote - etag_start;
    }
  }
  *length = etag_length;
  return etag_start;
}

/**
  Match an etag raw_tag_field with a list of tags in the comma-separated
  string field_to_match, using strong rules.

*/
inline static bool
do_strings_match_strongly(const char *raw_tag_field, int raw_tag_field_len, const char *comma_sep_tag_list,
                          int comma_sep_tag_list_len)
{
  StrList tag_list;
  const char *etag_start;
  int n, etag_length;

  // Can never match a weak tag with a strong compare
  if ((raw_tag_field_len >= 2) && (raw_tag_field[0] == 'W' && raw_tag_field[1] == '/')) {
    return false;
  }
  // Find the unalterated tag
  etag_start = find_etag(raw_tag_field, raw_tag_field_len, &etag_length);

  // Rip the field list into a comma-separated field list
  HttpCompat::parse_comma_list(&tag_list, comma_sep_tag_list, comma_sep_tag_list_len);

  // Loop over all the tags in the tag list
  for (Str *tag = tag_list.head; tag; tag = tag->next) {
    // If field is "*", then we got a match
    if ((tag->len == 1) && (tag->str[0] == '*')) {
      return true;
    }

    n = 0;

    if (((int)(tag->len - n) == etag_length) && (strncmp(etag_start, tag->str + n, etag_length) == 0)) {
      return true;
    }
  }

  return false;
}

/**
  Match an etag raw_tag_field with a list of tags in the comma-separated
  string field_to_match, using weak rules.

*/
inline static bool
do_strings_match_weakly(const char *raw_tag_field, int raw_tag_field_len, const char *comma_sep_tag_list,
                        int comma_sep_tag_list_len)
{
  StrList tag_list;
  const char *etag_start;
  const char *cur_tag;
  int etag_length, cur_tag_len;

  // Find the unalterated tag
  etag_start = find_etag(raw_tag_field, raw_tag_field_len, &etag_length);

  // Rip the field list into a comma-separated field list
  HttpCompat::parse_comma_list(&tag_list, comma_sep_tag_list, comma_sep_tag_list_len);

  for (Str *tag = tag_list.head; tag; tag = tag->next) {
    // If field is "*", then we got a match
    if ((tag->len == 1) && (tag->str[0] == '*')) {
      return true;
    }

    // strip off the leading 'W/' and quotation marks from the
    // current tag, then compare for equality with above tag.
    cur_tag = find_etag(tag->str, tag->len, &cur_tag_len);
    if ((cur_tag_len == etag_length) && (strncmp(cur_tag, etag_start, cur_tag_len) == 0)) {
      return true;
    }
  }
  return false;
}

inline static bool
is_asterisk(char *s)
{
  return ((s[0] == '*') && (s[1] == NUL));
}

inline static bool
is_empty(char *s)
{
  return (s[0] == NUL);
}

/**
  Given a set of alternates, select the best match.

  The current school of thought: quality 1st, freshness 2nd.  Loop through
  alternates and find the one with the highest quality factor. Then
  determine if it is fresh enough. If not, find the next best match. In
  keeping with "quality is job 1", subsequent matches will only be
  considered if their quality is equal to the quality of the first match.

  @return index in cache alternates vector.

*/
int
HttpTransactCache::SelectFromAlternates(CacheHTTPInfoVector *cache_vector, HTTPHdr *client_request,
                                        OverridableHttpConfigParams *http_config_params)
{
  time_t current_age, best_age = CacheHighAgeWatermark;
  time_t t_now         = 0;
  int best_index       = -1;
  float best_Q         = -1.0;
  float unacceptable_Q = 0.0;

  int alt_count = cache_vector->count();
  if (alt_count == 0) {
    return -1;
  }

  Debug("http_match", "[SelectFromAlternates] # alternates = %d", alt_count);
  Debug("http_seq", "[SelectFromAlternates] %d alternates for this cached doc", alt_count);
  if (is_debug_tag_set("http_alts")) {
    fprintf(stderr, "[alts] There are %d alternates for this request header.\n", alt_count);
  }

  // so that plugins can make cache reads for http
  // docs to check if the doc exists in the cache
  if (!client_request->valid()) {
    return 0;
  }

  for (int i = 0; i < alt_count; i++) {
    float Q;
    CacheHTTPInfo *obj       = cache_vector->get(i);
    HTTPHdr *cached_request  = obj->request_get();
    HTTPHdr *cached_response = obj->response_get();

    if (!(obj->object_key_get() == zero_key)) {
      ink_assert(cached_request->valid());
      ink_assert(cached_response->valid());

      Q = calculate_quality_of_match(http_config_params, client_request, cached_request, cached_response);

      if (alt_count > 1) {
        if (t_now == 0) {
          t_now = ink_local_time();
        }
        current_age = HttpTransactHeaders::calculate_document_age(obj->request_sent_time_get(), obj->response_received_time_get(),
                                                                  cached_response, cached_response->get_date(), t_now);
        // Overflow?
        if (current_age < 0) {
          current_age = CacheHighAgeWatermark;
        }
      } else {
        current_age = (time_t)0;
      }

      if (is_debug_tag_set("http_alts")) {
        fprintf(stderr, "[alts] ---- alternate #%d (Q = %g) has these request/response hdrs:\n", i + 1, Q);
        char b[4096];
        int used, tmp, offset;
        int done;

        offset = 0;
        do {
          used = 0;
          tmp  = offset;
          done = cached_request->print(b, sizeof(b) - 1, &used, &tmp);
          offset += used;
          b[used] = '\0';
          fprintf(stderr, "%s", b);
        } while (!done);

        offset = 0;
        do {
          used = 0;
          tmp  = offset;
          done = cached_response->print(b, sizeof(b) - 1, &used, &tmp);
          offset += used;
          b[used] = '\0';
          fprintf(stderr, "%s", b);
        } while (!done);
      }

      if ((Q > best_Q) || ((Q == best_Q) && (current_age <= best_age))) {
        best_Q     = Q;
        best_age   = current_age;
        best_index = i;
      }
    }
  }
  Debug("http_seq", "[SelectFromAlternates] Chosen alternate # %d", best_index);
  if (is_debug_tag_set("http_alts")) {
    fprintf(stderr, "[alts] and the winner is alternate number %d\n", best_index);
  }

  if ((best_index != -1) && (best_Q > unacceptable_Q)) {
    return best_index;
  } else {
    return -1;
  }
}

/**
  For cached req/res and incoming req, return quality of match.

  The current school of thought: quality 1st, freshness 2nd.  This
  function takes a user agent request client_request and the two headers
  for a cached object (obj_client_request and obj_origin_server_response),
  and returns a floating point number for how well the object matches
  the client's request.

  Two factors currently affect a match: Accept headers, which filter and
  sort the matches, and Vary headers, which constrain whether a dynamic
  document matches a request.

  Note: According to the specs, specific matching takes precedence over
  wildcard matching. For example, listed in precedence: text/html;q=0.5,
  text/ascii, image/'*', '*'/'*'. So, ideally, in choosing between
  alternates, we should given preference to those which matched
  specifically over those which matched with wildcards.

  @return quality (-1: no match, 0..1: poor..good).

*/
float
HttpTransactCache::calculate_quality_of_match(OverridableHttpConfigParams *http_config_param, HTTPHdr *client_request,
                                              HTTPHdr *obj_client_request, HTTPHdr *obj_origin_server_response)
{
  // For PURGE requests, any alternate is good really.
  if (client_request->method_get_wksidx() == HTTP_WKSIDX_PURGE) {
    return (float)1.0;
  }

  // Now calculate a quality based on all sorts of logic
  float q[4], Q;
  MIMEField *accept_field;
  MIMEField *cached_accept_field;
  MIMEField *content_field;

  // vary_skip_mask is used as a bitmask, 0b01 or 0b11 depending on the presence of Vary.
  // This allows us to AND each of the four configs against it; Table:
  //
  //   Conf   Mask          Conf   Mask         Conf   Mask
  //   ----   ----          ----   ----         ----   ----
  //    00  &  01 == false   01  &  01 == true   10  &  01 == false
  //    00  &  11 == false   01  &  11 == true   10  &  11 == true
  //
  // A true value means the check for that config can be skipped. Note: from a users
  // perspective, the configs are simply 0, 1 or 2.
  unsigned int vary_skip_mask = obj_origin_server_response->presence(MIME_PRESENCE_VARY) ? 1 : 3;

  // Make debug output happy
  q[1] = (q[2] = (q[3] = -2.0));

  // This content_field is used for a couple of headers, so get it first
  content_field = obj_origin_server_response->field_find(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);

  // Accept: header
  if (http_config_param->ignore_accept_mismatch & vary_skip_mask) {
    // Ignore it
    q[0] = 1.0;
  } else {
    accept_field = client_request->field_find(MIME_FIELD_ACCEPT, MIME_LEN_ACCEPT);

    // A NULL Accept or a NULL Content-Type field are perfect matches.
    if (content_field == nullptr || accept_field == nullptr) {
      q[0] = 1.0; // TODO: Why should this not be 1.001 ?? // leif
    } else {
      q[0] = calculate_quality_of_accept_match(accept_field, content_field);
    }
  }

  if (q[0] >= 0.0) {
    // Accept-Charset: header
    if (http_config_param->ignore_accept_charset_mismatch & vary_skip_mask) {
      // Ignore it
      q[1] = 1.0;
    } else {
      accept_field        = client_request->field_find(MIME_FIELD_ACCEPT_CHARSET, MIME_LEN_ACCEPT_CHARSET);
      cached_accept_field = obj_client_request->field_find(MIME_FIELD_ACCEPT_CHARSET, MIME_LEN_ACCEPT_CHARSET);

      // absence in both requests counts as exact match
      if (accept_field == nullptr && cached_accept_field == nullptr) {
        Debug("http_alternate", "Exact match for ACCEPT CHARSET (not in request nor cache)");
        q[1] = 1.001; // slightly higher weight to this guy
      } else {
        q[1] = calculate_quality_of_accept_charset_match(accept_field, content_field, cached_accept_field);
      }
    }

    if (q[1] >= 0.0) {
      // Accept-Encoding: header
      if (http_config_param->ignore_accept_encoding_mismatch & vary_skip_mask) {
        // Ignore it
        q[2] = 1.0;
      } else {
        accept_field        = client_request->field_find(MIME_FIELD_ACCEPT_ENCODING, MIME_LEN_ACCEPT_ENCODING);
        content_field       = obj_origin_server_response->field_find(MIME_FIELD_CONTENT_ENCODING, MIME_LEN_CONTENT_ENCODING);
        cached_accept_field = obj_client_request->field_find(MIME_FIELD_ACCEPT_ENCODING, MIME_LEN_ACCEPT_ENCODING);

        // absence in both requests counts as exact match
        if (accept_field == nullptr && cached_accept_field == nullptr) {
          Debug("http_alternate", "Exact match for ACCEPT ENCODING (not in request nor cache)");
          q[2] = 1.001; // slightly higher weight to this guy
        } else {
          q[2] = calculate_quality_of_accept_encoding_match(accept_field, content_field, cached_accept_field);
        }
      }

      if (q[2] >= 0.0) {
        // Accept-Language: header
        if (http_config_param->ignore_accept_language_mismatch & vary_skip_mask) {
          // Ignore it
          q[3] = 1.0;
        } else {
          accept_field        = client_request->field_find(MIME_FIELD_ACCEPT_LANGUAGE, MIME_LEN_ACCEPT_LANGUAGE);
          content_field       = obj_origin_server_response->field_find(MIME_FIELD_CONTENT_LANGUAGE, MIME_LEN_CONTENT_LANGUAGE);
          cached_accept_field = obj_client_request->field_find(MIME_FIELD_ACCEPT_LANGUAGE, MIME_LEN_ACCEPT_LANGUAGE);

          // absence in both requests counts as exact match
          if (accept_field == nullptr && cached_accept_field == nullptr) {
            Debug("http_alternate", "Exact match for ACCEPT LANGUAGE (not in request nor cache)");
            q[3] = 1.001; // slightly higher weight to this guy
          } else {
            q[3] = calculate_quality_of_accept_language_match(accept_field, content_field, cached_accept_field);
          }
        }
      }
    }
  }

  // final quality is minimum Q, or -1, if some match failed //
  Q = ((q[0] < 0) || (q[1] < 0) || (q[2] < 0) || (q[3] < 0)) ? -1.0 : q[0] * q[1] * q[2] * q[3];

  Debug("http_match", "    CalcQualityOfMatch: Accept match = %g", q[0]);
  Debug("http_seq", "    CalcQualityOfMatch: Accept match = %g", q[0]);
  Debug("http_alternate", "Content-Type and Accept %f", q[0]);

  Debug("http_match", "    CalcQualityOfMatch: AcceptCharset match = %g", q[1]);
  Debug("http_seq", "    CalcQualityOfMatch: AcceptCharset match = %g", q[1]);
  Debug("http_alternate", "Content-Type and Accept-Charset %f", q[1]);

  Debug("http_match", "    CalcQualityOfMatch: AcceptEncoding match = %g", q[2]);
  Debug("http_seq", "    CalcQualityOfMatch: AcceptEncoding match = %g", q[2]);
  Debug("http_alternate", "Content-Encoding and Accept-Encoding %f", q[2]);

  Debug("http_match", "    CalcQualityOfMatch: AcceptLanguage match = %g", q[3]);
  Debug("http_seq", "    CalcQualityOfMatch: AcceptLanguage match = %g", q[3]);
  Debug("http_alternate", "Content-Language and Accept-Language %f", q[3]);

  Debug("http_alternate", "Mult's Quality Factor: %f", Q);
  Debug("http_alternate", "----------End of Alternate----------");

  int force_alt = 0;

  if (Q > 0.0) {
    APIHook *hook;
    HttpAltInfo info;
    float qvalue;

    hook = http_global_hooks->get(TS_HTTP_SELECT_ALT_HOOK);
    if (hook) {
      info.m_client_req.copy_shallow(client_request);
      info.m_cached_req.copy_shallow(obj_client_request);
      info.m_cached_resp.copy_shallow(obj_origin_server_response);
      qvalue = 1.0;

      while (hook) {
        info.m_qvalue = 1.0;
        hook->invoke(TS_EVENT_HTTP_SELECT_ALT, &info);
        hook = hook->m_link.next;
        if (info.m_qvalue < 0.0) {
          info.m_qvalue = 0.0;
        } else if (info.m_qvalue > 1.0) {
          if (info.m_qvalue == FLT_MAX) {
            force_alt = 1;
          }
          info.m_qvalue = 1.0;
        }
        qvalue *= info.m_qvalue;
      }
      Q *= qvalue;

      // Clear out any SDK allocated values from the
      //   hdr handles
      info.m_client_req.clear();
      info.m_cached_req.clear();
      info.m_cached_resp.clear();
    }
  }

  if (Q >= 0.0 && !force_alt) { // make sense to check 'variability' only if Q >= 0.0
    // set quality to -1, if cached copy would vary for this request //
    Variability_t variability = CalcVariability(http_config_param, client_request, obj_client_request, obj_origin_server_response);

    if (variability != VARIABILITY_NONE) {
      Q = -1.0;
    }

    Debug("http_match", "    CalcQualityOfMatch: CalcVariability says variability = %d", (variability != VARIABILITY_NONE));
    Debug("http_seq", "    CalcQualityOfMatch: CalcVariability says variability = %d", (variability != VARIABILITY_NONE));
    Debug("http_match", "    CalcQualityOfMatch: Returning final Q = %g", Q);
    Debug("http_seq", "    CalcQualityOfMatch: Returning final Q = %g", Q);
  }

  return Q;
}

/**
  Match request Accept with response Content-Type.

  If the Accept field mime-type value is *, do not attempt to match,
  but note the q value for the wildcard match. If the type is not *,
  but the subtype is * and the Accept type and Content type match,
  again do not attempt to match, but note the q value. If neither of
  these two cases, match, keeping track of the highest q value for the
  matches. At the end of the loop over the Accept header field values,
  if the highest q value is -1.0 (there was no specific match), if there
  was a wildcard subtype match, set the q value to the wildcard subtype q
  value. If there is still no match, and there is a wildcard type match,
  set the q value to the wildcard type q value.

  We allow no Content-Type headers in responses to match with quality 1.0.

  @return quality (-1: no match, 0..1: poor..good).

*/
static inline bool
do_content_types_match(char *type1, char *subtype1, char *type2, char *subtype2)
{
  return ((is_asterisk(type1) || is_empty(type1) || (strcasecmp(type1, type2) == 0)) &&
          (is_asterisk(subtype1) || is_empty(subtype1) || (strcasecmp(subtype1, subtype2) == 0)));
}

float
HttpTransactCache::calculate_quality_of_accept_match(MIMEField *accept_field, MIMEField *content_field)
{
  float q = -1.0;
  const char *c_raw, *a_raw;
  int c_raw_len, a_raw_len;
  char c_type[32], c_subtype[32];
  Str *a_value;
  StrList c_param_list, a_values_list;
  bool wildcard_type_present    = false;
  bool wildcard_subtype_present = false;
  float wildcard_type_q         = 1.0;
  float wildcard_subtype_q      = 1.0;

  ink_assert((accept_field != nullptr) && (content_field != nullptr));

  // Extract the content-type field value before the semicolon.
  // This has to be done just once because assuming single
  // content-type in document. If more than one content
  // type, will have to do as in content-language, content-
  // encoding matching where we loop over both accept and
  // content-type fields.

  c_raw = content_field->value_get(&c_raw_len);
  HttpCompat::parse_semicolon_list(&c_param_list, c_raw, c_raw_len);
  Str *c_param = c_param_list.head;

  if (!c_param) {
    return (1.0);
  }
  // Parse the type and subtype of the Content-Type field.
  HttpCompat::parse_mime_type(c_param->str, c_type, c_subtype, sizeof(c_type), sizeof(c_subtype));

  // Now loop over Accept field values.
  // TODO: Should we check the return value (count) from this?
  accept_field->value_get_comma_list(&a_values_list);

  for (a_value = a_values_list.head; a_value; a_value = a_value->next) {
    // Get the raw string to the current comma-sep Accept field value
    a_raw     = a_value->str;
    a_raw_len = a_value->len;

    // Extract the field value before the semicolon
    StrList a_param_list;
    HttpCompat::parse_semicolon_list(&a_param_list, a_raw, a_raw_len);

    // Read the next type/subtype media-range
    Str *a_param = a_param_list.head;
    if (!a_param) {
      continue;
    }

    // Parse the type and subtype of the Accept field
    char a_type[32], a_subtype[32];
    HttpCompat::parse_mime_type(a_param->str, a_type, a_subtype, sizeof(a_type), sizeof(a_subtype));

    //      printf("matching Content-type; '%s/%s' with Accept value '%s/%s'\n",
    //             c_type,c_subtype,a_type,a_subtype);

    // Is there a wildcard in the type or subtype?
    if (is_asterisk(a_type)) {
      wildcard_type_present = true;
      wildcard_type_q       = HttpCompat::find_Q_param_in_strlist(&a_param_list);
    } else if (is_asterisk(a_subtype) && (strcasecmp(a_type, c_type) == 0)) {
      wildcard_subtype_present = true;
      wildcard_subtype_q       = HttpCompat::find_Q_param_in_strlist(&a_param_list);
    } else {
      // No wildcard. Do explicit matching of accept and content values.
      if (do_content_types_match(a_type, a_subtype, c_type, c_subtype)) {
        float tq;
        tq = HttpCompat::find_Q_param_in_strlist(&a_param_list);
        q  = (tq > q ? tq : q);
      }
    }
  }

  // At this point either there is an explicit match, in
  // which case q will not be -1.0 and will be returned.
  // If there was no explicit match, but the accept field
  // had wildcards, return the wildcard match q value.

  // No explicit match, but wildcard subtype match
  if ((q == -1.0) && (wildcard_subtype_present == true)) {
    q = wildcard_subtype_q;
  }
  // No explicit match, but wildcard type match.
  if ((q == -1.0) && (wildcard_type_present == true)) {
    q = wildcard_type_q;
  }
  return (q);
}

/**
  Match request Accept-Charset with response Content-Type.

  Extract the response charset from the Content-Type field - the charset
  is after the semicolon. Loop through the charsets in the request's
  Accept-Charset field. If the Accept-Charset value is a wildcard, do not
  attempt to match. Otherwise match and note the highest q value. If after
  the loop the q value is -1, indicating no match, then if Accept-Charset
  had a wildcard, allow it to match - setting q to the wildcard q value.
  If there is still no match and the Content-Type was the default charset,
  allow a match with a q value of 1.0.

  We allow no Content-Type headers in responses to match with quality 1.0.

  @return quality (-1: no match, 0..1: poor..good).

*/
static inline bool
does_charset_match(char *charset1, char *charset2)
{
  return (is_asterisk(charset1) || is_empty(charset1) || (strcasecmp(charset1, charset2) == 0));
}

float
HttpTransactCache::calculate_quality_of_accept_charset_match(MIMEField *accept_field, MIMEField *content_field,
                                                             MIMEField *cached_accept_field)
{
  float q = -1.0;
  const char *c_raw, *a_raw, *ca_raw;
  int c_raw_len, a_raw_len, ca_raw_len;
  StrList a_values_list;
  Str *a_value;
  char c_charset[128];
  char *a_charset;
  int a_charset_len;
  const char *default_charset = "utf-8";
  bool wildcard_present       = false;
  float wildcard_q            = 1.0;

  // prefer exact matches
  if (accept_field && cached_accept_field) {
    a_raw  = accept_field->value_get(&a_raw_len);
    ca_raw = cached_accept_field->value_get(&ca_raw_len);
    if (a_raw && ca_raw && a_raw_len == ca_raw_len && !strncmp(a_raw, ca_raw, a_raw_len)) {
      Debug("http_alternate", "Exact match for ACCEPT CHARSET");
      return (float)1.001; // slightly higher weight to this guy
    }
  }
  // return match if either ac or ct is missing
  // this check is different from accept-encoding
  if (accept_field == nullptr || content_field == nullptr) {
    return (float)1.0;
  }
  // get the charset of this content-type //
  c_raw = content_field->value_get(&c_raw_len);
  if (!HttpCompat::lookup_param_in_semicolon_string(c_raw, c_raw_len, "charset", c_charset, sizeof(c_charset) - 1)) {
    ink_strlcpy(c_charset, default_charset, sizeof(c_charset));
  }
  // Now loop over Accept-Charset field values.
  // TODO: Should we check the return value (count) from this?
  accept_field->value_get_comma_list(&a_values_list);

  for (a_value = a_values_list.head; a_value; a_value = a_value->next) {
    // Get the raw string to the current comma-sep Accept-Charset field value
    a_raw     = a_value->str;
    a_raw_len = a_value->len;

    // Extract the field value before the semicolon
    StrList a_param_list(true); // FIXME: copies & NUL-terminates strings
    HttpCompat::parse_semicolon_list(&a_param_list, a_raw, a_raw_len);

    if (a_param_list.head) {
      a_charset     = (char *)a_param_list.head->str;
      a_charset_len = a_param_list.head->len;
    } else {
      continue;
    }

    //      printf("matching Content-type; '%s' with Accept-Charset value '%s'\n",
    //             c_charset,a_charset);

    // dont match wildcards //
    if ((a_charset_len == 1) && (a_charset[0] == '*')) {
      wildcard_present = true;
      wildcard_q       = HttpCompat::find_Q_param_in_strlist(&a_param_list);
    } else {
      // if type matches, get the Q factor //
      if (does_charset_match(a_charset, c_charset)) {
        float tq;
        tq = HttpCompat::find_Q_param_in_strlist(&a_param_list);
        q  = (tq > q ? tq : q);
      }
    }
  }

  // if no match and wildcard present, allow match //
  if ((q == -1.0) && (wildcard_present == true)) {
    q = wildcard_q;
  }
  // if no match, still allow default_charset //
  if ((q == -1) && (strcasecmp(c_charset, default_charset) == 0)) {
    q = 1.0;
  }
  return (q);
}

/**
  Match request Accept-Encoding with response Content-Encoding.

  First determine if the cached document has identity encoding. This
  can be the case if the document has no Content-Encoding header field
  or if the Content-Encoding field explicitly lists "identity". Then,
  if there is no Accept-Encoding header and the cached response uses
  identity encoding return a match. If there is no Accept-Encoding header
  and the cached document uses some other form of encoding, also return
  a match, albeit one with a slightly lower q value (0.999).

  If none of the above cases occurs, compare Content-Encoding with
  Accept-Encoding, by looping over the Content-Encoding values (there
  may be more than one, since a document may be gzipped, followed by
  compressed, etc.). If any of the Content-Encoding values are not in
  the Accept-Encoding header, exit the loop. Before exiting, if there
  has not been a match, match a wildcard in the Accept-Encoding field
  and if still no match, match an identity encoding - this may happen
  if the request did not list "identity" in the Accept-Encoding field,
  but the response listed it in the Content-Encoding field. In this last
  case, match with a q value of 0.001.

  The return values are:
    - -1.0: Doesn't match
    - 0.999: No Accept-Encoding header, and Content-Encoding does not list
      "identity".
    - 0.001: Accept-Encoding was not empty, but Content-Encoding was
      either empty or explicitly listed "identity".
    - 0.0..1.0: Matches with a quality between 0 (poor) and 1 (good).

  @return quality (-1: no match, 0..1: poor..good).

*/
static inline bool
does_encoding_match(char *enc1, const char *enc2)
{
  if (is_asterisk(enc1) || ((strcasecmp(enc1, enc2)) == 0)) {
    return true;
  }

  // rfc2616,sec3.5: applications SHOULD consider "x-gzip" and "x-compress" to be
  //                equivalent to "gzip" and "compress" respectively
  if ((!strcasecmp(enc1, "gzip") && !strcasecmp(enc2, "x-gzip")) || (!strcasecmp(enc1, "x-gzip") && !strcasecmp(enc2, "gzip")) ||
      (!strcasecmp(enc1, "compress") && !strcasecmp(enc2, "x-compress")) ||
      (!strcasecmp(enc1, "x-compress") && !strcasecmp(enc2, "compress"))) {
    return true;
  }

  return false;
}

bool
HttpTransactCache::match_content_encoding(MIMEField *accept_field, const char *encoding_identifier)
{
  Str *a_value;
  const char *a_raw;
  StrList a_values_list;
  if (!accept_field) {
    return false;
  }
  // TODO: Should we check the return value (count) here?
  accept_field->value_get_comma_list(&a_values_list);

  for (a_value = a_values_list.head; a_value; a_value = a_value->next) {
    char *a_encoding = nullptr;
    StrList a_param_list;
    a_raw = a_value->str;
    HttpCompat::parse_semicolon_list(&a_param_list, a_raw);
    if (a_param_list.head) {
      a_encoding = (char *)a_param_list.head->str;
    } else {
      continue;
    }
    float q;
    q = HttpCompat::find_Q_param_in_strlist(&a_param_list);
    if (q != 0 && does_encoding_match(a_encoding, encoding_identifier)) {
      return true;
    }
  }
  return false;
}

// TODO: This used to take a length for c_raw, but that was never used, so removed it from the prototype.
static inline bool
match_accept_content_encoding(const char *c_raw, MIMEField *accept_field, bool *wildcard_present, float *wildcard_q, float *q)
{
  Str *a_value;
  const char *a_raw;
  StrList a_values_list;

  if (!accept_field) {
    return false;
  }
  // loop over Accept-Encoding elements, looking for match //
  // TODO: Should we check the return value (count) here?
  accept_field->value_get_comma_list(&a_values_list);

  for (a_value = a_values_list.head; a_value; a_value = a_value->next) {
    char *a_encoding = nullptr;
    StrList a_param_list;

    // Get the raw string to the current comma-sep Accept-Charset field value
    a_raw = a_value->str;

    // break Accept-Encoding piece into semi-colon separated parts //
    HttpCompat::parse_semicolon_list(&a_param_list, a_raw);
    if (a_param_list.head) {
      a_encoding = (char *)a_param_list.head->str;
    } else {
      continue;
    }

    if (is_asterisk(a_encoding)) {
      *wildcard_present = true;
      *wildcard_q       = HttpCompat::find_Q_param_in_strlist(&a_param_list);
      return true;
    } else if (does_encoding_match(a_encoding, c_raw)) {
      // if type matches, get the Q factor //
      float tq;
      tq = HttpCompat::find_Q_param_in_strlist(&a_param_list);
      *q = (tq > *q ? tq : *q);

      return true;
    } else {
      // so this c_raw value did not match this a_raw value. big deal.
    }
  }
  return false;
}

float
HttpTransactCache::calculate_quality_of_accept_encoding_match(MIMEField *accept_field, MIMEField *content_field,
                                                              MIMEField *cached_accept_field)
{
  float q                   = -1.0;
  bool is_identity_encoding = false;
  const char *c_encoding;
  int c_encoding_len;
  bool wildcard_present = false;
  float wildcard_q      = 1.0;
  StrList c_values_list;
  Str *c_value;
  const char *a_raw, *ca_raw;
  int a_raw_len, ca_raw_len;

  // prefer exact matches
  if (accept_field && cached_accept_field) {
    a_raw  = accept_field->value_get(&a_raw_len);
    ca_raw = cached_accept_field->value_get(&ca_raw_len);
    if (a_raw && ca_raw && a_raw_len == ca_raw_len && !strncmp(a_raw, ca_raw, a_raw_len)) {
      Debug("http_alternate", "Exact match for ACCEPT ENCODING");
      return (float)1.001; // slightly higher weight to this guy
    }
  }
  // return match if both ae and ce are missing
  // this check is different from accept charset
  if (accept_field == nullptr && content_field == nullptr) {
    return (float)1.0;
  }
  // if no Content-Encoding, treat as "identity" //
  if (!content_field) {
    Debug("http_match", "[calculate_quality_accept_encoding_match]: "
                        "response hdr does not have content-encoding.");
    is_identity_encoding = true;
  } else {
    // TODO: Should we check the return value (count) here?
    content_field->value_get_comma_list(&c_values_list);

    content_field->value_get(&c_encoding_len);
    if (c_encoding_len == 0) {
      is_identity_encoding = true;
    } else {
      // does this document have the identity encoding? //
      for (c_value = c_values_list.head; c_value; c_value = c_value->next) {
        c_encoding     = c_value->str;
        c_encoding_len = c_value->len;
        if ((c_encoding_len >= 8) && (strncasecmp(c_encoding, "identity", 8) == 0)) {
          is_identity_encoding = true;
          break;
        }
      }
    }
  }

  ///////////////////////////////////////////////////////////////////////
  // if no Accept-Encoding header, only match identity                 //
  //   The 1.1 spec says servers MAY assume that clients will accept   //
  //   any encoding if no header is sent.  Unforntunately, this does   //
  //   not work 1.0 clients & is particularly thorny when the proxy    //
  //   created the enconding as the result of a transform.  Http 1.1   //
  //   purists would say that if proxy encodes something it's really   //
  //   a transfer-encoding and not a content-encoding but again this   //
  //   causes problems with 1.0 clients                                //
  ///////////////////////////////////////////////////////////////////////
  if (!accept_field) {
    if (is_identity_encoding) {
      if (!cached_accept_field) {
        return ((float)1.0);
      } else {
        return ((float)0.001);
      }
    } else {
      return ((float)-1.0);
    }
  }

  // handle special case where no content-encoding in response, but
  // request has an accept-encoding header, possibly with the identity
  // field, with a q value;
  if (!content_field) {
    if (!match_accept_content_encoding("identity", accept_field, &wildcard_present, &wildcard_q, &q)) {
      // CE was not returned, and AE does not have identity
      if (match_content_encoding(accept_field, "gzip") and match_content_encoding(cached_accept_field, "gzip")) {
        return 1.0f;
      }
      goto encoding_wildcard;
    }
    // use q from identity match

  } else {
    // "Accept-encoding must correctly handle multiple content encoding"
    // The combined quality factor is the product of all quality factors.
    // (Note that there may be other possible choice, eg, min(),
    // but I think multiplication is the best.)
    // For example, if "content-encoding: a, b", and quality factors
    // of a and b (in accept-encoding header) are q_a and q_b, resp,
    // then the combined quality factor is (q_a * q_b).
    // If any one of the content-encoding is not matched,
    // then the q value will not be changed.
    float combined_q = 1.0;
    for (c_value = c_values_list.head; c_value; c_value = c_value->next) {
      float this_q = -1.0;
      if (!match_accept_content_encoding(c_value->str, accept_field, &wildcard_present, &wildcard_q, &this_q)) {
        goto encoding_wildcard;
      }
      combined_q *= this_q;
    }
    q = combined_q;
  }

encoding_wildcard:
  // match the wildcard now //
  if ((q == -1.0) && (wildcard_present == true)) {
    q = wildcard_q;
  }
  /////////////////////////////////////////////////////////////////////////
  // there was an Accept-Encoding, but it didn't match anything, at      //
  // any quality level --- if this is an identity-coded document, that's //
  // still okay, but otherwise, this is just not a match at all.         //
  /////////////////////////////////////////////////////////////////////////
  if ((q == -1.0) && is_identity_encoding) {
    if (match_content_encoding(accept_field, "gzip")) {
      if (match_content_encoding(cached_accept_field, "gzip")) {
        return 1.0f;
      } else {
        // always try to fetch GZIP content if we have not tried sending AE before
        return -1.0f;
      }
    } else if (cached_accept_field && !match_content_encoding(cached_accept_field, "gzip")) {
      return 0.001f;
    } else {
      return -1.0f;
    }
  }
  //      q = (float)-1.0;
  return (q);
}

/**
  Match request Accept-Language with response Content-Language.

  Language matching is a little more complicated because of "ranges".
  First, no Accept-Language header or no Content-Language headers match
  with q of 1. Otherwise, loop over Content-Languages. If there is a
  match with a language in the Accept-Language field, keep track of
  how many characters were in the value. The q value for the longest
  range is returned. If there was no explicit match or a mismatch,
  try wildcard matching.

  @return quality (-1: no match, 0..1: poor..good).

*/
static inline bool
does_language_range_match(const char *range1, const char *range2)
{
  while (*range1 && *range2 && (ParseRules::ink_tolower(*range1) == ParseRules::ink_tolower(*range2))) {
    range1 += 1;
    range2 += 1;
  }

  // matches if range equals tag, or if range is a lang prefix of tag
  if ((((*range1 == NUL) && (*range2 == NUL)) || ((*range1 == NUL) && (*range2 == '-')))) {
    return true;
  }

  return false;
}

static inline bool
match_accept_content_language(const char *c_raw, MIMEField *accept_field, bool *wildcard_present, float *wildcard_q, float *q,
                              int *a_range_length)
{
  const char *a_raw;
  int a_raw_len;
  StrList a_values_list;
  Str *a_value;

  ink_assert(accept_field != nullptr);

  // loop over each language-range pattern //
  // TODO: Should we check the return value (count) here?
  accept_field->value_get_comma_list(&a_values_list);

  for (a_value = a_values_list.head; a_value; a_value = a_value->next) {
    a_raw     = a_value->str;
    a_raw_len = a_value->len;

    char *a_range;
    StrList a_param_list;

    HttpCompat::parse_semicolon_list(&a_param_list, a_raw, a_raw_len);
    float tq = HttpCompat::find_Q_param_in_strlist(&a_param_list);

    /////////////////////////////////////////////////////////////////////
    // This algorithm is a bit wierd --- the resulting Q factor is     //
    // the Q value corresponding to the LONGEST range field that       //
    // matched, or if none matched, then the Q value of any asterisk.  //
    // Also, if the lang value is "", meaning that no Content-Language //
    // was specified, this document matches all accept headers.        //
    /////////////////////////////////////////////////////////////////////
    if (a_param_list.head) {
      a_range         = (char *)a_param_list.head->str;
      *a_range_length = a_param_list.head->len;
    } else {
      continue;
    }

    if (is_asterisk(a_range)) {
      *wildcard_present = true;
      *wildcard_q       = HttpCompat::find_Q_param_in_strlist(&a_param_list);
      return true;
    } else if (does_language_range_match(a_range, c_raw)) {
      *q = tq;
      // This is disabled, so removed max_a_range_length from prototype
      //          if (*a_range_length > *max_a_range_length) {
      //              *q = tq;
      //              *max_a_range_length = *a_range_length;
      //          }
      return true;
    } else {
    }
  }

  return false;
}

// FIX: This code is icky, and i suspect wrong in places, particularly
//      beacuse parts of match_accept_content_language are commented out.
//      It looks like lots of hacks were done.  The code should probably
//      be updated to use the code in HttpCompat::match_accept_language.

float
HttpTransactCache::calculate_quality_of_accept_language_match(MIMEField *accept_field, MIMEField *content_field,
                                                              MIMEField *cached_accept_field)
{
  float q = -1.0;
  int a_range_length;
  bool wildcard_present = false;
  float wildcard_q      = 1.0;
  float min_q           = 1.0;
  bool match_found      = false;
  StrList c_values_list;
  Str *c_value;
  const char *c_raw, *a_raw, *ca_raw;
  int a_raw_len, ca_raw_len;

  // Bug 2393700 prefer exact matches
  if (accept_field && cached_accept_field) {
    a_raw  = accept_field->value_get(&a_raw_len);
    ca_raw = cached_accept_field->value_get(&ca_raw_len);
    if (a_raw && ca_raw && a_raw_len == ca_raw_len && !strncmp(a_raw, ca_raw, a_raw_len)) {
      Debug("http_alternate", "Exact match for ACCEPT LANGUAGE");
      return (float)1.001; // slightly higher weight to this guy
    }
  }

  if (!accept_field) {
    return (1.0);
  }
  // handle special case where no content-language in response, but
  // request has an accept-language header, possibly with the identity
  // field, with a q value;

  if (!content_field) {
    if (match_accept_content_language("identity", accept_field, &wildcard_present, &wildcard_q, &q, &a_range_length)) {
      goto language_wildcard;
    }
    Debug("http_match", "[calculate_quality_accept_language_match]: "
                        "response hdr does not have content-language.");
    return (1.0);
  }

  // loop over content languages //
  // TODO: Should we check the return value (count) here?
  content_field->value_get_comma_list(&c_values_list);
  for (c_value = c_values_list.head; c_value; c_value = c_value->next) {
    c_raw = c_value->str;

    // get Content-Language value //
    if (match_accept_content_language(c_raw, accept_field, &wildcard_present, &wildcard_q, &q, &a_range_length)) {
      min_q       = (min_q < q ? min_q : q);
      match_found = true;
    }
  }
  if (match_found) {
    q = min_q;
  } else {
    q = -1.0;
  }

language_wildcard:
  // match the wildcard now //
  if ((q == -1.0) && (wildcard_present == true)) {
    q = wildcard_q;
  }
  return (q);
}

/**
  If the cached object contains a Vary header, then the object only
  matches if ALL of the headers named in Vary are present in the new
  request, and these match the headers in the stored request.  We relax
  this rule to allow matches if neither the current nor original client
  headers contained a varying header. This is different from what is
  stated in the specs.

*/
Variability_t
HttpTransactCache::CalcVariability(OverridableHttpConfigParams *http_config_params, HTTPHdr *client_request,
                                   HTTPHdr *obj_client_request, HTTPHdr *obj_origin_server_response)
{
  ink_assert(http_config_params != nullptr);
  ink_assert(client_request != nullptr);
  ink_assert(obj_client_request != nullptr);
  ink_assert(obj_origin_server_response != nullptr);

  Variability_t variability = VARIABILITY_NONE;
  if (http_config_params->cache_enable_default_vary_headers || obj_origin_server_response->presence(MIME_PRESENCE_VARY)) {
    ///////////////////////////////////////////////////////////////////////
    // If the origin server sent a Vary header in the response, use that //
    // Vary, otherwise use the default. Ivry adds: However if the origin //
    // server was a non-compliant 1.1 and did not send a Vary header,    //
    // treat as 1.0 with no Vary header.                                 //
    ///////////////////////////////////////////////////////////////////////
    StrList vary_list;
    int num_vary_values = obj_origin_server_response->value_get_comma_list(MIME_FIELD_VARY, MIME_LEN_VARY, &vary_list);

    if (num_vary_values <= 0) { // no vary hdr, so use defaults if enabled
      const char *vary_values = nullptr;
      const char *content_type;
      int content_type_len;
      char type[32], subtype[32];

      content_type = obj_origin_server_response->value_get(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &content_type_len);

      if (content_type) {
        HttpCompat::parse_mime_type_with_len(content_type, content_type_len, type, subtype, sizeof(type), sizeof(subtype));
      } else {
        type[0]    = '\0';
        subtype[0] = '\0';
      }

      Debug("http_match", "      type = '%s', subtype = '%s'", type, subtype);

      if (http_config_params->cache_enable_default_vary_headers) {
        if (strcasecmp(type, "text") == 0) {
          Debug("http_match", "      Using default text vary headers");
          vary_values = http_config_params->cache_vary_default_text;
        } else if (strcasecmp(type, "image") == 0) {
          Debug("http_match", "      Using default image vary headers");
          vary_values = http_config_params->cache_vary_default_images;
        } else {
          Debug("http_match", "      Using default other vary headers");
          vary_values = http_config_params->cache_vary_default_other;
        }
      }
      // convert the comma-sep string from the config var into a list
      HttpCompat::parse_comma_list(&vary_list, (vary_values ? vary_values : ""));
    }

    if (is_debug_tag_set("http_match") && (vary_list.head)) {
      Debug("http_match", "Vary list of %d elements", vary_list.count);
      vary_list.dump(stderr);
    }

    // for each field that varies, see if current & original hdrs match //
    for (Str *field = vary_list.head; field != nullptr; field = field->next) {
      if (field->len == 0) {
        continue;
      }

      /////////////////////////////////////////////////////////////
      // If the field name is unhandled, we should probably do a //
      // string comparison on the values of this extension field //
      // but currently we just treat it equivalent to a '*'.     //
      /////////////////////////////////////////////////////////////

      Debug("http_match", "Vary: %s", field->str);
      if (((field->str[0] == '*') && (field->str[1] == NUL))) {
        Debug("http_match", "Wildcard variability --- object not served from cache");
        variability = VARIABILITY_ALL;
        break;
      }
      ////////////////////////////////////////////////////////////////////////////////////////
      // Special case: if 'proxy.config.http.global_user_agent_header' set                  //
      // we should ignore Vary: User-Agent.                                                 //
      ////////////////////////////////////////////////////////////////////////////////////////
      if (http_config_params->global_user_agent_header && !strcasecmp((char *)field->str, "User-Agent")) {
        continue;
      }

      // Disable Vary mismatch checking for Accept-Encoding.  This is only safe to
      // set if you are promising to fix any Accept-Encoding/Content-Encoding mismatches.
      if (http_config_params->ignore_accept_encoding_mismatch && !strcasecmp((char *)field->str, "Accept-Encoding")) {
        continue;
      }

      ///////////////////////////////////////////////////////////////////
      // Take the current vary field and look up the headers in        //
      // the current client, and the original client.  The cached      //
      // object varies unless BOTH the current client and the original //
      // client contain the header, and the header values are equal.   //
      // We relax this to allow a match if NEITHER have the header.    //
      //                                                               //
      // While header "equality" appears to be header-specific, the    //
      // RFC2068 spec implies that matching only needs to account for  //
      // differences in whitespace and support for multiple headers    //
      // with the same name.  Case is presumably also insignificant.   //
      // Other variations (such as q=1 vs. a field with no q factor)   //
      // mean that the values DO NOT match.                            //
      ///////////////////////////////////////////////////////////////////

      ink_assert(strlen(field->str) == field->len);

      char *field_name_str = (char *)hdrtoken_string_to_wks(field->str, field->len);
      if (field_name_str == nullptr) {
        field_name_str = (char *)field->str;
      }

      MIMEField *cached_hdr_field  = obj_client_request->field_find(field_name_str, field->len);
      MIMEField *current_hdr_field = client_request->field_find(field_name_str, field->len);

      // Header values match? //
      if (!HttpCompat::do_header_values_rfc2068_14_43_match(cached_hdr_field, current_hdr_field)) {
        variability = VARIABILITY_SOME;
        break;
      }
    }
  }

  return variability;
}

/**
  If the request has If-modified-since or If-none-match,
  HTTP_STATUS_NOT_MODIFIED is returned if both or the existing one
  (if only one exists) fails; otherwise, the response's status code
  is returned.

  If the request has If-unmodified-since or If-match,
  HTTP_STATUS_PRECONDITION_FAILED is returned if one fails; otherwise,
  the response's status code is returned.

  If the request is a RANGE request with If-range,
  HTTP_STATUS_RANGE_NOT_SATISFIABLE is returned if the If-range condition
  is not satisfied (or fails); that means the document is changed and
  the whole document should be returned with 200 status code. Otherwise,
  the response's status code is returned.

  @return status code: HTTP_STATUS_NOT_MODIFIED,
    HTTP_STATUS_PRECONDITION_FAILED, or HTTP_STATUS_RANGE_NOT_SATISFIABLE.

*/
HTTPStatus
HttpTransactCache::match_response_to_request_conditionals(HTTPHdr *request, HTTPHdr *response, ink_time_t response_received_time)
{
  HTTPStatus response_code = HTTP_STATUS_NONE;

  ink_assert(response->status_get() != HTTP_STATUS_NOT_MODIFIED);
  ink_assert(response->status_get() != HTTP_STATUS_PRECONDITION_FAILED);
  ink_assert(response->status_get() != HTTP_STATUS_RANGE_NOT_SATISFIABLE);

  if (!(request->presence(MIME_PRESENCE_IF_MODIFIED_SINCE | MIME_PRESENCE_IF_NONE_MATCH | MIME_PRESENCE_IF_UNMODIFIED_SINCE |
                          MIME_PRESENCE_IF_MATCH | MIME_PRESENCE_RANGE))) {
    return response->status_get();
  }

  // If-None-Match: may match weakly //
  if (request->presence(MIME_PRESENCE_IF_NONE_MATCH)) {
    int raw_etags_len, comma_sep_tag_list_len;
    const char *raw_etags          = response->value_get(MIME_FIELD_ETAG, MIME_LEN_ETAG, &raw_etags_len);
    const char *comma_sep_tag_list = nullptr;

    if (raw_etags) {
      comma_sep_tag_list = request->value_get(MIME_FIELD_IF_NONE_MATCH, MIME_LEN_IF_NONE_MATCH, &comma_sep_tag_list_len);
      if (!comma_sep_tag_list) {
        comma_sep_tag_list     = "";
        comma_sep_tag_list_len = 0;
      }

      ////////////////////////////////////////////////////////////////////////
      // If we have an etag and a if-none-match, we are talking to someone  //
      // who is doing a 1.1 revalidate. Since this is a GET request with no //
      // sub-ranges, we can do a weak validation.                           //
      ////////////////////////////////////////////////////////////////////////
      if (do_strings_match_weakly(raw_etags, raw_etags_len, comma_sep_tag_list, comma_sep_tag_list_len)) {
        return HTTP_STATUS_NOT_MODIFIED;
      } else {
        return response->status_get();
      }
    }
  }

  // If-Modified-Since //
  if (request->presence(MIME_PRESENCE_IF_MODIFIED_SINCE)) {
    if (response->presence(MIME_PRESENCE_LAST_MODIFIED)) {
      ink_time_t lm_value = response->get_last_modified();

      // we won't return NOT_MODIFIED if Last-modified is too recent
      if ((lm_value == 0) || (request->get_if_modified_since() < lm_value)) {
        return response->status_get();
      }

      response_code = HTTP_STATUS_NOT_MODIFIED;
    } else if (response->presence(MIME_PRESENCE_DATE)) {
      ink_time_t date_value = response->get_date();

      // we won't return NOT_MODIFIED if Date is too recent
      if ((date_value == 0) || (request->get_if_modified_since() < date_value)) {
        return response->status_get();
      }

      response_code = HTTP_STATUS_NOT_MODIFIED;
    } else {
      // we won't return NOT_MODIFIED if received time is too recent
      if (request->get_if_modified_since() < response_received_time) {
        return response->status_get();
      }

      response_code = HTTP_STATUS_NOT_MODIFIED;
    }
  }

  // There is no If-none-match, and If-modified-since failed,
  // so return NOT_MODIFIED
  if (response_code != HTTP_STATUS_NONE) {
    return response_code;
  }

  // If-Match: must match strongly //
  if (request->presence(MIME_PRESENCE_IF_MATCH)) {
    int raw_etags_len, comma_sep_tag_list_len;
    const char *raw_etags          = response->value_get(MIME_FIELD_ETAG, MIME_LEN_ETAG, &raw_etags_len);
    const char *comma_sep_tag_list = nullptr;

    if (raw_etags) {
      comma_sep_tag_list = request->value_get(MIME_FIELD_IF_MATCH, MIME_LEN_IF_MATCH, &comma_sep_tag_list_len);
    }

    if (!comma_sep_tag_list) {
      comma_sep_tag_list     = "";
      comma_sep_tag_list_len = 0;
    }

    if (!raw_etags) {
      raw_etags     = "";
      raw_etags_len = 0;
    }

    if (do_strings_match_strongly(raw_etags, raw_etags_len, comma_sep_tag_list, comma_sep_tag_list_len)) {
      return response->status_get();
    } else {
      return HTTP_STATUS_PRECONDITION_FAILED;
    }
  }

  // If-Unmodified-Since //
  if (request->presence(MIME_PRESENCE_IF_UNMODIFIED_SINCE)) {
    // lm_value is zero if Last-modified not exists
    ink_time_t lm_value = response->get_last_modified();

    // Condition fails if Last-modified not exists
    if ((request->get_if_unmodified_since() < lm_value) || (lm_value == 0)) {
      return HTTP_STATUS_PRECONDITION_FAILED;
    } else {
      response_code = response->status_get();
    }
  }

  // There is no If-match, and If-unmodified-since passed,
  // so return the original response code
  if (response_code != HTTP_STATUS_NONE) {
    return response_code;
  }

  // Handling If-Range header:
  // As Range && If-Range don't occur often, we want to put the
  // If-Range code in the end
  if (request->presence(MIME_PRESENCE_RANGE) && request->presence(MIME_PRESENCE_IF_RANGE)) {
    int raw_len, comma_sep_list_len;

    const char *if_value = request->value_get(MIME_FIELD_IF_RANGE, MIME_LEN_IF_RANGE, &comma_sep_list_len);

    // this is an ETag, similar to If-Match
    if (!if_value || if_value[0] == '"' || (comma_sep_list_len > 1 && if_value[1] == '/')) {
      if (!if_value) {
        if_value           = "";
        comma_sep_list_len = 0;
      }

      const char *raw_etags = response->value_get(MIME_FIELD_ETAG, MIME_LEN_ETAG, &raw_len);

      if (!raw_etags) {
        raw_etags = "";
        raw_len   = 0;
      }

      if (do_strings_match_strongly(raw_etags, raw_len, if_value, comma_sep_list_len)) {
        return response->status_get();
      } else {
        return HTTP_STATUS_RANGE_NOT_SATISFIABLE;
      }
    }
    // this a Date, similar to If-Unmodified-Since
    else {
      // lm_value is zero if Last-modified not exists
      ink_time_t lm_value = response->get_last_modified();

      // condition fails if Last-modified not exists
      if ((request->get_if_range_date() < lm_value) || (lm_value == 0)) {
        return HTTP_STATUS_RANGE_NOT_SATISFIABLE;
      } else {
        return response->status_get();
      }
    }
  }

  return response->status_get();
}
