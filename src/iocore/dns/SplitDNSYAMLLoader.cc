/** @file

  YAML loader for SplitDNS configuration

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

#include "iocore/dns/SplitDNSYAMLLoader.h"

#include "tscpp/util/ts_errata.h"

#include <yaml-cpp/yaml.h>

#include <sstream>
#include <string>

static std::string
mark_to_error_message(YAML::Mark mark)
{
  std::stringstream ss;
  ss << "At line " << mark.line << " column " << mark.column << '.';
  return ss.str();
}

namespace splitdns
{
namespace yaml
{
  swoc::Errata
  SplitDNSYAMLLoader::setUpSplitDNSFromYAMLTree(YAML::Node const &current_node)
  {
    swoc::Errata err;

    if (!current_node["dns"]) {
      err = swoc::Errata(ERRATA_ERROR, "Root tag 'dns' not found.");
      err.note(mark_to_error_message(current_node.Mark()));
    } else if (!current_node["dns"]["split"]) {
      err = swoc::Errata(ERRATA_ERROR, "Tag 'split' not found.");
      err.note(mark_to_error_message(current_node["dns"].Mark()));
    }

    return err;
  }
} // namespace yaml
} // namespace splitdns
