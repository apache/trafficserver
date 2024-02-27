/** @file

    swoc::file unit tests.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <iostream>
#include <unordered_map>
#include <fstream>

#include "swoc/swoc_file.h"
#include "catch.hpp"

using namespace swoc;
using namespace swoc::literals;

// --------------------

static TextView
set_env_var(TextView name, TextView value = ""_tv) {
  TextView zret;
  if (nullptr != getenv(name.data())) {
    zret.assign(value);
  }

  if (!value.empty()) {
    setenv(name.data(), value.data(), 1);
  } else {
    unsetenv(name.data());
  }

  return zret;
}

// --------------------
TEST_CASE("swoc_file", "[libswoc][swoc_file]") {
  file::path p1("/home");
  REQUIRE(p1.string() == "/home");
  auto p2 = p1 / "bob";
  REQUIRE(p2.string() == "/home/bob");
  p2 = p2 / "git/ats/";
  REQUIRE(p2.string() == "/home/bob/git/ats/");
  p2 /= "lib/ts";
  REQUIRE(p2.string() == "/home/bob/git/ats/lib/ts");
  p2 /= "/home/dave";
  REQUIRE(p2.string() == "/home/dave");
  auto p3 = file::path("/home/dave") / "git/tools";
  REQUIRE(p3.string() == "/home/dave/git/tools");
  REQUIRE(p3.parent_path().string() == "/home/dave/git");
  REQUIRE(p3.parent_path().parent_path().string() == "/home/dave");
  REQUIRE(p1.parent_path().string() == "/");

  REQUIRE(p1 == p1);
  REQUIRE(p1 != p2);

  // This is primarily to check working with std::string and file::path.
  std::string s1{"/home/evil/dave"};
  file::path fp{s1};
  std::error_code ec;
  [[maybe_unused]] auto mtime = file::last_write_time(s1, ec);
  REQUIRE(ec.value() != 0);

  fp = s1; // Make sure this isn't ambiguous

  // Verify path can be used as a hashed key for STL containers.
  [[maybe_unused]] std::unordered_map<file::path, std::string> container;
}

TEST_CASE("swoc_file_io", "[libswoc][swoc_file_io]") {
  file::path file("unit_tests/test_swoc_file.cc");
  std::error_code ec;
  std::string content = swoc::file::load(file, ec);
  REQUIRE(ec.value() == 0);
  REQUIRE(content.size() > 0);
  REQUIRE(content.find("swoc::file::path") != content.npos);

  // Check some file properties.
  REQUIRE(swoc::file::is_readable(file) == true);
  auto fs = swoc::file::status(file, ec);
  REQUIRE(ec.value() == 0);
  REQUIRE(swoc::file::is_dir(fs) == false);
  REQUIRE(swoc::file::is_regular_file(fs) == true);

  // See if converting to absolute works (at least somewhat).
  REQUIRE(file.is_relative());
  auto abs{swoc::file::absolute(file, ec)};
  REQUIRE(ec.value() == 0);
  REQUIRE(abs.is_absolute());
  fs = swoc::file::status(abs, ec); // needs to be the same as for @a file
  REQUIRE(ec.value() == 0);
  REQUIRE(swoc::file::is_dir(fs) == false);
  REQUIRE(swoc::file::is_regular_file(fs) == true);

  // Failure case.
  file    = "../unit-tests/no_such_file.txt";
  content = swoc::file::load(file, ec);
  REQUIRE(ec.value() == 2);
  REQUIRE(swoc::file::is_readable(file) == false);

  file::path f1{"/etc/passwd"};
  file::path f2("/etc/init.d");
  file::path f3("/dev/null");
  file::path f4("/argle/bargle");
  REQUIRE(file::exists(f1));
  REQUIRE(file::exists(f2));
  REQUIRE(file::exists(f3));
  REQUIRE_FALSE(file::exists(f4));
  fs = file::status(f1, ec);
  REQUIRE(file::exists(fs));
  fs = file::status(f4, ec);
  REQUIRE_FALSE(file::exists(fs));
  REQUIRE_FALSE(file::exists(file::file_status{}));
}

TEST_CASE("path::filename", "[libswoc][file]") {
  CHECK(file::path("/foo/bar.txt").filename() == file::path("bar.txt"));
  CHECK(file::path("/foo/.bar").filename() == file::path(".bar"));
  CHECK(file::path("/foo/bar").filename() == file::path("bar"));
  CHECK(file::path("/foo/bar/").filename() == file::path(""));
  CHECK(file::path("/foo/.").filename() == file::path("."));
  CHECK(file::path("/foo/..").filename() == file::path(".."));
  CHECK(file::path("/foo/../bar").filename() == file::path("bar"));
  CHECK(file::path("/foo/../bar/").filename() == file::path(""));
  CHECK(file::path(".").filename() == file::path("."));
  CHECK(file::path("..").filename() == file::path(".."));
  CHECK(file::path("/").filename() == file::path(""));
  CHECK(file::path("//host").filename() == file::path("host"));

  CHECK(file::path("/alpha/bravo").relative_path() == file::path("alpha/bravo"));
  CHECK(file::path("alpha/bravo").relative_path() == file::path("alpha/bravo"));
}

TEST_CASE("swoc::file::temp_directory_path", "[libswoc][swoc_file]") {
  // Clean all temp dir env variables and save the values.
  std::string s1{set_env_var("TMPDIR")};
  std::string s2{set_env_var("TEMPDIR")};
  std::string s3{set_env_var("TMP")};
  std::string s;

  // If nothing defined return "/tmp"
  CHECK(file::temp_directory_path() == file::path("/tmp"));

  // TMPDIR defined.
  set_env_var("TMPDIR", "/temp_alpha");
  CHECK(file::temp_directory_path().view() == "/temp_alpha");
  set_env_var("TMPDIR"); // clear

  // TEMPDIR
  set_env_var("TEMPDIR", "/temp_bravo");
  CHECK(file::temp_directory_path().view() == "/temp_bravo");
  // TMP defined, it should take precedence over TEMPDIR.
  set_env_var("TMP", "/temp_alpha");
  CHECK(file::temp_directory_path() == file::path("/temp_alpha"));
  // TMPDIR defined, it should take precedence over TMP.
  s = set_env_var("TMPDIR", "/temp_charlie");
  CHECK(file::temp_directory_path() == file::path("/temp_charlie"));
  set_env_var("TMPDIR", s);
  set_env_var("TMP", s);
  set_env_var("TEMPDIR", s);

  // Restore all temp dir env variables to their previous state.
  set_env_var("TMPDIR", s1);
  set_env_var("TEMPDIR", s2);
  set_env_var("TMP", s3);
}

TEST_CASE("file::path::create_directories", "[libswoc][swoc_file]") {
  std::error_code ec;
  file::path tempdir = file::temp_directory_path();

  CHECK_FALSE(file::create_directory(file::path(), ec));
  CHECK(ec.value() == EINVAL);
  CHECK_FALSE(file::create_directories(file::path(), ec));

  file::path testdir1 = tempdir / "dir1";
  CHECK(file::create_directories(testdir1, ec));
  CHECK(file::exists(testdir1));

  file::path testdir2 = testdir1 / "dir2";
  CHECK(file::create_directories(testdir2, ec));
  CHECK(file::exists(testdir1));

  // Cleanup
  CHECK(file::remove_all(testdir1, ec) == 2);
  CHECK_FALSE(file::exists(testdir1));
}

TEST_CASE("ts_file::path::remove", "[libswoc][fs_file]") {
  std::error_code ec;
  file::path tempdir = file::temp_directory_path();

  CHECK_FALSE(file::remove(file::path(), ec));
  CHECK(ec.value() == EINVAL);

  file::path testdir1 = tempdir / "dir1";
  file::path testdir2 = testdir1 / "dir2";
  file::path file1    = testdir2 / "alpha.txt";
  file::path file2    = testdir2 / "bravo.txt";
  file::path file3    = testdir2 / "charlie.txt";

  // Simple creation and removal of a directory /tmp/dir1
  CHECK(file::create_directories(testdir1, ec));
  CHECK(file::exists(testdir1));
  CHECK(file::remove(testdir1, ec));
  CHECK_FALSE(file::exists(testdir1));

  // Create /tmp/dir1/dir2 and remove /tmp/dir1/dir2 => /tmp/dir1 should exist
  CHECK(file::create_directories(testdir2, ec));
  CHECK(file::remove(testdir2, ec));
  CHECK(file::exists(testdir1));

  // Create a file, remove it, test if exists and then attempting to remove it again should fail.
  CHECK(file::create_directories(testdir2, ec));
  auto creatfile = [](char const *name) {
    std::ofstream out(name);
    out << "Simple test file " << name << std::endl;
    out.close();
  };
  creatfile(file1.c_str());
  creatfile(file2.c_str());
  creatfile(file3.c_str());

  CHECK(file::exists(file1));
  CHECK(file::remove(file1, ec));
  CHECK_FALSE(file::exists(file1));
  CHECK_FALSE(file::remove(file1, ec));

  // Clean up.
  CHECK_FALSE(file::remove(testdir1, ec));
  CHECK(file::remove_all(testdir1, ec) == 4);
  CHECK_FALSE(file::exists(testdir1));
}

TEST_CASE("file::path::canonical", "[libswoc][swoc_file]") {
  std::error_code ec;
  file::path tempdir    = file::canonical(file::temp_directory_path(), ec);
  file::path testdir1   = tempdir / "libswoc_can_1";
  file::path testdir2   = testdir1 / "libswoc_can_2";
  file::path testdir3   = testdir2 / "libswoc_can_3";
  file::path unorthodox = testdir3 / file::path("..") / file::path("..") / "libswoc_can_2";

  // Invalid empty file::path.
  CHECK(file::path() == file::canonical(file::path(), ec));
  CHECK(ec.value() == EINVAL);

  // Fail if directory does not exist
  CHECK(file::path() == file::canonical(unorthodox, ec));
  CHECK(ec.value() == ENOENT);

  // Create the dir3 and test again
  CHECK(create_directories(testdir3, ec));
  CHECK(file::exists(testdir3));
  CHECK(file::exists(testdir2));
  CHECK(file::exists(testdir1));
  CHECK(file::exists(unorthodox));
  CHECK(file::canonical(unorthodox, ec) == testdir2);
  CHECK(ec.value() == 0);

  // Cleanup
  CHECK(file::remove_all(testdir1, ec) > 0);
  CHECK_FALSE(file::exists(testdir1));
}

TEST_CASE("file::path::copy", "[libts][swoc_file]") {
  std::error_code ec;
  file::path tempdir  = file::temp_directory_path();
  file::path testdir1 = tempdir / "libswoc_cp_alpha";
  file::path testdir2 = testdir1 / "libswoc_cp_bravo";
  file::path file1    = testdir2 / "original.txt";
  file::path file2    = testdir2 / "copy.txt";

  // Invalid empty path, both to and from parameters.
  CHECK_FALSE(file::copy(file::path(), file::path(), ec));
  CHECK(ec.value() == EINVAL);

  CHECK(file::create_directories(testdir2, ec));
  std::ofstream file(file1.string());
  file << "Simple test file";
  file.close();
  CHECK(file::exists(file1));

  // Invalid empty path, now from parameter is ok but to is empty
  CHECK_FALSE(file::copy(file1, file::path(), ec));
  CHECK(ec.value() == EINVAL);

  // successful copy: "to" is directory
  CHECK(file::copy(file1, testdir2, ec));
  CHECK(ec.value() == 0);

  // successful copy: "to" is file
  CHECK(file::copy(file1, file2, ec));
  CHECK(ec.value() == 0);

  // Compare the content
  CHECK(file::load(file1, ec) == file::load(file2, ec));

  // Cleanup
  CHECK(file::remove_all(testdir1, ec));
  CHECK_FALSE(file::exists(testdir1));
}
