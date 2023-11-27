/*
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

#include "cripts/Lulu.hpp"
#include "cripts/Preamble.hpp"

Matcher::PCRE::~PCRE()
{
  for (auto &it : _regexes) {
    auto &[_, re] = it;

    pcre2_code_free(re);
  }
}

void *
Matcher::PCRE::Result::malloc(PCRE2_SIZE size, void *context)
{
  auto *result   = static_cast<Result *>(context);
  std::byte *ret = result->_ctx_data + result->_ctx_ix;

  TSReleaseAssert(size < (sizeof(result->_ctx_data) - result->_ctx_ix));
  result->_ctx_ix += size;

  return ret;
}

void
Matcher::PCRE::add(Cript::string_view regex, uint32_t options, bool jit)
{
  int errorcode          = 0;
  PCRE2_SIZE erroroffset = 0;
  pcre2_code *re =
    pcre2_compile(reinterpret_cast<PCRE2_SPTR>(regex.data()), regex.length(), options, &errorcode, &erroroffset, nullptr);

  if (!re) {
    PCRE2_UCHAR error[256];

    pcre2_get_error_message(errorcode, error, sizeof(error));
    TSError("PCRE compile error `%s': %.*s", error, static_cast<int>(regex.length()), regex.data());
    TSReleaseAssert(!"Failed to compile regex");
  } else {
    if (jit) {
      if (0 != pcre2_jit_compile(re, PCRE2_JIT_COMPLETE)) {
        TSError("PCRE JIT compile error: %.*s", static_cast<int>(regex.length()), regex.data());
        TSReleaseAssert(!"Failed to JIT compile regex");
      }
    }
    _regexes.emplace_back(regex, re);
  }
}

Matcher::PCRE::Result
Matcher::PCRE::contains(Cript::string_view subject, PCRE2_SIZE offset, uint32_t options)
{
  Matcher::PCRE::Result res(subject);
  pcre2_general_context *ctx = pcre2_general_context_create(
    &Matcher::PCRE::Result::malloc, [](void *, void *) -> void {}, static_cast<void *>(&res));

  for (auto &it : _regexes) {
    auto &[_, re] = it;
    res._data     = pcre2_match_data_create_from_pattern(re, ctx);
    int ret = pcre2_match(re, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.length(), offset, options, res._data, nullptr);

    if (ret >= 0) {
      res._ovector = pcre2_get_ovector_pointer(res._data);
      res._match   = &it - &_regexes[0] + 1; // This is slightly odd, but it's one more than the zero-based indexes.
      break;
    }
  }

  return res;
}
