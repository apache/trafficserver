/* @file
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
#include <exception>

#include <yaml-cpp/yaml.h>

#include "records/I_RecCore.h"
#include "records/P_RecCore.h"

#include "shared/overridable_txn_vars.h"

namespace record::field_names
{
static constexpr auto Name{"name"};
static constexpr auto RecordType{"record_type"};
static constexpr auto Version{"version"};
static constexpr auto RSB{"raw_stat_block"};
static constexpr auto Order{"order"};
static constexpr auto Access{"access"};
static constexpr auto UpdateStatus{"update_status"};
static constexpr auto UpdateType{"update_type"};
static constexpr auto CheckType{"checktype"};
static constexpr auto Source{"source"};
static constexpr auto SyntaxCheck{"syntax_check"};
static constexpr auto RecordClass{"record_class"};
static constexpr auto Overridable{"overridable"};
static constexpr auto DataType{"data_type"};
static constexpr auto CurrentValue{"current_value"};
static constexpr auto DefaultValue{"default_value"};

} // namespace record::field_names

namespace YAML
{
template <> struct convert<RecRecord> {
  static Node
  encode(const RecRecord &record)
  {
    namespace field = record::field_names;
    Node node;
    try {
      node[field::Name]         = record.name ? record.name : "null";
      node[field::RecordType]   = static_cast<int>(record.data_type);
      node[field::Version]      = record.version;
      node[field::RSB]          = record.rsb_id;
      node[field::Order]        = record.order;
      node[field::Access]       = static_cast<int>(record.config_meta.access_type);
      node[field::UpdateStatus] = static_cast<int>(record.config_meta.update_required);
      node[field::UpdateType]   = record.config_meta.update_type;
      node[field::CheckType]    = static_cast<int>(record.config_meta.check_type);
      node[field::Source]       = static_cast<int>(record.config_meta.source);
      node[field::SyntaxCheck]  = record.config_meta.check_expr ? record.config_meta.check_expr : "null";
      node[field::RecordClass]  = static_cast<int>(record.rec_type);

      if (record.name) {
        const auto it            = ts::Overridable_Txn_Vars.find(record.name);
        node[field::Overridable] = (it == ts::Overridable_Txn_Vars.end()) ? "false" : "true";
      }

      switch (record.data_type) {
      case RECD_INT:
        node[field::DataType]     = "INT";
        node[field::CurrentValue] = record.data.rec_int;
        node[field::DefaultValue] = record.data_default.rec_int;
        break;
      case RECD_FLOAT:
        node[field::DataType]     = "FLOAT";
        node[field::CurrentValue] = record.data.rec_float;
        node[field::DefaultValue] = record.data_default.rec_float;
        break;
      case RECD_STRING:
        node[field::DataType]     = "STRING";
        node[field::CurrentValue] = record.data.rec_string ? record.data.rec_string : "null";
        node[field::DefaultValue] = record.data_default.rec_string ? record.data_default.rec_string : "null";
        break;
      case RECD_COUNTER:
        node[field::DataType]     = "COUNTER";
        node[field::CurrentValue] = record.data.rec_counter;
        node[field::DefaultValue] = record.data_default.rec_counter;
        break;
      default:
        // this is an error, internal we should flag it
        break;
      }
    } catch (std::exception const &e) {
      return node;
    }

    return node;
  }
};

template <> struct convert<RecUpdateT> {
  static Node
  encode(const RecUpdateT &type)
  {
    return Node{static_cast<int>(type)};
  }
};
} // namespace YAML