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
#include "cripts/Files.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace
{
// Each case runs in its own process, so the configuration directory it sets is seen by no other case.
struct ConfigDir {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
                               ("files_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

  ConfigDir()
  {
    std::filesystem::create_directories(path);
    setenv("PROXY_CONFIG_CONFIG_DIR", path.c_str(), 1);
  }
  ~ConfigDir()
  {
    std::error_code ec; // Best effort: a leftover temp directory must not fail the run.
    std::filesystem::remove_all(path, ec);
  }
};

} // namespace

TEST_CASE("File::Path::Rebase: a name under the configuration directory", "[cripts][Files]")
{
  ConfigDir          config;
  cripts::File::Path path{"keys/secret.hex"};

  path.Rebase();
  CHECK(path == std::filesystem::canonical(config.path) / "keys/secret.hex");
}

TEST_CASE("File::Path::Rebase: a name that escapes the configuration directory", "[cripts][Files]")
{
  ConfigDir          config;
  cripts::File::Path path{"../secret.hex"};

  path.Rebase();
  CHECK(path.empty());
}

TEST_CASE("File::Path::Rebase: a missing configuration directory", "[cripts][Files]")
{
  ConfigDir config;

  setenv("PROXY_CONFIG_CONFIG_DIR", (config.path / "missing").c_str(), 1);

  cripts::File::Path path{"secret.hex"};

  CHECK_NOTHROW(path.Rebase());
  CHECK(path.empty());
}

TEST_CASE("File::Path::Rebase: a name under a directory the user cannot search", "[cripts][Files]")
{
  if (geteuid() == 0) {
    SKIP("running as root");
  }

  ConfigDir          config;
  auto               sealed = config.path / "sealed";
  cripts::File::Path path{"sealed/secret.hex"};

  std::filesystem::create_directories(sealed);
  std::filesystem::permissions(sealed, std::filesystem::perms::none);
  CHECK_NOTHROW(path.Rebase());
  CHECK(path.empty());
  std::filesystem::permissions(sealed, std::filesystem::perms::owner_all);
}
