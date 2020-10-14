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

#include "JsonRpc.h"

#include <iostream>
#include <chrono>
#include <system_error>

#include "json/YAMLCodec.h"

namespace rpc
{
static constexpr auto logTag = "rpc";

namespace error = jsonrpc::error;

JsonRpc::Dispatcher::response_type
JsonRpc::Dispatcher::dispatch(jsonrpc::RpcRequestInfo const &request) const
{
  if (request.is_notification()) {
    std::error_code ec;
    invoke_notification_handler(request, ec);
    if (ec) {
      return {std::nullopt, ec};
    }
  } else {
    return invoke_handler(request);
  }

  return {};
}

jsonrpc::MethodHandler
JsonRpc::Dispatcher::find_handler(const std::string &methodName) const
{
  std::lock_guard<std::mutex> lock(_mutex);

  auto search = _methods.find(methodName);
  return search != std::end(_methods) ? search->second : nullptr;
}

jsonrpc::NotificationHandler
JsonRpc::Dispatcher::find_notification_handler(const std::string &notificationName) const
{
  std::lock_guard<std::mutex> lock(_mutex);

  auto search = _notifications.find(notificationName);
  return search != std::end(_notifications) ? search->second : nullptr;
}

JsonRpc::Dispatcher::response_type
JsonRpc::Dispatcher::invoke_handler(jsonrpc::RpcRequestInfo const &request) const
{
  auto handler = find_handler(request.method);
  if (!handler) {
    return {std::nullopt, error::RpcErrorCode::METHOD_NOT_FOUND};
  }
  jsonrpc::RpcResponseInfo response{request.id};

  try {
    auto const &rv = handler(*request.id, request.params);

    if (rv.isOK()) {
      response.callResult.result = rv.result();
    } else {
      // if we have some errors to log, then include it.
      response.callResult.errata = rv.errata();
    }
  } catch (std::exception const &e) {
    Debug(logTag, "Ups, something happened during the callback invocation: %s", e.what());
    return {std::nullopt, error::RpcErrorCode::INTERNAL_ERROR};
  }

  return {response, {}};
}

void
JsonRpc::Dispatcher::invoke_notification_handler(jsonrpc::RpcRequestInfo const &notification, std::error_code &ec) const
{
  auto handler = find_notification_handler(notification.method);
  if (!handler) {
    ec = error::RpcErrorCode::METHOD_NOT_FOUND;
    return;
  }
  try {
    handler(notification.params);
  } catch (std::exception const &e) {
    Debug(logTag, "Ups, something happened during the callback(notification) invocation: %s", e.what());
    // it's a notification so we do not care much.
  }
}

bool
JsonRpc::Dispatcher::remove_handler(std::string const &name)
{
  std::lock_guard<std::mutex> lock(_mutex);
  auto foundIt = std::find_if(std::begin(_methods), std::end(_methods), [&](auto const &p) { return p.first == name; });
  if (foundIt != std::end(_methods)) {
    _methods.erase(foundIt);
    return true;
  }

  return false;
}

bool
JsonRpc::Dispatcher::remove_notification_handler(std::string const &name)
{
  std::lock_guard<std::mutex> lock(_mutex);
  auto foundIt = std::find_if(std::begin(_notifications), std::end(_notifications), [&](auto const &p) { return p.first == name; });
  if (foundIt != std::end(_notifications)) {
    _notifications.erase(foundIt);
    return true;
  }

  return false;
}

// RPC

void
JsonRpc::register_internal_api()
{
  // register. TMP
  if (!_dispatcher.add_handler("show_registered_handlers",
                               // TODO: revisit this.
                               [this](std::string_view const &id, const YAML::Node &req) -> ts::Rv<YAML::Node> {
                                 return _dispatcher.show_registered_handlers(id, req);
                               })) {
    Warning("Something happened, we couldn't register the rpc show_registered_handlers handler");
  }
}

bool
JsonRpc::remove_handler(std::string const &name)
{
  return _dispatcher.remove_handler(name);
}

bool
JsonRpc::remove_notification_handler(std::string const &name)
{
  return _dispatcher.remove_notification_handler(name);
}

static inline jsonrpc::RpcResponseInfo
make_error_response(jsonrpc::RpcRequestInfo const &req, std::error_code const &ec)
{
  jsonrpc::RpcResponseInfo resp;

  // we may have been able to collect the id, if so, use it.
  if (req.id) {
    resp.id = req.id;
  }

  resp.rpcError = ec; // std::make_optional<jsonrpc::RpcError>(ec.value(), ec.message());

  return resp;
}

static inline jsonrpc::RpcResponseInfo
make_error_response(std::error_code const &ec)
{
  jsonrpc::RpcResponseInfo resp;

  resp.rpcError = ec;
  // std::make_optional<jsonrpc::RpcError>(ec.value(), ec.message());
  return resp;
}

std::optional<std::string>
JsonRpc::handle_call(std::string_view request)
{
  Debug(logTag, "Incoming request '%s'", request.data());

  std::error_code ec;
  try {
    // let's decode all the incoming messages into our own types.
    jsonrpc::RpcRequest const &msg = Decoder::decode(request, ec);

    // If any error happened within the request, they will be kept inside each
    // particular request, as they would need to be converted back in a proper error response.
    if (ec) {
      auto response = make_error_response(ec);
      return Encoder::encode(response);
    }

    jsonrpc::RpcResponse response{msg.is_batch()};
    for (auto const &[req, decode_error] : msg.get_messages()) {
      // As per jsonrpc specs we do care about invalid messages as long as they are well-formed,  our decode logic will make their
      // best to build up a request, if any errors were detected during decoding, we will save the error and make it part of the
      // RPCRequest elements for further use.
      if (!decode_error) {
        // request seems ok and ready to be dispatched. The dispatcher will tell us if the method exist and if so, it will dispatch
        // the call and gives us back the response.
        auto &&[encodedResponse, ec] = _dispatcher.dispatch(req);

        // On any error, ec will have a value
        if (!ec) {
          // we only get valid responses if it was a method request, not
          // for notifications.
          if (encodedResponse) {
            response.add_message(*encodedResponse);
          }
        } else {
          // get an error response, we may have the id, so let's try to use it.
          response.add_message(make_error_response(req, ec));
        }

      } else {
        // If the request was marked as error(decode error), we still need to send the error back, so we save it.
        response.add_message(make_error_response(req, decode_error));
      }
    }

    // We will not have a response for notification(s); This could be a batch of notifications only.
    return response.is_notification() ? std::nullopt : std::make_optional<std::string>(Encoder::encode(response));

  } catch (std::exception const &ex) {
    ec = jsonrpc::error::RpcErrorCode::INTERNAL_ERROR;
  }

  return {Encoder::encode(make_error_response(ec))};
}

ts::Rv<YAML::Node>
JsonRpc::Dispatcher::show_registered_handlers(std::string_view const &, const YAML::Node &)
{
  ts::Rv<YAML::Node> resp;
  // use the same lock?
  std::lock_guard<std::mutex> lock(_mutex);
  for (auto const &m : _methods) {
    std::string sm = m.first;
    resp.result()["methods"].push_back(sm);
  }

  for (auto const &m : _notifications) {
    std::string sm = m.first;
    resp.result()["notifications"].push_back(sm);
  }

  return resp;
}
} // namespace rpc