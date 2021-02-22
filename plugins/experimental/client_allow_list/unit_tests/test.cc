/** @file

  SSL client certificate verification plugin, unit test for utility
  source file.

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

#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string_view>

#include <client_allow_list.h>

using client_allow_list_plugin::matcher;
using client_allow_list_plugin::other_matcher_idxs;
using client_allow_list_plugin::none_matcher_idxs;
using client_allow_list_plugin::sname_to_matcher_idxs;
using client_allow_list_plugin::Init;
using client_allow_list_plugin::check_name;

#define REQUIRE(EXPR) (require((EXPR), #EXPR, __LINE__))

#undef DIR
#define DIR "experimental/client_allow_list/unit_tests/"

namespace
{
void
require(bool expr, char const *expr_str, int line_num)
{
  if (!expr) {
    std::printf("FAILURE: %s line=%d\n", expr_str, line_num);
    std::exit(1);
  }
}

bool
matcher_idxs_same(std::vector<unsigned> const &idxs1, std::vector<unsigned> const &idxs2)
{
  if (idxs1.size() != idxs2.size()) {
    return false;
  }
  for (std::size_t i = 0; i < idxs1.size(); ++i) {
    if (idxs1[i] != idxs2[i]) {
      return false;
    }
  }
  return true;
}

char ut_printf_output[2 * 1024];
std::size_t ut_printf_output_size;

#define UT_CHECK_OUTPUT(S) ut_check_output((S), __LINE__)

void
ut_check_output(const char *expected, int line)
{
  if (std::string_view(ut_printf_output, ut_printf_output_size) != expected) {
    std::printf("FAILURE: unexpected output: code line=%d\n", line);
    std::printf("EXPECTED: %s\n", expected);
    std::printf("ACTUAL:   %.*s\n", static_cast<int>(ut_printf_output_size), ut_printf_output);
    std::exit(1);
  }
}

template <typename T>
void
reset(T &v)
{
  v.~T();
  ::new (&v) T();
}

// Reset globals between tests, so that multiple configurations can be tested in a single UT executable.
//
void
reset_globals()
{
  reset(sname_to_matcher_idxs);
  reset(none_matcher_idxs);
  reset(other_matcher_idxs);
  reset(matcher);

  ut_printf_output_size = 0;
}

// Returns true if error handled correctly.
//
bool
bad_config(int n_arg, char const *const *arg)
{
  bool caught{false};

  try {
    Init()(n_arg, arg);

  } catch (ClientAllowListUTException &) {
    caught = true;
  }
  REQUIRE(ut_printf_output_size < sizeof(ut_printf_output));

  return caught;
}

} // end anonymous namespace

void
ut_printf(char const *fmt, std::va_list args)
{
  int n = std::vsnprintf(ut_printf_output + ut_printf_output_size, sizeof(ut_printf_output) - ut_printf_output_size, fmt, args);
  REQUIRE(n > 0);
  ut_printf_output_size += n;
  REQUIRE(ut_printf_output_size < sizeof(ut_printf_output));
  ut_printf_output[ut_printf_output_size++] = '\n';
}

int
main()
{
  // Each sub-block is a test of a different configuation.

  // Tests of good configurations.

  {
    char const *args[] = {"dummy plugin name", "*.bbb"};
    Init()(2, args);

    REQUIRE(matcher.size() == 1);

    REQUIRE(sname_to_matcher_idxs.size() == 0);

    REQUIRE(other_matcher_idxs.size() == 1);
    REQUIRE(matcher_idxs_same(other_matcher_idxs, none_matcher_idxs));
    REQUIRE(check_name(other_matcher_idxs, "aaa.bbb"));
  }
  REQUIRE(!ut_printf_output_size);

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", "aaa", "*.bbb", "ccc.*", "ddd.*.eee"};
    Init()(5, args);

    REQUIRE(matcher.size() == 4);

    REQUIRE(sname_to_matcher_idxs.size() == 0);

    REQUIRE(other_matcher_idxs.size() == 4);
    REQUIRE(matcher_idxs_same(other_matcher_idxs, none_matcher_idxs));
    REQUIRE(check_name(other_matcher_idxs, "aaa"));
    REQUIRE(check_name(other_matcher_idxs, "ddd.xxx.eee"));
    REQUIRE(check_name(other_matcher_idxs, "aaa.bbb"));
  }
  REQUIRE(!ut_printf_output_size);

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "good1.yaml"};
    Init()(2, args);

    REQUIRE(matcher.size() == 7);

    REQUIRE(sname_to_matcher_idxs.size() == 6);

    {
      REQUIRE(none_matcher_idxs.size() == 4);
      auto m2 = sname_to_matcher_idxs.find("yahoo.com");
      REQUIRE(m2 != nullptr);
      REQUIRE(matcher_idxs_same(*m2, none_matcher_idxs));
      REQUIRE(check_name(none_matcher_idxs, "aaa"));
      REQUIRE(!check_name(none_matcher_idxs, "aa"));
      REQUIRE(!check_name(none_matcher_idxs, "aab"));
      REQUIRE(check_name(none_matcher_idxs, "ddd.xxx.eee"));
      REQUIRE(!check_name(none_matcher_idxs, "ddd.xxx.efe"));
      REQUIRE(check_name(none_matcher_idxs, "aaa.bbb"));
    }
    {
      auto m = sname_to_matcher_idxs.find("xxx");
      REQUIRE(m != nullptr);
      REQUIRE(m->size() == 0);
      REQUIRE(!check_name(*m, "anything.com"));
    }
    {
      REQUIRE(other_matcher_idxs.size() == 2);
      auto m2 = sname_to_matcher_idxs.find("huffpost.com");
      REQUIRE(m2 != nullptr);
      REQUIRE(matcher_idxs_same(*m2, other_matcher_idxs));
      auto m3 = sname_to_matcher_idxs.find("aOl.CoM");
      REQUIRE(m3 != nullptr);
      REQUIRE(matcher_idxs_same(*m3, other_matcher_idxs));
      REQUIRE(check_name(other_matcher_idxs, "fff"));
      REQUIRE(check_name(other_matcher_idxs, "ff.bbb"));
    }
    {
      auto m = sname_to_matcher_idxs.find("uuu");
      REQUIRE(m != nullptr);
      REQUIRE(m->size() == 1);
      REQUIRE(check_name(*m, "vvv"));
    }
    {
      auto m = sname_to_matcher_idxs.find("yyy");
      REQUIRE(m != nullptr);
      REQUIRE(m->size() == 1);
      REQUIRE(check_name(*m, "anything.com"));
    }
    {
      auto m = sname_to_matcher_idxs.find("not_there");
      REQUIRE(nullptr == m);
    }
  }
  REQUIRE(!ut_printf_output_size);

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "good2.yaml"};
    Init()(2, args);

    REQUIRE(matcher.size() == 1);

    REQUIRE(sname_to_matcher_idxs.size() == 1);

    REQUIRE(none_matcher_idxs.size() == 0);
    REQUIRE(other_matcher_idxs.size() == 0);

    {
      auto m = sname_to_matcher_idxs.find("uuu");
      REQUIRE(m != nullptr);
      REQUIRE(m->size() == 1);
      REQUIRE(check_name(*m, "vvv"));
    }
  }
  REQUIRE(!ut_printf_output_size);

  // Tests of bad configurations.

  reset_globals();
  {
    char const *args[] = {"dummy plugin name"};
    REQUIRE(bad_config(1, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: must provide at least one plugin parameter\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", "aaa*bbb*ccc"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: bad certificate name pattern aaa*bbb*ccc\n"
                  "client_allow_list: fatal error\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "not-there.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: YAML::Exception \"bad file\" when parsing YAML config file ./" DIR "not-there.yaml\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad1.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: YAML config file ./" DIR "bad1.yaml is empty\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad2.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: YAML::Exception \"invalid node; this may result from using a map"
                  " iterator as a sequence iterator, or vice-versa\" when parsing YAML config file ./" DIR "bad2.yaml\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad3.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: empty server name list\n"
                  "client_allow_list: config error: file=./" DIR "bad3.yaml line=2 column=1\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad4.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: blank space not allowed in server name list\n"
                  "client_allow_list: config error: file=./" DIR "bad4.yaml line=2 column=1\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad5.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: <none> used more than once\n"
                  "client_allow_list: config error: file=./" DIR "bad5.yaml line=4 column=1\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad6.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: <other> used more than once\n"
                  "client_allow_list: config error: file=./" DIR "bad6.yaml line=4 column=1\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad7.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: cert names for SNI server name \"twice\" previously specified\n"
                  "client_allow_list: config error: file=./" DIR "bad7.yaml line=4 column=1\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad8.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: YAML::Exception \"yaml-cpp: error at line 4, column 3: end of map"
                  " not found\" when parsing YAML config file ./" DIR "bad8.yaml\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad9.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: YAML::Exception \"yaml-cpp: error at line 3, column 3: bad conversion\""
                  " when parsing YAML config file ./" DIR "bad9.yaml\n");

  reset_globals();
  {
    char const *args[] = {"dummy plugin name", DIR "bad10.yaml"};
    REQUIRE(bad_config(2, args));
  }
  UT_CHECK_OUTPUT("client_allow_list: YAML::Exception \"yaml-cpp: error at line 4, column 5: the referenced "
                  "anchor is not defined\" when parsing YAML config file ./" DIR "bad10.yaml\n");

  return 0;
}

#include <../src/tscore/HashFNV.cc>
#include <../src/tscore/Hash.cc>
