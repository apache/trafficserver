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

#include "mgmt/rpc/jsonrpc/JsonRPCManager.h"

#include <iostream>
#include <chrono>
#include <system_error>
#include <mutex>
#include <condition_variable>

#include <swoc/swoc_meta.h>

#include "mgmt/rpc/jsonrpc/json/YAMLCodec.h"

namespace
{
// RPC service info constants.
const std::string RPC_SERVICE_METHOD_STR{"method"};
const std::string RPC_SERVICE_NOTIFICATION_STR{"notification"};
const std::string RPC_SERVICE_NAME_KEY{"name"};
const std::string RPC_SERVICE_TYPE_KEY{"type"};
const std::string RPC_SERVICE_PROVIDER_KEY{"provider"};
const std::string RPC_SERVICE_SCHEMA_KEY{"schema"};
const std::string RPC_SERVICE_METHODS_KEY{"methods"};
const std::string RPC_SERVICE_NOTIFICATIONS_KEY{"notifications"};
const std::string RPC_SERVICE_PRIVILEGED_KEY{"privileged"};
const std::string RPC_SERVICE_N_A_STR{"N/A"};

// jsonrpc log tag.
constexpr auto logTag    = "rpc";
constexpr auto logTagMsg = "rpc.msg";
} // namespace

namespace rpc
{
RPCRegistryInfo core_ats_rpc_service_provider_handle = {
  "Traffic Server JSONRPC 2.0 API" // Provider's description
};

// plugin rpc handling variables.
std::mutex g_rpcHandlingMutex;
std::condition_variable g_rpcHandlingCompletion;
swoc::Rv<YAML::Node> g_rpcHandlerResponseData;
bool g_rpcHandlerProcessingCompleted{false};

// --- Helpers
swoc::Errata
check_for_blockers(Context const &ctx, TSRPCHandlerOptions const &options)
{
  if (auto err = ctx.get_auth().is_blocked(options); !err.is_ok()) {
    return std::move(err.note(swoc::Errata(std::error_code(unsigned(error::RPCErrorCode::Unauthorized), std::generic_category()),
                                           ERRATA_ERROR, swoc::Errata::AUTO)));
  }
  return {};
}

// --- Dispatcher
JsonRPCManager::Dispatcher::Dispatcher()
{
  register_service_descriptor_handler();
}
void
JsonRPCManager::Dispatcher::register_service_descriptor_handler()
{
  if (!this->add_handler<Dispatcher::Method, MethodHandlerSignature>(
        "show_registered_handlers",
        [this](std::string_view const &id, const YAML::Node &req) -> swoc::Rv<YAML::Node> {
          return show_registered_handlers(id, req);
        },
        &core_ats_rpc_service_provider_handle, {{NON_RESTRICTED_API}})) {
    Warning("Handler already registered.");
  }

  if (!this->add_handler<Dispatcher::Method, MethodHandlerSignature>(
        "get_service_descriptor",
        [this](std::string_view const &id, const YAML::Node &req) -> swoc::Rv<YAML::Node> {
          return get_service_descriptor(id, req);
        },
        &core_ats_rpc_service_provider_handle, {{NON_RESTRICTED_API}})) {
    Warning("Handler already registered.");
  }
}

JsonRPCManager::Dispatcher::response_type
JsonRPCManager::Dispatcher::dispatch(Context const &ctx, specs::RPCRequestInfo const &request) const
{
  std::error_code ec;
  auto const &handler = find_handler(request, ec);

  if (ec) {
    specs::RPCResponseInfo resp{request.id};
    resp.error.ec = ec;
    return resp;
  }

  // We have got a valid handler, we will now check if the context holds any restriction for this handler to be called.
  if (auto errata = check_for_blockers(ctx, handler.get_options()); !errata.is_ok()) {
    specs::RPCResponseInfo resp{request.id};
    resp.error.ec   = ec;
    resp.error.data = std::move(errata);
    return resp;
  }

  if (request.is_notification()) {
    return invoke_notification_handler(handler, request);
  }

  // just a method call.
  return invoke_method_handler(handler, request);
}

JsonRPCManager::Dispatcher::InternalHandler const &
JsonRPCManager::Dispatcher::find_handler(specs::RPCRequestInfo const &request, std::error_code &ec) const
{
  static InternalHandler no_handler{};

  std::lock_guard<std::mutex> lock(_mutex);

  InternalHandlers::const_iterator search = _handlers.find(request.method);

  if (search == std::end(_handlers)) {
    // no more checks, no handler either notification or method
    ec = error::RPCErrorCode::METHOD_NOT_FOUND;
    return no_handler;
  } // Handler's method type should match the requested method type.
  else if ((request.is_method() && search->second.is_method()) || (request.is_notification() && !search->second.is_method())) {
    return search->second;
  }

  ec = error::RPCErrorCode::INVALID_REQUEST;
  return no_handler;
}

JsonRPCManager::Dispatcher::response_type
JsonRPCManager::Dispatcher::invoke_method_handler(JsonRPCManager::Dispatcher::InternalHandler const &handler,
                                                  specs::RPCRequestInfo const &request) const
{
  specs::RPCResponseInfo response{request.id};

  try {
    auto rv = handler.invoke(request);

    if (rv.is_ok()) {
      response.callResult.result = rv.result();
    } else {
      // if we have some errors to log, then include it.
      response.callResult.errata = std::move(rv.errata());
    }
  } catch (std::exception const &e) {
    Debug(logTag, "Oops, something happened during the callback invocation: %s", e.what());
    response.error.ec = error::RPCErrorCode::ExecutionError;
  }

  return response;
}

JsonRPCManager::Dispatcher::response_type
JsonRPCManager::Dispatcher::invoke_notification_handler(JsonRPCManager::Dispatcher::InternalHandler const &handler,
                                                        specs::RPCRequestInfo const &notification) const
{
  try {
    handler.invoke(notification);
  } catch (std::exception const &e) {
    Debug(logTag, "Oops, something happened during the callback(notification) invocation: %s", e.what());
    // it's a notification so we do not care much.
  }

  return {std::nullopt};
}

bool
JsonRPCManager::Dispatcher::remove_handler(std::string_view name)
{
  std::lock_guard<std::mutex> lock(_mutex);
  auto foundIt = std::find_if(std::begin(_handlers), std::end(_handlers), [&](auto const &p) { return p.first == name; });
  if (foundIt != std::end(_handlers)) {
    _handlers.erase(foundIt);
    return true;
  }

  return false;
}
// --- JsonRPCManager
bool
JsonRPCManager::remove_handler(std::string_view name)
{
  return _dispatcher.remove_handler(name);
}

std::optional<std::string>
JsonRPCManager::handle_call(Context const &ctx, std::string const &request)
{
  Debug(logTagMsg, "--> JSONRPC request\n'%s'", request.c_str());

  std::error_code ec;
  try {
    // let's decode all the incoming messages into our own types.
    specs::RPCRequest const &msg = Decoder::decode(request, ec);

    // If any error happened within the request, they will be kept inside each
    // particular request, as they would need to be converted back in a proper error response.
    if (ec) {
      specs::RPCResponseInfo resp;
      resp.error.ec = ec;
      return Encoder::encode(resp);
    }

    specs::RPCResponse response{msg.is_batch()};
    for (auto const &[req, decode_error] : msg.get_messages()) {
      // As per jsonrpc specs we do care about invalid messages as long as they are well-formed,  our decode logic will make their
      // best to build up a request, if any errors were detected during decoding, we will save the error and make it part of the
      // RPCRequest elements for further use.
      if (!decode_error) {
        // request seems ok and ready to be dispatched. The dispatcher will tell us if the method exist and if so, it will dispatch
        // the call and gives us back the response.
        auto encodedResponse = _dispatcher.dispatch(ctx, req);

        if (encodedResponse) {
          // if any error was detected during invocation or before, the response will have the error field set, so this will
          // internally be converted to the right response type.
          response.add_message(std::move(*encodedResponse));
        } // else it's a notification and no error.

      } else {
        // If the request was marked as an error(decode error), we still need to send the error back, so we save it.
        specs::RPCResponseInfo resp{req.id};
        // resp.error.assign(swoc::Errata(decode_error));
        resp.error.ec = decode_error;
        response.add_message(std::move(resp));
      }
    }

    // We will not have a response for notification(s); This could be a batch of notifications only.
    std::optional<std::string> resp;

    if (!response.is_notification()) {
      resp = Encoder::encode(response);
      Debug(logTagMsg, "<-- JSONRPC Response\n '%s'", (*resp).c_str());
    }

    return resp;
  } catch (std::exception const &ex) {
    ec = error::RPCErrorCode::INTERNAL_ERROR;
  }

  specs::RPCResponseInfo resp;
  resp.error.ec = ec;
  return {Encoder::encode(resp)};
}

// ---------------------------- InternalHandler ---------------------------------
inline swoc::Rv<YAML::Node>
JsonRPCManager::Dispatcher::InternalHandler::invoke(specs::RPCRequestInfo const &request) const
{
  swoc::Rv<YAML::Node> ret;
  std::visit(swoc::meta::vary{[](std::monostate) -> void { /* no op */ },
                              [&request](Notification const &handler) -> void {
                                // Notification handler call. Ignore response, there is no completion cv check in here basically
                                // because we do  not deal with any response, the callee can just re-schedule the work if
                                // needed. We fire and forget.
                                handler.cb(request.params);
                              },
                              [&ret, &request](Method const &handler) -> void {
                                // Regular Method Handler call, No cond variable check here, this should have not be created by
                                // a plugin.
                                ret = handler.cb(request.id, request.params);
                              },
                              [&ret, &request](PluginMethod const &handler) -> void {
                                // We call the method handler, we'll lock and wait till the condition_variable
                                // gets set on the other side. The handler may return immediately with no response being set.
                                // cond var will give us green to proceed.
                                handler.cb(request.id, request.params);
                                std::unique_lock<std::mutex> lock(g_rpcHandlingMutex);
                                g_rpcHandlingCompletion.wait(lock, []() { return g_rpcHandlerProcessingCompleted; });
                                g_rpcHandlerProcessingCompleted = false;
                                // swoc::Rv this will handle both, error and success cases.
                                ret = std::move(g_rpcHandlerResponseData);
                                lock.unlock();
                              }},
             this->_func);
  return ret;
}

inline bool
JsonRPCManager::Dispatcher::InternalHandler::is_method() const
{
  const auto index = static_cast<InternalHandler::VariantTypeIndexId>(_func.index());
  switch (index) {
  case VariantTypeIndexId::METHOD:
  case VariantTypeIndexId::METHOD_FROM_PLUGIN:
    return true;
    break;
  default:;
  }
  // For now, we say, if not method, it's a notification.
  return false;
}

swoc::Rv<YAML::Node>
JsonRPCManager::Dispatcher::show_registered_handlers(std::string_view const &, const YAML::Node &)
{
  swoc::Rv<YAML::Node> resp;
  std::lock_guard<std::mutex> lock(_mutex);
  for (auto const &[name, handler] : _handlers) {
    std::string const &key = handler.is_method() ? RPC_SERVICE_METHODS_KEY : RPC_SERVICE_NOTIFICATIONS_KEY;
    resp.result()[key].push_back(name);
  }
  return resp;
}

// -----------------------------------------------------------------------------
// This jsonrpc handler can provides a service descriptor for the RPC
swoc::Rv<YAML::Node>
JsonRPCManager::Dispatcher::get_service_descriptor(std::string_view const &, const YAML::Node &)
{
  YAML::Node rpcService;
  std::lock_guard<std::mutex> lock(_mutex);

  for (auto const &[name, handler] : _handlers) {
    YAML::Node method;
    method[RPC_SERVICE_NAME_KEY] = name;
    method[RPC_SERVICE_TYPE_KEY] = handler.is_method() ? RPC_SERVICE_METHOD_STR : RPC_SERVICE_NOTIFICATION_STR;
    /* most of this information will be eventually populated from the RPCRegistryInfo object */
    auto regInfo = handler.get_reg_info();
    std::string provider;
    if (regInfo && regInfo->provider.size()) {
      provider = std::string{regInfo->provider};
    } else {
      provider = RPC_SERVICE_N_A_STR;
    }
    method[RPC_SERVICE_PROVIDER_KEY]   = provider;
    method[RPC_SERVICE_PRIVILEGED_KEY] = handler.get_options().auth.restricted;
    YAML::Node schema{YAML::NodeType::Map}; // no schema for now, but we have a placeholder for it. Schema should provide
                                            // description and all the details about the call
    method[RPC_SERVICE_SCHEMA_KEY] = std::move(schema);
    rpcService[RPC_SERVICE_METHODS_KEY].push_back(method);
  }

  return rpcService;
}
} // namespace rpc
