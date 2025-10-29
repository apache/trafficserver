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

#include <tsutil/Regex.h>
#include <tsutil/Assert.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <array>
#include <vector>
#include <mutex>
#include <utility>

static_assert(RE_CASE_INSENSITIVE == PCRE2_CASELESS, "Update RE_CASE_INSENSITIVE for current PCRE2 version.");
static_assert(RE_UNANCHORED == PCRE2_MULTILINE, "Update RE_UNANCHORED for current PCRE2 version.");
static_assert(RE_ANCHORED == PCRE2_ANCHORED, "Update RE_ANCHORED for current PCRE2 version.");
static_assert(RE_NOTEMPTY == PCRE2_NOTEMPTY, "Update RE_NOTEMPTY for current PCRE2 version.");

static_assert(RE_ERROR_NOMATCH == PCRE2_ERROR_NOMATCH, "Update RE_ERROR_NOMATCH for current PCRE2 version.");
static_assert(RE_ERROR_NULL == PCRE2_ERROR_NULL, "Update RE_ERROR_NULL for current PCRE2 version.");

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

//----------------------------------------------------------------------------
class RegexContext
{
public:
  static RegexContext *
  get_instance()
  {
    thread_local RegexContext ctx;
    return &ctx;
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
  pcre2_match_context   *_match_context   = nullptr;
  pcre2_jit_stack       *_jit_stack       = nullptr;
};

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
  pcre2_general_context *ctx = pcre2_general_context_create(&RegexMatches::malloc, &RegexMatches::free, static_cast<void *>(this));

  pcre2_match_data *match_data = pcre2_match_data_create(size, ctx);
  debug_assert_message(match_data, "Failed to allocate pcre2 match data from custom context");

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

  return ::malloc(size);
}

//----------------------------------------------------------------------------
void
RegexMatches::free(void *p, void *caller)
{
  auto *matches = static_cast<RegexMatches *>(caller);

  // Call free for any p outside _buffer
  // If the pcre2 context requests more data than fits in our builtin buffer, we will call malloc
  // to fulfil that request.
  // !his checks for any pointers outside of our buffer in order to free that memory up.
  //
  // nullptr is outside of our buffer, but its ok to call ::free with nullptr.
  if (!(p >= matches->_buffer && p < matches->_buffer + sizeof(matches->_buffer))) {
    ::free(p);
  }
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
struct RegexMatchContext::_MatchContext {
  static pcre2_match_context *
  get(_MatchContextPtr const &p)
  {
    return static_cast<pcre2_match_context *>(p._ptr);
  }
  static void
  set(_MatchContextPtr &p, pcre2_match_context *ptr)
  {
    p._ptr = ptr;
  }
};

//----------------------------------------------------------------------------
RegexMatchContext::RegexMatchContext()
{
  auto ctx = pcre2_match_context_create(nullptr);
  debug_assert_message(ctx, "Failed to allocate custom pcre2 match context");
  _MatchContext::set(_match_context, ctx);
}

//----------------------------------------------------------------------------
RegexMatchContext::RegexMatchContext(RegexMatchContext const &other)
{
  auto ptr = _MatchContext::get(other._match_context);
  if (nullptr != ptr) {
    pcre2_match_context *const ctx = pcre2_match_context_copy(ptr);
    _MatchContext::set(_match_context, ctx);
  }
}

//----------------------------------------------------------------------------
RegexMatchContext &
RegexMatchContext::operator=(RegexMatchContext const &other)
{
  if (&other != this) {
    auto ptr = _MatchContext::get(other._match_context);
    if (nullptr != ptr) {
      pcre2_match_context *const ctx = pcre2_match_context_copy(ptr);
      _MatchContext::set(_match_context, ctx);
    } else {
      _MatchContext::set(_match_context, nullptr);
    }
  }
  return *this;
}

//----------------------------------------------------------------------------
RegexMatchContext::~RegexMatchContext()
{
  auto ptr = _MatchContext::get(_match_context);
  if (ptr != nullptr) {
    pcre2_match_context_free(ptr);
  }
}

//----------------------------------------------------------------------------
void
RegexMatchContext::setMatchLimit(uint32_t limit)
{
  auto ptr = _MatchContext::get(_match_context);
  if (ptr != nullptr) {
    pcre2_set_match_limit(ptr, limit);
  }
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
Regex::Regex(Regex const &other)
{
  auto *other_code = _Code::get(other._code);
  if (other_code != nullptr) {
    // Use PCRE2's built-in function to deep copy the compiled pattern
    auto *copied_code = pcre2_code_copy(other_code);
    _Code::set(_code, copied_code);
  }
}

//----------------------------------------------------------------------------
Regex &
Regex::operator=(Regex const &other)
{
  if (this != &other) {
    // Use copy-and-swap idiom: create a temporary copy, then swap with it
    Regex temp(other); // Copy constructor does the deep copy

    // Swap the internal pointers
    std::swap(_code, temp._code);
    // temp's destructor will clean up our old _code
  }
  return *this;
}

//----------------------------------------------------------------------------
Regex::Regex(Regex &&that) noexcept
{
  _code = that._code;
  _Code::set(that._code, nullptr);
}

//----------------------------------------------------------------------------
Regex &
Regex::operator=(Regex &&other)
{
  if (this != &other) {
    auto ptr = _Code::get(_code);
    if (ptr != nullptr) {
      pcre2_code_free(ptr);
    }
    _code = other._code;
    _Code::set(other._code, nullptr);
  }
  return *this;
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
  int         erroroffset;

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
  int        error_code;
  auto       code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(), flags, &error_code, &error_offset,
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
  return this->exec(subject, 0);
}

//----------------------------------------------------------------------------
bool
Regex::exec(std::string_view subject, uint32_t flags) const
{
  if (_Code::get(_code) == nullptr) {
    return false;
  }
  RegexMatches matches;

  int count = this->exec(subject, matches, flags);
  return count >= 0;
}

//----------------------------------------------------------------------------
int32_t
Regex::exec(std::string_view subject, RegexMatches &matches) const
{
  return this->exec(subject, matches, 0);
}

//----------------------------------------------------------------------------
int32_t
Regex::exec(std::string_view subject, RegexMatches &matches, uint32_t flags, RegexMatchContext const *const matchContext) const
{
  auto code = _Code::get(_code);

  // check if there is a compiled regex
  if (code == nullptr) {
    return PCRE2_ERROR_NULL;
  }

  // Use the provided or the thread global context?
  auto const match_context = [&]() -> pcre2_match_context * {
    if (nullptr == matchContext) {
      return RegexContext::get_instance()->get_match_context();
    } else {
      return RegexMatchContext::_MatchContext::get(matchContext->_match_context);
    }
  }();

  int count = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), 0, flags,
                          RegexMatches::_MatchData::get(matches._match_data), match_context);

  matches._size = count;

  // match was successful
  if (count >= 0) {
    matches._subject = subject;

    // match but the output vector was too small, adjust the size of the matches
    if (count == 0) {
      matches._size = pcre2_get_ovector_count(RegexMatches::_MatchData::get(matches._match_data));
    }
  }

  return count;
}

//----------------------------------------------------------------------------
int32_t
Regex::captureCount() const
{
  uint32_t captures = 0;
  if (pcre2_pattern_info(_Code::get(_code), PCRE2_INFO_CAPTURECOUNT, &captures) != 0) {
    return -1;
  }
  return static_cast<int32_t>(captures);
}

//----------------------------------------------------------------------------
int32_t
Regex::backrefMax() const
{
  uint32_t refs = 0;
  if (pcre2_pattern_info(_Code::get(_code), PCRE2_INFO_BACKREFMAX, &refs) != 0) {
    return -1;
  }
  return static_cast<int32_t>(refs);
}

//----------------------------------------------------------------------------
bool
Regex::empty() const
{
  return _Code::get(_code) == nullptr;
}

//----------------------------------------------------------------------------
DFA::~DFA() {}

//----------------------------------------------------------------------------
bool
DFA::build(const std::string_view pattern, unsigned flags)
{
  Regex       rxp;
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
DFA::compile(const std::string_view pattern, unsigned flags)
{
  release_assert(_patterns.empty());
  this->build(pattern, flags);
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(const std::string_view *const patterns, int npatterns, unsigned flags)
{
  _patterns.reserve(npatterns); // try to pre-allocate.
  for (int i = 0; i < npatterns; ++i) {
    this->build(patterns[i], flags);
  }
  return _patterns.size();
}

//----------------------------------------------------------------------------
int32_t
DFA::compile(const char *const *patterns, int npatterns, unsigned flags)
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
