/** @file

  Decode the records.yaml configuration file.

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

#include "P_RecCore.h"
#include "records/RecYAMLDecoder.h"
#include "records/RecYAMLDefs.h"

#include "tscore/Diags.h"
#include "tsutil/YamlCfg.h"
#include "records/RecordsConfig.h"

#include <string_view>

#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>

static std::array<std::string_view, 5> Node_Type_to_Str{
  {"Undefined", "Null", "Scalar", "Sequence", "Map"}
};
namespace
{
constexpr std::string_view CONFIG_RECORD_PREFIX{"proxy.config"};
const inline std::string   RECORD_YAML_ROOT_STR{"records"};

} // namespace

namespace detail
{
void                             flatten_node(CfgNode const &field, RecYAMLNodeHandler handler, swoc::Errata &errata);
std::pair<RecDataT, std::string> try_deduce_type(YAML::Node const &node);

// Helper class to make the code less verbose when lock is needed.
struct scoped_cond_lock {
  scoped_cond_lock(bool lock = false) : _lock(lock)
  {
    if (_lock) {
      ink_rwlock_wrlock(&g_records_rwlock);
    }
  }
  ~scoped_cond_lock()
  {
    if (_lock) {
      ink_rwlock_unlock(&g_records_rwlock);
    }
  }
  bool _lock{false};
};
} // namespace detail

void
SetRecordFromYAMLNode(CfgNode const &field, swoc::Errata &errata)
{
  std::string record_name{field.get_record_name()};
  RecT        rec_type{RecT::RECT_CONFIG};
  RecDataT    data_type{RecDataT::RECD_NULL};
  RecCheckT   check_type{RecCheckT::RECC_NULL};
  std::string check_expr;
  // this function (GetRec..) should be generic and possibly getting the value either
  // from where it gets it currently or a schema file.
  if (const auto *found = GetRecordElementByName(record_name.c_str()); found) {
    if (REC_TYPE_IS_STAT(found->type)) {
      ink_release_assert(REC_TYPE_IS_STAT(found->type));
    }
    rec_type   = found->type;
    data_type  = found->value_type;
    check_type = found->check;
    if (found->regex) {
      check_expr = found->regex;
    }
  } else {
    // Not registered in ATS, could be a plugin or an invalid(not registered) records.
    // Externally registered records should have the type set in each field (!!int, !!float, etc), otherwise we will not be able to
    // deduce the type and we could end up doing a bad type cast at the end. So we say if there is no type(tag) specified, then
    // we ignore it.
    auto [dtype, e] = detail::try_deduce_type(field.value_node);
    if (!e.empty()) {
      errata.note(ERRATA_WARN, "Ignoring field '{}' at Line {}. Not registered and {}", field.node.as<std::string>(),
                  field.node.Mark().line + 1, e);
      // We can't continue without knowing the type.
      return;
    }
    data_type = dtype; // field tags found.
  }

  // It could happen that a field was set to null. We only care for string type, we want
  // this to be explicitly set so the librecords can deal with this. For non strings we
  // will use the default value.
  //
  if (YAML::NodeType::Null == field.value_node.Type()) {
    switch (data_type) {
    case RecDataT::RECD_INT:
    case RecDataT::RECD_FLOAT:
      errata.note(ERRATA_DEBUG, "Field '{}' set to null. Default value will be used", field.node.as<std::string>());
      return;
    default:;
    }
  }

  std::string field_value = field.value_node.as<std::string>(); // in case of a string, the library will give us the literal
                                                                // 'null' which is exactly what we want.

  std::string value_str = RecConfigOverrideFromEnvironment(record_name.c_str(), field_value.c_str());
  RecSourceT  source    = (field_value == value_str ? REC_SOURCE_EXPLICIT : REC_SOURCE_ENV);

  if (source == REC_SOURCE_ENV) {
    errata.note(ERRATA_DEBUG, "'{}' was override with '{}' using an env variable", record_name, value_str);
  }

  if (!check_expr.empty() && RecordValidityCheck(value_str.c_str(), check_type, check_expr.c_str()) == false) {
    errata.note(ERRATA_WARN, "{} - Validity Check failed. '{}' against '{}'. Default value will be used", record_name, check_expr,
                value_str);
    return;
  }

  RecData data;
  memset(&data, 0, sizeof(RecData));
  RecDataSetFromString(data_type, &data, value_str.c_str());
  RecSetRecord(rec_type, record_name.c_str(), data_type, &data, nullptr, source, false);
  RecDataZero(data_type, &data);
};

swoc::Errata
ParseRecordsFromYAML(YAML::Node root, RecYAMLNodeHandler handler, bool lock /*false by default*/)
{
  [[maybe_unused]] detail::scoped_cond_lock cond_lock(lock);

  swoc::Errata errata;
  if (YAML::NodeType::Map != root.Type()) {
    return swoc::Errata(ERRATA_ERROR, "Node is expected to be a map, got '{}' instead.", root.Type());
  }

  if (auto ts = root[RECORD_YAML_ROOT_STR]; ts.size()) {
    for (auto &&n : ts) {
      detail::flatten_node({n.first, n.second, CONFIG_RECORD_PREFIX}, handler, errata);
    }
  } else {
    return swoc::Errata(ERRATA_ERROR, "'{}' root key not present or no fields to read. Default values will be used",
                        RECORD_YAML_ROOT_STR);
  }

  return errata;
}

namespace detail
{
std::pair<RecDataT, std::string>
try_deduce_type(YAML::Node const &node)
{
  // Using the tag.
  std::string_view tag = node.Tag();
  if (tag == ts::Yaml::YAML_FLOAT_TAG_URI) {
    return {RecDataT::RECD_FLOAT, {}};
  } else if (tag == ts::Yaml::YAML_INT_TAG_URI) {
    return {RecDataT::RECD_INT, {}};
  } else if (tag == ts::Yaml::YAML_STR_TAG_URI) {
    return {RecDataT::RECD_STRING, {}};
  } else if (tag == ts::Yaml::YAML_INT_TAG_URI) {
    return {RecDataT::RECD_INT, {}};
  } else if (tag == ts::Yaml::YAML_BOOL_TAG_URI) {
    return {RecDataT::RECD_INT, {}};
  } else if (tag == ts::Yaml::YAML_NULL_TAG_URI) {
    return {RecDataT::RECD_NULL, {}};
  }
  std::string text;
  return {RecDataT::RECD_NULL, swoc::bwprint(text, "Unknown tag type '{}'", tag)};
}

/// @brief Iterate over a node and build up the field name from it.
///
/// This function walks down a YAML node till it find a scalar type while building the record name, so if a node is something like
/// this:
///
///   diags:
///     debug:
///       enabled: 0
///
/// this function will build up the record name "diags.debug.enabled"  and then it prepend the "proxy.config" to each name, this
/// will be the record name already known by ATS. Every time  a scalar node is completed then the handler function will be called.
///
/// @param field Parent node.
/// @param handler Scalar node function handler, called every time a scalar type is found.
/// @param errata Holds the errors detected.
void
flatten_node(CfgNode const &field, RecYAMLNodeHandler handler, swoc::Errata &errata)
{
  switch (field.value_node.Type()) {
  case YAML::NodeType::Map: {
    field.append_field_name();
    for (auto &it : field.value_node) {
      flatten_node({it.first, it.second, field.get_record_name()}, handler, errata);
    }
  } break;
  case YAML::NodeType::Sequence:
  case YAML::NodeType::Scalar:
  case YAML::NodeType::Null: {
    field.append_field_name();
    handler(field, errata);
  } break;
  default:; // done
  }
}
} // namespace detail

namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const & /* spec ATS_UNUSED */, YAML::NodeType::value type)
{
  return w.write(Node_Type_to_Str[type]);
}
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const & /* spec ATS_UNUSED */, YAML::Node const &node)
{
  return w.write(node.as<std::string>());
}
} // namespace swoc
