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

//----------------------------------------------------------------------------
namespace
{
void *
my_malloc(size_t size, void * /*caller*/)
{
  void *ptr = malloc(size);
  return ptr;
}

void
my_free(void *ptr, void * /*caller*/)
{
  free(ptr);
}
} // namespace

//----------------------------------------------------------------------------
class RegexContext
{
public:
  static RegexContext *
  get_instance()
  {
    if (!_regex_context) {
      _regex_context = new RegexContext();
    }
    return _regex_context;
  }
  ~RegexContext()
  {
    if (_general_context != nullptr) {
      pcre2_general_context_free(_general_context);
    }
    if (_compile_context != nullptr) {
      pcre2_compile_context_free(_compile_context);
    }
    if (_match_context != nullptr) {
      pcre2_match_context_free(_match_context);
    }
    if (_jit_stack != nullptr) {
      pcre2_jit_stack_free(_jit_stack);
    }
  }
  pcre2_general_context *
  get_general_context()
  {
    return _general_context;
  }
  pcre2_compile_context *
  get_compile_context()
  {
    return _compile_context;
  }
  pcre2_match_context *
  get_match_context()
  {
    return _match_context;
  }

private:
  RegexContext()
  {
    _general_context = pcre2_general_context_create(my_malloc, my_free, nullptr);
    _compile_context = pcre2_compile_context_create(_general_context);
    _match_context   = pcre2_match_context_create(_general_context);
    _jit_stack       = pcre2_jit_stack_create(4096, 1024 * 1024, nullptr); // 1 page min and 1MB max
    pcre2_jit_stack_assign(_match_context, nullptr, _jit_stack);
  }
  pcre2_general_context *_general_context = nullptr;
  pcre2_compile_context *_compile_context = nullptr;
  pcre2_match_context *_match_context     = nullptr;
  pcre2_jit_stack *_jit_stack             = nullptr;
  thread_local static RegexContext *_regex_context;
};

thread_local RegexContext *RegexContext::_regex_context = nullptr;

//----------------------------------------------------------------------------
namespace
{
struct RegexContextCleanup {
  ~RegexContextCleanup() { delete RegexContext::get_instance(); }
};
thread_local RegexContextCleanup cleanup;
} // namespace

//----------------------------------------------------------------------------
RegexMatches::RegexMatches(uint32_t size)
{
  pcre2_general_context *ctx = pcre2_general_context_create(
    &RegexMatches::malloc, [](void *, void *) -> void {}, static_cast<void *>(this));

  _match_data = pcre2_match_data_create(size, ctx);
}

//----------------------------------------------------------------------------
void *
RegexMatches::malloc(size_t size, void *caller)
{
  auto *matches = static_cast<RegexMatches *>(caller);

  // allocate from the buffer if possible
  if (size <= sizeof(matches->_buffer) - matches->_buffer_bytes_used) {
    void *ptr                    = matches->_buffer + matches->_buffer_bytes_used;
    matches->_buffer_bytes_used += size;
    return ptr;
  }

  // otherwise use system malloc if the buffer is too small
  void *ptr = ::malloc(size);
  return ptr;
}

//----------------------------------------------------------------------------
RegexMatches::~RegexMatches()
{
  if (_match_data != nullptr) {
    pcre2_match_data_free(_match_data);
  }
}

//----------------------------------------------------------------------------
size_t *
RegexMatches::get_ovector_pointer()
{
  return pcre2_get_ovector_pointer(_match_data);
}

//----------------------------------------------------------------------------
int32_t
RegexMatches::size() const
{
  return _size;
}

//----------------------------------------------------------------------------
pcre2_match_data *
RegexMatches::get_match_data()
{
  return _match_data;
}

//----------------------------------------------------------------------------
void
RegexMatches::set_size(int32_t size)
{
  _size = size;
}

//----------------------------------------------------------------------------
void
RegexMatches::set_subject(std::string_view subject)
{
  _subject = subject;
}

//----------------------------------------------------------------------------
std::string_view
RegexMatches::operator[](size_t index) const
{
  // check if the index is valid
  if (index >= pcre2_get_ovector_count(_match_data)) {
    return std::string_view();
  }

  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(_match_data);
  return std::string_view(_subject.data() + ovector[2 * index], ovector[2 * index + 1] - ovector[2 * index]);
}

//----------------------------------------------------------------------------
Regex::Regex(Regex &&that) noexcept
{
  _code      = that._code;
  that._code = nullptr;
}

//----------------------------------------------------------------------------
Regex::~Regex()
{
  if (_code != nullptr) {
    pcre2_code_free(_code);
  }
}

//----------------------------------------------------------------------------
bool
Regex::compile(std::string_view pattern, uint32_t flags)
{
  std::string error;
  int erroroffset;

  return this->compile(pattern, error, erroroffset, flags);
}

//----------------------------------------------------------------------------
bool
Regex::compile(std::string_view pattern, std::string &error, int &erroroffset, uint32_t flags)
{
  if (_code != nullptr) {
    pcre2_code_free(_code);
  }
  PCRE2_SIZE error_offset;
  int error_code;
  _code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), flags, &error_code, &error_offset,
                        RegexContext::get_instance()->get_compile_context());
  if (!_code) {
    erroroffset = error_offset;

    // get pcre2 error message
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(error_code, buffer, sizeof(buffer));
    error.assign((char *)buffer);
    return false;
  }

  // support for JIT
  pcre2_jit_compile(_code, PCRE2_JIT_COMPLETE);

  return true;
}

//----------------------------------------------------------------------------
bool
Regex::exec(std::string_view subject) const
{
  if (_code == nullptr) {
    return false;
  }
  RegexMatches matches;

  int count = this->exec(subject, matches);
  return count > 0;
}

//----------------------------------------------------------------------------
int32_t
Regex::exec(std::string_view subject, RegexMatches &matches) const
{
  if (_code == nullptr) {
    return 0;
  }
  int count = pcre2_match(_code, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), 0, 0, matches.get_match_data(),
                          RegexContext::get_instance()->get_match_context());

  matches.set_size(count);

  if (count < 0) {
    return count;
  }

  if (count > 0) {
    matches.set_subject(subject);
  }

  return count;
}

//----------------------------------------------------------------------------
int32_t
Regex::get_capture_count()
{
  int captures = -1;
  if (pcre2_pattern_info(_code, PCRE2_INFO_CAPTURECOUNT, &captures) != 0) {
    return -1;
  }
  return captures;
}

//----------------------------------------------------------------------------
DFA::~DFA() {}

//----------------------------------------------------------------------------
bool
DFA::build(const std::string_view pattern, unsigned flags)
{
  Regex rxp;
  std::string string{pattern};

  if (!(flags & RE_UNANCHORED)) {
    flags |= RE_ANCHORED;
  }

  if (!rxp.compile(pattern, flags)) {
    return false;
  }
  _patterns.emplace_back(std::move(rxp), std::move(string));
  return true;
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(std::string_view pattern, unsigned flags)
{
  assert(_patterns.empty());
  this->build(pattern, flags);
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(std::string_view *patterns, int npatterns, unsigned flags)
{
  _patterns.reserve(npatterns); // try to pre-allocate.
  for (int i = 0; i < npatterns; ++i) {
    this->build(patterns[i], flags);
  }
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(const char **patterns, int npatterns, unsigned flags)
{
  _patterns.reserve(npatterns); // try to pre-allocate.
  for (int i = 0; i < npatterns; ++i) {
    this->build(patterns[i], flags);
  }
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::match(std::string_view str) const
{
  for (auto spot = _patterns.begin(), limit = _patterns.end(); spot != limit; ++spot) {
    if (spot->_re.exec(str)) {
      return spot - _patterns.begin();
    }
  }

  return -1;
}
