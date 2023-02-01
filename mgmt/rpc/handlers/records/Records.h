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

#include <string_view>
#include <yaml-cpp/yaml.h>
#include "tscore/Errata.h"

namespace rpc::handlers::records
{
///
/// @brief Record lookups. This is a RPC function handler that retrieves a YAML::Node which will contain the result of a records
/// lookup. @see RecRecord.
/// Incoming parameter is expected to be a sequence, params will be converted to a @see RequestRecordElement
/// and the response will be a YAML node that contains the findings base on the query type. @see RequestRecordElement recTypes will
/// lead the search.
/// @param id JSONRPC client's id.
/// @param params lookup_records query structure.
/// @return ts::Rv<YAML::Node> A node or an error. If ok, the node will hold the @c "recordList" sequence with the findings. In case
/// of any missed search, ie: when paseed types didn't match the found record(s), the particular error will be added to the @c
/// "errorList" field.
///
ts::Rv<YAML::Node> lookup_records(std::string_view const &id, YAML::Node const &params);

///
/// @brief A RPC function handler that clear all the metrics.
///
/// @param id JSONRPC client's id.
/// @param params Nothing, this will be ignored.
/// @return ts::Rv<YAML::Node> An empty YAML::Node or the proper Errata with the tracked error.
///
ts::Rv<YAML::Node> clear_all_metrics_records(std::string_view const &id, YAML::Node const &);

///
/// @brief A RPC  function  handler that clear a specific set of metrics.
/// The @c "errorList" field will only be set if there is any error cleaning a specific metric.
/// @param id JSONRPC client's id.
/// @param params A list of records to update. @see RequestRecordElement
/// @return ts::Rv<YAML::Node> A YAML::Node or the proper Errata with the tracked error.
///
ts::Rv<YAML::Node> clear_metrics_records(std::string_view const &id, YAML::Node const &params);
} // namespace rpc::handlers::records
