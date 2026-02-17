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

#include "mgmt/config/ConfigReloadErrors.h"
#include "TrafficCtlStatus.h"
namespace
{
/// We use yamlcpp as codec implementation.
using Codec = yamlcpp_json_emitter;

using StringToFormatFlagsMap                  = std::unordered_map<std::string_view, BasePrinter::Options::FormatFlags>;
const StringToFormatFlagsMap _Fmt_str_to_enum = {
  {"json", BasePrinter::Options::FormatFlags::JSON},
  {"rpc",  BasePrinter::Options::FormatFlags::RPC }
};

constexpr std::string_view YAML_PREFIX{"records."};
constexpr std::string_view RECORD_PREFIX{"proxy.config."};

/// Convert YAML-style path (records.diags.debug) to record name format (proxy.config.diags.debug).
/// If the path doesn't start with "records.", it's returned unchanged.
std::string
yaml_to_record_name(std::string_view path)
{
  swoc::TextView tv{path};
  if (tv.starts_with(YAML_PREFIX)) {
    return std::string{RECORD_PREFIX} + std::string{path.substr(YAML_PREFIX.size())};
  }
  return std::string{path};
}

void
display_errors(BasePrinter *printer, std::vector<ConfigReloadResponse::Error> const &errors)
{
  std::string text;
  if (auto iter = std::begin(errors); iter != std::end(errors)) {
    auto print_error = [&](auto &&e) { printer->write_output(swoc::bwprint(text, "Message: {}, Code: {}", e.message, e.code)); };
    printer->write_output("------------ Errors ----------");
    print_error(*iter);
    ++iter;
    for (; iter != std::end(errors); ++iter) {
      printer->write_output("--");
      print_error(*iter);
    }
  }
}
} // namespace

BasePrinter::Options::FormatFlags
parse_print_opts(ts::Arguments *args)
{
  BasePrinter::Options::FormatFlags val{BasePrinter::Options::FormatFlags::NOT_SET};

  if (args->get("default")) {
    val |= BasePrinter::Options::FormatFlags::SHOW_DEFAULT;
  }

  if (args->get("records")) { // records overrule the rest of the formats.
    val |= BasePrinter::Options::FormatFlags::RECORDS;
    return val;
  }

  if (auto data = args->get("format"); data) {
    StringToFormatFlagsMap::const_iterator search = _Fmt_str_to_enum.find(data.value());
    if (search != std::end(_Fmt_str_to_enum)) {
      val |= search->second;
    }
  }
  return val;
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
RPCAccessor::invoke_rpc(std::string const &request, std::chrono::milliseconds timeout_ms, int attempts)
{
  if (_printer->print_rpc_message()) {
    std::string text;
    swoc::bwprint(text, "--> {}", request);
    _printer->write_debug(std::string_view{text});
  }
  if (auto resp = _rpcClient.invoke(request, timeout_ms, attempts); !resp.empty()) {
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
RPCAccessor::invoke_rpc(shared::rpc::ClientRequest const &request, std::chrono::milliseconds timeout_ms, int attempts)
{
  std::string encodedRequest = Codec::encode(request);
  std::string resp           = invoke_rpc(encodedRequest, timeout_ms, attempts);
  return Codec::decode(resp);
}

void
RPCAccessor::invoke_rpc(shared::rpc::ClientRequest const &request, std::string &resp, std::chrono::milliseconds timeout_ms,
                        int attempts)
{
  std::string encodedRequest = Codec::encode(request);
  resp                       = invoke_rpc(encodedRequest, timeout_ms, attempts);
}
// -----------------------------------------------------------------------------------------------------------------------------------
ConfigCommand::ConfigCommand(ts::Arguments *args) : RecordCommand(args)
{
  BasePrinter::Options printOpts(parse_print_opts(args));
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
  } else if (args->get(RESET_STR)) {
    _printer      = std::make_unique<ConfigSetPrinter>(printOpts);
    _invoked_func = [&]() { config_reset(); };
  } else if (args->get(STATUS_STR)) {
    _printer      = std::make_unique<ConfigReloadPrinter>(printOpts);
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

std::string
CtrlCommand::invoke_rpc(std::string const &request)
{
  auto timeout  = std::chrono::milliseconds(std::stoi(get_parsed_arguments()->get("read-timeout").value()));
  auto attempts = std::stoi(get_parsed_arguments()->get("read-attempts").value());

  return RPCAccessor::invoke_rpc(request, timeout, attempts);
}

shared::rpc::JSONRPCResponse
CtrlCommand::invoke_rpc(shared::rpc::ClientRequest const &request)
{
  auto timeout  = std::chrono::milliseconds(std::stoi(get_parsed_arguments()->get("read-timeout").value()));
  auto attempts = std::stoi(get_parsed_arguments()->get("read-attempts").value());

  return RPCAccessor::invoke_rpc(request, timeout, attempts);
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
  std::string token = get_parsed_arguments()->get("token").value();
  std::string count = get_parsed_arguments()->get("count").value();

  if (!count.empty() && !token.empty()) {
    // can't use both.
    if (!_printer->is_json_format()) {
      _printer->write_output("You can't use both --token and --count options together. Ignoring --count");
    }
    count = ""; // server will ignore this if token is set anyways.
  }

  auto resp = fetch_config_reload(token, count);

  if (resp.error.size()) {
    display_errors(_printer.get(), resp.error);
    App_Exit_Status_Code = CTRL_EX_ERROR;
    return;
  }

  if (resp.tasks.size() > 0) {
    for (const auto &task : resp.tasks) {
      _printer->as<ConfigReloadPrinter>()->print_reload_report(task, true);
    }
  }
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

ConfigReloadResponse
ConfigCommand::fetch_config_reload(std::string const &token, std::string const &count)
{
  // traffic_ctl config status [--token <token>] [--count <n>]

  FetchConfigReloadStatusRequest request{
    FetchConfigReloadStatusRequest::Params{token, count}
  };

  auto response = invoke_rpc(request); // server will handle if token is empty or not.

  _printer->write_output(response); // in case of errors.
  return response.result.as<ConfigReloadResponse>();
}

void
ConfigCommand::track_config_reload_progress(std::string const &token, std::chrono::milliseconds refresh_interval)
{
  FetchConfigReloadStatusRequest request{
    FetchConfigReloadStatusRequest::Params{token, "1" /* last reload if any*/}
  };
  auto resp = invoke_rpc(request);

  if (resp.is_error()) {
    _printer->write_output(resp);
    return;
  }

  while (!Signal_Flagged.load()) {
    auto decoded_response = resp.result.as<ConfigReloadResponse>();

    _printer->write_output(resp);
    if (decoded_response.tasks.empty()) {
      _printer->write_output(resp);
      App_Exit_Status_Code = CTRL_EX_ERROR;
      return;
    }

    ConfigReloadResponse::ReloadInfo current_task = decoded_response.tasks[0];
    _printer->as<ConfigReloadPrinter>()->write_progress_line(current_task);

    // Check if reload has reached a terminal state
    if (current_task.status == "success" || current_task.status == "fail" || current_task.status == "timeout") {
      std::cout << "\n";
      if (current_task.status != "success") {
        std::string hint;
        _printer->write_output(swoc::bwprint(hint, "\n  Details : traffic_ctl config status -t {}", current_task.config_token));
      }
      break;
    }
    std::this_thread::sleep_for(refresh_interval);

    request = FetchConfigReloadStatusRequest{
      FetchConfigReloadStatusRequest::Params{token, "1" /* last reload if any*/}
    };
    resp = invoke_rpc(request);
    if (resp.is_error()) {
      _printer->write_output(resp);
      break;
    }
  }
}

std::string
ConfigCommand::read_data_input(std::string const &data_arg)
{
  if (data_arg.empty()) {
    return {};
  }

  // @- means stdin
  if (data_arg == "@-") {
    std::istreambuf_iterator<char> begin(std::cin), end;
    return std::string(begin, end);
  }

  // @filename means read from file
  if (data_arg[0] == '@') {
    std::string   filename = data_arg.substr(1);
    std::ifstream file(filename);
    if (!file) {
      _printer->write_output("Error: Cannot open file '" + filename + "'");
      App_Exit_Status_Code = CTRL_EX_ERROR;
      return {};
    }
    std::istreambuf_iterator<char> begin(file), end;
    return std::string(begin, end);
  }

  // Otherwise, treat as inline YAML string
  return data_arg;
}

ConfigReloadResponse
ConfigCommand::config_reload(std::string const &token, bool force, YAML::Node const &configs)
{
  auto resp = invoke_rpc(ConfigReloadRequest{
    ConfigReloadRequest::Params{token, force, configs}
  });
  // base class method will handle error and json output if needed.
  _printer->write_output(resp);
  return resp.result.as<ConfigReloadResponse>();
}

void
ConfigCommand::config_reset()
{
  auto const &paths = get_parsed_arguments()->get(RESET_STR);

  // Build lookup request - always use REGEX to support partial path matching
  shared::rpc::RecordLookupRequest lookup_request;

  if (paths.empty() || (paths.size() == 1 && paths[0] == "records")) {
    lookup_request.emplace_rec(".*", shared::rpc::REGEX, shared::rpc::CONFIG_REC_TYPES);
  } else {
    for (auto const &path : paths) {
      // Convert YAML-style path (records.*) to record name format (proxy.config.*)
      auto record_path = yaml_to_record_name(path);
      lookup_request.emplace_rec(record_path, shared::rpc::REGEX, shared::rpc::CONFIG_REC_TYPES);
    }
  }

  // Lookup matching records
  auto lookup_response = invoke_rpc(lookup_request);
  if (lookup_response.is_error()) {
    _printer->write_output(lookup_response);
    return;
  }

  // Build reset request from modified records (current != default)
  auto const            &records = lookup_response.result.as<shared::rpc::RecordLookUpResponse>();
  ConfigSetRecordRequest set_request;

  for (auto const &rec : records.recordList) {
    if (rec.currentValue != rec.defaultValue) {
      set_request.params.push_back(ConfigSetRecordRequest::Params{rec.name, rec.defaultValue});
    }
  }

  if (set_request.params.size() == 0) {
    std::cout << "No records to reset (all matching records are already at default values)\n";
    return;
  }

  _printer->write_output(invoke_rpc(set_request));
}

void
ConfigCommand::config_reload()
{
  std::string token     = get_parsed_arguments()->get("token").value();
  bool        force     = get_parsed_arguments()->get("force") ? true : false;
  auto        data_args = get_parsed_arguments()->get("data");

  bool show_details = get_parsed_arguments()->get("show-details") ? true : false;
  bool monitor      = get_parsed_arguments()->get("monitor") ? true : false;

  float refresh_secs      = std::stof(get_parsed_arguments()->get("refresh-int").value());
  float initial_wait_secs = std::stof(get_parsed_arguments()->get("initial-wait").value());

  if (monitor && show_details) {
    // ignore monitor if details is set.
    monitor = false;
  }

  // Warn about --force behavior
  if (force) {
    _printer->write_output("Warning: --force does not stop running handlers.");
    _printer->write_output("         If a reload is actively processing, handlers may run in parallel.");
    _printer->write_output("");
  }

  // Parse inline config data if provided (supports multiple -d arguments)
  YAML::Node configs;
  for (auto const &data_arg : data_args) {
    if (data_arg.empty()) {
      continue;
    }

    std::string data_content = read_data_input(data_arg);
    if (data_content.empty() && App_Exit_Status_Code == CTRL_EX_ERROR) {
      return; // Error already reported by read_data_input
    }

    try {
      YAML::Node parsed = YAML::Load(data_content);
      if (!parsed.IsMap()) {
        _printer->write_output("Error: Data must be a YAML map with config keys (e.g., ip_allow, sni)");
        App_Exit_Status_Code = CTRL_EX_ERROR;
        return;
      }
      // Merge parsed content into configs (later files override earlier ones)
      for (auto const &kv : parsed) {
        configs[kv.first.as<std::string>()] = kv.second;
      }
    } catch (YAML::Exception const &ex) {
      _printer->write_output(std::string("Error: Invalid YAML data in '") + data_arg + "': " + ex.what());
      App_Exit_Status_Code = CTRL_EX_ERROR;
      return;
    }
  }

  using ConfigError = config::reload::errors::ConfigReloadError;

  auto contains_error = [](std::vector<ConfigReloadResponse::Error> const &errors, ConfigError error) -> bool {
    const int code = static_cast<int>(error);
    for (auto const &n : errors) {
      if (n.code == code) {
        return true;
      }
    }
    return false;
  };

  std::string text;
  bool        token_exist{false};
  bool        in_progress{false};

  if (show_details) {
    bool include_logs = get_parsed_arguments()->get("include-logs") ? true : false;

    ConfigReloadResponse resp = config_reload(token, force, configs);
    if (contains_error(resp.error, ConfigError::RELOAD_IN_PROGRESS)) {
      if (resp.tasks.size() > 0) {
        const auto &task = resp.tasks[0];
        _printer->write_output(swoc::bwprint(text, "\xe2\x9f\xb3 Reload in progress [{}]", task.config_token));
        _printer->as<ConfigReloadPrinter>()->print_reload_report(task, include_logs);
      }
      return;
    } else if (contains_error(resp.error, ConfigError::TOKEN_ALREADY_EXISTS)) {
      token_exist = true;
    } else if (resp.error.size()) {
      display_errors(_printer.get(), resp.error);
      App_Exit_Status_Code = CTRL_EX_ERROR;
      return;
    }

    if (token_exist) {
      _printer->write_output(swoc::bwprint(text, "\xe2\x9c\x97 Token '{}' already in use", token));
    } else {
      _printer->write_output(swoc::bwprint(text, "\xe2\x9c\x94 Reload scheduled [{}]. Waiting for details...", resp.config_token));
      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(initial_wait_secs * 1000)));
    }

    resp = fetch_config_reload(token);
    if (resp.error.size()) {
      display_errors(_printer.get(), resp.error);
      App_Exit_Status_Code = CTRL_EX_ERROR;
      return;
    }

    if (resp.tasks.size() > 0) {
      const auto &task = resp.tasks[0];
      // _printer->as<ConfigReloadPrinter>()->print_basic_ri_line(task, true, true, 0);
      _printer->as<ConfigReloadPrinter>()->print_reload_report(task, include_logs);
    }
  } else if (monitor) {
    _printer->disable_json_format(); // monitor output is not json.
    ConfigReloadResponse resp = config_reload(token, force, configs);

    if (contains_error(resp.error, ConfigError::RELOAD_IN_PROGRESS)) {
      in_progress = true;
      if (!resp.tasks.empty()) {
        _printer->write_output(swoc::bwprint(text, "\xe2\x9f\xb3 Reload in progress [{}]", resp.tasks[0].config_token));
      }
    } else if (contains_error(resp.error, ConfigError::TOKEN_ALREADY_EXISTS)) {
      _printer->write_output(swoc::bwprint(text, "\xe2\x9c\x97 Token '{}' already in use\n", token));
      _printer->write_output(swoc::bwprint(text, "  Status : traffic_ctl config status -t {}", token));
      _printer->write_output("  Retry  : traffic_ctl config reload");
      return;
    } else if (resp.error.size()) {
      display_errors(_printer.get(), resp.error);
      App_Exit_Status_Code = CTRL_EX_ERROR;
      return;
    } else {
      _printer->write_output(swoc::bwprint(text, "\xe2\x9c\x94 Reload scheduled [{}]", resp.config_token));
    }

    if (!in_progress) {
      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(initial_wait_secs * 1000))); // wait before first poll
    } // else no need to wait, we can start fetching right away.

    track_config_reload_progress(resp.config_token, std::chrono::milliseconds(static_cast<int>(refresh_secs * 1000)));
  } else {
    ConfigReloadResponse resp = config_reload(token, force, configs);
    if (contains_error(resp.error, ConfigError::RELOAD_IN_PROGRESS)) {
      if (!resp.tasks.empty()) {
        std::string tk = resp.tasks[0].config_token;
        _printer->write_output(swoc::bwprint(text, "\xe2\x9f\xb3 Reload in progress [{}]\n", tk));
        _printer->write_output(swoc::bwprint(text, "  Monitor : traffic_ctl config reload -t {} -m", tk));
        _printer->write_output(swoc::bwprint(text, "  Details : traffic_ctl config status -t {}", tk));
        _printer->write_output("  Force   : traffic_ctl config reload --force  (may conflict with the running reload)");
      }
    } else if (contains_error(resp.error, ConfigError::TOKEN_ALREADY_EXISTS)) {
      _printer->write_output(swoc::bwprint(text, "\xe2\x9c\x97 Token '{}' already in use\n", token));
      _printer->write_output(swoc::bwprint(text, "  Status : traffic_ctl config status -t {}", token));
      _printer->write_output("  Retry  : traffic_ctl config reload");
    } else if (resp.error.size()) {
      display_errors(_printer.get(), resp.error);
      App_Exit_Status_Code = CTRL_EX_ERROR;
      return;
    } else {
      _printer->write_output(swoc::bwprint(text, "\xe2\x9c\x94 Reload scheduled [{}]\n", resp.config_token));
      _printer->write_output(swoc::bwprint(text, "  Monitor : traffic_ctl config reload -t {} -m", resp.config_token));
      _printer->write_output(swoc::bwprint(text, "  Details : traffic_ctl config reload -t {} -s -l", resp.config_token));
    }

    if (resp.tasks.size() > 0) {
      const auto &task = resp.tasks[0];
      _printer->as<ConfigReloadPrinter>()->print_reload_report(task);
    }
  }

  // Show warning for inline config (not persisted to disk)
  if (configs.size() > 0 && App_Exit_Status_Code != CTRL_EX_ERROR) {
    _printer->write_output("");
    _printer->write_output("Note: Inline configuration is NOT persisted to disk.");
    _printer->write_output("      Server restart will revert to file-based configuration.");
  }
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
  _printer->disable_json_format(); // monitor is not json.
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
HostDBCommand::HostDBCommand(ts::Arguments *args) : CtrlCommand(args)
{
  BasePrinter::Options printOpts{parse_print_opts(args)};
  if (get_parsed_arguments()->get(STATUS_STR)) {
    _printer      = std::make_unique<HostDBStatusPrinter>(printOpts);
    _invoked_func = [&]() { status_get(); };
  }
}

void
HostDBCommand::status_get()
{
  HostDBGetStatusRequest::Params params;

  auto const &data = get_parsed_arguments()->get(STATUS_STR);
  if (data.size() >= 1) {
    params = {
      data[0],
    };
  }

  HostDBGetStatusRequest request{params};

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
    if (printOpts._format & BasePrinter::Options::FormatFlags::NOT_SET) {
      // overwrite this and let it drop json instead.
      printOpts._format |= BasePrinter::Options::FormatFlags::RPC;
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
        _printer->write_debug(response);
      } else {
        _printer->write_output(swoc::bwprint(text, "\n[ {} ]\n --> \n{}\n", filename, content));
        _printer->write_output(swoc::bwprint(text, "<--\n{}\n", response));
      }

    } catch (std::exception const &ex) {
      App_Exit_Status_Code = CTRL_EX_ERROR;
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
    App_Exit_Status_Code = CTRL_EX_ERROR;
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
  } else if (get_parsed_arguments()->get(DEBUG_STR)) {
    _printer      = std::make_unique<GenericPrinter>(printOpts);
    _invoked_func = [&]() { server_debug(); };
  } else if (get_parsed_arguments()->get(STATUS_STR)) {
    _printer      = std::make_unique<ServerStatusPrinter>(printOpts);
    _invoked_func = [&]() { server_status(); };
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

void
ServerCommand::server_debug()
{
  // Set ATS to enable or disable debug at runtime.
  const bool enable = get_parsed_arguments()->get(ENABLE_STR);
  const bool append = get_parsed_arguments()->get(APPEND_STR);

  // If the following is not passed as options then the request will ignore them as default values
  // will be set.
  std::string       tags      = get_parsed_arguments()->get(TAGS_STR).value();
  const std::string client_ip = get_parsed_arguments()->get(CLIENT_IP_STR).value();

  // If append mode is enabled and tags are provided, fetch current tags and combine
  if (append && !tags.empty()) {
    shared::rpc::RecordLookupRequest lookup_request;
    lookup_request.emplace_rec("proxy.config.diags.debug.tags", shared::rpc::NOT_REGEX, shared::rpc::CONFIG_REC_TYPES);
    auto lookup_response = invoke_rpc(lookup_request);

    if (!lookup_response.is_error()) {
      auto const &records = lookup_response.result.as<shared::rpc::RecordLookUpResponse>();
      if (!records.recordList.empty()) {
        std::string current_tags = records.recordList[0].currentValue;
        if (!current_tags.empty()) {
          // Combine: current|new
          tags = current_tags + "|" + tags;
        }
      }
    }
  }

  const SetDebugServerRequest         request{enable, tags, client_ip};
  shared::rpc::JSONRPCResponse const &response = invoke_rpc(request);

  swoc::LocalBufferWriter<512> bw;

  bw.print("■ TS Runtime debug set to »{}({})«", enable ? "ON" : "OFF", enable ? (!client_ip.empty() ? "2" : "1") : "0");
  if (enable) {
    bw.print(" - tags »\"{}\"«, client_ip »{}«", !tags.empty() ? tags : "unchanged", !client_ip.empty() ? client_ip : "unchanged");
  }
  if (response.is_error()) {
    _printer->write_output(response);
  } else {
    _printer->write_output(bw.view());
  }
}

void
ServerCommand::server_status()
{
  shared::rpc::JSONRPCResponse response = invoke_rpc(GetServerStatusRequest{});
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
