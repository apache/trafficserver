/** @file

  traffic_ctl

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

#include <iostream>

#include "tscore/I_Layout.h"
#include "tscore/runroot.h"
#include "tscore/ArgParser.h"

#include "CtrlCommands.h"

static constexpr int status_code{0};
int
main(int argc, const char **argv)
{
  ts::ArgParser parser;

  std::shared_ptr<CtrlCommand> command;
  auto CtrlUnimplementedCommand = [](std::string_view cmd) { std::cout << "Command " << cmd << " unimplemented.\n"; };

  parser.add_description("Apache Traffic Server RPC CLI");
  parser.add_global_usage("traffic_ctl [OPTIONS] CMD [ARGS ...]");
  parser.require_commands();

  parser.add_option("--debug", "", "Enable debugging output")
    .add_option("--version", "-V", "Print version string")
    .add_option("--help", "-h", "Print usage information")
    .add_option("--run-root", "", "using TS_RUNROOT as sandbox", "TS_RUNROOT", 1)
    .add_option("--format", "-f", "Use a specific output format legacy|pretty", "", 1, "legacy", "format")
    .add_option("--debugrpc", "-r", "Show raw rpc message before/after calling the Server.");

  auto &config_command     = parser.add_command("config", "Manipulate configuration records").require_commands();
  auto &metric_command     = parser.add_command("metric", "Manipulate performance metrics").require_commands();
  auto &server_command     = parser.add_command("server", "Stop, restart and examine the server").require_commands();
  auto &storage_command    = parser.add_command("storage", "Manipulate cache storage").require_commands();
  auto &plugin_command     = parser.add_command("plugin", "Interact with plugins").require_commands();
  auto &host_command       = parser.add_command("host", "Interact with host status").require_commands();
  auto &direct_rpc_command = parser.add_command("rpc", "Interact with the rpc api").require_commands();

  // config commands
  config_command.add_command("defaults", "Show default information configuration values", [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config defaults [OPTIONS]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command
    .add_command("describe", "Show detailed information about configuration values", "", MORE_THAN_ONE_ARG_N,
                 [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config describe RECORD [RECORD ...]");
  config_command.add_command("diff", "Show non-default configuration values", [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config diff [OPTIONS]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command.add_command("get", "Get one or more configuration values", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config get [OPTIONS] RECORD [RECORD ...]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command
    .add_command("match", "Get configuration matching a regular expression", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config match [OPTIONS] REGEX [REGEX ...]")
    .add_option("--records", "", "Emit output in records.config format");
  config_command.add_command("reload", "Request a configuration reload", [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config reload");
  config_command.add_command("status", "Check the configuration status", [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config status");
  config_command.add_command("set", "Set a configuration value", "", 2, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl config set RECORD VALUE");

  // host commands
  host_command.add_command("status", "Get one or more host statuses", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl host status HOST  [HOST  ...]");
  host_command.add_command("down", "Set down one or more host(s)", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl host down HOST [OPTIONS]")
    .add_option("--time", "-I", "number of seconds that a host is marked down", "", 1, "0")
    .add_option("--reason", "", "reason for marking the host down, one of 'manual|active|local", "", 1, "manual");
  host_command.add_command("up", "Set up one or more host(s)", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl host up METRIC value")
    .add_option("--reason", "", "reason for marking the host up, one of 'manual|active|local", "", 1, "manual");

  // metric commands
  metric_command.add_command("get", "Get one or more metric values", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl metric get METRIC [METRIC ...]");
  metric_command.add_command("clear", "Clear all metric values", [&]() { command->execute(); });
  metric_command.add_command("describe", "Show detailed information about one or more metric values", "", MORE_THAN_ONE_ARG_N,
                             [&]() { command->execute(); }); // not implemented
  metric_command.add_command("match", "Get metrics matching a regular expression", "", MORE_THAN_ZERO_ARG_N,
                             [&]() { command->execute(); });
  metric_command.add_command("monitor", "Display the value of a metric over time", "", MORE_THAN_ZERO_ARG_N,
                             [&]() { CtrlUnimplementedCommand("monitor"); }); // not implemented
  metric_command.add_command("zero", "Clear one or more metric values", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); });

  // plugin command
  plugin_command.add_command("msg", "Send message to plugins - a TAG and the message DATA", "", 2, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl plugin msg TAG DATA");

  // server commands
  server_command.add_command("backtrace", "Show a full stack trace of the traffic_server process",
                             [&]() { CtrlUnimplementedCommand("backtrace"); });
  server_command.add_command("restart", "Restart Traffic Server", [&]() { CtrlUnimplementedCommand("restart"); })
    .add_example_usage("traffic_ctl server restart [OPTIONS]")
    .add_option("--drain", "", "Wait for client connections to drain before restarting");
  server_command.add_command("start", "Start the proxy", [&]() { CtrlUnimplementedCommand("start"); })
    .add_example_usage("traffic_ctl server start [OPTIONS]")
    .add_option("--clear-cache", "", "Clear the disk cache on startup")
    .add_option("--clear-hostdb", "", "Clear the DNS cache on startup");
  server_command.add_command("status", "Show the proxy status", [&]() { CtrlUnimplementedCommand("status"); })
    .add_example_usage("traffic_ctl server status");
  server_command.add_command("stop", "Stop the proxy", [&]() { CtrlUnimplementedCommand("stop"); })
    .add_example_usage("traffic_ctl server stop [OPTIONS]")
    .add_option("--drain", "", "Wait for client connections to drain before stopping");
  server_command.add_command("drain", "Drain the requests", [&]() { command->execute(); })
    .add_example_usage("traffic_ctl server drain [OPTIONS]")
    .add_option("--no-new-connection", "-N", "Wait for new connections down to threshold before starting draining")
    .add_option("--undo", "-U", "Recover server from the drain mode");

  // storage commands
  storage_command
    .add_command("offline", "Take one or more storage volumes offline", "", MORE_THAN_ONE_ARG_N, [&]() { command->execute(); })
    .add_example_usage("storage offline DEVICE [DEVICE ...]");
  storage_command.add_command("status", "Show the storage configuration", "", MORE_THAN_ONE_ARG_N,
                              [&]() { command->execute(); }); // not implemented

  // direct rpc commands, handy for debug and trouble shooting
  direct_rpc_command
    .add_command("file", "Send direct JSONRPC request to the server from a passed file(s)", "", MORE_THAN_ONE_ARG_N,
                 [&]() { command->execute(); })
    .add_example_usage("traffic_ctl rpc file request.yaml");
  direct_rpc_command.add_command("get-api", "Request full API from server", "", 0, [&]() { command->execute(); })
    .add_example_usage("traffic_ctl rpc get-api");
  direct_rpc_command
    .add_command("input", "Read from standard input. Ctrl-D to send the request", "", 0, [&]() { command->execute(); })
    .add_option("--raw", "-r",
                "No json/yaml parse validation will take place, the raw content will be directly send to the server.", "", 0, "",
                "raw")
    .add_example_usage("traffic_ctl rpc input ");

  try {
    auto args = parser.parse(argv);
    argparser_runroot_handler(args.get("run-root").value(), argv[0]);
    Layout::create();

    if (args.get("config")) {
      command = std::make_shared<ConfigCommand>(args);
    } else if (args.get("metric")) {
      command = std::make_shared<MetricCommand>(args);
    } else if (args.get("server")) {
      command = std::make_shared<ServerCommand>(args);
    } else if (args.get("storage")) {
      command = std::make_shared<StorageCommand>(args);
    } else if (args.get("plugin")) {
      command = std::make_shared<PluginCommand>(args);
    } else if (args.get("host")) {
      command = std::make_shared<HostCommand>(args);
    } else if (args.get("rpc")) {
      command = std::make_shared<DirectRPCCommand>(args);
    }
    // Execute
    args.invoke();
  } catch (std::exception const &ex) {
    std::cout << "Error found.\n" << ex.what() << '\n';
  }

  return status_code;
}
