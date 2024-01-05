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

#include "tsutil/ts_errata.h"

#include <swoc/bwf_base.h>
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
  using err_type = swoc::Errata;

  class SplitDNSYAMLLoader
  {
  public:
    static swoc::Errata
    load(std::string const &content, SplitDNS &out)
    {
      auto current_node{YAML::Load(content)};
      return self_type::setUpSplitDNSFromYAMLTree(current_node);
    }

  private:
    using self_type = SplitDNSYAMLLoader;

    static swoc::Errata setUpSplitDNSFromYAMLTree(YAML::Node const &current_node);
  };

  inline swoc::Errata
  load(std::string_view config_filename, SplitDNS &out)
  {
    std::error_code ec;
    auto content{swoc::file::load(config_filename, ec)};
    if (ec.value() != 0) {
      return swoc::Errata{ec, ERRATA_ERROR, "Failed to load {} : {}", config_filename, ec};
    }

    if (auto err{SplitDNSYAMLLoader::load(content, out)}; !err.is_ok()) {
      std::string while_loading_note;
      swoc::bwprint(while_loading_note, "While loading {}", config_filename);
      err.note(while_loading_note);
      return err;
    }

    return swoc::Errata{};
  }

} // namespace yaml
} // namespace splitdns
