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
#include "Records.h"

#include <system_error> // TODO: remove
#include <string>
#include <string_view>

#include "handlers/common/RecordsUtils.h"

#include "tscore/BufferWriter.h"
#include "records/I_RecCore.h"
#include "records/P_RecCore.h"
#include "tscore/Diags.h"

namespace rpc::handlers::records
{
namespace err = rpc::handlers::errors;

ts::Rv<YAML::Node>
get_records(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;

  auto check = [](RecT rec_type, std::error_code &ec) { return true; };

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

} // namespace rpc::handlers::records