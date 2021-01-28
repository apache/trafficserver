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

#include <system_error>
#include <string>
#include <string_view>

#include "handlers/common/RecordsUtils.h"
// #include "common/yaml/codecs.h"
///
/// @brief Local definitions to map requests and responsponses(not fully supported yet) to custom structures. All this definitions
/// are used during decoding and encoding of the  RPC requests.
///
namespace
{
/// @brief This class maps the incoming rpc record request in general. This should be used to handle all the data around the
/// record requests.
///
struct RequestRecordElement {
  std::string recName;       //!< Incoming record name, this is used for a regex as well.
  bool isRegex{false};       //!< set to true if the lookup should be done by using a regex instead a full name.
  std::vector<int> recTypes; //!< incoming rec_types

  /// @brief test if the requests is intended to use a regex.
  bool
  is_regex_req() const
  {
    return isRegex;
  }
};

///
/// @brief Class used to wrap non recoverable lookup errors during a lookup call, this errors will then, be pushed inside the
/// errorList nodes.
///
struct ErrorInfo {
  ErrorInfo(int _code)
    : code(_code) //!< recordName and message should be set manually, unless it's created from an @c std::error_code
  {
  }
  // Build it from a @c std::error_code
  ErrorInfo(std::error_code ec) : code(ec.value()), message(ec.message()) {}
  int code; //!< Error code, it's not mandatory to include the message if we have the code instead. The message can be found in
            // the documentation if the code is returned.
  std::string recordName; //!< record name may not be available in some cases, instead we can use a message. Message will use
                          // their own field name.
  std::string message;
};

static constexpr auto RECORD_NAME_REGEX{"record_name_regex"};
static constexpr auto RECORD_NAME{"record_name"};
static constexpr auto RECORD_TYPES{"rec_types"};
static constexpr auto ERROR_CODE{"code"};
static constexpr auto ERROR_MESSAGE{"message"};

} // namespace
// using namespace rpc::codec::types;
// YAML Converter for the incoming record request @see RequestRecordElement. Make sure you protect this by try/catch. We may get
// some invalid types.
namespace YAML
{
// using namespace rpc::codec::types;
template <> struct convert<RequestRecordElement> {
  static bool
  decode(Node const &node, RequestRecordElement &info)
  {
    if (!node[RECORD_NAME_REGEX] && !node[RECORD_NAME]) {
      // if we don't get any specific name, seems a bit risky to send them all back. At least some * would be nice.
      return false;
    }

    // if both are provided, we can't proceed.
    if (node[RECORD_NAME_REGEX] && node[RECORD_NAME]) {
      return false;
    }

    // TODO: Add "type" paramater to just say, `config`, `metric`. May be handier.

    if (auto n = node[RECORD_TYPES]) {
      // if it's empty should be ok, will get all of them.
      if (n && n.IsSequence()) {
        auto const &passedTypes = n.as<std::vector<int>>();
        for (auto rt : passedTypes) {
          switch (rt) {
          case RECT_NULL:
          case RECT_CONFIG:
          case RECT_PROCESS:
          case RECT_NODE:
          case RECT_LOCAL:
          case RECT_PLUGIN:
          case RECT_ALL:
            info.recTypes.push_back(rt);
            break;
          default:
            // this field allows 1x1 match from the enum.
            // we may accept the bitwise being passed as param in the future.
            return false;
          }
        }
      }
    }

    if (auto n = node[RECORD_NAME_REGEX]) {
      info.recName = n.as<std::string>();
      info.isRegex = true;
    } else {
      info.recName = node[RECORD_NAME].as<std::string>();
      info.isRegex = false;
    }

    return true;
  }
};

template <> struct convert<ErrorInfo> {
  static Node
  encode(ErrorInfo const &errorInfo)
  {
    Node errorInfoNode;
    errorInfoNode[ERROR_CODE] = errorInfo.code;
    if (!errorInfo.message.empty()) {
      errorInfoNode[ERROR_MESSAGE] = errorInfo.message;
    }
    if (!errorInfo.recordName.empty()) {
      errorInfoNode[RECORD_NAME] = errorInfo.recordName;
    }

    return errorInfoNode;
  }
};
} // namespace YAML

namespace
{
static unsigned
bitwise(std::vector<int> const &values)
{
  unsigned recType = RECT_ALL;
  if (values.size() > 0) {
    auto it = std::begin(values);

    recType = *it;
    ++it;
    for (; it != std::end(values); ++it) {
      recType |= *it;
    }
  }

  return recType;
}

namespace utils = rpc::handlers::records::utils;
namespace err   = rpc::handlers::errors;

static auto
find_record_by_name(RequestRecordElement const &element)
{
  unsigned recType = bitwise(element.recTypes);

  return utils::get_yaml_record(element.recName, [recType](RecT rec_type, std::error_code &ec) {
    if ((recType & rec_type) == 0) {
      ec = err::RecordError::REQUESTED_TYPE_MISMATCH;
      return false;
    }
    return true;
  });
}

static auto
find_records_by_regex(RequestRecordElement const &element)
{
  unsigned recType = bitwise(element.recTypes);

  return utils::get_yaml_record_regex(element.recName, recType);
}

static auto
find_records(RequestRecordElement const &element)
{
  if (element.is_regex_req()) {
    return find_records_by_regex(element);
  }
  return find_record_by_name(element);
}

} // namespace

namespace rpc::handlers::records
{
namespace err = rpc::handlers::errors;

ts::Rv<YAML::Node>
lookup_records(std::string_view const &id, YAML::Node const &params)
{
  // TODO: we may want to deal with our own object instead of a node here.
  YAML::Node recordList, errorList;

  for (auto &&node : params) {
    RequestRecordElement recordElement;
    try {
      recordElement = node.as<RequestRecordElement>();
    } catch (YAML::Exception const &) {
      errorList.push_back(ErrorInfo{{err::RecordError::INVALID_INCOMING_DATA}});
      continue;
    }

    auto &&[recordNode, error] = find_records(recordElement);

    if (error) {
      ErrorInfo ei{error.value()};
      ei.recordName = recordElement.recName;

      errorList.push_back(ei);
      continue;
    }

    // Regex lookup, will get us back a sequence, of nodes. In this case we will add them one by 1 so we get a list of objects and
    // not a sequence inside the result object, this can be changed ofc but for now this is fine.
    if (recordNode.IsSequence()) {
      for (auto &&n : recordNode) {
        recordList.push_back(std::move(n));
      }
    } else if (recordNode.IsMap()) {
      recordList.push_back(std::move(recordNode));
    }
  }

  YAML::Node resp;
  if (!recordList.IsNull()) {
    resp["recordList"] = recordList;
  }
  if (!errorList.IsNull()) {
    resp["errorList"] = errorList;
  }
  return resp;
}

ts::Rv<YAML::Node>
clear_all_metrics_records(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;
  ts::Rv<YAML::Node> resp;
  if (RecResetStatRecord(RECT_NULL, true) != REC_ERR_OKAY) {
    return ts::Errata{rpc::handlers::errors::RecordError::RECORD_WRITE_ERROR};
  }

  return resp;
}

ts::Rv<YAML::Node>
clear_metrics_records(std::string_view const &id, YAML::Node const &params)
{
  using namespace rpc::handlers::records::utils;

  YAML::Node resp, errorList;

  for (auto &&element : params) {
    RequestRecordElement recordElement;
    try {
      recordElement = element.as<RequestRecordElement>();
    } catch (YAML::Exception const &) {
      errorList.push_back(ErrorInfo{{err::RecordError::INVALID_INCOMING_DATA}});
      continue;
    }

    if (!recordElement.recName.empty()) {
      if (RecResetStatRecord(recordElement.recName.data()) != REC_ERR_OKAY) {
        // This could be due the fact that the record is already cleared or the metric does not have any significant
        // value.
        ErrorInfo ei{err::RecordError::RECORD_WRITE_ERROR};
        ei.recordName = recordElement.recName;
        errorList.push_back(ei);
      }
    } else {
      errorList.push_back(ErrorInfo{{err::RecordError::INVALID_INCOMING_DATA}});
      continue;
    }
  }

  if (!errorList.IsNull()) {
    resp["errorList"] = errorList;
  }

  return resp;
}

} // namespace rpc::handlers::records
