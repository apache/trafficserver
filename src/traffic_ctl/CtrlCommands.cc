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
#include "CtrlCommands.h"

#include <fstream>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>

#include <swoc/TextView.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_base.h>

#include "jsonrpc/CtrlRPCRequests.h"
#include "jsonrpc/ctrl_yaml_codecs.h"

namespace
{
/// We use yamlcpp as codec implementation.
using Codec = yamlcpp_json_emitter;

using StringToOutputFormatMap                  = std::unordered_map<std::string_view, BasePrinter::Options::OutputFormat>;
const StringToOutputFormatMap _Fmt_str_to_enum = {
  {"json", BasePrinter::Options::OutputFormat::JSON},
  {"rpc",  BasePrinter::Options::OutputFormat::RPC }
};
} // namespace

BasePrinter::Options::OutputFormat
parse_format(ts::Arguments *args)
{
  if (args->get("records")) {
    return BasePrinter::Options::OutputFormat::RECORDS;
  }

  BasePrinter::Options::OutputFormat val{BasePrinter::Options::OutputFormat::NOT_SET};

  if (auto data = args->get("format"); data) {
    StringToOutputFormatMap::const_iterator search = _Fmt_str_to_enum.find(data.value());
    if (search != std::end(_Fmt_str_to_enum)) {
      val = search->second;
    }
  }
  return val;
}
BasePrinter::Options
parse_print_opts(ts::Arguments *args)
{
  return {parse_format(args)};
}

std::atomic_int CtrlCommand::Signal_Flagged{0};
//------------------------------------------------------------------------------------------------------------------------------------
CtrlCommand::CtrlCommand(ts::Arguments *args) : _arguments(args) {}

void
CtrlCommand::execute()
{
  if (_invoked_func) {
    _invoked_func();
  } else {
    throw std::logic_error("CtrlCommand::execute(): Internal error. There should be a function to invoke. (_invoked_func not set)");
  }
}

std::string
RPCAccessor::invoke_rpc(std::string const &request)
{
  if (_printer->print_rpc_message()) {
    std::string text;
    swoc::bwprint(text, "--> {}", request);
    _printer->write_debug(std::string_view{text});
  }
  if (auto resp = _rpcClient.invoke(request); !resp.empty()) {
    // all good.
    if (_printer->print_rpc_message()) {
      std::string text;
      swoc::bwprint(text, "<-- {}", resp);
      _printer->write_debug(std::string_view{text});
    }
    return resp;
  }

  return {};
}

shared::rpc::JSONRPCResponse
RPCAccessor::invoke_rpc(shared::rpc::ClientRequest const &request)
{
  std::string encodedRequest = Codec::encode(request);
  std::string resp           = invoke_rpc(encodedRequest);
  return Codec::decode(resp);
}

void
RPCAccessor::invoke_rpc(shared::rpc::ClientRequest const &request, std::string &resp)
{
  std::string encodedRequest = Codec::encode(request);
  resp                       = invoke_rpc(encodedRequest);
}
// -----------------------------------------------------------------------------------------------------------------------------------
ConfigCommand::ConfigCommand(ts::Arguments *args) : RecordCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};
  if (args->get(MATCH_STR)) {
    _printer      = std::make_unique<RecordPrinter>(printOpts);
    _invoked_func = [&]() { config_match(); };
  } else if (args->get(GET_STR)) {
    _printer      = std::make_unique<RecordPrinter>(printOpts);
    _invoked_func = [&]() { config_get(); };
  } else if (args->get(DIFF_STR)) {
    _printer      = std::make_unique<DiffConfigPrinter>(printOpts);
    _invoked_func = [&]() { config_diff(); };
  } else if (args->get(DESCRIBE_STR)) {
    _printer      = std::make_unique<RecordDescribePrinter>(printOpts);
    _invoked_func = [&]() { config_describe(); };
  } else if (args->get(DEFAULTS_STR)) {
    _printer      = std::make_unique<RecordPrinter>(printOpts);
    _invoked_func = [&]() { config_defaults(); };
  } else if (args->get(SET_STR)) {
    _printer      = std::make_unique<ConfigSetPrinter>(printOpts);
    _invoked_func = [&]() { config_set(); };
  } else if (args->get(STATUS_STR)) {
    _printer      = std::make_unique<ConfigStatusPrinter>(printOpts);
    _invoked_func = [&]() { config_status(); };
  } else if (args->get(RELOAD_STR)) {
    _printer      = std::make_unique<ConfigReloadPrinter>(printOpts);
    _invoked_func = [&]() { config_reload(); };
  } else if (args->get(REGISTRY_STR)) {
    _printer      = std::make_unique<ConfigShowFileRegistryPrinter>(printOpts);
    _invoked_func = [&]() { config_show_file_registry(); };
  } else {
    // work in here.
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
  _printer->write_output(record_fetch(get_parsed_arguments()->get(MATCH_STR), shared::rpc::REGEX, RecordQueryType::CONFIG));
}

void
ConfigCommand::config_get()
{
  _printer->write_output(record_fetch(get_parsed_arguments()->get(GET_STR), shared::rpc::NOT_REGEX, RecordQueryType::CONFIG));
}

void
ConfigCommand::config_describe()
{
  _printer->write_output(record_fetch(get_parsed_arguments()->get(DESCRIBE_STR), shared::rpc::NOT_REGEX, RecordQueryType::CONFIG));
}
void
ConfigCommand::config_defaults()
{
  const bool                   configs{true};
  shared::rpc::JSONRPCResponse response = invoke_rpc(GetAllRecordsRequest{configs});
  _printer->write_output(response);
}
void
ConfigCommand::config_diff()
{
  GetAllRecordsRequest         request{true};
  shared::rpc::JSONRPCResponse response = invoke_rpc(request);
  _printer->write_output(response);
}

void
ConfigCommand::config_status()
{
  ConfigStatusRequest          request;
  shared::rpc::JSONRPCResponse response = invoke_rpc(request);
  _printer->write_output(response);
}

void
ConfigCommand::config_set()
{
  auto const            &data = get_parsed_arguments()->get(SET_STR);
  ConfigSetRecordRequest request{
    {data[0], data[1]}
  };
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
MetricCommand::MetricCommand(ts::Arguments *args) : RecordCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};
  if (args->get(MATCH_STR)) {
    _printer      = std::make_unique<MetricRecordPrinter>(printOpts);
    _invoked_func = [&]() { metric_match(); };
  } else if (args->get(GET_STR)) {
    _printer      = std::make_unique<MetricRecordPrinter>(printOpts);
    _invoked_func = [&]() { metric_get(); };
  } else if (args->get(DESCRIBE_STR)) {
    _printer      = std::make_unique<RecordDescribePrinter>(printOpts);
    _invoked_func = [&]() { metric_describe(); };
  } else if (args->get(CLEAR_STR)) {
    _printer      = std::make_unique<GenericPrinter>(printOpts);
    _invoked_func = [&]() { metric_clear(); };
  } else if (args->get(ZERO_STR)) {
    _printer      = std::make_unique<GenericPrinter>(printOpts);
    _invoked_func = [&]() { metric_zero(); };
  } else if (args->get(MONITOR_STR)) {
    _printer      = std::make_unique<MetricRecordPrinter>(printOpts);
    _invoked_func = [&]() { metric_monitor(); };
  }
}

void
MetricCommand::metric_get()
{
  _printer->write_output(record_fetch(get_parsed_arguments()->get(GET_STR), shared::rpc::NOT_REGEX, RecordQueryType::METRIC));
}

void
MetricCommand::metric_match()
{
  _printer->write_output(record_fetch(get_parsed_arguments()->get(MATCH_STR), shared::rpc::REGEX, RecordQueryType::METRIC));
}

void
MetricCommand::metric_describe()
{
  _printer->write_output(record_fetch(get_parsed_arguments()->get(DESCRIBE_STR), shared::rpc::NOT_REGEX, RecordQueryType::METRIC));
}

void
MetricCommand::metric_clear()
{
  [[maybe_unused]] auto const &response = invoke_rpc(ClearAllMetricRequest{});
}

void
MetricCommand::metric_zero()
{
  auto               records = get_parsed_arguments()->get(ZERO_STR);
  ClearMetricRequest request{// names
                             {{std::begin(records), std::end(records)}}};

  [[maybe_unused]] auto const &response = invoke_rpc(request);
}

void
MetricCommand::metric_monitor()
{
  ts::ArgumentData const &arg = get_parsed_arguments()->get(MONITOR_STR);
  std::string             err_text;

  //
  // Note: if any of the string->number fails, the exception will be caught by the invoke function from the ArgParser.
  //
  const int32_t count = std::stoi(get_parsed_arguments()->get("count").value());
  int32_t       query_count{0};
  const int32_t interval = std::stoi(get_parsed_arguments()->get("interval").value());
  // default count is 0.
  if (count < 0 || interval <= 0) {
    throw std::runtime_error(swoc::bwprint(err_text, "monitor: invalid input, count: {}(>=0), interval: {}(>=1)", count, interval));
  }

  // keep track of each metric
  struct ctx {
    float min{std::numeric_limits<float>::max()};
    float max{std::numeric_limits<float>::lowest()};
    float sum{0.0f};
    float last{0.0f};
  };

  // Keep track of the requested metric(s), we support more than one at the same time.

  // To be used to print all the stats. This is a lambda function as this could
  // be called when SIGINT is invoked, so we dump what we have before exit.
  auto dump = [&](std::unordered_map<std::string, ctx> const &_summary) {
    if (_summary.size() == 0) {
      // nothing to report.
      return;
    }

    _printer->write_output(swoc::bwprint(err_text, "--- metric monitor statistics({}) ---", query_count));

    for (auto const &item : _summary) {
      ctx const &s   = item.second;
      const int  avg = s.sum / query_count;
      _printer->write_output(swoc::bwprint(err_text, "┌ {}\n└─ min/avg/max = {:.5}/{}/{:.5}", item.first, s.min, avg, s.max));
    }
  };

  std::unordered_map<std::string, ctx> summary;

  while (!Signal_Flagged.load()) {
    // Request will hold all metrics in a single message.
    shared::rpc::JSONRPCResponse const &resp = record_fetch(arg, shared::rpc::NOT_REGEX, RecordQueryType::METRIC);

    if (resp.is_error()) { // something went wrong in the server, report it.
      _printer->write_output(resp);
      return;
    }

    auto const &response = resp.result.as<shared::rpc::RecordLookUpResponse>();
    if (response.errorList.size() && response.recordList.size() == 0) {
      // nothing to be done or report, use '-f rpc'  for details.
      break;
    }

    for (auto &&rec : response.recordList) { // requested metric(s)
      auto       &s   = summary[rec.name];   // We will update it.
      const float val = std::stof(rec.currentValue);

      s.sum += val;
      s.max  = std::max<float>(s.max, val);
      s.min  = std::min<float>(s.min, val);
      std::string symbol;
      if (query_count > 0) {
        if (val > s.last) {
          symbol = "+";
        } else if (val < s.last) {
          symbol = "-";
        }
      }
      s.last = val;
      _printer->write_output(swoc::bwprint(err_text, "{}: {} {}", rec.name, rec.currentValue, symbol));
    }

    if ((query_count++ == count - 1) && count > 0 /* could be a forever loop*/) {
      break;
    }

    sleep(interval);
  }

  // all done, print summary.
  dump(summary);
}
//------------------------------------------------------------------------------------------------------------------------------------
// TODO, let call the super const
HostCommand::HostCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};
  if (get_parsed_arguments()->get(STATUS_STR)) {
    _printer      = std::make_unique<GetHostStatusPrinter>(printOpts);
    _invoked_func = [&]() { status_get(); };
  } else if (get_parsed_arguments()->get(DOWN_STR)) {
    _printer      = std::make_unique<SetHostStatusPrinter>(printOpts);
    _invoked_func = [&]() { status_down(); };
  } else if (get_parsed_arguments()->get(UP_STR)) {
    _printer      = std::make_unique<SetHostStatusPrinter>(printOpts);
    _invoked_func = [&]() { status_up(); };
  }
}

void
HostCommand::status_get()
{
  auto const          &data = get_parsed_arguments()->get(STATUS_STR);
  HostGetStatusRequest request{
    {std::begin(data), std::end(data)}
  };

  auto response = invoke_rpc(request);

  _printer->write_output(response);
}

void
HostCommand::status_down()
{
  auto                 hosts = get_parsed_arguments()->get(DOWN_STR);
  HostSetStatusRequest request{
    {HostSetStatusRequest::Params::Op::DOWN,
     {std::begin(hosts), std::end(hosts)},
     get_parsed_arguments()->get(REASON_STR).value(),
     "0"}
  };
  auto response = invoke_rpc(request);
  _printer->write_output(response);
}

void
HostCommand::status_up()
{
  auto                 hosts = get_parsed_arguments()->get(UP_STR);
  HostSetStatusRequest request{
    {HostSetStatusRequest::Params::Op::UP,
     {std::begin(hosts), std::end(hosts)},
     get_parsed_arguments()->get(REASON_STR).value(),
     "0"}
  };

  auto response = invoke_rpc(request);
  _printer->write_output(response);
}
//------------------------------------------------------------------------------------------------------------------------------------
PluginCommand::PluginCommand(ts::Arguments *args) : CtrlCommand(args)
{
  if (get_parsed_arguments()->get(MSG_STR)) {
    _invoked_func = [&]() { plugin_msg(); };
  }
  _printer = std::make_unique<GenericPrinter>(parse_print_opts(args));
}

void
PluginCommand::plugin_msg()
{
  auto                              msgs = get_parsed_arguments()->get(MSG_STR);
  BasicPluginMessageRequest::Params params;
  params.tag = msgs[0];
  if (msgs.size() > 1) {
    // have a value
    params.str = msgs[1];
  }
  BasicPluginMessageRequest request{params};
  auto                      response = invoke_rpc(request);
  _printer->write_output(response);
}
//------------------------------------------------------------------------------------------------------------------------------------
DirectRPCCommand::DirectRPCCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};

  if (get_parsed_arguments()->get(GET_API_STR)) {
    _printer      = std::make_unique<RPCAPIPrinter>(printOpts);
    _invoked_func = [&]() { get_rpc_api(); };
    return;
  } else if (get_parsed_arguments()->get(FILE_STR)) {
    _invoked_func = [&]() { from_file_request(); };
  } else if (get_parsed_arguments()->get(INPUT_STR)) {
    _invoked_func = [&]() { read_from_input(); };
  } else if (get_parsed_arguments()->get(INVOKE_STR)) {
    _invoked_func = [&]() { invoke_method(); };
    if (printOpts._format == BasePrinter::Options::OutputFormat::NOT_SET) {
      // overwrite this and let it drop json instead.
      printOpts._format = BasePrinter::Options::OutputFormat::RPC;
    }
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
  auto filenames = get_parsed_arguments()->get(FILE_STR);
  for (auto &&filename : filenames) {
    std::string text;
    // run some basic validation on the passed files, they should
    try {
      std::ifstream file(filename);
      std::string   content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

      if (!validate_input(content)) {
        _printer->write_output(
          swoc::bwprint(text, "Content not accepted. expecting a valid sequence or structure. {} skipped.\n", filename));
        continue;
      }
      std::string const &response = invoke_rpc(content);
      if (_printer->is_json_format()) {
        // as we have the raw json in here, we cna just directly print it
        _printer->write_output(response);
      } else {
        _printer->write_output(swoc::bwprint(text, "\n[ {} ]\n --> \n{}\n", filename, content));
        _printer->write_output(swoc::bwprint(text, "<--\n{}\n", response));
      }

    } catch (std::exception const &ex) {
      _printer->write_output(swoc::bwprint(text, "Error found: {}\n", ex.what()));
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
    if (!get_parsed_arguments()->get(RAW_STR) && !validate_input(content)) {
      _printer->write_output(swoc::bwprint(text, "Content not accepted. expecting a valid sequence or structure\n"));
      return;
    }
    std::string const &response = invoke_rpc(content);
    _printer->write_output("--> Request sent.\n");
    _printer->write_output(swoc::bwprint(text, "\n<-- {}\n", response));
  } catch (std::exception const &ex) {
    _printer->write_output(swoc::bwprint(text, "Error found: {}\n", ex.what()));
  }
}

void
DirectRPCCommand::invoke_method()
{
  shared::rpc::ClientRequest request;
  if (auto method = get_parsed_arguments()->get(INVOKE_STR); method) {
    request.method = method.value();
    // We build up the parameter content if passed.
    if (auto params = get_parsed_arguments()->get(PARAMS_STR); params) {
      std::ostringstream ss;
      for (auto &&param : params) {
        ss << param;
        ss << '\n';
      }
      request.params = YAML::Load(ss.str()); // let if fail if this is bad.
    }
    _printer->write_output(invoke_rpc(request));
  }
}

//------------------------------------------------------------------------------------------------------------------------------------
ServerCommand::ServerCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};
  if (get_parsed_arguments()->get(DRAIN_STR)) {
    _printer      = std::make_unique<GenericPrinter>(printOpts);
    _invoked_func = [&]() { server_drain(); };
  }
}

void
ServerCommand::server_drain()
{
  shared::rpc::JSONRPCResponse response;
  // TODO, can call_request take a && ?? if needed in the cmd just pass by ref.

  if (get_parsed_arguments()->get(UNDO_STR)) {
    response = invoke_rpc(ServerStopDrainRequest{});
  } else {
    const bool              newConn = get_parsed_arguments()->get(NO_NEW_CONN_STR);
    ServerStartDrainRequest request{{newConn}};
    response = invoke_rpc(request);
  }

  _printer->write_output(response);
}
// //------------------------------------------------------------------------------------------------------------------------------------
StorageCommand::StorageCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};
  if (get_parsed_arguments()->get(STATUS_STR)) {
    _printer      = std::make_unique<CacheDiskStoragePrinter>(printOpts);
    _invoked_func = [&]() { get_storage_status(); };
  } else if (get_parsed_arguments()->get(OFFLINE_STR)) {
    _printer      = std::make_unique<CacheDiskStorageOfflinePrinter>(printOpts);
    _invoked_func = [&]() { set_storage_offline(); };
  }
}

void
StorageCommand::get_storage_status()
{
  auto                          disks = get_parsed_arguments()->get(STATUS_STR);
  GetStorageDeviceStatusRequest request{{{std::begin(disks), std::end(disks)}}};
  auto                          response = invoke_rpc(request);
  _printer->write_output(response);
}
void
StorageCommand::set_storage_offline()
{
  auto                           disks = get_parsed_arguments()->get(OFFLINE_STR);
  SetStorageDeviceOfflineRequest request{{{std::begin(disks), std::end(disks)}}};
  auto                           response = invoke_rpc(request);
  _printer->write_output(response);
}
//------------------------------------------------------------------------------------------------------------------------------------
