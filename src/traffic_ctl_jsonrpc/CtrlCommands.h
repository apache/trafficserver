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

// traffic_ctl specifics
#include "jsonrpc/RPCClient.h"
#include "jsonrpc/yaml_codecs.h"
#include "CtrlPrinters.h"

// ----------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Base Control Command class.
/// This class should be used as a base class for every new command or group of commands that are related.
/// The base class will provide the client communication through the @c invoke_call member function. Arguments that were
/// parsed by the traffic_ctl are available as a member to all the derived classes.
class CtrlCommand
{
  /// We use yamlcpp as codec implementation.
  using Codec = yamlcpp_json_emitter;

public:
  virtual ~CtrlCommand() = default;

  /// @brief This object will hold the arguments for now.
  CtrlCommand(ts::Arguments args);

  /// @brief Main execution point for a particular command. This function will invoke @c _invoked_func which should be set
  ///        by the derived class. In case you do not want the @c _invoked_func to be called directly, you should override this
  ///        member function and call it yourself. @c RecordCommand does it and forwards the call to his childrens.
  ///        If @c _invoked_func is not properly set, the function will not be called.
  virtual void execute();

protected:
  /// @brief Invoke the remote server. This is the very basic function which does not play or interact with any codec. Request
  ///        and message should be already en|de coded.
  /// @param request A string representation of the json/yaml request.
  /// @return a string with the json/yaml response.
  /// @note This function does print the raw string if requested by the "--debugrpc". No printer involved, standard output.
  std::string invoke_rpc(std::string const &request);

  /// @brief Function that calls the rpc server. This function takes a json objects and uses the defined coded to convert them to a
  ///        string. This function will call invoke_rpc(string) overload.
  /// @param A Client request.
  /// @return A server response.
  specs::JSONRPCResponse invoke_rpc(CtrlClientRequest const &request);

  ts::Arguments _arguments;              //!< parsed traffic_ctl arguments.
  std::unique_ptr<BasePrinter> _printer; //!< Specific output formatter. This should be created by the derived class.
  bool _debugRpcRawMsg{false};           //!< This will be set in case of "--debugrpc" was specified.

  /// @brief The whole design is that the command will execute the @c _invoked_func once invoked. This function ptr should be
  ///        set by the appropriated derived class base on the passed parameters. The derived class have the option to override
  ///        the execute() function and do something else. Check @c RecordCommand as an example.
  std::function<void(void)> _invoked_func; //!< Actual function that the command will execute.

private:
  RPCClient _rpcClient; //!< RPC socket client implementation.
};

// -----------------------------------------------------------------------------------------------------------------------------------
///
/// @brief Record Command Implementation
///        Used as base class for any command that needs to access to a TS record.
///        If deriving from this class, make sure you implement @c execute_subcommand() and call the _invoked_func yourself.
class RecordCommand : public CtrlCommand
{
public:
  virtual ~RecordCommand() = default;
  /// @brief RecordCommand constructor.
  RecordCommand(ts::Arguments args);
  /// @brief We will override this function as we want to call execute_subcommand() in the derived class.
  void execute();

protected:
  /// @brief Handy enum to hold which kind of records we are requesting.
  enum class RecordQueryType { CONFIG = 0, METRIC };
  /// @brief Function to fetch record from the rpc server.
  /// @param argData argument's data.
  /// @param isRegex if the request should be done by regex or name.
  /// @param recQueryType Config or Metric.
  specs::JSONRPCResponse record_fetch(ts::ArgumentData argData, bool isRegex, RecordQueryType recQueryType);

  /// @brief To be override
  virtual void
  execute_subcommand()
  {
  }
};
// -----------------------------------------------------------------------------------------------------------------------------------
class ConfigCommand : public RecordCommand
{
  void config_match();
  void config_get();
  void config_describe();
  void config_defaults();
  void config_diff();
  void config_status();
  void config_set();
  void config_reload();

public:
  ConfigCommand(ts::Arguments args);
  void execute_subcommand();
};
// -----------------------------------------------------------------------------------------------------------------------------------
class MetricCommand : public RecordCommand
{
  void metric_get();
  void metric_match();
  void metric_describe();
  void metric_clear();
  void metric_zero();

public:
  MetricCommand(ts::Arguments args);
  void execute_subcommand();
};
// -----------------------------------------------------------------------------------------------------------------------------------
class HostCommand : public CtrlCommand
{
public:
  HostCommand(ts::Arguments args);

private:
  void status_get();
  void status_down();
  void status_up();
};
// -----------------------------------------------------------------------------------------------------------------------------------
class PluginCommand : public CtrlCommand
{
public:
  PluginCommand(ts::Arguments args);

private:
  void plugin_msg();
};
// -----------------------------------------------------------------------------------------------------------------------------------
class DirectRPCCommand : public CtrlCommand
{
public:
  DirectRPCCommand(ts::Arguments args);

private:
  void from_file_request();
  void get_rpc_api();
  void read_from_input();
  /// run a YAML validation on the input.
  bool validate_input(std::string_view in) const;
};
// -----------------------------------------------------------------------------------------------------------------------------------
class ServerCommand : public CtrlCommand
{
public:
  ServerCommand(ts::Arguments args);

private:
  void server_drain();
};
//
// -----------------------------------------------------------------------------------------------------------------------------------
struct StorageCommand : public CtrlCommand {
  StorageCommand(ts::Arguments args);

private:
  void get_storage_status();
  void set_storage_offline();
};
// -----------------------------------------------------------------------------------------------------------------------------------
