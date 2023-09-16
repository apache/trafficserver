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

#include <regex.h>
#include "common.h"
#include <stdbool.h>
#include <string.h>

bool
match_hash(const char *needle, const char *haystack)
{
  return false;
}

bool
match_regex(const char *pattern, const char *uri)
{
  struct re_pattern_buffer pat_buff;

  pat_buff.translate = 0;
  pat_buff.fastmap   = 0;
  pat_buff.buffer    = 0;
  pat_buff.allocated = 0;

  re_syntax_options = RE_SYNTAX_POSIX_MINIMAL_EXTENDED;

  PluginDebug("Testing regex pattern /%s/ against \"%s\"", pattern, uri);

  const char *comp_err = re_compile_pattern(pattern, strlen(pattern), &pat_buff);

  if (comp_err) {
    PluginDebug("Regex Compilation ERROR: %s", comp_err);
    return false;
  }

  int match_ret;
  match_ret = re_match(&pat_buff, uri, strlen(uri), 0, 0);
  regfree(&pat_buff);

  return match_ret >= 0;
}
