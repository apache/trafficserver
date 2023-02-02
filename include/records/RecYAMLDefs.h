/** @file

  Public RecCore declarations

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

#include <functional>
#include <yaml-cpp/yaml.h>

#include <swoc/BufferWriter.h>
#include "tscpp/util/ts_errata.h"

struct CfgNode;

///
/// @brief Handler callback signature to deal with the found record.
///
///
using RecYAMLNodeHandler = std::function<void(CfgNode const &field, swoc::Errata &)>;
//------------------------------------------------------------------------------
///
/// @brief A wrapper class around a @c YAML::Node which also holds the record name.
/// The record name is constructed base on every field name, so every child field will be appended to the
/// parent name using a dot as separator.
///
///
struct CfgNode {
  // /// @brief Construct a configuration node using just the @c YAML::Node
  // /// @param n The parsed @c YAML::node.
  // CfgNode(YAML::Node const &n, YAML::Node const &v) : node{n}, value_node{v} {}

  /// @brief Construct a configuration node using just the @c YAML::Node and the @c record_name
  /// @param n The parsed @c YAML::node.
  /// @param base_record_name The base record name from where the record name should be built up.
  CfgNode(YAML::Node const &n, YAML::Node const &v, std::string_view base_record_name) : node{n}, value_node{v}
  {
    _legacy.record_name = base_record_name;
  }

  /// @brief Returns the built record name.
  ///
  /// The record name was build up base on the yaml structure: i.e.:
  ///
  /// diags:
  ///   debug:
  ///     tag: rpc
  ///
  /// this function will return diags.debug.tag
  std::string_view
  get_record_name() const
  {
    return _legacy.record_name;
  }

  /// @brief Append field name in order to build up the record name.
  void
  append_field_name() const
  {
    if (!_legacy.record_name.empty()) {
      _legacy.record_name.append(".");
      _legacy.record_name.append(node.as<std::string>());
    } else {
      _legacy.record_name.append(node.as<std::string>());
    }
  }

  // public
  YAML::Node node;
  YAML::Node value_node;

private:
  /// @brief Keep track of legacy stuff like the name.
  struct Legacy {
    std::string record_name;
  };
  mutable Legacy _legacy;
};
