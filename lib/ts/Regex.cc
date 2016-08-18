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
#include "ts/ink_thread.h"
#include "ts/ink_memory.h"
#include "ts/Regex.h"

#ifdef PCRE_CONFIG_JIT
struct RegexThreadKey {
  RegexThreadKey() { ink_thread_key_create(&this->key, (void (*)(void *)) & pcre_jit_stack_free); }
  ink_thread_key key;
};

static RegexThreadKey k;

static pcre_jit_stack *
get_jit_stack(void *data ATS_UNUSED)
{
  pcre_jit_stack *jit_stack;

  if ((jit_stack = (pcre_jit_stack *)ink_thread_getspecific(k.key)) == NULL) {
    jit_stack = pcre_jit_stack_alloc(ats_pagesize(), 1024 * 1024); // 1 page min and 1MB max
    ink_thread_setspecific(k.key, (void *)jit_stack);
  }

  return jit_stack;
}
#endif

bool
Regex::compile(const char *pattern, const unsigned flags)
{
  const char *error;
  int erroffset;
  int options    = 0;
  int study_opts = 0;

  if (regex)
    return false;

  if (flags & RE_CASE_INSENSITIVE) {
    options |= PCRE_CASELESS;
  }

  if (flags & RE_ANCHORED) {
    options |= PCRE_ANCHORED;
  }

  regex = pcre_compile(pattern, options, &error, &erroffset, NULL);
  if (error) {
    regex = NULL;
    return false;
  }

#ifdef PCRE_CONFIG_JIT
  study_opts |= PCRE_STUDY_JIT_COMPILE;
#endif

  regex_extra = pcre_study(regex, study_opts, &error);

#ifdef PCRE_CONFIG_JIT
  if (regex_extra)
    pcre_assign_jit_stack(regex_extra, &get_jit_stack, NULL);
#endif

  return true;
}

int
Regex::get_capture_count()
{
  int captures = -1;
  if (pcre_fullinfo(regex, regex_extra, PCRE_INFO_CAPTURECOUNT, &captures) != 0) {
    return -1;
  }

  return captures;
}

bool
Regex::exec(const char *str)
{
  return exec(str, strlen(str));
}

bool
Regex::exec(const char *str, int length)
{
  int ovector[30];
  return exec(str, length, ovector, countof(ovector));
}

bool
Regex::exec(const char *str, int length, int *ovector, int ovecsize)
{
  int rv;

  rv = pcre_exec(regex, regex_extra, str, length, 0, 0, ovector, ovecsize);
  return rv > 0 ? true : false;
}

Regex::~Regex()
{
  if (regex_extra)
#ifdef PCRE_CONFIG_JIT
    pcre_free_study(regex_extra);
#else
    pcre_free(regex_extra);
#endif
  if (regex)
    pcre_free(regex);
}

DFA::~DFA()
{
  dfa_pattern *p = _my_patterns;
  dfa_pattern *t;

  while (p) {
    if (p->_re)
      delete p->_re;
    if (p->_p)
      ats_free(p->_p);
    t = p->_next;
    ats_free(p);
    p = t;
  }
}

dfa_pattern *
DFA::build(const char *pattern, unsigned flags)
{
  dfa_pattern *ret;
  int rv;

  if (!(flags & RE_UNANCHORED)) {
    flags |= RE_ANCHORED;
  }

  ret     = (dfa_pattern *)ats_malloc(sizeof(dfa_pattern));
  ret->_p = NULL;

  ret->_re = new Regex();
  rv       = ret->_re->compile(pattern, flags);
  if (rv == -1) {
    delete ret->_re;
    ats_free(ret);
    return NULL;
  }

  ret->_idx  = 0;
  ret->_p    = ats_strndup(pattern, strlen(pattern));
  ret->_next = NULL;
  return ret;
}

int
DFA::compile(const char *pattern, unsigned flags)
{
  ink_assert(_my_patterns == NULL);
  _my_patterns = build(pattern, flags);
  if (_my_patterns)
    return 0;
  else
    return -1;
}

int
DFA::compile(const char **patterns, int npatterns, unsigned flags)
{
  const char *pattern;
  dfa_pattern *ret = NULL;
  dfa_pattern *end = NULL;
  int i;

  for (i = 0; i < npatterns; i++) {
    pattern = patterns[i];
    ret     = build(pattern, flags);
    if (!ret) {
      continue;
    }

    if (!_my_patterns) {
      _my_patterns        = ret;
      _my_patterns->_next = NULL;
      _my_patterns->_idx  = i;
    } else {
      end = _my_patterns;
      while (end->_next) {
        end = end->_next;
      }
      end->_next = ret; // add to end
      ret->_idx  = i;
    }
  }

  return 0;
}

int
DFA::match(const char *str) const
{
  return match(str, strlen(str));
}

int
DFA::match(const char *str, int length) const
{
  int rc;
  dfa_pattern *p = _my_patterns;

  while (p) {
    rc = p->_re->exec(str, length);
    if (rc > 0) {
      return p->_idx;
    }
    p = p->_next;
  }

  return -1;
}
