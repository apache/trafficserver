/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cookie.h"
#include "common.h"
#include <ts/ts.h>
#include <string.h>

const char *
next_cookie(const char *cookie, size_t *cookie_ct, const char **k, size_t *k_ct, const char **v, size_t *v_ct)
{
  if (!k || !k_ct || !v || !v_ct || !cookie_ct || !*cookie_ct) {
    return NULL;
  }
  const char *end = cookie + *cookie_ct;

  while (cookie != end && (*cookie == ' ' || *cookie == '\t' || *cookie == '\v')) {
    ++cookie;
  }

  *k = cookie;

  while (cookie != end && *cookie != '=' && *cookie != ';') {
    ++cookie;
  }

  if (cookie == end || *cookie != '=') {
    /* Cookies that don't have an equal are treated as values, not keys. */
    *v    = *k;
    *v_ct = cookie - *v;
    *k    = NULL;
    *k_ct = 0;
    goto done;
  }

  *k_ct = cookie - *k;
  ++cookie;
  *v = cookie;

  while (cookie != end && *cookie != ';') {
    ++cookie;
  }

  *v_ct = cookie - *v;

done:
  PluginDebug("Checking next cookie with %ld bytes of key and %ld bytes of value", *k_ct, *v_ct);
  if (cookie != end) {
    ++cookie;
  }
  *cookie_ct = end - cookie;
  return cookie;
}

const char *
get_cookie_value(const char **cookie, size_t *cookie_ct, const char *key, size_t *ct)
{
  PluginDebug("Parsing cookie %.*s looking for %s", (int)*cookie_ct, *cookie, key);
  const char *k, *v;
  size_t k_ct, v_ct;
  size_t key_ct = strlen(key);
  while ((*cookie = next_cookie(*cookie, cookie_ct, &k, &k_ct, &v, &v_ct))) {
    PluginDebug("Checking cookie '%.*s' '%.*s'", (int)k_ct, k, (int)v_ct, v);
    if (key_ct == k_ct && (k_ct == 0 || !strncmp(k, key, k_ct))) {
      PluginDebug("Found value for %s: (%p)%.*s", key, v, (int)v_ct, v);
      *ct = v_ct;
      return v;
    }
  }
  *ct = 0;
  return NULL;
}
