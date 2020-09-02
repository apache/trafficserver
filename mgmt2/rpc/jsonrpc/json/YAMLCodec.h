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

#include <yaml-cpp/yaml.h>
#include "rpc/jsonrpc/error/RpcError.h"
#include "rpc/jsonrpc/Defs.h"

namespace rpc::jsonrpc
{
// placeholder for schema validation - Only on parameters node.

class yamlcpp_json_decoder
{
  static std::pair<RpcRequestInfo, std::error_code>
  decode(YAML::Node const &node)
  {
    // using namespace yaml::helper;

    // try it first so we can use for a possible error.
    RpcRequestInfo request;
    if (!node.IsDefined() || (node.Type() != YAML::NodeType::Map) || (node.size() == 0)) {
      // We only care about structure with elements.
      // If we do not check this now, when we query for a particular key we
      return {request, error::RpcErrorCode::INVALID_REQUEST};
    }

    try {
      {
        // All this may be obsolete if we decided to accept only strings.
        if (auto id = node["id"]) {
          if (id.IsNull()) {
            // if it's present, it should be valid.
            return {request, error::RpcErrorCode::NullId};
          }

          try {
            request.id = id.as<std::string>();
          } catch (YAML::Exception const &) {
            return {request, error::RpcErrorCode::InvalidIdType};
          }
        } // else ->  it's fine, could be a notification.
      }
      // version
      {
        if (auto version = node["jsonrpc"]) {
          try {
            request.jsonrpc = version.as<std::string>();

            if (request.jsonrpc != JSONRPC_VERSION) {
              return {request, error::RpcErrorCode::InvalidVersion};
            }
          } catch (YAML::Exception const &ex) {
            return {request, error::RpcErrorCode::InvalidVersionType};
          }
        } else {
          return {request, error::RpcErrorCode::MissingVersion};
        }
      }
      // method
      {
        if (auto method = node["method"]) {
          try {
            request.method = method.as<std::string>();
          } catch (YAML::Exception const &ex) {
            return {request, error::RpcErrorCode::InvalidMethodType};
          }
        } else {
          return {request, error::RpcErrorCode::MissingMethod};
        }
      }
      // params
      {
        if (auto params = node["params"]) {
          // TODO: check schema.
          switch (params.Type()) {
          case YAML::NodeType::Map:
          case YAML::NodeType::Sequence:
            break;
          default:
            return {request, error::RpcErrorCode::InvalidParamType};
          }
          request.params = std ::move(params);
        }
        // else -> params can be omitted
      }
    } catch (std::exception const &e) {
      // we want to keep the request as we will respond with a message.
      return {request, error::RpcErrorCode::PARSE_ERROR};
    }
    // TODO  We may want to extend the error handling and inform the user if there is one more field invalid
    // in the request, so far we notify only the first one, we can use the data field to add more errors in it.
    // ts::Errata
    return {request, {}};
  }

public:
  static RpcRequest
  extract(std::string_view request, std::error_code &ec)
  {
    RpcRequest msg;
    try {
      YAML::Node node = YAML::Load(request.data());
      switch (node.Type()) {
      case YAML::NodeType::Map: { // 4
        // single element
        msg.add_message(decode(node));
      } break;
      case YAML::NodeType::Sequence: { // 3
        // in case we get [] which seems to be a batch, but as there is no elements,
        // as per the jsonrpc 2.0 we should response with single element and with no
        // sequence.
        // trick to avoid sending the response as a sequence when we can a incoming empty sequence '[]'
        if (node.size() > 0) {
          // it's a batch
          msg.is_batch(true);
          msg.reserve(node.size());
        }

        // Make sure we have something to decode.
        if (node.size() > 0) {
          for (auto &&n : node) {
            msg.add_message(decode(n));
          }
        } else {
          // The node can be well-formed but invalid base on jsonrpc, ie: [].
          // In this case we mark  it as invalid.
          ec = jsonrpc::error::RpcErrorCode::INVALID_REQUEST;
        }
      } break;
      default:
        ec = jsonrpc::error::RpcErrorCode::INVALID_REQUEST;
        break;
      }
    } catch (YAML::Exception const &e) {
      ec = jsonrpc::error::RpcErrorCode::PARSE_ERROR;
    }
    return msg;
  }
};

class yamlcpp_json_encoder
{
public:
  static std::string
  encode(const RpcResponseInfo &resp)
  {
    YAML::Emitter json;
    json << YAML::DoubleQuoted << YAML::Flow;
    encode(resp, json);

    return json.c_str();
  }

  static std::string
  encode(const RpcResponse &response)
  {
    YAML::Emitter json;
    json << YAML::DoubleQuoted << YAML::Flow;
    {
      if (response.is_batch()) {
        json << YAML::BeginSeq;
      }

      for (const auto &resp : response.get_messages()) {
        encode(resp, json);
      }

      if (response.is_batch()) {
        json << YAML::EndSeq;
      }
    }

    return json.c_str();
  }

private:
  static void
  encode(const std::optional<std::string> &id, YAML::Emitter &json)
  {
    // workaround, we should find a better way, we should be able to us null.
    if (id) {
      json << YAML::Key << "id" << YAML::Value << *id;
    } else {
      // We do not insert null as it will break the json, we need null and not ~
      // json << YAML::Null;
    }
  }

  static void
  encode(RpcError const &error, ts::Errata const &errata, YAML::Emitter &json)
  {
    json << YAML::Key << "error";
    json << YAML::BeginMap;
    json << YAML::Key << "code" << YAML::Value << error.code;
    json << YAML::Key << "message" << YAML::Value << error.message;
    if (errata.size() > 0) {
      json << YAML::Key << "data";
      json << YAML::BeginSeq;
      for (auto const &err : errata) {
        json << YAML::BeginMap;
        json << YAML::Key << "code" << YAML::Value << err.getCode();
        json << YAML::Key << "message" << YAML::Value << err.text();
        json << YAML::EndMap;
      }
      json << YAML::EndSeq;
    }
    json << YAML::EndMap;
  }

  static void
  encode(RpcError const &error, YAML::Emitter &json)
  {
    encode(error, ts::Errata{}, json);
  }

  static void
  encode(ts::Errata const &errata, YAML::Emitter &json)
  {
    // static void encode(ts::Errata const& errata, YAML::Emitter &json) {
    std::error_code ec{jsonrpc::error::RpcErrorCode::ExecutionError};
    RpcError rpcError{ec.value(), ec.message()};
    encode(rpcError, errata, json);
  }

  static void
  encode(const RpcResponseInfo &resp, YAML::Emitter &json)
  {
    json << YAML::BeginMap;
    json << YAML::Key << "jsonrpc" << YAML::Value << JSONRPC_VERSION;

    // 1 - Error has preference, we ignore result on error.

    // 2 - Three kind of scenarios here.
    //     RPC error, User error or a valid response.
    //     so, if we have a rpc error we should set it, if not, then the error may have been
    //     flagged by the user in which case we use ExecutionError.

    if (resp.rpcError) {
      encode(*resp.rpcError, json);
    } else if (resp.callResult.errata.size() > 0) {
      encode(resp.callResult.errata, json);
    } else {
      // finally the result if no error.
      json << YAML::Key << "result" << YAML::Value;

      // hope the user set it. If not, make it safe.
      if (!resp.callResult.result.IsNull()) {
        json << resp.callResult.result;
      } else {
        // TODO: do we want to let it return null or by default we put success as there was no error.
        json << "success";
      }
    }

    encode(resp.id, json);
    json << YAML::EndMap;
  }
};
} // namespace rpc::jsonrpc
