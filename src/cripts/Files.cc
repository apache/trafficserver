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

// Our include dependencies are unfortunate ...
extern std::string RecConfigReadConfigDir();

namespace cripts
{

std::filesystem::file_status
File::Status(const File::Path &path)
{
  return std::filesystem::status(path);
}

File::Path &
File::Path::Rebase()
{
  if (std::filesystem::status(*this).type() != std::filesystem::file_type::regular) {
    auto config_dir = std::filesystem::canonical(RecConfigReadConfigDir());
    auto rebased    = std::filesystem::weakly_canonical(config_dir / *this);

    if (auto mm = std::mismatch(config_dir.begin(), config_dir.end(), rebased.begin(), rebased.end());
        mm.first == config_dir.end()) {
      static_cast<super_type &>(*this) = rebased;
    } else {
      TSError("[Cripts] File::Path::Rebase: '%s' escapes config directory, clearing path", this->c_str());
      static_cast<super_type &>(*this).clear();
    }
  }

  return *this;
}

} // namespace cripts
