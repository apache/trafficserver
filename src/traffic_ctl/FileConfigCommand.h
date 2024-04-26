/**
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

#include <iostream>
#include <fstream>
#include <utility>

#include "tscore/ArgParser.h"
#include "CtrlCommands.h"
#include "swoc/TextView.h"

/// @brief Very basic flat YAML node handling.
///
/// The whole idea is to be able to set some YAML nodes and being able to search for flat nodes, nodes
/// can be created if requested. This should also help on creating the whole node tree from a legacy
/// record variable style. For more complex updates we should tweak this class a bit.
struct FlatYAMLAccessor {
  ///
  /// @brief Find a node based on the passed record variable name.
  ///
  /// This function will only search for a specific node, it will not create one.
  /// @param variable the record name.
  /// @return std::pair<bool, YAML::Node>  First value will be set to true if found and the relevant Node will be set in the second
  /// parameter
  ///

  std::pair<bool, YAML::Node> find_node(swoc::TextView variable);
  /// @brief Find a node based on the passed record variable name. Create if not exist.
  ///
  /// This function will first try to find the passed variable, if not it will create a new with the passed tree.
  /// @param variable the record name.
  /// @param all_docs Search in all the nodes from the parsed stream. Default is true. No need to change this now.
  /// @return
  YAML::Node find_or_create_node(swoc::TextView variable, bool all_docs = true);

  /// @brief Build up a YAML node including TAG and Value. This is used to append just a single variable in a file.
  /// @param variable The record name.
  /// @param value The value to be set on the node.
  /// @param tag The tag to be set on the node.
  /// @param out The YAML::Emitter used to build up the node. This can just be streamed out to a string or a file.
  void make_tree_node(swoc::TextView variable, swoc::TextView value, swoc::TextView tag, YAML::Emitter &out);

  /// @brief  Set the internal list of nodes from the parsed file. Caller should deal with the YAML::LoadAll or any other way.
  /// @param streams A list of docs.
  void
  load(std::vector<YAML::Node> streams)
  {
    _docs = std::move(streams);
  }

protected:
  std::vector<YAML::Node>      _docs;
  std::unique_ptr<BasePrinter> _printer; //!< Specific output formatter. This should be created by the derived class.
};

/// @brief Class used to deal with the config file changes. Append or modify an existing records.yaml field
class FileConfigCommand : public CtrlCommand, FlatYAMLAccessor
{
  static inline const std::string SET_STR{"set"}; // we support get, set only for now.
  static inline const std::string GET_STR{"get"};

  static inline const std::string COLD_STR{"cold"};     // Meaning that the change is on a file.
  static inline const std::string UPDATE_STR{"update"}; // Append the new field to a file.

  void config_set();
  void config_get();

public:
  FileConfigCommand(ts::Arguments *args);
};
