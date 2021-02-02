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

#include <iostream>
#include <map>
#include <functional>
#include <tuple>
#include <forward_list>
#include <string_view>
#include <mutex>

#include "tscore/Errata.h"
#include "tscore/Diags.h"

#include "Defs.h"

namespace rpc
{
// forward
namespace json_codecs
{
  class yamlcpp_json_decoder;
  class yamlcpp_json_encoder;
} // namespace json_codecs

///
/// @brief JSONRPC registration and JSONRPC invocation logic  https://www.jsonrpc.org/specification
/// doc TBC
class JsonRPCManager
{
  using NotificationHandler = std::function<void(const YAML::Node &)>;
  using MethodHandler       = std::function<ts::Rv<YAML::Node>(std::string_view const &, const YAML::Node &)>;

private:
  /// @note In case we want to change the codecs and use another library, we just need to follow the same signatures @see
  /// yamlcpp_json_decoder and @see yamlcpp_json_encoder.
  // We use the yamlcpp library by default.
  using Decoder = json_codecs::yamlcpp_json_decoder;
  using Encoder = json_codecs::yamlcpp_json_encoder;

protected: // protected: For unit test.
  JsonRPCManager()                       = default;
  JsonRPCManager(JsonRPCManager const &) = delete;
  JsonRPCManager(JsonRPCManager &&)      = delete;
  JsonRPCManager &operator=(JsonRPCManager const &) = delete;
  JsonRPCManager &operator=(JsonRPCManager &&) = delete;

private:
  ///
  /// @brief Internal class to hold the registered handlers.
  ///
  /// It holds the method and notifications as well as provides the mechanism to call each particular handler.
  ///
  class Dispatcher
  {
    using response_type = std::pair<std::optional<specs::RPCResponseInfo>, std::error_code>;

  public:
    /// Add a method handler to the internal container
    /// @return True if was successfully added, False otherwise.
    template <typename Handler> bool add_handler(std::string const &name, Handler &&handler);

    /// Add a notification handler to the internal container
    /// @return True if was successfully added, False otherwise.
    template <typename Handler> bool add_notification_handler(std::string const &name, Handler &&handler);

    /// @brief Find and call the request's callback. If any error, the return type will have the specific error.
    ///        For notifications the @c RPCResponseInfo will not be set as part of the response. @c response_type
    response_type dispatch(specs::RPCRequestInfo const &request) const;

    /// @brief  Find a particular registered handler(method) by its associated name.
    /// @return A pair. The handler itself and a boolean flag indicating that the handler was found. If not found, second will
    ///         be false and the handler null.
    MethodHandler find_handler(const std::string &methodName) const;

    /// @brief  Find a particular registered handler(notification) by its associated name.
    /// @return A pair. The handler itself and a boolean flag indicating that the handler was found. If not found, second will
    ///         be false and the handler null.
    NotificationHandler find_notification_handler(const std::string &notificationName) const;

    /// @brief Removes a method handler
    bool remove_handler(std::string const &name);

    /// @brief Removes a notification method handler
    bool remove_notification_handler(std::string const &name);

    /// Register our own api to be exposed by the rpc.
    void register_api();

    // JSONRPC API - here for now.
    ts::Rv<YAML::Node> show_registered_handlers(std::string_view const &, const YAML::Node &);

  private:
    response_type invoke_handler(specs::RPCRequestInfo const &request) const;
    void invoke_notification_handler(specs::RPCRequestInfo const &notification, std::error_code &ec) const;

    std::unordered_map<std::string, MethodHandler> _methods;             ///< Registered handler methods container.
    std::unordered_map<std::string, NotificationHandler> _notifications; ///< Registered handler notification container.

    mutable std::mutex _mutex; ///< insert/find/delete mutex.
  };

public:
  /// Register an information function to show the registred handler we have in the RPC.
  void register_internal_api();

  ///
  /// @brief Add new registered method handler to the JSON RPC engine.
  ///
  /// @tparam Func The callback function type. \link specs::MethodHandler
  /// @param name Name to be exposed by the RPC Engine, this should match the incoming request. i.e: If you register 'get_stats'
  ///             then the incoming jsonrpc call should have this very same name in the 'method' field. .. {...'method':
  ///             'get_stats'...} .
  /// @param call The function to be regsitered.  \link specs::MethodHandler
  /// @return bool Boolean flag. true if the callback was successfully added, false otherwise
  ///
  /// @note @see \link specs::method_handler_from_member_function if the registered function is a member function.
  template <typename Func> bool add_handler(const std::string &name, Func &&call);

  ///
  /// @brief Remove handler from the registered method handlers.
  ///
  /// @param name Method name.
  /// @return true If all good.
  /// @return false If we could not remove it.
  ///
  bool remove_handler(std::string const &name);

  ///
  /// @brief Add new registered notification handler to the JSON RPC engine.
  ///
  /// @tparam Func The callback function type. \link NotificationHandler
  /// @param name Name to be exposed by the RPC Engine.
  /// @param call The callback function that needs to be regsitered.  \link NotificationHandler
  /// @return bool Boolean flag. true if the callback was successfully added, false otherwise
  ///
  /// @note @see \link specs::notification_handler_from_member_function if the registered function is a member function.
  template <typename Func> bool add_notification_handler(const std::string &name, Func &&call);

  ///
  /// @brief Remove handler from the registered notification handlers.
  ///
  /// @param name Method name.
  /// @return true If all good.
  /// @return false If we could not remove it.
  ///
  bool remove_notification_handler(std::string const &name);

  ///
  /// @brief This function handles the incoming jsonrpc request and distpatch the associated registered handler.
  ///
  /// @param jsonString The incoming jsonrpc 2.0 message. \link https://www.jsonrpc.org/specification
  /// @return std::optional<std::string> For methods, a valid jsonrpc 2.0 json string will be passed back. Notifications will not
  ///         contain any json back.
  ///
  std::optional<std::string> handle_call(std::string_view jsonString);

  ///
  /// @brief Get the instance of the whole RPC engine.
  ///
  /// @return JsonRPCManager& The JsonRPCManager protocol implementation object.
  ///
  static JsonRPCManager &
  instance()
  {
    static JsonRPCManager rpc;
    return rpc;
  }

private:
  // TODO: think about moving this out and let the server get access to this.
  Dispatcher _dispatcher;
};

/// Template impl.
template <typename Handler>
bool
JsonRPCManager::add_handler(const std::string &name, Handler &&call)
{
  return _dispatcher.add_handler(name, std::forward<Handler>(call));
}

template <typename Handler>
bool
JsonRPCManager::add_notification_handler(const std::string &name, Handler &&call)
{
  return _dispatcher.add_notification_handler(name, std::forward<Handler>(call));
}

template <typename Handler>
bool
JsonRPCManager::Dispatcher::add_handler(std::string const &name, Handler &&handler)
{
  std::lock_guard<std::mutex> lock(_mutex);
  return _methods.emplace(name, std::move(handler)).second;
}

template <typename Handler>
bool
JsonRPCManager::Dispatcher::add_notification_handler(std::string const &name, Handler &&handler)
{
  std::lock_guard<std::mutex> lock(_mutex);
  return _notifications.emplace(name, std::forward<Handler>(handler)).second;
}

/// Set of convenience API function. Users can avoid having to name the whole rps::JsonRPCManager::instance() to use the object.
///
/// @see JsonRPCManager::add_handler for details
template <typename Func>
inline bool
add_handler(const std::string &name, Func &&call)
{
  return JsonRPCManager::instance().add_handler(name, std::forward<Func>(call));
}
/// @see JsonRPCManager::remove_handler for details
inline bool
remove_handler(std::string const &name)
{
  return JsonRPCManager::instance().remove_handler(name);
}
/// @see JsonRPCManager::add_notification_handler for details
template <typename Func>
inline bool
add_notification_handler(const std::string &name, Func &&call)
{
  return JsonRPCManager::instance().add_notification_handler(name, std::forward<Func>(call));
}
/// @see JsonRPCManager::remove_notification_handler for details
inline bool
remove_notification_handler(std::string const &name)
{
  return JsonRPCManager::instance().remove_notification_handler(name);
}
} // namespace rpc
