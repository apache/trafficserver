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

#include <array>

#include "tscore/ink_platform.h"
#include "tscore/ink_thread.h"
#include "tscore/ink_memory.h"
#include "tscore/Regex.h"

#if defined(PCRE_CONFIG_JIT) && !defined(darwin) // issue with macOS Catalina and pcre 8.43
struct RegexThreadKey {
  RegexThreadKey() { ink_thread_key_create(&this->key, reinterpret_cast<void (*)(void *)>(&pcre_jit_stack_free)); }
  ink_thread_key key;
};

static RegexThreadKey k;

static pcre_jit_stack *
get_jit_stack(void *data ATS_UNUSED)
{
  pcre_jit_stack *jit_stack;

  if ((jit_stack = static_cast<pcre_jit_stack *>(ink_thread_getspecific(k.key))) == nullptr) {
    jit_stack = pcre_jit_stack_alloc(ats_pagesize(), 1024 * 1024); // 1 page min and 1MB max
    ink_thread_setspecific(k.key, (void *)jit_stack);
  }

  return jit_stack;
}
#endif

Regex::Regex(Regex &&that) noexcept : regex(that.regex), regex_extra(that.regex_extra)
{
  that.regex       = nullptr;
  that.regex_extra = nullptr;
}

bool
Regex::compile(const char *pattern, const unsigned flags)
{
  const char *error;
  int erroffset;
  int options    = 0;
  int study_opts = 0;

  if (regex) {
    return false;
  }

  if (flags & RE_CASE_INSENSITIVE) {
    options |= PCRE_CASELESS;
  }

  if (flags & RE_ANCHORED) {
    options |= PCRE_ANCHORED;
  }

  regex = pcre_compile(pattern, options, &error, &erroffset, nullptr);
  if (error) {
    regex = nullptr;
    return false;
  }

#if defined(PCRE_CONFIG_JIT) && !defined(darwin) // issue with macOS Catalina and pcre 8.43
  study_opts |= PCRE_STUDY_JIT_COMPILE;
#endif

  regex_extra = pcre_study(regex, study_opts, &error);

#if defined(PCRE_CONFIG_JIT) && !defined(darwin) // issue with macOS Catalina and pcre 8.43
  if (regex_extra) {
    pcre_assign_jit_stack(regex_extra, &get_jit_stack, nullptr);
  }
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
Regex::exec(std::string_view const &str)
{
  std::array<int, DEFAULT_GROUP_COUNT * 3> ovector = {{0}};
  return this->exec(str, ovector.data(), ovector.size());
}

bool
Regex::exec(std::string_view const &str, int *ovector, int ovecsize)
{
  int rv;

  rv = pcre_exec(regex, regex_extra, str.data(), int(str.size()), 0, 0, ovector, ovecsize);
  return rv > 0;
}

Regex::~Regex()
{
  if (regex_extra) {
#if defined(PCRE_CONFIG_JIT) && !defined(darwin) // issue with macOS Catalina and pcre 8.43
    pcre_free_study(regex_extra);
#else
    pcre_free(regex_extra);
#endif
  }
  if (regex) {
    pcre_free(regex);
  }
}

DFA::~DFA() {}

bool
DFA::build(std::string_view const &pattern, unsigned flags)
{
  Regex rxp;
  std::string string{pattern};

  if (!(flags & RE_UNANCHORED)) {
    flags |= RE_ANCHORED;
  }

  if (!rxp.compile(string.c_str(), flags)) {
    return false;
  }
  _patterns.emplace_back(std::move(rxp), std::move(string));
  return true;
}

int
DFA::compile(std::string_view const &pattern, unsigned flags)
{
  ink_assert(_patterns.empty());
  this->build(pattern, flags);
  return _patterns.size();
}

int
DFA::compile(std::string_view *patterns, int npatterns, unsigned flags)
{
  _patterns.reserve(npatterns); // try to pre-allocate.
  for (int i = 0; i < npatterns; ++i) {
    this->build(patterns[i], flags);
  }
  return _patterns.size();
}

int
DFA::compile(const char **patterns, int npatterns, unsigned flags)
{
  _patterns.reserve(npatterns); // try to pre-allocate.
  for (int i = 0; i < npatterns; ++i) {
    this->build(patterns[i], flags);
  }
  return _patterns.size();
}

int
DFA::match(std::string_view const &str) const
{
  // This is ugly, but the external interface needs to be @c const even though it's not really.
  // This handles making the iterator non-const.
  auto &pv{const_cast<decltype(_patterns) &>(_patterns)};
  for (auto spot = pv.begin(), limit = pv.end(); spot != limit; ++spot) {
    if (spot->_re.exec(str)) {
      return spot - _patterns.begin();
    }
  }

  return -1;
}
