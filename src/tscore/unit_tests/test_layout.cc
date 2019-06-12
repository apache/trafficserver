/** @file

  Test file for layout structure

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

#include "catch.hpp"

#include "tscore/I_Layout.h"
#include "tscore/ink_platform.h"

std::string
append_slash(const char *path)
{
  std::string ret(path);
  if (ret.back() != '/')
    ret.append("/");
  return ret;
}

// test cases: [constructor], [env_constructor], [create], [relative], [relative_to], [update_sysconfdir]
// ======= test for layout ========

TEST_CASE("constructor test", "[constructor]")
{
  Layout layout;
  // test for constructor
  REQUIRE(layout.prefix == TS_BUILD_PREFIX);
  REQUIRE(layout.sysconfdir == layout.relative(TS_BUILD_SYSCONFDIR));
}

TEST_CASE("environment variable constructor test", "[env_constructor]")
{
  std::string newpath = append_slash(TS_BUILD_PREFIX) + "env";
  setenv("TS_ROOT", newpath.c_str(), true);

  Layout layout;
  REQUIRE(layout.prefix == newpath);
  REQUIRE(layout.sysconfdir == layout.relative_to(newpath, TS_BUILD_SYSCONFDIR));
  unsetenv("TS_ROOT");
}

TEST_CASE("layout create test", "[create]")
{
  Layout::create();
  REQUIRE(Layout::get()->prefix == TS_BUILD_PREFIX);
  REQUIRE(Layout::get()->sysconfdir == Layout::get()->relative(TS_BUILD_SYSCONFDIR));
}

// tests below based on the created layout
TEST_CASE("relative test", "[relative]")
{
  // relative (1 argument)
  std::string_view sv("file");
  std::string str1 = append_slash(TS_BUILD_PREFIX) + "file";
  REQUIRE(Layout::get()->relative(sv) == str1);
}

TEST_CASE("relative to test", "[relative_to]")
{
  // relative to (2 parameters)
  std::string str1 = append_slash(TS_BUILD_PREFIX) + "file";
  REQUIRE(Layout::relative_to(Layout::get()->prefix, "file") == str1);

  // relative to (4 parameters)
  char config_file[PATH_NAME_MAX];
  Layout::relative_to(config_file, sizeof(config_file), Layout::get()->sysconfdir, "records.config");
  std::string a = Layout::relative_to(Layout::get()->sysconfdir, "records.config");
  std::string b = config_file;
  REQUIRE(a == b);
}

TEST_CASE("update_sysconfdir test", "[update_sysconfdir]")
{
  Layout::get()->update_sysconfdir("/abc");
  REQUIRE(Layout::get()->sysconfdir == "/abc");
}
