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
#include <variant>
#include <mutex>

#include "tscore/Diags.h"
#include "ts/apidefs.h"

#include "mgmt/rpc/jsonrpc/Defs.h"
#include "mgmt/rpc/jsonrpc/Context.h"

namespace rpc
{
// forward
namespace json_codecs
{
  class yamlcpp_json_decoder;
  class yamlcpp_json_encoder;
} // namespace json_codecs

///
/// @brief This class keeps all relevant @c RPC provider's info.
///
struct RPCRegistryInfo {
  std::string_view provider; ///< Who's the rpc endpoint provider, could be ATS or a plugins. When requesting the service info from
                             ///< the rpc node, this will be part of the service info.
};

///
/// @brief JSONRPC registration and JSONRPC invocation logic  https://www.jsonrpc.org/specification
/// doc TBC
class JsonRPCManager
{
private:
  /// @note In case we want to change the codecs and use another library, we just need to follow the same signatures @see
  /// yamlcpp_json_decoder and @see yamlcpp_json_encoder.
  // We use the yamlcpp library by default.
  using Decoder = json_codecs::yamlcpp_json_decoder;
  using Encoder = json_codecs::yamlcpp_json_encoder;

public:
  // Possible RPC method signatures.
  using MethodHandlerSignature       = std::function<swoc::Rv<YAML::Node>(std::string_view const &, const YAML::Node &)>;
  using PluginMethodHandlerSignature = std::function<void(std::string_view const &, const YAML::Node &)>;
  using NotificationHandlerSignature = std::function<void(const YAML::Node &)>;

  ///
  /// @brief Add new registered method handler to the JSON RPC engine.
  ///
  /// @tparam Func The callback function type. See @c MethodHandlerSignature
  /// @param name Name to be exposed by the RPC Engine, this should match the incoming request. i.e: If you register 'get_stats'
  ///             then the incoming jsonrpc call should have this very same name in the 'method' field. .. {...'method':
  ///             'get_stats'...} .
  /// @param call The function handler.
  /// @param info RPCRegistryInfo pointer.
  /// @param opt  Handler options, used to pass information about the registered handler.
  /// @return bool Boolean flag. true if the callback was successfully added, false otherwise
  ///
  template <typename Func>
  bool add_method_handler(std::string_view name, Func &&call, const RPCRegistryInfo *info, TSRPCHandlerOptions const &opt);

  ///
  /// @brief Add new registered method handler(from a plugin scope) to the JSON RPC engine.
  ///
  /// Function to add a new RPC handler from a plugin context. This will be invoked by @c TSRPCRegisterMethodHandler. If you
  /// register your handler by using this API then you have to  express the result of the processing by either calling
  /// @c TSInternalHandlerDone or @c TSInternalHandlerError in case of an error.
  /// When a function registered by this mechanism gets called, the response from the handler will not matter, instead we will rely
  /// on what the @c TSInternalHandlerDone or @c TSInternalHandlerError have set.
  ///
  /// @note If you are not a plugin, do not call this function. Use @c add_method_handler instead.
  ///
  /// @tparam Func The callback function type. See @c PluginMethodHandlerSignature
  /// @param name Name to be exposed by the RPC Engine, this should match the incoming request. i.e: If you register 'get_stats'
  ///             then the incoming jsonrpc call should have this very same name in the 'method' field. .. {...'method':
  ///             'get_stats'...} .
  /// @param call The function handler.
  /// @param info RPCRegistryInfo pointer.
  /// @param opt  Handler options, used to pass information about the registered handler.
  /// @return bool Boolean flag. true if the callback was successfully added, false otherwise
  ///
  template <typename Func>
  bool add_method_handler_from_plugin(const std::string &name, Func &&call, const RPCRegistryInfo *info,
                                      TSRPCHandlerOptions const &opt);

  ///
  /// @brief Add new registered notification handler to the JSON RPC engine.
  ///
  /// @tparam Func The callback function type. See @c NotificationHandlerSignature
  /// @param name Name to be exposed by the RPC Engine.
  /// @param call The callback function that needs handler.
  /// @param info RPCRegistryInfo pointer.
  /// @param opt  Handler options, used to pass information about the registered handler.
  /// @return bool Boolean flag. true if the callback was successfully added, false otherwise
  ///
  template <typename Func>
  bool add_notification_handler(std::string_view name, Func &&call, const RPCRegistryInfo *info, TSRPCHandlerOptions const &opt);

  ///
  /// @brief This function handles the incoming jsonrpc request and dispatch the associated registered handler.
  ///
  /// @param ctx @c Context object used pass information between rpc layers.
  /// @param jsonString The incoming jsonrpc 2.0 message. \link https://www.jsonrpc.org/specification
  /// @return std::optional<std::string> For methods, a valid jsonrpc 2.0 json string will be passed back. Notifications will not
  ///         contain any json back.
  ///
  std::optional<std::string> handle_call(Context const &ctx, std::string const &jsonString);

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

protected: // For unit test.
  JsonRPCManager()                                  = default;
  JsonRPCManager(JsonRPCManager const &)            = delete;
  JsonRPCManager(JsonRPCManager &&)                 = delete;
  JsonRPCManager &operator=(JsonRPCManager const &) = delete;
  JsonRPCManager &operator=(JsonRPCManager &&)      = delete;

  ///
  /// @brief Remove handler from the registered method handlers. Test only.
  ///
  /// @param name Method name.
  /// @return true If all is good.
  /// @return false If we could not remove it.
  ///
  bool        remove_handler(std::string_view name);
  friend bool test_remove_handler(std::string_view name);

private:
  ///
  /// @brief Internal class that holds and handles the dispatch logic.
  ///
  /// It holds methods and notifications as well as provides the mechanism to call each particular handler.
  ///
  /// Design notes:
  ///
  /// This class holds a std::unordered_map<std::string, InternalHandler> as a main table for all the callbacks.
  /// The @c InternalHandler wraps a std::variant with the supported handler types, depending on each handler type the invocation
  /// varies. All handlers gets call synchronously with the difference that for Plugin handlers (see @c PluginMethod) we will wait
  /// for the response to be set, plugins are provided with an API to deal with different responses(success or error), plugins do
  /// not require to respond to the callback with a response, see @c PluginMethodHandlerSignature .
  /// @c FunctionWrapper class holds the actual @c std::function<T> object, this class is needed to make easy to handle equal
  /// signatures inside a @c std::variant
  class Dispatcher
  {
    /// The response type used internally, notifications won't fill in the optional response.  Internal response's @ ec will be set
    /// in case of any error.
    using response_type = std::optional<specs::RPCResponseInfo>;

    ///
    /// @brief Class that wraps the actual std::function<T>.
    ///
    /// @tparam T Handler signature See @c MethodHandlerSignature @c PluginMethodHandlerSignature @c NotificationHandlerSignature
    ///
    template <typename T> struct FunctionWrapper {
      FunctionWrapper(T &&t) : cb(std::forward<T>(t)) {}

      T cb; ///< Function handler (std::function<T>)
    };

    struct InternalHandler; ///< fw declaration
    /// To avoid long lines.
    using InternalHandlers = std::unordered_map<std::string, InternalHandler>;

  public:
    Dispatcher();
    /// Add a method handler to the internal container
    /// @return True if was successfully added, False otherwise.
    template <typename FunctionWrapperType, typename Handler>
    bool add_handler(std::string_view name, Handler &&handler, const RPCRegistryInfo *info, TSRPCHandlerOptions const &opt);

    /// Find and call the request's callback. If any error occurs, the return type will have the specific error.
    /// For notifications the @c RPCResponseInfo will not be set as part of the response. @c response_type
    response_type dispatch(Context const &ctx, specs::RPCRequestInfo const &request) const;

    /// Find a particular registered handler(method) by its associated name.
    /// @return A pair. The handler itself and a boolean flag indicating that the handler was found. If not found, second will
    ///         be false and the handler null.
    InternalHandler const &find_handler(specs::RPCRequestInfo const &request, std::error_code &ec) const;
    /// Removes a method handler. Unit test mainly.
    bool remove_handler(std::string_view name);

    // JSONRPC API - here for now.
    swoc::Rv<YAML::Node> show_registered_handlers(std::string_view const &, const YAML::Node &);
    swoc::Rv<YAML::Node> get_service_descriptor(std::string_view const &, const YAML::Node &);

    // Supported handler endpoint types.
    using Method       = FunctionWrapper<MethodHandlerSignature>;
    using PluginMethod = FunctionWrapper<PluginMethodHandlerSignature>;
    // Plugins and non plugins handlers have no difference from the point of view of the RPC manager, we call and we do not expect
    // for the work to be finished. Notifications have no response at all.
    using Notification = FunctionWrapper<NotificationHandlerSignature>;

  private:
    /// Register our own api into the RPC manager. This will expose the methods and notifications registered in the RPC
    void register_service_descriptor_handler();

    // Functions to deal with the handler invocation.
    response_type invoke_method_handler(InternalHandler const &handler, specs::RPCRequestInfo const &request) const;
    response_type invoke_notification_handler(InternalHandler const &handler, specs::RPCRequestInfo const &request) const;

    ///
    /// @brief Class that wraps callable objects of any RPC specific type. If provided, this class also holds a valid registry
    /// information
    ///
    /// This class holds the actual callable object from one of our supported types, this helps us to
    /// simplify the logic to insert and fetch callable objects from our container.
    struct InternalHandler {
      InternalHandler() = default;
      InternalHandler(const RPCRegistryInfo *info, TSRPCHandlerOptions const &opt) : _regInfo(info), _options(opt) {}
      /// Sets the handler.
      template <class T, class F> void set_callback(F &&t);
      explicit                         operator bool() const;
      bool                             operator!() const;
      /// Invoke the actual handler callback.
      swoc::Rv<YAML::Node> invoke(specs::RPCRequestInfo const &request) const;
      /// Check if the handler was registered as method.
      bool is_method() const;

      /// Returns the internal registry info.
      const RPCRegistryInfo *
      get_reg_info() const
      {
        return _regInfo;
      }

      /// Returns the configured options associated with this particular handler.
      TSRPCHandlerOptions const &
      get_options() const
      {
        return _options;
      }

    private:
      // We need to keep this match with the order of types in the _func variant. This will help us to identify the holding type.
      enum class VariantTypeIndexId : std::size_t { NOTIFICATION = 1, METHOD = 2, METHOD_FROM_PLUGIN = 3 };
      // We support these three for now. This can easily be extended to support other signatures.
      // that's one of the main points of the InternalHandler
      std::variant<std::monostate, Notification, Method, PluginMethod> _func;
      const RPCRegistryInfo                                           *_regInfo =
        nullptr; ///< Can hold internal information about the handler, this could be null as it is optional.
                 ///< This pointer can eventually holds important information about the call.
      TSRPCHandlerOptions _options;
    };
    // We will keep all the handlers wrapped inside the InternalHandler class, this will help us
    // to have a single container for all the types(method, notification & plugin method(cond var)).
    InternalHandlers   _handlers; ///< Registered handler container.
    mutable std::mutex _mutex;    ///< insert/find/delete mutex.
  };

  Dispatcher _dispatcher; ///< Internal handler container and dispatcher logic object.
};

// ------------------------------ JsonRPCManager -------------------------------
template <typename Handler>
bool
JsonRPCManager::add_method_handler(std::string_view name, Handler &&call, const RPCRegistryInfo *info,
                                   TSRPCHandlerOptions const &opt)
{
  return _dispatcher.add_handler<Dispatcher::Method, Handler>(name, std::forward<Handler>(call), info, opt);
}
template <typename Handler>
bool
JsonRPCManager::add_method_handler_from_plugin(const std::string &name, Handler &&call, const RPCRegistryInfo *info,
                                               TSRPCHandlerOptions const &opt)
{
  return _dispatcher.add_handler<Dispatcher::PluginMethod, Handler>(name, std::forward<Handler>(call), info, opt);
}

template <typename Handler>
bool
JsonRPCManager::add_notification_handler(std::string_view name, Handler &&call, const RPCRegistryInfo *info,
                                         TSRPCHandlerOptions const &opt)
{
  return _dispatcher.add_handler<Dispatcher::Notification, Handler>(name, std::forward<Handler>(call), info, opt);
}

// ----------------------------- InternalHandler -------------------------------
template <class T, class F>
void
JsonRPCManager::Dispatcher::InternalHandler::set_callback(F &&f)
{
  // T would be one of the handler endpoint types.
  _func = T{std::forward<F>(f)};
}
inline JsonRPCManager::Dispatcher::InternalHandler::operator bool() const
{
  return _func.index() != 0;
}
bool inline JsonRPCManager::Dispatcher::InternalHandler::operator!() const
{
  return _func.index() == 0;
}

// ----------------------------- Dispatcher ------------------------------------
template <typename FunctionWrapperType, typename Handler>
bool
JsonRPCManager::Dispatcher::add_handler(std::string_view name, Handler &&handler, const RPCRegistryInfo *info,
                                        TSRPCHandlerOptions const &opt)
{
  std::lock_guard<std::mutex> lock(_mutex);
  InternalHandler             call{info, opt};
  call.set_callback<FunctionWrapperType>(std::forward<Handler>(handler));
  return _handlers.emplace(name, std::move(call)).second;
}

} // namespace rpc
