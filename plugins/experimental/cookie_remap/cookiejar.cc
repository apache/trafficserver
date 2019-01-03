/*
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

#include "cookiejar.h"
#include "strip.h"
#include <string.h>

/* allowed cookie-name definition from RFC
 * cookie-name       = token
 * token             = <token, defined in [RFC2616], Section 2.2>
 * token             = 1*<any CHAR except CTLs or separators>
 * separators        = "(" | ")" | "<" | ">" | "@"
 *                   | "," | ";" | ":" | "\" | <">
 *                   | "/" | "[" | "]" | "?" | "="
 *                   | "{" | "}" | SP | HT
 * CTL               = <any US-ASCII control character
 *                     (octets 0 - 31) and DEL (127)>
 * SP                = <US-ASCII SP, space (32)>
 * HT                = <US-ASCII HT, horizontal-tab (9)>
 */

static const int rfc_cookie_name_table[256] = {
  /* 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F            */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 00-0F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 10-1F */
  0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, /* 20-2F */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, /* 30-3F */
  0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 40-4F */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, /* 50-5F */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 60-6F */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, /* 70-7F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 80-8F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 90-9F */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* A0-AF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* B0-BF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* C0-CF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* D0-DF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* E0-EF */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* F0-FF */
};

bool
CookieJar::create(const string &strCookie)
{
  if (strCookie.empty()) {
    return false;
  }

  if (parse(strCookie, "; ", true, true) != 0) {
    return false;
  }

  return true;
}

int
CookieJar::parse(const string &arg, const char *sepstr, bool val_check, bool mainElement)
{
  char *arg_copy;
  char *cp;
  char *key;

  if ((arg_copy = strdup(arg.c_str())) == nullptr)
    return -1;

  cp           = arg_copy;
  char empty[] = "";
  for (key = strsep(&cp, sepstr); key != nullptr; key = strsep(&cp, sepstr)) {
    int val_len;
    char *val   = strchr(key, '=');
    char *addme = nullptr;

    if (val) {
      /* split key and value */
      *val++  = '\0';
      val_len = strlen(val);
      if (val_len > 0) {
        /* if we have DQUOTES around our value then drop them */

        if (val_len > 1 && val[0] == '"' && val[val_len - 1] == '"') {
          val[val_len - 1] = '\0';
          addme            = val + 1;

          /* update the value length accordingly */

          val_len -= 2;
        } else {
          addme = val; /* We have got a valid value eg: "YL=a" */
        }

        /* verify that the value is valid according to our configured
         * opton and possibly strip out invalid characters. */

        if (val_check && verify_value(addme, val_len) != 0)
          continue;
      } else {
        // Empty cookie case eg: "L="
        addme = empty;
      }
    } else /* If val is nullptr, then no need to add this kv pair to hashtable, skip this key */
    {
      continue;
    }

    /* we are going to verify the key name for our top level
     * cookie names only so we'll use the val_check variables
     * to know what we're processing */

    if (val_check && verify_name(key) != 0)
      continue;

    if (mainElement)
      addElement(key, addme);
    else
      addSubElement(key, addme);
  }

  free(arg_copy);
  return 0;
}

void
CookieJar::addElement(const char *key, const char *val)
{
  /* The insert method avoids duplicates */
  CookieVal cval;
  cval.m_val = val;
  m_jar.insert(std::make_pair(key, cval));
}

void
CookieJar::addSubElement(const char *key, const char *val)
{
  /* The insert method avoids duplicates */
  m_currentVal->m_subelements.insert(std::make_pair(key, val));
}

int
CookieJar::verify_value(char *val, int val_len)
{
  char *buf;
  char *data_ptr      = nullptr;
  char data_buf[1024] = {
    0,
  };
  int buf_len;

  if (val_len > static_cast<int>(sizeof(data_buf) - 1)) {
    buf_len = val_len + 1;
    if ((data_ptr = static_cast<char *>(malloc(buf_len))) == nullptr)
      return -1;

    buf = data_ptr;
  } else {
    buf     = data_buf;
    buf_len = sizeof(data_buf);
  }

  if (get_stripped(val, val_len, buf, &buf_len, 0) != STRIP_RESULT_OK) {
    if (data_ptr)
      free(data_ptr);
    return -1;
  }

  /* returned buf_len includes the null terminator
   * so we need to verify that somehow we didn't end up
   * with a bigger buffer than what we started with */

  if (buf_len > val_len + 1) {
    if (data_ptr)
      free(data_ptr);
    return -1;
  }

  memcpy(val, buf, buf_len);
  if (data_ptr)
    free(data_ptr);

  return 0;
}

int
CookieJar::verify_name(char *name)
{
  char *p;

  for (p = name; *p; p++) {
    /* if we get any invalid characters then return failure
     * in order to skip this cookie completely */

    if (rfc_cookie_name_table[(int)*p] == 0)
      return -1;
  }

  return 0;
}

bool
CookieJar::get_full(const string &cookie_name, string &val)
{
  if (m_jar.find(cookie_name) != m_jar.end()) {
    val = m_jar[cookie_name].m_val;
    return true;
  }
  return false;
}

bool
CookieJar::get_part(const string &cookie_name, const string &part_name, string &val)
{
  if (m_jar.empty())
    return false;

  if (m_jar.find(cookie_name) == m_jar.end()) {
    /* full cookie not found */
    return false;
  }

  CookieVal &fe = m_jar[cookie_name];
  /* check if we need to do lazy evaluation */
  if (fe.parts_inited == false) {
    /* since we already validated the value for the full cookie
     * there is no need to validate the components again so
     * we'll be passing 0/false for the val_check argument */

    m_currentVal = &fe;
    if (parse(fe.m_val, "&", false, false) != 0)
      return false;

    fe.parts_inited = true;
    m_currentVal    = nullptr;
  }

  if (fe.m_subelements.find(part_name) == fe.m_subelements.end()) {
    return false; /* not found */
  }

  val = fe.m_subelements[part_name];
  return true;
}
