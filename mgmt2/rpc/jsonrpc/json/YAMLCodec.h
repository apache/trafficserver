/**
  @file YAMLCodec.h
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
///
/// @note The overall design is to make this classes @c yamlcpp_json_decoder and @c yamlcpp_json_encoder plugables into the Json Rpc
/// encode/decode logic. yamlcpp does not give us all the behavior we need, such as the way it handles the null values. Json needs
/// to use literal null and yamlcpp uses ~. If this is a problem, then we may need to change the codec implementation, we just
/// follow the api and it should work with minimum changes.
///
///
/// The basic interface for the decoder to implement:
///
///     static RpcRequest extract(std::string_view request, std::error_code &ec)
///
/// and for the encoder to implement:
///
///  static std::string encode(const RpcResponseInfo &resp)
///  static std::string encode(const RpcResponse &response)
///

///
/// @brief Implements json to request object decoder.
/// This class converts and validate the incoming string into a request object.
///
class yamlcpp_json_decoder
{
  ///
  /// @brief Function that decodes and validate the fields base on the JSONRPC 2.0 protocol, All this is based on the specs for each
  /// field.
  ///
  /// @param node  @see YAML::Node that contains all the fields.
  /// @return std::pair<RpcRequestInfo, std::error_code> It returns a pair of request and an the error reporting.
  ///
  static std::pair<RpcRequestInfo, std::error_code>
  decode_and_validate(YAML::Node const &node) noexcept
  {
    RpcRequestInfo request;
    if (!node.IsDefined() || (node.Type() != YAML::NodeType::Map) || (node.size() == 0)) {
      // We only care about structures with elements.
      return {request, error::RpcErrorCode::INVALID_REQUEST};
    }

    try {
      // try the id first so we can use for a possible error.
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
    // TODO  We may want to extend the error handling and inform the user if there is  more than one invalid field in the request,
    // so far we notify only the first one, we can use the data field to add more errors in it. ts::Errata
    return {request, {/*ok*/}};
  }

public:
  ///
  /// @brief Decode a string, either json or yaml into a @see RpcRequest . @c ec will report the error if occurs @see RpcErrorCode
  ///
  /// @param request The string request, this should be either json or yaml.
  /// @param ec Output value, The error reporting.
  /// @return RpcRequest A valid rpc response object if no errors.
  ///
  static RpcRequest
  decode(std::string_view request, std::error_code &ec) noexcept
  {
    RpcRequest msg;
    try {
      YAML::Node node = YAML::Load(request.data());
      switch (node.Type()) {
      case YAML::NodeType::Map: { // 4
        // single element
        msg.add_message(decode_and_validate(node));
      } break;
      case YAML::NodeType::Sequence: { // 3
        // In case we get [] which is valid sequence but invalid jsonrpc message.
        if (node.size() > 0) {
          // it's a batch
          msg.is_batch(true);
          msg.reserve(node.size());

          for (auto &&n : node) {
            msg.add_message(decode_and_validate(n));
          }
        } else {
          // Valid json but invalid base on jsonrpc specs, ie: [].
          ec = jsonrpc::error::RpcErrorCode::INVALID_REQUEST;
        }
      } break;
      default:
        // Only Sequences or Objects are valid.
        ec = jsonrpc::error::RpcErrorCode::INVALID_REQUEST;
        break;
      }
    } catch (YAML::Exception const &e) {
      ec = jsonrpc::error::RpcErrorCode::PARSE_ERROR;
    }
    return msg;
  }
};

///
/// @brief Implements request to string encoder.
/// This class converts a request(including errors) into a json string.
///
class yamlcpp_json_encoder
{
  ///
  /// @brief Encode the ID if present.
  /// If the id is not present which could be interpret as null(should not happen), it will not be set into the emitter, it will be
  /// ignored. This is due the way that yamlcpp deals with the null, which instead of the literal null, it uses ~
  static void
  encode_id(const std::optional<std::string> &id, YAML::Emitter &json)
  {
    // workaround, we should find a better way, we should be able to use literal null if needed
    if (id) {
      json << YAML::Key << "id" << YAML::Value << *id;
    } else {
      // We do not insert null as it will break the json, we need literal null and not ~ (as per yaml)
      // json << YAML::Null;
    }
  }

  ///
  /// @brief Function to encode an error.
  /// Error could be from two sources, presence of @c std::error_code means a high level and the @c ts::Errata a callee . Both will
  /// be written into the passed @c out YAML::Emitter. This is mainly a convenience class for the other two encode_* functions.
  ///
  /// @param error std::error_code High level, main error.
  /// @param errata  the Errata from the callee
  /// @param json   output parameter. YAML::Emitter.
  ///
  static void
  encode_error(std::error_code error, ts::Errata const &errata, YAML::Emitter &json)
  {
    json << YAML::Key << "error";
    json << YAML::BeginMap;
    json << YAML::Key << "code" << YAML::Value << error.value();
    json << YAML::Key << "message" << YAML::Value << error.message();
    if (!errata.isOK()) {
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

  /// Convenience functions to call encode_error.
  static void
  encode_error(std::error_code error, YAML::Emitter &json)
  {
    ts::Errata errata{};
    encode_error(error, errata, json);
  }
  /// Convenience functions to call encode_error.
  static void
  encode_error(ts::Errata const &errata, YAML::Emitter &json)
  {
    encode_error({jsonrpc::error::RpcErrorCode::ExecutionError}, errata, json);
  }

  ///
  /// @brief Function to encode a single response(no batch) into an emitter.
  ///
  /// @param resp Response object
  /// @param json output yaml emitter
  ///
  static void
  encode(const RpcResponseInfo &resp, YAML::Emitter &json)
  {
    json << YAML::BeginMap;
    json << YAML::Key << "jsonrpc" << YAML::Value << JSONRPC_VERSION;

    // Important! As per specs, errors have preference over the result, we ignore result if error was set.

    if (resp.rpcError) {
      // internal library detected error: Decoding, etc.
      encode_error(resp.rpcError, json);
    }
    // Registered handler error: They have set the error on the response from the registered handler. This uses ExecutionError as
    // top error.
    else if (!resp.callResult.errata.isOK()) {
      encode_error(resp.callResult.errata, json);
    }
    // A valid response: The registered handler have set the proper result and no error was flagged.
    else {
      json << YAML::Key << "result" << YAML::Value;

      // Could be the case that the registered handler did not set the result. Make sure it was set before inserting it.
      if (!resp.callResult.result.IsNull()) {
        json << resp.callResult.result;
      } else {
        // TODO: do we want to let it return null or by default we put success when there was no error and no result either. Maybe
        // empty?
        json << "success";
      }
    }

    // insert the id.
    encode_id(resp.id, json);
    json << YAML::EndMap;
  }

public:
  ///
  /// @brief Convert @see RpcResponseInfo into a std::string.
  ///
  /// @param resp The rpc object to be converted @see RpcResponseInfo
  /// @return std::string The string representation of the response object
  ///
  static std::string
  encode(const RpcResponseInfo &resp)
  {
    YAML::Emitter json;
    json << YAML::DoubleQuoted << YAML::Flow;
    encode(resp, json);

    return json.c_str();
  }

  ///
  /// @brief Convert @see RpcResponse into a std::string, this handled a batch response.
  ///
  /// @param response The object to be converted
  /// @return std::string the string representation of the response object after being encode.
  ///
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
};
} // namespace rpc::jsonrpc
