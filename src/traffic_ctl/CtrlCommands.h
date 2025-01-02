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

#include "tscore/ArgParser.h"

#include "shared/rpc/RPCClient.h"
#include "CtrlPrinters.h"

/// @brief Class that provides access to the RPC side of things.
struct RPCAccessor {
protected:
  /// @brief Invoke the remote server. This is the very basic function which does not play or interact with any codec. Request
  ///        and message should be already en|de coded.
  /// @param request A string representation of the json/yaml request.
  /// @return a string with the json/yaml response.
  /// @note This function does print the raw string if requested by the "--format". No printer involved, standard output.
  std::string invoke_rpc(std::string const &request);

  /// @brief Function that calls the rpc server. This function takes a json objects and uses the defined coded to convert them to a
  ///        string. This function will call invoke_rpc(string) overload.
  /// @param A Client request.
  /// @return A server response.
  shared::rpc::JSONRPCResponse invoke_rpc(shared::rpc::ClientRequest const &request);

  /// @brief Function that calls the rpc server. The response will not be decoded, it will be a raw string.
  void invoke_rpc(shared::rpc::ClientRequest const &request, std::string &bw);

  std::unique_ptr<BasePrinter> _printer; //!< Specific output formatter. This should be created by the derived class.
private:
  shared::rpc::RPCClient _rpcClient; //!< RPC socket client implementation.
};

// ----------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Base Control Command class.
/// This class should be used as a base class for every new command or group of commands that are related.
/// The base class will provide the client communication through the @c invoke_call member function. Arguments that were
/// parsed by the traffic_ctl are available as a member to all the derived classes.
class CtrlCommand
{
public:
  virtual ~CtrlCommand() = default;

  /// @brief This object will hold the arguments for now.
  /// @note If you don't need to handle the args in your derived class you should just inherit CtrlCommand ctor:
  ///       'using CtrlCommand::CtrlCommand;'
  CtrlCommand(ts::Arguments *args);

  /// @brief Main execution point for a particular command. This function will invoke @c _invoked_func which should be set
  ///        by the derived class. In case you do not want the @c _invoked_func to be called directly, you should override this
  ///        member function and call it yourself.
  ///        If @c _invoked_func is not properly set, the function will throw @c logic_error
  virtual void execute();

  /// @brief This variable is used to mark if a Signal was flagged by the application. Default value is 0 and the signal number
  ///        should be set when the signal is handled.
  static std::atomic_int Signal_Flagged;

protected:
  /// @brief The whole design is that the command will execute the @c _invoked_func once invoked. This function ptr should be
  ///        set by the appropriated derived class base on the passed parameters. The derived class have the option to override
  ///        the execute() function and do something else. Check @c RecordCommand as an example.
  std::function<void(void)> _invoked_func; //!< Actual function that the command will execute.

  /// @brief Return the parsed arguments.
  ts::Arguments *
  get_parsed_arguments()
  {
    return _arguments;
  }

private:
  ts::Arguments *_arguments = nullptr; //!< parsed traffic_ctl arguments.
};

// -----------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Record Command Implementation
///        Used as base class for any command that needs to access to a TS record.
///        If deriving from this class, make sure you implement @c execute_subcommand() and call the _invoked_func yourself.
class RecordCommand : public CtrlCommand, public RPCAccessor
{
public:
  using CtrlCommand::CtrlCommand;
  virtual ~RecordCommand() = default;

protected:
  static inline const std::string MATCH_STR{"match"};
  static inline const std::string GET_STR{"get"};
  static inline const std::string DESCRIBE_STR{"describe"};

  /// @brief Handy enum to hold which kind of records we are requesting.
  enum class RecordQueryType { CONFIG = 0, METRIC };
  /// @brief Function to fetch record from the rpc server.
  /// @param argData argument's data.
  /// @param isRegex if the request should be done by regex or name.
  /// @param recQueryType Config or Metric.
  shared::rpc::JSONRPCResponse record_fetch(ts::ArgumentData argData, bool isRegex, RecordQueryType recQueryType);
};
// -----------------------------------------------------------------------------------------------------------------------------------
class ConfigCommand : public RecordCommand
{
  static inline const std::string DIFF_STR{"diff"};
  static inline const std::string DEFAULTS_STR{"defaults"};
  static inline const std::string SET_STR{"set"};
  static inline const std::string COLD_STR{"cold"};
  static inline const std::string APPEND_STR{"append"};
  static inline const std::string STATUS_STR{"status"};
  static inline const std::string RELOAD_STR{"reload"};
  static inline const std::string REGISTRY_STR{"registry"};

  void config_match();
  void config_get();
  void config_describe();
  void config_defaults();
  void config_diff();
  void config_status();
  void config_set();
  void file_config_set();
  void config_reload();
  void config_show_file_registry();

public:
  ConfigCommand(ts::Arguments *args);
};
// -----------------------------------------------------------------------------------------------------------------------------------
class MetricCommand : public RecordCommand
{
  static inline const std::string MONITOR_STR{"monitor"};

  void metric_get();
  void metric_match();
  void metric_describe();
  void metric_monitor();

public:
  MetricCommand(ts::Arguments *args);
};
// -----------------------------------------------------------------------------------------------------------------------------------
class HostCommand : public CtrlCommand, public RPCAccessor
{
public:
  HostCommand(ts::Arguments *args);

private:
  static inline const std::string STATUS_STR{"status"};
  static inline const std::string DOWN_STR{"down"};
  static inline const std::string UP_STR{"up"};
  static inline const std::string REASON_STR{"reason"};

  void status_get();
  void status_down();
  void status_up();
};
// -----------------------------------------------------------------------------------------------------------------------------------
class PluginCommand : public CtrlCommand, public RPCAccessor
{
public:
  PluginCommand(ts::Arguments *args);

private:
  static inline const std::string MSG_STR{"msg"};
  void                            plugin_msg();
};
// -----------------------------------------------------------------------------------------------------------------------------------
class DirectRPCCommand : public CtrlCommand, public RPCAccessor
{
public:
  DirectRPCCommand(ts::Arguments *args);

private:
  static inline const std::string GET_API_STR{"get-api"};
  static inline const std::string FILE_STR{"file"};
  static inline const std::string INPUT_STR{"input"};
  static inline const std::string INVOKE_STR{"invoke"};
  static inline const std::string RAW_STR{"raw"};
  static inline const std::string PARAMS_STR{"params"};

  void from_file_request();
  void get_rpc_api();
  void read_from_input();
  void invoke_method();
  /// run a YAML validation on the input.
  bool validate_input(std::string const &in) const;
};
// -----------------------------------------------------------------------------------------------------------------------------------
class ServerCommand : public CtrlCommand, public RPCAccessor
{
public:
  ServerCommand(ts::Arguments *args);

private:
  static inline const std::string DRAIN_STR{"drain"};
  static inline const std::string UNDO_STR{"undo"};
  static inline const std::string NO_NEW_CONN_STR{"no-new-connection"};

  static inline const std::string DEBUG_STR{"debug"};
  static inline const std::string ENABLE_STR{"enable"};
  static inline const std::string DISABLE_STR{"disable"};
  static inline const std::string TAGS_STR{"tags"};
  static inline const std::string CLIENT_IP_STR{"client_ip"};

  void server_drain();
  void server_debug();
};
//
// -----------------------------------------------------------------------------------------------------------------------------------
struct StorageCommand : public CtrlCommand, public RPCAccessor {
  StorageCommand(ts::Arguments *args);

private:
  static inline const std::string STATUS_STR{"status"};
  static inline const std::string OFFLINE_STR{"offline"};

  void get_storage_status();
  void set_storage_offline();
};
// -----------------------------------------------------------------------------------------------------------------------------------

BasePrinter::Options::FormatFlags parse_print_opts(ts::Arguments *args);
