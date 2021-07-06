/** @file

  Validate hostname matches certificate according to RFC6125

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

#include <memory.h>
#include <strings.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "tscore/ink_memory.h"

using equal_fn = bool (*)(const unsigned char *, size_t, const unsigned char *, size_t);

/* Return a ptr to a valid wildcard or NULL if not found
 *
 * Using OpenSSL default flags:
 *   X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS = False
 *   X509_CHECK_FLAG_MULTI_LABEL_WILDCARDS  = False
 * At most one wildcard per pattern.
 * No wildcards inside IDNA labels (a full label match is ok:
 *                                  *.a.b matches xn--something-or-other.a.b .)
 * No wildcards after the first label.
 */

static const unsigned char *
find_wildcard_in_hostname(const unsigned char *p, size_t len, bool idna_subject)
{
  size_t i = 0;
  // Minimum wildcard length *.a.b
  if (len < 5) {
    return nullptr;
  }

  int wildcard_pos = -1;
  // Find last dot (can't be last) -- memrchr is GNU extension....
  size_t final_dot_pos = 0;
  for (i = len - 2; i > 1; i--) {
    if (p[i] == '.') {
      final_dot_pos = i;
      break;
    }
  }
  // Final dot minimal pos is a.b.xxxxxx
  if (final_dot_pos < 3) {
    return nullptr;
  }

  for (i = 0; i < final_dot_pos; i++) {
    /*
     * Make sure there are at least two '.' in the string
     */
    if (p[i] == '*') {
      if (wildcard_pos != -1) {
        // Multiple wildcards in first label
        break;
      } else if (i == 0 ||                                         // First char is wildcard
                 ((i < final_dot_pos - 1) && (p[i + 1] == '.'))) { // Found a trailing wildcard in the first label

        // IDNA hostnames must match a full label
        if (idna_subject && (i != 0 || p[i + 1] != '.')) {
          break;
        }

        wildcard_pos = i;
      } else {
        // Either mid-label wildcard or not enough dots
        break;
      }
    }
    // String contains at least two dots.
    if (p[i] == '.') {
      if (wildcard_pos != -1) {
        return &p[wildcard_pos];
      }
      // Only valid wildcard is in the first label
      break;
    }
  }
  return nullptr;
}

/*
 * Comparison functions
 * @param pattern is the value from the certificate
 * @param subject is the value from the client request
 */

/* Compare while ASCII ignoring case. */
static bool
equal_nocase(const unsigned char *pattern, size_t pattern_len, const unsigned char *subject, size_t subject_len)
{
  if (pattern_len != subject_len) {
    return false;
  }
  return (strncasecmp((char *)pattern, (char *)subject, pattern_len) == 0);
}

/* Compare using memcmp. */
static bool
equal_case(const unsigned char *pattern, size_t pattern_len, const unsigned char *subject, size_t subject_len)
{
  if (pattern_len != subject_len) {
    return false;
  }
  return (memcmp(pattern, subject, pattern_len) == 0);
}

/*
 * Compare the prefix and suffix with the subject, and check that the
 * characters in-between are valid.
 */
static bool
wildcard_match(const unsigned char *prefix, size_t prefix_len, const unsigned char *suffix, size_t suffix_len,
               const unsigned char *subject, size_t subject_len)
{
  const unsigned char *wildcard_start;
  const unsigned char *wildcard_end;
  const unsigned char *p;

  if (subject_len < prefix_len + suffix_len) {
    return false;
  }
  if (!equal_nocase(prefix, prefix_len, subject, prefix_len)) {
    return false;
  }
  wildcard_start = subject + prefix_len;
  wildcard_end   = subject + (subject_len - suffix_len);
  if (!equal_nocase(wildcard_end, suffix_len, suffix, suffix_len)) {
    return false;
  }
  /*
   * If the wildcard makes up the entire first label, it must match at
   * least one character.
   */
  if (prefix_len == 0 && *suffix == '.') {
    if (wildcard_start == wildcard_end) {
      return false;
    }
  }
  /* The wildcard may match a literal '*' */
  if (wildcard_end == wildcard_start + 1 && *wildcard_start == '*') {
    return true;
  }
  /*
   * Check that the part matched by the wildcard contains only
   * permitted characters and only matches a single label
   */
  for (p = wildcard_start; p != wildcard_end; ++p) {
    if (!(('a' <= *p && *p <= 'z') || ('A' <= *p && *p <= 'Z') || ('0' <= *p && *p <= '9') || *p == '-' || *p == '_')) {
      return false;
    }
  }
  return true;
}

/* Compare using wildcards. */
static bool
equal_wildcard(const unsigned char *pattern, size_t pattern_len, const unsigned char *subject, size_t subject_len)
{
  const unsigned char *wildcard = nullptr;

  bool is_idna = (subject_len > 4 && strncasecmp(reinterpret_cast<const char *>(subject), "xn--", 4) == 0);
  /*
   * Subject names starting with '.' can only match a wildcard pattern
   * via a subject sub-domain pattern suffix match (that we don't allow).
   */
  if (subject_len > 5 && subject[0] != '.') {
    wildcard = find_wildcard_in_hostname(pattern, pattern_len, is_idna);
  }

  if (wildcard == nullptr) {
    return equal_nocase(pattern, pattern_len, subject, subject_len);
  }
  return wildcard_match(pattern, wildcard - pattern, wildcard + 1, (pattern + pattern_len) - wildcard - 1, subject, subject_len);
}

/*
 * Compare an ASN1_STRING to a supplied string. only compare if string matches the specified type
 *
 * Returns true if the strings match, false otherwise
 */

static bool
do_check_string(ASN1_STRING *a, int cmp_type, equal_fn equal, const unsigned char *b, size_t blen, char **peername)
{
  bool retval = false;

  if (!a->data || !a->length || cmp_type != a->type) {
    return false;
  }
  retval = equal(a->data, a->length, b, blen);
  if (retval && peername) {
    *peername = ats_strndup((char *)a->data, a->length);
  }
  return retval;
}

bool
validate_hostname(X509 *x, const unsigned char *hostname, bool is_ip, char **peername)
{
  GENERAL_NAMES *gens = nullptr;
  X509_NAME *name     = nullptr;
  int i;
  int alt_type;
  bool retval = false;
  ;
  equal_fn equal;
  size_t hostname_len = strlen((char *)hostname);

  if (!is_ip) {
    alt_type = V_ASN1_IA5STRING;
    equal    = equal_wildcard;
  } else {
    alt_type = V_ASN1_OCTET_STRING;
    equal    = equal_case;
  }

  // Check SANs for a match.
  gens = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(x, NID_subject_alt_name, nullptr, nullptr));
  if (gens) {
    // BoringSSL has sk_GENERAL_NAME_num() return size_t.
    for (i = 0; i < static_cast<int>(sk_GENERAL_NAME_num(gens)); i++) {
      GENERAL_NAME *gen;
      ASN1_STRING *cstr;
      gen = sk_GENERAL_NAME_value(gens, i);

      if (is_ip && gen->type == GEN_IPADD) {
        cstr = gen->d.iPAddress;
      } else if (!is_ip && gen->type == GEN_DNS) {
        cstr = gen->d.dNSName;
      } else {
        continue;
      }

      if ((retval = do_check_string(cstr, alt_type, equal, hostname, hostname_len, peername)) == true) {
        // We got a match
        break;
      }
    }
    GENERAL_NAMES_free(gens);
    if (retval) {
      return retval;
    }
  }
  // No SAN match -- check the subject
  i    = -1;
  name = X509_get_subject_name(x);

  while ((i = X509_NAME_get_index_by_NID(name, NID_commonName, i)) >= 0) {
    ASN1_STRING *str;
    int astrlen;
    unsigned char *astr;
    str = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, i));
    // Convert to UTF-8
    astrlen = ASN1_STRING_to_UTF8(&astr, str);

    if (astrlen < 0) {
      return -1;
    }
    retval = equal(astr, astrlen, hostname, hostname_len);
    if (retval && peername) {
      *peername = ats_strndup((char *)astr, astrlen);
    }
    OPENSSL_free(astr);
    return retval;
  }
  return false;
}
