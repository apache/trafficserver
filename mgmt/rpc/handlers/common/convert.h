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

#include "RecordsUtils.h"

///
/// @brief Namespace to group all the names used for key access to the yaml lookup nodes.
///
///
namespace constants_rec
{
static constexpr auto REC{"record"};

static constexpr auto NAME{"record_name"};
static constexpr auto RECORD_TYPE{"record_type"};
static constexpr auto RECORD_VERSION{"version"};
static constexpr auto REGISTERED{"registered"};
static constexpr auto RSB{"raw_stat_block"};
static constexpr auto ORDER{"order"};
static constexpr auto ACCESS_TYPE{"access_type"};
static constexpr auto UPDATE_STATUS{"update_status"};
static constexpr auto UPDATE_TYPE{"update_type"};
static constexpr auto CHECK_TYPE{"checktype"};
static constexpr auto SOURCE{"source"};
static constexpr auto CHECK_EXPR{"check_expr"};
static constexpr auto CLASS{"record_class"};
static constexpr auto OVERRIDABLE{"overridable"};
static constexpr auto DATA_TYPE{"data_type"};
static constexpr auto CURRENT_VALUE{"current_value"};
static constexpr auto DEFAULT_VALUE{"default_value"};
static constexpr auto CONFIG_META{"config_meta"};
static constexpr auto STAT_META{"stat_meta"};

static constexpr auto PERSIST_TYPE{"persist_type"};

} // namespace constants_rec

namespace YAML
{
///
/// @brief specialize convert template class for RecPersistT
///
template <> struct convert<RecPersistT> {
  static Node
  encode(const RecPersistT &type)
  {
    return Node{static_cast<int>(type)};
  }
};

///
/// @brief specialize convert template class for RecConfigMeta
///
template <> struct convert<RecConfigMeta> {
  static Node
  encode(const RecConfigMeta &configMeta)
  {
    Node node;
    // TODO: do we really want each specific encode implementation for each enum type?
    node[constants_rec::ACCESS_TYPE]   = static_cast<int>(configMeta.access_type);
    node[constants_rec::UPDATE_STATUS] = static_cast<int>(configMeta.update_required);
    node[constants_rec::UPDATE_TYPE]   = static_cast<int>(configMeta.update_type);
    node[constants_rec::CHECK_TYPE]    = static_cast<int>(configMeta.check_type);
    node[constants_rec::SOURCE]        = static_cast<int>(configMeta.source);
    node[constants_rec::CHECK_EXPR]    = configMeta.check_expr ? configMeta.check_expr : "null";

    return node;
  }
};

///
/// @brief specialize convert template class for RecStatMeta
///
template <> struct convert<RecStatMeta> {
  static Node
  encode(const RecStatMeta &statMeta)
  {
    // TODO. just make sure that we know which data should be included here.
    Node node;
    node[constants_rec::PERSIST_TYPE] = statMeta.persist_type;
    return node;
  }
};

///
/// @brief specialize convert template class for RecRecord
///
template <> struct convert<RecRecord> {
  static Node
  encode(const RecRecord &record)
  {
    Node node;
    try {
      node[constants_rec::NAME]           = record.name ? record.name : "null";
      node[constants_rec::RECORD_TYPE]    = static_cast<int>(record.data_type);
      node[constants_rec::RECORD_VERSION] = record.version;
      node[constants_rec::REGISTERED]     = record.registered;
      node[constants_rec::RSB]            = record.rsb_id;
      node[constants_rec::ORDER]          = record.order;

      if (REC_TYPE_IS_CONFIG(record.rec_type)) {
        node[constants_rec::CONFIG_META] = record.config_meta;
      } else if (REC_TYPE_IS_STAT(record.rec_type)) {
        node[constants_rec::STAT_META] = record.stat_meta;
      }

      node[constants_rec::CLASS] = static_cast<int>(record.rec_type);

      if (record.name) {
        const auto it                    = ts::Overridable_Txn_Vars.find(record.name);
        node[constants_rec::OVERRIDABLE] = (it == ts::Overridable_Txn_Vars.end()) ? "false" : "true";
      }

      switch (record.data_type) {
      case RECD_INT:
        node[constants_rec::DATA_TYPE]     = "INT";
        node[constants_rec::CURRENT_VALUE] = record.data.rec_int;
        node[constants_rec::DEFAULT_VALUE] = record.data_default.rec_int;
        break;
      case RECD_FLOAT:
        node[constants_rec::DATA_TYPE]     = "FLOAT";
        node[constants_rec::CURRENT_VALUE] = record.data.rec_float;
        node[constants_rec::DEFAULT_VALUE] = record.data_default.rec_float;
        break;
      case RECD_STRING:
        node[constants_rec::DATA_TYPE]     = "STRING";
        node[constants_rec::CURRENT_VALUE] = record.data.rec_string ? record.data.rec_string : "null";
        node[constants_rec::DEFAULT_VALUE] = record.data_default.rec_string ? record.data_default.rec_string : "null";
        break;
      case RECD_COUNTER:
        node[constants_rec::DATA_TYPE]     = "COUNTER";
        node[constants_rec::CURRENT_VALUE] = record.data.rec_counter;
        node[constants_rec::DEFAULT_VALUE] = record.data_default.rec_counter;
        break;
      default:
        // this is an error, internal we should flag it
        break;
      }
    } catch (std::exception const &e) {
      // we create an empty map node, we do not want to have a null. revisit this.
      YAML::NodeType::value kind = YAML::NodeType::Map;
      node                       = YAML::Node{kind};
    }

    Node yrecord;
    yrecord[constants_rec::REC] = node;
    return yrecord;
  }
};

} // namespace YAML