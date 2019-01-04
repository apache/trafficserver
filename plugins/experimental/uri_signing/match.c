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

#include "common.h"
#include "ts/ts.h"
#include <stdbool.h>
#include <pcre.h>
#include <string.h>

bool
match_glob(const char *needle, const char *haystack)
{
  return false;
}

bool
match_regex(const char *pattern, const char *uri)
{
  const char *err;
  int err_off;
  PluginDebug("Testing regex pattern /%s/ against \"%s\"", pattern, uri);
  pcre *re = pcre_compile(pattern, PCRE_ANCHORED | PCRE_UCP | PCRE_UTF8, &err, &err_off, NULL);
  if (!re) {
    PluginDebug("Regex /%s/ failed to compile.", pattern);
    return false;
  }

  int rc = pcre_exec(re, NULL, uri, strlen(uri), 0, 0, NULL, 0);
  pcre_free(re);
  return rc >= 0;
}
