/** @file

  YAML loader for SplitDNS configuration - header

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

#include "P_SplitDNSProcessor.h"

#include "tscpp/util/ts_errata.h"

#include <swoc/swoc_file.h>
#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <ostream>
#include <string_view>
#include <system_error>
#include <utility>

namespace splitdns
{
namespace yaml
{
  class SplitDNSYAMLLoader
  {
  public:
    static swoc::Errata
    load(std::string const &content, SplitDNS &out)
    {
      self_type loader{content, out};
      return loader.setUpSplitDNSFromYAMLTree();
    }

  private:
    using self_type = SplitDNSYAMLLoader;

    YAML::Node current_node;

    SplitDNSYAMLLoader(std::string const &content, SplitDNS &out) { this->current_node = YAML::Load(content); }

    swoc::Errata setUpSplitDNSFromYAMLTree();
  };

  inline void
  load(std::string_view config_filename, SplitDNS &out, std::ostream &errorstream)
  {
    std::error_code ec;
    auto content{swoc::file::load(config_filename, ec)};
    if (ec.value() == 0) {
      if (auto err{SplitDNSYAMLLoader::load(content, out)}; !err.is_ok()) {
        errorstream << err << "While loading " << config_filename << ".\n";
      }
    } else {
      errorstream << "Failed to load " << config_filename << '.';
    }
  }

} // namespace yaml
} // namespace splitdns
