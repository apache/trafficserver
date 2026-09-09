/** @file

  Shared helpers for the config unit tests.

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

#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace config::testing
{

/** Temp directory owned by this process, removed when the process exits.
 *
 * Each test case runs as its own concurrent ctest process, so a directory
 * shared between them would let one case delete another's file mid-read. A
 * directory per process rather than a unique file name keeps the file names
 * these tests depend on, e.g. a legacy storage.config finding its sibling
 * volume.config.
 */
inline std::filesystem::path const &
per_process_temp_dir()
{
  class ScopedDir
  {
  public:
    ScopedDir()
    {
      _path = std::filesystem::temp_directory_path() / ("ats_config_test." + std::to_string(getpid()));
      std::filesystem::create_directories(_path);
    }

    ~ScopedDir()
    {
      std::error_code ec; // Best effort: a leftover temp directory must not fail the run.
      std::filesystem::remove_all(_path, ec);
    }

    std::filesystem::path const &
    path() const
    {
      return _path;
    }

  private:
    std::filesystem::path _path;
  };

  static ScopedDir const dir;

  return dir.path();
}

/// A file in per_process_temp_dir(), removed when it goes out of scope.
class TempFile
{
public:
  TempFile(std::string const &filename, std::string const &content)
  {
    _path = per_process_temp_dir() / filename;
    std::ofstream ofs(_path);
    ofs << content;
  }

  ~TempFile()
  {
    std::error_code ec; // Best effort: cleanup must not throw out of a destructor.
    std::filesystem::remove(_path, ec);
  }

  std::string
  path() const
  {
    return _path.string();
  }

private:
  std::filesystem::path _path;
};

} // namespace config::testing
