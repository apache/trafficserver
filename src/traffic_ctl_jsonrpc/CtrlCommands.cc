/** @file

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
#include <fstream>
#include <unordered_map>

#include "CtrlCommands.h"
#include "jsonrpc/CtrlRPCRequests.h"
#include "jsonrpc/ctrl_yaml_codecs.h"
namespace
{
/// We use yamlcpp as codec implementation.
using Codec = yamlcpp_json_emitter;

const std::unordered_map<std::string_view, BasePrinter::Options::Format> _Fmt_str_to_enum = {
  {"pretty", BasePrinter::Options::Format::PRETTY},  {"legacy", BasePrinter::Options::Format::LEGACY},
  {"json", BasePrinter::Options::Format::JSON},      {"req", BasePrinter::Options::Format::DATA_REQ},
  {"resp", BasePrinter::Options::Format::DATA_RESP}, {"all", BasePrinter::Options::Format::DATA_ALL}};

BasePrinter::Options::Format
parse_format(ts::Arguments &args)
{
  if (args.get("records")) {
    return BasePrinter::Options::Format::RECORDS;
  }

  BasePrinter::Options::Format val{BasePrinter::Options::Format::LEGACY};

  if (auto data = args.get("format"); data) {
    ts::TextView fmt{data.value()};
    if ("data" == fmt.prefix(':')) {
      fmt.take_prefix_at(':');
    }
    if (auto search = _Fmt_str_to_enum.find(fmt); search != std::end(_Fmt_str_to_enum)) {
      val = search->second;
    }
  }
  return val;
}

BasePrinter::Options
parse_print_opts(ts::Arguments &args)
{
  return {parse_format(args)};
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------------------
CtrlCommand::CtrlCommand(ts::Arguments args) : _arguments(args) {}

void
CtrlCommand::execute()
{
  if (_invoked_func) {
    _invoked_func();
  }
}

std::string
CtrlCommand::invoke_rpc(std::string const &request)
{
  if (_printer->print_req_msg()) {
    std::string text;
    ts::bwprint(text, "--> {}", request);
    _printer->write_debug(std::string_view{text});
  }
  if (auto resp = _rpcClient.invoke(request); !resp.empty()) {
    // all good.
    if (_printer->print_resp_msg()) {
      std::string text;
      ts::bwprint(text, "<-- {}", resp);
      _printer->write_debug(std::string_view{text});
    }
    return resp;
  }

  return {};
}

shared::rpc::JSONRPCResponse
CtrlCommand::invoke_rpc(shared::rpc::ClientRequest const &request)
{
  std::string encodedRequest = Codec::encode(request);
  std::string resp           = invoke_rpc(encodedRequest);
  return Codec::decode(resp);
}

void
CtrlCommand::invoke_rpc(shared::rpc::ClientRequest const &request, std::string &resp)
{
  std::string encodedRequest = Codec::encode(request);
  resp                       = invoke_rpc(encodedRequest);
}
// -----------------------------------------------------------------------------------------------------------------------------------
RecordCommand::RecordCommand(ts::Arguments args) : CtrlCommand(args) {}

void
RecordCommand::execute()
{
  execute_subcommand();
}
ConfigCommand::ConfigCommand(ts::Arguments args) : RecordCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(_arguments)};
  if (args.get("match")) {
    _printer      = std::make_unique<RecordPrinter>(printOpts);
    _invoked_func = [&]() { config_match(); };
  } else if (args.get("get")) {
    _printer      = std::make_unique<RecordPrinter>(printOpts);
    _invoked_func = [&]() { config_get(); };
  } else if (args.get("diff")) {
    _printer      = std::make_unique<DiffConfigPrinter>(printOpts);
    _invoked_func = [&]() { config_diff(); };
  } else if (args.get("describe")) {
    _printer      = std::make_unique<RecordDescribePrinter>(printOpts);
    _invoked_func = [&]() { config_describe(); };
  } else if (args.get("defaults")) {
    _printer      = std::make_unique<RecordPrinter>(printOpts);
    _invoked_func = [&]() { config_defaults(); };
  } else if (args.get("set")) {
    _printer      = std::make_unique<ConfigSetPrinter>(printOpts);
    _invoked_func = [&]() { config_set(); };
  } else if (args.get("status")) {
    _printer      = std::make_unique<RecordPrinter>(printOpts);
    _invoked_func = [&]() { config_status(); };
  } else if (args.get("reload")) {
    _printer      = std::make_unique<ConfigReloadPrinter>(printOpts);
    _invoked_func = [&]() { config_reload(); };
  } else if (args.get("registry")) {
    _printer      = std::make_unique<ConfigShowFileRegistryPrinter>(printOpts);
    _invoked_func = [&]() { config_show_file_registry(); };
  } else {
    // work in here.
  }
}

void
ConfigCommand::execute_subcommand()
{
  if (_invoked_func) {
    _invoked_func();
  }
}

shared::rpc::JSONRPCResponse
RecordCommand::record_fetch(ts::ArgumentData argData, bool isRegex, RecordQueryType recQueryType)
{
  shared::rpc::RecordLookupRequest request;
  for (auto &&it : argData) {
    request.emplace_rec(it, isRegex,
                        recQueryType == RecordQueryType::CONFIG ? shared::rpc::CONFIG_REC_TYPES : shared::rpc::METRIC_REC_TYPES);
  }
  return invoke_rpc(request);
}

void
ConfigCommand::config_match()
{
  _printer->write_output(record_fetch(_arguments.get("match"), shared::rpc::REGEX, RecordQueryType::CONFIG));
}

void
ConfigCommand::config_get()
{
  _printer->write_output(record_fetch(_arguments.get("get"), shared::rpc::NOT_REGEX, RecordQueryType::CONFIG));
}

void
ConfigCommand::config_describe()
{
  _printer->write_output(record_fetch(_arguments.get("describe"), shared::rpc::NOT_REGEX, RecordQueryType::CONFIG));
}
void
ConfigCommand::config_defaults()
{
  const bool configs{true};
  shared::rpc::JSONRPCResponse response = invoke_rpc(GetAllRecordsRequest{configs});
  _printer->write_output(response);
}
void
ConfigCommand::config_diff()
{
  GetAllRecordsRequest request{true};
  shared::rpc::JSONRPCResponse response = invoke_rpc(request);
  _printer->write_output(response);
}

void
ConfigCommand::config_status()
{
  ConfigStatusRequest request;
  shared::rpc::JSONRPCResponse response = invoke_rpc(request);
  _printer->write_output(response);
}

void
ConfigCommand::config_set()
{
  auto const &data = _arguments.get("set");
  ConfigSetRecordRequest request{{data[0], data[1]}};
  shared::rpc::JSONRPCResponse response = invoke_rpc(request);

  _printer->write_output(response);
}
void
ConfigCommand::config_reload()
{
  _printer->write_output(invoke_rpc(ConfigReloadRequest{}));
}
void
ConfigCommand::config_show_file_registry()
{
  _printer->write_output(invoke_rpc(ConfigShowFileRegistryRequest{}));
}
//------------------------------------------------------------------------------------------------------------------------------------
MetricCommand::MetricCommand(ts::Arguments args) : RecordCommand(args)
{
  // auto const fmt = parse_format(_arguments);
  BasePrinter::Options printOpts{parse_print_opts(_arguments)};
  if (args.get("match")) {
    _printer      = std::make_unique<MetricRecordPrinter>(printOpts);
    _invoked_func = [&]() { metric_match(); };
  } else if (args.get("get")) {
    _printer      = std::make_unique<MetricRecordPrinter>(printOpts);
    _invoked_func = [&]() { metric_get(); };
  } else if (args.get("describe")) {
    _printer      = std::make_unique<RecordDescribePrinter>(printOpts);
    _invoked_func = [&]() { metric_describe(); };
  } else if (args.get("clear")) {
    _printer      = std::make_unique<GenericPrinter>(printOpts);
    _invoked_func = [&]() { metric_clear(); };
  } else if (args.get("zero")) {
    _printer      = std::make_unique<GenericPrinter>(printOpts);
    _invoked_func = [&]() { metric_zero(); };
  }
}

void
MetricCommand::execute_subcommand()
{
  if (_invoked_func) {
    _invoked_func();
  }
}

void
MetricCommand::metric_get()
{
  _printer->write_output(record_fetch(_arguments.get("get"), shared::rpc::NOT_REGEX, RecordQueryType::METRIC));
}

void
MetricCommand::metric_match()
{
  _printer->write_output(record_fetch(_arguments.get("match"), shared::rpc::REGEX, RecordQueryType::METRIC));
}

void
MetricCommand::metric_describe()
{
  _printer->write_output(record_fetch(_arguments.get("describe"), shared::rpc::NOT_REGEX, RecordQueryType::METRIC));
}

void
MetricCommand::metric_clear()
{
  [[maybe_unused]] auto const &response = invoke_rpc(ClearAllMetricRequest{});
}

void
MetricCommand::metric_zero()
{
  auto records = _arguments.get("zero");
  ClearMetricRequest request{// names
                             {{std::begin(records), std::end(records)}}};

  [[maybe_unused]] auto const &response = invoke_rpc(request);
}
//------------------------------------------------------------------------------------------------------------------------------------
// TODO, let call the super const
HostCommand::HostCommand(ts::Arguments args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(_arguments)};
  if (_arguments.get("status")) {
    _printer      = std::make_unique<GetHostStatusPrinter>(printOpts);
    _invoked_func = [&]() { status_get(); };
  } else if (_arguments.get("down")) {
    _printer      = std::make_unique<SetHostStatusPrinter>(printOpts);
    _invoked_func = [&]() { status_down(); };
  } else if (_arguments.get("up")) {
    _printer      = std::make_unique<SetHostStatusPrinter>(printOpts);
    _invoked_func = [&]() { status_up(); };
  }
}

void
HostCommand::status_get()
{
  auto const &data = _arguments.get("status");
  HostGetStatusRequest request;
  for (auto it : data) {
    std::string name = std::string{HostGetStatusRequest::STATUS_PREFIX} + "." + it;
    request.emplace_rec(name, shared::rpc::NOT_REGEX, shared::rpc::METRIC_REC_TYPES);
  }
  auto response = invoke_rpc(request);

  _printer->write_output(response);
}

void
HostCommand::status_down()
{
  auto hosts = _arguments.get("down");
  HostSetStatusRequest request{
    {HostSetStatusRequest::Params::Op::DOWN, {std::begin(hosts), std::end(hosts)}, _arguments.get("reason").value(), "0"}};
  auto response = invoke_rpc(request);
  _printer->write_output(response);
}

void
HostCommand::status_up()
{
  auto hosts = _arguments.get("up");
  HostSetStatusRequest request{
    {HostSetStatusRequest::Params::Op::UP, {std::begin(hosts), std::end(hosts)}, _arguments.get("reason").value(), "0"}};

  auto response = invoke_rpc(request);
  _printer->write_output(response);
}
//------------------------------------------------------------------------------------------------------------------------------------
PluginCommand::PluginCommand(ts::Arguments args) : CtrlCommand(args)
{
  if (_arguments.get("msg")) {
    _invoked_func = [&]() { plugin_msg(); };
  }
  _printer = std::make_unique<GenericPrinter>(parse_print_opts(_arguments));
}

void
PluginCommand::plugin_msg()
{
  auto msgs = _arguments.get("msg");
  BasicPluginMessageRequest::Params params;
  params.tag = msgs[0];
  if (msgs.size() > 1) {
    // have a value
    params.str = msgs[1];
  }
  BasicPluginMessageRequest request{params};
  auto response = invoke_rpc(request);
}
//------------------------------------------------------------------------------------------------------------------------------------
DirectRPCCommand::DirectRPCCommand(ts::Arguments args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(_arguments)};

  if (_arguments.get("get-api")) {
    _printer      = std::make_unique<RPCAPIPrinter>(printOpts);
    _invoked_func = [&]() { get_rpc_api(); };
    return;
  } else if (_arguments.get("file")) {
    _invoked_func = [&]() { from_file_request(); };
  } else if (_arguments.get("input")) {
    _invoked_func = [&]() { read_from_input(); };
  }

  _printer = std::make_unique<GenericPrinter>(printOpts);
}

bool
DirectRPCCommand::validate_input(std::string const &in) const
{
  // validate the input
  YAML::Node content = YAML::Load(in);
  if (content.Type() != YAML::NodeType::Map && content.Type() != YAML::NodeType::Sequence) {
    return false;
  }

  return true;
}

void
DirectRPCCommand::from_file_request()
{
  // TODO: remove all the output messages from here if possible
  auto filenames = _arguments.get("file");
  for (auto &&filename : filenames) {
    std::string text;
    // run some basic validation on the passed files, they should
    try {
      std::ifstream file(filename);
      std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

      if (!validate_input(content)) {
        _printer->write_output(
          ts::bwprint(text, "Content not accepted. expecting a valid sequence or structure. {} skipped.\n", filename));
        continue;
      }
      std::string const &response = invoke_rpc(content);
      if (_printer->is_json_format()) {
        // as we have the raw json in here, we cna just directly print it
        _printer->write_output(response);
      } else {
        _printer->write_output(ts::bwprint(text, "\n[ {} ]\n --> \n{}\n", filename, content));
        _printer->write_output(ts::bwprint(text, "<--\n{}\n", response));
      }

    } catch (std::exception const &ex) {
      _printer->write_output(ts::bwprint(text, "Error found: {}\n", ex.what()));
    }
  }
}

void
DirectRPCCommand::get_rpc_api()
{
  auto response = invoke_rpc(ShowRegisterHandlersRequest{});
  _printer->write_output(response);
}

void
DirectRPCCommand::read_from_input()
{
  // TODO: remove all the output messages from here if possible
  std::string text;

  try {
    _printer->write_output(">> Ctrl-D to fire the request. Ctrl-C to exit\n");
    std::cin >> std::noskipws;
    // read cin.
    std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    if (!_arguments.get("raw") && !validate_input(content)) {
      _printer->write_output(ts::bwprint(text, "Content not accepted. expecting a valid sequence or structure\n"));
      return;
    }
    std::string const &response = invoke_rpc(content);
    _printer->write_output("--> Request sent.\n");
    _printer->write_output(ts::bwprint(text, "\n<-- {}\n", response));
  } catch (std::exception const &ex) {
    _printer->write_output(ts::bwprint(text, "Error found: {}\n", ex.what()));
  }
}

//------------------------------------------------------------------------------------------------------------------------------------
ServerCommand::ServerCommand(ts::Arguments args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(_arguments)};
  if (_arguments.get("drain")) {
    _printer      = std::make_unique<GenericPrinter>(printOpts);
    _invoked_func = [&]() { server_drain(); };
  }
}

void
ServerCommand::server_drain()
{
  shared::rpc::JSONRPCResponse response;
  // TODO, can call_request take a && ?? if needed in the cmd just pass by ref.

  if (_arguments.get("undo")) {
    response = invoke_rpc(ServerStopDrainRequest{});
  } else {
    bool newConn = _arguments.get("no-new-connection");
    ServerStartDrainRequest request{{newConn}};
    response = invoke_rpc(request);
  }

  _printer->write_output(response);
}
// //------------------------------------------------------------------------------------------------------------------------------------
StorageCommand::StorageCommand(ts::Arguments args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(_arguments)};
  if (_arguments.get("status")) {
    _printer      = std::make_unique<CacheDiskStoragePrinter>(printOpts);
    _invoked_func = [&]() { get_storage_status(); };
  } else if (_arguments.get("offline")) {
    _printer      = std::make_unique<CacheDiskStorageOfflinePrinter>(printOpts);
    _invoked_func = [&]() { set_storage_offline(); };
  }
}

void
StorageCommand::get_storage_status()
{
  auto disks = _arguments.get("status");
  GetStorageDeviceStatusRequest request{{{std::begin(disks), std::end(disks)}}};
  auto response = invoke_rpc(request);
  _printer->write_output(response);
}
void
StorageCommand::set_storage_offline()
{
  auto disks = _arguments.get("offline");
  SetStorageDeviceOfflineRequest request{{{std::begin(disks), std::end(disks)}}};
  auto response = invoke_rpc(request);
  _printer->write_output(response);
}
// //------------------------------------------------------------------------------------------------------------------------------------
