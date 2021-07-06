/** @file

    ts::file unit tests.

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

#include <iostream>
#include <fstream> /* ofstream */

#include "tscore/ts_file.h"
#include "../../../tests/include/catch.hpp"

using ts::file::path;

// --------------------
TEST_CASE("ts_file", "[libts][ts_file]")
{
  path p1("/home");
  REQUIRE(p1.string() == "/home");
  auto p2 = p1 / "bob";
  REQUIRE(p2.string() == "/home/bob");
  p2 = p2 / "git/ats/";
  REQUIRE(p2.string() == "/home/bob/git/ats/");
  p2 /= "lib/ts";
  REQUIRE(p2.string() == "/home/bob/git/ats/lib/ts");
  p2 /= "/home/dave";
  REQUIRE(p2.string() == "/home/dave");
  path p3 = path("/home/dave") / "git/tools";
  REQUIRE(p3.string() == "/home/dave/git/tools");
}

TEST_CASE("ts_file_io", "[libts][ts_file_io]")
{
  path file("/etc/hosts");
  std::error_code ec;
  std::string content = ts::file::load(file, ec);
  REQUIRE(ec.value() == 0);
  REQUIRE(content.size() > 0);
  REQUIRE(content.find("localhost") != content.npos);

  // Check some file properties.
  REQUIRE(ts::file::is_readable(file) == true);
  auto fs = ts::file::status(file, ec);
  REQUIRE(ec.value() == 0);
  REQUIRE(ts::file::is_dir(fs) == false);
  REQUIRE(ts::file::is_regular_file(fs) == true);

  // Failure case.
  file    = "unit-tests/no_such_file.txt";
  content = ts::file::load(file, ec);
  REQUIRE(ec.value() == 2);
  REQUIRE(ts::file::is_readable(file) == false);
}

TEST_CASE("ts_file::path::parent_path", "[libts][fs_file]")
{
  CHECK(ts::file::path("/").parent_path() == path("/"));
  CHECK(ts::file::path("/absolute/path/file.txt").parent_path() == ts::file::path("/absolute/path"));
  CHECK(ts::file::path("/absolute/path/.").parent_path() == ts::file::path("/absolute/path"));

  CHECK(ts::file::path("relative/path/file.txt").parent_path() == ts::file::path("relative/path"));
  CHECK(ts::file::path("relative/path/.").parent_path() == ts::file::path("relative/path"));
  CHECK(ts::file::path(".").parent_path() == ts::file::path(""));
}

static std::string
setenvvar(const std::string &name, const std::string &value)
{
  std::string saved;
  if (nullptr != getenv(name.c_str())) {
    saved.assign(value);
  }

  if (!value.empty()) {
    setenv(name.c_str(), value.c_str(), 1);
  } else {
    unsetenv(name.c_str());
  }

  return saved;
}

TEST_CASE("ts_file::path::temp_directory_path", "[libts][fs_file]")
{
  // Clean all temp dir env variables.
  std::string s1 = setenvvar("TMPDIR", std::string());
  std::string s2 = setenvvar("TEMPDIR", std::string());
  std::string s3 = setenvvar("TMP", std::string());
  std::string s;

  // If nothing defined return "/tmp"
  CHECK(ts::file::temp_directory_path() == ts::file::path("/tmp"));

  // TMPDIR defined.
  s = setenvvar("TMPDIR", "/temp_dirname1");
  CHECK(ts::file::temp_directory_path() == ts::file::path("/temp_dirname1"));
  setenvvar("TMPDIR", s);

  // TEMPDIR
  s = setenvvar("TEMPDIR", "/temp_dirname");
  CHECK(ts::file::temp_directory_path() == ts::file::path("/temp_dirname"));
  // TMP defined, it should take precedence over TEMPDIR.
  s = setenvvar("TMP", "/temp_dirname1");
  CHECK(ts::file::temp_directory_path() == ts::file::path("/temp_dirname1"));
  // TMPDIR defined, it should take precedence over TMP.
  s = setenvvar("TMPDIR", "/temp_dirname2");
  CHECK(ts::file::temp_directory_path() == ts::file::path("/temp_dirname2"));
  setenvvar("TMPDIR", s);
  setenvvar("TMP", s);
  setenvvar("TEMPDIR", s);

  // Restore all temp dir env variables to their previous state.
  setenvvar("TMPDIR", s1);
  setenvvar("TEMPDIR", s2);
  setenvvar("TMP", s3);
}

TEST_CASE("ts_file::path::create_directories", "[libts][fs_file]")
{
  std::error_code ec;
  path tempdir = ts::file::temp_directory_path();

  CHECK_FALSE(ts::file::create_directories(path(), ec));
  CHECK(ec.value() == EINVAL);

  path testdir1 = tempdir / "dir1";
  CHECK(ts::file::create_directories(testdir1, ec));
  CHECK(ts::file::exists(testdir1));

  path testdir2 = testdir1 / "dir2";
  CHECK(ts::file::create_directories(testdir1, ec));
  CHECK(ts::file::exists(testdir1));

  // Cleanup
  CHECK(ts::file::remove(testdir1, ec));
  CHECK_FALSE(ts::file::exists(testdir1));
}

TEST_CASE("ts_file::path::remove", "[libts][fs_file]")
{
  std::error_code ec;
  path tempdir = ts::file::temp_directory_path();

  CHECK_FALSE(ts::file::remove(path(), ec));
  CHECK(ec.value() == EINVAL);

  path testdir1 = tempdir / "dir1";
  path testdir2 = testdir1 / "dir2";
  path file1    = testdir2 / "test.txt";

  // Simple creation and removal of a directory /tmp/dir1
  CHECK(ts::file::create_directories(testdir1, ec));
  CHECK(ts::file::exists(testdir1));
  CHECK(ts::file::remove(testdir1, ec));
  CHECK_FALSE(ts::file::exists(testdir1));

  // Create /tmp/dir1/dir2 and remove /tmp/dir1/dir2 => /tmp/dir1 should exist
  CHECK(ts::file::create_directories(testdir2, ec));
  CHECK(ts::file::remove(testdir2, ec));
  CHECK(ts::file::exists(testdir1));

  // Create a file, remove it, test if exists and then attempting to remove it again should fail.
  CHECK(ts::file::create_directories(testdir2, ec));
  std::ofstream file(file1.string());
  file << "Simple test file";
  file.close();
  CHECK(ts::file::exists(file1));
  CHECK(ts::file::remove(file1, ec));
  CHECK_FALSE(ts::file::exists(file1));
  CHECK_FALSE(ts::file::remove(file1, ec));

  // Clean up.
  CHECK(ts::file::remove(testdir1, ec));
  CHECK_FALSE(ts::file::exists(testdir1));
}

TEST_CASE("ts_file::path::canonical", "[libts][fs_file]")
{
  std::error_code ec;
  path tempdir    = ts::file::canonical(ts::file::temp_directory_path(), ec);
  path testdir1   = tempdir / "dir1";
  path testdir2   = testdir1 / "dir2";
  path testdir3   = testdir2 / "dir3";
  path unorthodox = testdir3 / path("..") / path("..") / "dir2";

  // Invalid empty path.
  CHECK(path() == ts::file::canonical(path(), ec));
  CHECK(ec.value() == EINVAL);

  // Fail if directory does not exist
  CHECK(path() == ts::file::canonical(unorthodox, ec));
  CHECK(ec.value() == ENOENT);

  // Create the dir3 and test again
  CHECK(create_directories(testdir3, ec));
  CHECK(ts::file::exists(testdir3));
  CHECK(ts::file::exists(testdir2));
  CHECK(ts::file::exists(testdir1));
  CHECK(ts::file::exists(unorthodox));
  CHECK(ts::file::canonical(unorthodox, ec) == testdir2);
  CHECK(ec.value() == 0);

  // Cleanup
  CHECK(ts::file::remove(testdir1, ec));
  CHECK_FALSE(ts::file::exists(testdir1));
}

TEST_CASE("ts_file::path::filename", "[libts][fs_file]")
{
  CHECK(ts::file::filename(path("/foo/bar.txt")) == path("bar.txt"));
  CHECK(ts::file::filename(path("/foo/.bar")) == path(".bar"));
  CHECK(ts::file::filename(path("/foo/bar")) == path("bar"));
  CHECK(ts::file::filename(path("/foo/bar/")) == path(""));
  CHECK(ts::file::filename(path("/foo/.")) == path("."));
  CHECK(ts::file::filename(path("/foo/..")) == path(".."));
  CHECK(ts::file::filename(path("/foo/../bar")) == path("bar"));
  CHECK(ts::file::filename(path("/foo/../bar/")) == path(""));
  CHECK(ts::file::filename(path(".")) == path("."));
  CHECK(ts::file::filename(path("..")) == path(".."));
  CHECK(ts::file::filename(path("/")) == path(""));
  CHECK(ts::file::filename(path("//host")) == path("host"));
}

TEST_CASE("ts_file::path::copy", "[libts][fs_file]")
{
  std::error_code ec;
  path tempdir  = ts::file::temp_directory_path();
  path testdir1 = tempdir / "dir1";
  path testdir2 = testdir1 / "dir2";
  path file1    = testdir2 / "test1.txt";
  path file2    = testdir2 / "test2.txt";

  // Invalid empty path, both to and from parameters.
  CHECK_FALSE(ts::file::copy(path(), path(), ec));
  CHECK(ec.value() == EINVAL);

  CHECK(ts::file::create_directories(testdir2, ec));
  std::ofstream file(file1.string());
  file << "Simple test file";
  file.close();
  CHECK(ts::file::exists(file1));

  // Invalid empty path, now from parameter is ok but to is empty
  CHECK_FALSE(ts::file::copy(file1, path(), ec));
  CHECK(ec.value() == EINVAL);

  // successfull copy: "to" is directory
  CHECK(ts::file::copy(file1, testdir2, ec));
  CHECK(ec.value() == 0);

  // successful copy: "to" is file
  CHECK(ts::file::copy(file1, file2, ec));
  CHECK(ec.value() == 0);

  // Compare the content
  CHECK(ts::file::load(file1, ec) == ts::file::load(file2, ec));

  // Cleanup
  CHECK(ts::file::remove(testdir1, ec));
  CHECK_FALSE(ts::file::exists(testdir1));
}
