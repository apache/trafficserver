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

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <array>
#include <assert.h>
#include <vector>
#include <mutex>

static_assert(RE_CASE_INSENSITIVE == PCRE2_CASELESS, "Update RE_CASE_INSERSITIVE for current PCRE2 version.");
static_assert(RE_UNANCHORED == PCRE2_MULTILINE, "Update RE_MULTILINE for current PCRE2 version.");
static_assert(RE_ANCHORED == PCRE2_ANCHORED, "Update RE_ANCHORED for current PCRE2 version.");

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

class RegexContext; // defined below
class RegexContextCleanup
{
public:
  void push_back(RegexContext *ctx);
  ~RegexContextCleanup();

private:
  std::vector<RegexContext *> _contexts;
  std::mutex _mutex;
};
RegexContextCleanup regex_context_cleanup;

//----------------------------------------------------------------------------
class RegexContext
{
public:
  static RegexContext *
  get_instance()
  {
    if (_shutdown == true) {
      return nullptr;
    }

    if (_regex_context == nullptr) {
      _regex_context = new RegexContext();
      regex_context_cleanup.push_back(_regex_context);
    }
    return _regex_context;
  }
  ~RegexContext()
  {
    _shutdown = true;

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
  static bool _shutdown; // flag to indicate destructor was called, so no new instances can be created
};

thread_local RegexContext *RegexContext::_regex_context = nullptr;
bool RegexContext::_shutdown                            = false;

//----------------------------------------------------------------------------

RegexContextCleanup::~RegexContextCleanup()
{
  std::lock_guard<std::mutex> guard(_mutex);
  for (auto ctx : _contexts) {
    delete ctx;
  }
}
void
RegexContextCleanup::push_back(RegexContext *ctx)
{
  std::lock_guard<std::mutex> guard(_mutex);
  _contexts.push_back(ctx);
}

} // namespace

//----------------------------------------------------------------------------
struct RegexMatches::_MatchData {
  static pcre2_match_data *
  get(_MatchDataPtr const &p)
  {
    return static_cast<pcre2_match_data *>(p._ptr);
  }
  static void
  set(_MatchDataPtr &p, pcre2_match_data *ptr)
  {
    p._ptr = ptr;
  }
};

//----------------------------------------------------------------------------
RegexMatches::RegexMatches(uint32_t size)
{
  pcre2_general_context *ctx = pcre2_general_context_create(
    &RegexMatches::malloc, [](void *, void *) -> void {}, static_cast<void *>(this));


  pcre2_match_data *match_data = pcre2_match_data_create(size, ctx);
  if (match_data == nullptr) {
    // buffer was too small, allocate from heap
    match_data = pcre2_match_data_create(size, RegexContext::get_instance()->get_general_context());
  }

  _MatchData::set(_match_data, match_data);

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

  // return nullptr if buffer is too small
  return nullptr;
}

//----------------------------------------------------------------------------
RegexMatches::~RegexMatches()
{
  auto ptr = _MatchData::get(_match_data);
  if (ptr != nullptr) {
    pcre2_match_data_free(ptr);
  }
}

//----------------------------------------------------------------------------
size_t *
RegexMatches::get_ovector_pointer()
{
  return pcre2_get_ovector_pointer(_MatchData::get(_match_data));
}

//----------------------------------------------------------------------------
int32_t
RegexMatches::size() const
{
  return _size;
}

//----------------------------------------------------------------------------
std::string_view
RegexMatches::operator[](size_t index) const
{
  // check if the index is valid
  if (index >= pcre2_get_ovector_count(_MatchData::get(_match_data))) {
    return std::string_view();
  }

  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(_MatchData::get(_match_data));
  return std::string_view(_subject.data() + ovector[2 * index], ovector[2 * index + 1] - ovector[2 * index]);
}

//----------------------------------------------------------------------------
struct Regex::_Code {
  static pcre2_code *
  get(_CodePtr const &p)
  {
    return static_cast<pcre2_code *>(p._ptr);
  }
  static void
  set(_CodePtr &p, pcre2_code *ptr)
  {
    p._ptr = ptr;
  }
};

//----------------------------------------------------------------------------
Regex::Regex(Regex &&that) noexcept
{
  _code = that._code;
  _Code::set(that._code, nullptr);
}

//----------------------------------------------------------------------------
Regex::~Regex()
{
  auto ptr = _Code::get(_code);
  if (ptr != nullptr) {
    pcre2_code_free(ptr);
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
  // free the existing compiled regex if there is one
  if (auto ptr = _Code::get(_code); ptr != nullptr) {
    pcre2_code_free(ptr);
  }

  // get the RegexContext instance - should only be null when shutting down
  RegexContext *regex_context = RegexContext::get_instance();
  if (regex_context == nullptr) {
    return false;
  }

  PCRE2_SIZE error_offset;
  int error_code;
  auto code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), flags, &error_code, &error_offset,
                            regex_context->get_compile_context());
  if (!code) {
    erroroffset = error_offset;

    // get pcre2 error message
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(error_code, buffer, sizeof(buffer));
    error.assign((char *)buffer);
    return false;
  }

  // support for JIT
  pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);

  _Code::set(_code, code);

  return true;
}

//----------------------------------------------------------------------------
bool
Regex::exec(std::string_view subject) const
{
  if (_Code::get(_code) == nullptr) {
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
  auto code = _Code::get(_code);

  // check if there is a compiled regex
  if (code == nullptr) {
    return 0;
  }
  int count = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), 0, 0,
                          RegexMatches::_MatchData::get(matches._match_data), RegexContext::get_instance()->get_match_context());

  matches._size = count;

  if (count < 0) {
    return count;
  }

  if (count > 0) {
    matches._subject = subject;
  }

  return count;
}

//----------------------------------------------------------------------------
int32_t
Regex::get_capture_count()
{
  int captures = -1;
  if (pcre2_pattern_info(_Code::get(_code), PCRE2_INFO_CAPTURECOUNT, &captures) != 0) {
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
