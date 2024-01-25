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

#include "tsutil/Regex.h"

#include <array>
#include <assert.h>

#if __has_include(<pcre/pcre.h>)
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

namespace
{
inline pcre *
as_pcre(void *p)
{
  return static_cast<pcre *>(p);
}
inline pcre_extra *
as_extra(void *p)
{
  return static_cast<pcre_extra *>(p);
}
} // namespace

#ifdef PCRE_CONFIG_JIT
/*
Using two thread locals avoids the deadlock because without the thread local object access, get_jit_stack doesn't call
the TLS init function which ends up calling __cxx_thread_atexit(which locks the dl_whatever mutex). Since the raw
pointer doesn't have a destructor to call, it doesn't need to call this. Interestingly, get_jit_stack was calling the
TLS init function to setup the destructor call at thread exit whether or not the class was declared in the function
body.
*/
namespace
{
thread_local pcre_jit_stack *jit_stack;

struct JitStackCleanup {
  ~JitStackCleanup()
  {
    if (jit_stack) {
      pcre_jit_stack_free(jit_stack);
    }
  }
};

thread_local JitStackCleanup jsc;

pcre_jit_stack *
get_jit_stack(void *)
{
  if (!jit_stack) {
    jit_stack = pcre_jit_stack_alloc(4096, 1024 * 1024); // 1 page min and 1MB max
  }
  return jit_stack;
}

} // end anonymous namespace
#endif // def PCRE_CONFIG_JIT

Regex::Regex(Regex &&that) noexcept : regex(that.regex), regex_extra(that.regex_extra)
{
  that.regex       = nullptr;
  that.regex_extra = nullptr;
}

bool
Regex::compile(const char *pattern, const unsigned flags)
{
  const char *error = nullptr;
  int erroffset     = 0;
  return this->compile(pattern, &error, &erroffset, flags);
}

bool
Regex::compile(const char *pattern, const char **error, int *erroffset, const unsigned flags)
{
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

  regex = pcre_compile(pattern, options, error, erroffset, nullptr);
  if (error) {
    regex = nullptr;
    return false;
  }

#ifdef PCRE_CONFIG_JIT
  study_opts |= PCRE_STUDY_JIT_COMPILE;
#endif

  regex_extra = pcre_study(as_pcre(regex), study_opts, error);

#ifdef PCRE_CONFIG_JIT
  if (regex_extra) {
    pcre_assign_jit_stack(as_extra(regex_extra), &get_jit_stack, nullptr);
  }
#endif

  return true;
}

int
Regex::get_capture_count()
{
  int captures = -1;
  if (pcre_fullinfo(as_pcre(regex), as_extra(regex_extra), PCRE_INFO_CAPTURECOUNT, &captures) != 0) {
    return -1;
  }

  return captures;
}

bool
Regex::exec(std::string_view const &str) const
{
  int ovector[DEFAULT_GROUP_COUNT * 3];
  int rval = this->exec(str, ovector, DEFAULT_GROUP_COUNT * 3);
  return rval > 0;
}

int
Regex::exec(std::string_view const &str, int *ovector, int ovecsize) const
{
  return pcre_exec(as_pcre(regex), as_extra(regex_extra), str.data(), static_cast<int>(str.size()), 0, 0, ovector, ovecsize);
}

Regex::~Regex()
{
  if (regex_extra) {
#ifdef PCRE_CONFIG_JIT
    pcre_free_study(as_extra(regex_extra));
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
  assert(_patterns.empty());
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
  for (auto spot = _patterns.begin(), limit = _patterns.end(); spot != limit; ++spot) {
    if (spot->_re.exec(str)) {
      return spot - _patterns.begin();
    }
  }

  return -1;
}
