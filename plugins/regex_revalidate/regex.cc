/** @file

  PCRE support wrapper.

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

#include "regex.h"

#ifdef PCRE_CONFIG_JIT
#include <pthread.h>

struct RegexThreadKey {
  RegexThreadKey() { pthread_key_create(&this->key, reinterpret_cast<void (*)(void *)>(&pcre_jit_stack_free)); }
  pthread_key_t key;
};

static RegexThreadKey k;

static pcre_jit_stack *
get_jit_stack(void *)
{
  pcre_jit_stack *jit_stack;

  if ((jit_stack = static_cast<pcre_jit_stack *>(pthread_getspecific(k.key))) == nullptr) {
    jit_stack = pcre_jit_stack_alloc(8192, 1024 * 1024); // 1 page min and 1MB max
    pthread_setspecific(k.key, (void *)jit_stack);
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

  if (nullptr != this->regex) {
    return false;
  }

  if (flags & CASE_INSENSITIVE) {
    options |= PCRE_CASELESS;
  }

  if (flags & ANCHORED) {
    options |= PCRE_ANCHORED;
  }

  this->regex = pcre_compile(pattern, options, &error, &erroffset, nullptr);
  if (error) {
    this->regex = nullptr;
    return false;
  }

#ifdef PCRE_CONFIG_JIT
  study_opts |= PCRE_STUDY_JIT_COMPILE;
#endif

  this->regex_extra = pcre_study(this->regex, study_opts, &error);

#ifdef PCRE_CONFIG_JIT
  if (nullptr != this->regex_extra) {
    pcre_assign_jit_stack(this->regex_extra, &get_jit_stack, nullptr);
  }
#endif

  return true;
}

bool
Regex::matches(std::string_view const &src) const
{
  return 0 <= exec(src, nullptr, 0);
}

int
Regex::exec(std::string_view const &src, int *ovector, int const ovecsize) const
{
  return pcre_exec(this->regex, this->regex_extra, src.data(), src.size(), 0, 0, ovector, ovecsize);
}

Regex::~Regex()
{
  if (regex_extra) {
#ifdef PCRE_CONFIG_JIT
    pcre_free_study(regex_extra);
#else
    pcre_free(regex_extra);
#endif
  }
  if (regex) {
    pcre_free(regex);
  }
}
