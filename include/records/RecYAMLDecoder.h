/** @file

  Public RecYamlDecoder declarations

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

#include "records/RecYAMLDefs.h"

/// @brief Create a configuration record based on the passed @c YAML node
///
/// This function will try to get the records details from the RecordElement
/// defined in the RecordsConfig.cc, if the records is defined then all the details
/// like record type and value will be used. If the records is not defined then this
/// function will try to deduce this values from the @c YAML node instead, for that
/// it's necessary for the node to have a type associated, this is done by using a
/// @c YAML tag for the type. (!!float, !!int, etc).
///
/// @note Nodes that aren't defined in ATS, like nodes that are registered by plugins
///       should specify the type in the yaml configuration, otherwise will be ignored as
///       we cannot assume any type.
///
/// @param node Parsed configuration node.
/// @param value Parsed Value node.
/// @param errata Output variable, errors will be added in the passed Errata.
void SetRecordFromYAMLNode(const CfgNode &field, swoc::Errata &errata);

/// @brief Open and parse a records.yaml config file.
///
/// This function parses the yaml file, then it calls @c ParseRecordsFromYAML to handle the file content.
/// This function does not lock, make sure you work around the @c g_records_rwlock
/// @param path Path to the configuration file
/// @param handler Function callback to handle each parsed node and value.
/// @return This function will collect details about the parsing.
swoc::Errata RecYAMLConfigFileParse(const char *path, RecYAMLNodeHandler handler);

///
/// @brief This function parses the YAML root node ("ts") and convert each field
///        into a record style object.
///
/// As we keep the internal records without a change we should rebuild each records name
/// from a YAML structure, this function parses the yaml and while walking down to the scalar
/// node it builds the record name, this where the handler gets called.
///
/// Example:
///
/// ts:
///   exec_thread:
///       autoconfig:
///         scale: 1.0
///
///  -> Will be flatten to "proxy.config.exec_thread.autoconfig.scale". Note that this function
/// prefix with "proxy.config" to the generated record name.
///
/// @note @c handler will be called every time the parser found a scalar node, so the caller of this
/// function should handle the parsed node. This function will not modify any internal records data, this
/// is up to the handler to do it.
///
/// @note This function is separated from RecYAMLConfigFileParse as this can also be called
///       independently, i.e.: From a RPC handler.
///
/// @param root Top YAML node, it should contain the "ts" element.
/// @param handle Callback that will process each parsed node.
/// @param lock true if we want this function to lock g_records_rwlock. If you are locking already,
///             then leave it as false.
/// @return swoc::Errata Collected notes from the YAML parsing
swoc::Errata ParseRecordsFromYAML(YAML::Node root, RecYAMLNodeHandler handler, bool lock = false);
