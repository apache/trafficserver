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

#include "Metrics.h"
#include "rpc/handlers/common/RecordsUtils.h"

namespace rpc::handlers::metrics
{
static unsigned STATS_RECORD_TYPES = RECT_PROCESS | RECT_PLUGIN | RECT_NODE;
static constexpr auto logTag{"rpc.metric"};

namespace err = rpc::handlers::errors;

ts::Rv<YAML::Node>
get_metric_records(std::string_view const &id, YAML::Node const &params)
{
  ts::Rv<YAML::Node> resp;

  using namespace rpc::handlers::records::utils;
  std::error_code ec;

  auto check = [](RecT rec_type, std::error_code &ec) {
    if (!REC_TYPE_IS_STAT(rec_type)) {
      // we have an issue
      ec = rpc::handlers::errors::RecordError::RECORD_NOT_METRIC;
      return false;
    }
    return true;
  };

  for (auto &&element : params) {
    auto const &recordName = element.as<std::string>();
    auto &&[node, error]   = get_yaml_record(recordName, check);

    if (error) {
      resp.errata().push(error);
      continue;
    }

    resp.result().push_back(std::move(node));
  }

  return resp;
}

ts::Rv<YAML::Node>
get_metric_records_regex(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;

  ts::Rv<YAML::Node> resp;

  for (auto &&element : params) {
    auto const &recordName = element.as<std::string>();

    auto &&[node, error] = get_yaml_record_regex(recordName, STATS_RECORD_TYPES);

    if (error) {
      resp.errata().push(error);
      continue;
    }
    // node may have more than one.
    for (auto &&n : node) {
      resp.result().push_back(std::move(n));
    }
  }

  return resp;
}

ts::Rv<YAML::Node>
clear_all_metrics(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;
  Debug(logTag, "Cleaning metrics.");
  if (RecResetStatRecord(RECT_NULL, true) != REC_ERR_OKAY) {
    Debug(logTag, "Error while cleaning the stats.");
    return ts::Errata{rpc::handlers::errors::RecordError::RECORD_WRITE_ERROR};
  }

  return resp;
}

ts::Rv<YAML::Node>
clear_metrics(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;

  for (auto &&element : params) {
    auto const &name = element.as<std::string>();
    if (!name.empty()) {
      if (RecResetStatRecord(name.data()) != REC_ERR_OKAY) {
        // This could be due the fact that the record is already cleared or the metric does not have any significant
        // value.
        std::error_code ec{rpc::handlers::errors::RecordError::RECORD_WRITE_ERROR};
        resp.errata().push(ec);
      }
    }
  }

  return resp;
}
} // namespace rpc::handlers::metrics