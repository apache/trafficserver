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

#include <iostream>
#include <unordered_map>
#include <string_view>

#include <swoc/swoc_meta.h>
#include "tsutil/ts_bw_format.h"

#include "CtrlPrinters.h"
#include "jsonrpc/ctrl_yaml_codecs.h"
#include "PrintUtils.h"

#include "TrafficCtlStatus.h"

swoc::BufferWriter &
bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, FloatDate const &wrap)
{
  return bwformat(w, spec, swoc::bwf::Date(static_cast<time_t>(swoc::svtod(wrap._src)), wrap._fmt));
}

//------------------------------------------------------------------------------------------------------------------------------------

namespace
{
void
print_record_error_list(std::vector<shared::rpc::RecordLookUpResponse::RecordError> const &errors)
{
  if (auto iter = std::begin(errors); iter != std::end(errors)) {
    App_Exit_Status_Code = CTRL_EX_ERROR; // Set the exit code to error, so we can return it later.
    std::cout << "------------ Errors ----------\n";
    std::cout << *iter;
    ++iter;
    for (; iter != std::end(errors); ++iter) {
      std::cout << "--\n";
      std::cout << *iter;
    }
  }
}

} // namespace
void
BasePrinter::write_output(shared::rpc::JSONRPCResponse const &response)
{
  // If json, then we print the full message, either ok or error.
  if (this->is_json_format()) {
    write_output_json(response.fullMsg);
    return;
  }

  if (response.is_error()) {
    App_Exit_Status_Code = CTRL_EX_ERROR; // Set the exit code to error, so we can return it later.

    // If an error is present, then as per the specs we can ignore the jsonrpc.result field,
    // so we print the error and we are done here!
    std::cout << response.error.as<shared::rpc::JSONRPCError>(); // Already formatted.
    return;
  }

  if (!response.result.IsNull()) {
    // on you!
    // Found convinient to let the derived class deal with the specifics.
    write_output(response.result);
  }
}

void
BasePrinter::write_output(std::string_view output) const
{
  if (is_json_format()) {
    // if json format, no other output is expected to avoid mixing formats.
    // Specially if you consume the json output with a tool.
    return;
  }
  std::cout << output << '\n';
}

void
BasePrinter::write_debug(std::string_view output) const
{
  std::cout << output << '\n';
}
void
BasePrinter::write_output_json(YAML::Node const &node) const
{
  YAML::Emitter out;
  out << YAML::DoubleQuoted << YAML::Flow;
  out << node;
  std::cout << out.c_str() << '\n';
}
//------------------------------------------------------------------------------------------------------------------------------------
void
RecordPrinter::write_output(YAML::Node const &result)
{
  auto const &response = result.as<shared::rpc::RecordLookUpResponse>();
  std::string text;
  // if yaml is needed
  RecNameToYaml::RecInfoList recordList;
  for (auto &&recordInfo : response.recordList) {
    if (!recordInfo.registered) {
      std::cout << recordInfo.name
                << ": Unrecognized configuration value. Record is a configuration name/value but is not registered\n";
      continue;
    }
    if (!is_records_format()) {
      std::cout << recordInfo.name << ": " << recordInfo.currentValue;
      if (should_include_default()) {
        std::cout << " # default " << recordInfo.defaultValue;
      }
      std::cout << '\n';
    } else {
      recordList.push_back(std::make_tuple(recordInfo.name, recordInfo.currentValue, recordInfo.defaultValue));
    }
  }

  if (is_records_format() && recordList.size() > 0) {
    std::cout << RecNameToYaml{std::move(recordList), should_include_default()}.string() << '\n';
  }
  // we print errors if found.
  print_record_error_list(response.errorList);
}
//------------------------------------------------------------------------------------------------------------------------------------
void
MetricRecordPrinter::write_output(YAML::Node const &result)
{
  auto response = result.as<shared::rpc::RecordLookUpResponse>();
  for (auto &&recordInfo : response.recordList) {
    std::cout << recordInfo.name << " " << recordInfo.currentValue << '\n';
  }
}
//------------------------------------------------------------------------------------------------------------------------------------

void
DiffConfigPrinter::write_output(YAML::Node const &result)
{
  auto        response = result.as<shared::rpc::RecordLookUpResponse>();
  std::string text;

  // if yaml is needed
  RecNameToYaml::RecInfoList recordList;
  for (auto &&recordInfo : response.recordList) {
    auto const &currentValue = recordInfo.currentValue;
    auto const &defaultValue = recordInfo.defaultValue;
    const bool  hasChanged   = (currentValue != defaultValue);
    if (hasChanged) {
      if (!is_records_format()) {
        std::cout << swoc::bwprint(text, "{} has changed\n", recordInfo.name);
        std::cout << swoc::bwprint(text, "\tCurrent Value: {}\n", currentValue);
        std::cout << swoc::bwprint(text, "\tDefault Value: {}\n", defaultValue);
      } else {
        recordList.push_back(std::make_tuple(recordInfo.name, recordInfo.currentValue, recordInfo.defaultValue));
      }
    }
  }

  if (is_records_format() && recordList.size() > 0) {
    std::cout << RecNameToYaml{std::move(recordList), WithDefaults}.string() << '\n';
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
ConfigReloadPrinter::write_output([[maybe_unused]] YAML::Node const &result)
{
  // no op, ctrl command will handle the output directly.
  // BasePrinter will handle the error and the json output if needed.
}
namespace
{
void
group_files(const ConfigReloadResponse::ReloadInfo &info, std::vector<const ConfigReloadResponse::ReloadInfo *> &files)
{
  if (!info.meta.is_main_task) {
    files.push_back(&info);
  }
  for (const auto &sub : info.sub_tasks) {
    group_files(sub, files);
  }
}

// Calculate duration in milliseconds from ms-since-epoch timestamps
inline int
duration_ms(int64_t start_ms, int64_t end_ms)
{
  return (end_ms >= start_ms) ? static_cast<int>(end_ms - start_ms) : -1;
}

// Format millisecond timestamp as human-readable date with milliseconds
// Output format: "YYYY Mon DD HH:MM:SS.mmm"
std::string
format_time_ms(int64_t ms_timestamp)
{
  if (ms_timestamp <= 0) {
    return "-";
  }
  std::time_t seconds = ms_timestamp / 1000;
  int         millis  = ms_timestamp % 1000;

  std::string buf;
  swoc::bwprint(buf, "{}.{:03d}", swoc::bwf::Date(seconds), millis);
  return buf;
}

// Build a UTF-8 progress bar.  @a width = number of visual characters.
std::string
build_progress_bar(int done, int total, int width = 20)
{
  int         filled = total > 0 ? (done * width / total) : 0;
  std::string bar;
  bar.reserve(width * 3);
  for (int i = 0; i < width; ++i) {
    bar += (i < filled) ? "\xe2\x96\x88" : "\xe2\x96\x91"; // █ or ░
  }
  return bar;
}

// Human-readable duration string from milliseconds.
std::string
format_duration(int ms)
{
  if (ms < 0) {
    return "-";
  }
  if (ms < 1000) {
    return std::to_string(ms) + "ms";
  }
  if (ms < 60000) {
    return std::to_string(ms / 1000) + "." + std::to_string((ms % 1000) / 100) + "s";
  }
  return std::to_string(ms / 60000) + "m " + std::to_string((ms % 60000) / 1000) + "s";
}

// Map task status string to a single-character icon for compact display.
const char *
status_icon(const std::string &status)
{
  if (status == "success") {
    return "\xe2\x9c\x94"; // ✔
  }
  if (status == "fail") {
    return "\xe2\x9c\x97"; // ✗
  }
  if (status == "in_progress" || status == "created") {
    return "\xe2\x97\x8c"; // ◌
  }
  if (status == "timeout") {
    return "\xe2\x9f\xb3"; // ⟳
  }
  return "?";
}

// Approximate visual width of a UTF-8 string (each code point counts as 1 column).
int
visual_width(const std::string &s)
{
  int w = 0;
  for (size_t i = 0; i < s.size();) {
    auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
    } else if (c < 0xE0) {
      i += 2;
    } else if (c < 0xF0) {
      i += 3;
    } else {
      i += 4;
    }
    ++w;
  }
  return w;
}

// Build a dot-leader string: " ···· " of the given visual width (min 2).
std::string
dot_fill(int width)
{
  if (width < 2) {
    width = 2;
  }
  std::string out(" ");
  for (int i = 1; i < width - 1; ++i) {
    out += "\xc2\xb7"; // · (middle dot U+00B7)
  }
  out += ' ';
  return out;
}

// Recursively print a task and its children using tree-drawing characters.
// @param prefix        characters printed before this task's icon (tree connectors from parent)
// @param child_prefix  base prefix for this task's log lines and its children's connectors
// @param content_width visual columns available for icon+name+dots+duration (shrinks per nesting)
void
print_task_tree(const ConfigReloadResponse::ReloadInfo &f, bool full_report, const std::string &prefix,
                const std::string &child_prefix, int content_width = 55)
{
  std::string fname;
  if (f.filename.empty() || f.filename == "<none>") {
    fname = f.description;
  } else {
    fname = f.filename;
  }

  int dur_ms = duration_ms(f.meta.created_time_ms, f.meta.last_updated_time_ms);

  // Build label and right-aligned duration
  std::string label   = std::string(status_icon(f.status)) + " " + fname;
  std::string dur_str = format_duration(dur_ms);

  // Right-pad duration to fixed width so values align
  constexpr int DUR_COL = 6;
  while (static_cast<int>(dur_str.size()) < DUR_COL) {
    dur_str = " " + dur_str;
  }

  // Dot fill between label and duration
  int label_vw = visual_width(label);
  int gap      = content_width - label_vw - DUR_COL;

  std::cout << prefix << label << dot_fill(gap) << dur_str;

  // Annotate non-success terminal states so failures stand out
  if (f.status == "fail") {
    std::cout << "  \xe2\x9c\x97 FAIL";
  } else if (f.status == "timeout") {
    std::cout << "  \xe2\x9f\xb3 TIMEOUT";
  }
  std::cout << "\n";

  bool has_children = !f.sub_tasks.empty();

  // Log lines: indented under the task, with tree continuation line if children follow.
  if (full_report && !f.logs.empty()) {
    std::string log_pfx = has_children ? (child_prefix + "\xe2\x94\x82  ") : (child_prefix + "   ");
    for (const auto &log : f.logs) {
      std::cout << log_pfx << log << '\n';
    }
  }

  // Children: draw tree connectors.  Each nesting level eats 3 visual columns.
  for (size_t i = 0; i < f.sub_tasks.size(); ++i) {
    bool        is_last          = (i == f.sub_tasks.size() - 1);
    std::string sub_prefix       = child_prefix + (is_last ? "\xe2\x94\x94\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80 ");
    std::string sub_child_prefix = child_prefix + (is_last ? "   " : "\xe2\x94\x82  ");
    print_task_tree(f.sub_tasks[i], full_report, sub_prefix, sub_child_prefix, content_width - 3);
  }
}

} // namespace
void
ConfigReloadPrinter::write_progress_line(const ConfigReloadResponse::ReloadInfo &info)
{
  if (this->is_json_format()) {
    return;
  }

  int done{0}, total{0};

  auto count_tasks = [&](auto &&self, const ConfigReloadResponse::ReloadInfo &ri) -> void {
    if (ri.sub_tasks.empty()) {
      if (ri.status == "success" || ri.status == "fail") {
        done++;
      }
      total++;
    }
    for (const auto &sub : ri.sub_tasks) {
      self(self, sub);
    }
  };
  count_tasks(count_tasks, info);

  bool terminal = (info.status == "success" || info.status == "fail" || info.status == "timeout");

  int dur_ms = duration_ms(info.meta.created_time_ms, info.meta.last_updated_time_ms);

  std::string bar = build_progress_bar(done, total);

  // \r + ANSI clear-to-EOL overwrites the previous line in place.
  std::cout << "\r\033[K" << status_icon(info.status) << " [" << info.config_token << "] " << bar << " " << done << "/" << total
            << "  " << info.status;
  if (terminal) {
    std::cout << "  (" << format_duration(dur_ms) << ")";
  }
  std::cout << std::flush;
}

void
ConfigReloadPrinter::print_reload_report(const ConfigReloadResponse::ReloadInfo &info, bool full_report)
{
  if (this->is_json_format()) {
    return;
  }

  int overall_duration = duration_ms(info.meta.created_time_ms, info.meta.last_updated_time_ms);

  int total{0}, completed{0}, failed{0}, created{0}, in_progress{0};

  auto calculate_summary = [&](auto &&self, const ConfigReloadResponse::ReloadInfo &ri) -> void {
    if (ri.sub_tasks.empty()) {
      if (ri.status == "success") {
        completed++;
      } else if (ri.status == "fail") {
        failed++;
      } else if (ri.status == "created") {
        created++;
      } else if (ri.status == "in_progress") {
        in_progress++;
      }
      total++;
    }
    if (!ri.sub_tasks.empty()) {
      for (const auto &sub : ri.sub_tasks) {
        self(self, sub);
      }
    }
  };

  std::vector<const ConfigReloadResponse::ReloadInfo *> files;
  group_files(info, files);
  calculate_summary(calculate_summary, info);

  std::string start_time_str = format_time_ms(info.meta.created_time_ms);
  std::string end_time_str   = format_time_ms(info.meta.last_updated_time_ms);

  // ── Header ──
  std::cout << status_icon(info.status) << " Reload [" << info.status << "] \xe2\x80\x94 " << info.config_token << "\n";
  std::cout << "  Started : " << start_time_str << '\n';
  std::cout << "  Finished: " << end_time_str << '\n';
  std::cout << "  Duration: " << format_duration(overall_duration) << "\n\n";

  // ── Summary ──
  std::cout << "  \xe2\x9c\x94 " << completed << " success  \xe2\x97\x8c " << in_progress << " in-progress  \xe2\x9c\x97 " << failed
            << " failed  (" << total << " total)\n";

  // ── Task tree ──
  if (!files.empty()) {
    std::cout << "\n  Tasks:\n";
  }
  const std::string base_prefix("   ");
  for (const auto &sub : info.sub_tasks) {
    print_task_tree(sub, full_report, base_prefix, base_prefix);
  }
}

//------------------------------------------------------------------------------------------------------------------------------------
void
ConfigShowFileRegistryPrinter::write_output(YAML::Node const &result)
{
  if (auto &&registry = result["config_registry"]) {
    if (is_json_format()) {
      write_output_json(registry);
      return;
    }
    for (auto &&element : registry) {
      std::cout << "┌ " << element["file_path"] << '\n';
      std::cout << "└┬ Config name: " << element["config_record_name"] << '\n';
      std::cout << " ├ Parent config: " << element["parent_config"] << '\n';
      std::cout << " ├ Root access needed: " << element["root_access_needed"] << '\n';
      std::cout << " └ Is required: " << element["is_required"] << '\n';
    }
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
ConfigSetPrinter::write_output(YAML::Node const &result)
{
  using TypeToStringMap                                      = std::unordered_map<std::string, std::string>;
  static const TypeToStringMap Update_Type_To_String_Message = {
    {"0", "Set {}"                                                                                          }, // UNDEFINED
    {"1", "Set {}, please wait 10 seconds for traffic server to sync configuration, restart is not required"}, // DYNAMIC
    {"2", "Set {}, restart required"                                                                        }  // RESTART_TS
  };
  std::string text;
  try {
    auto const &response = result.as<ConfigSetRecordResponse>();
    for (auto &&updatedRec : response.data) {
      TypeToStringMap::const_iterator search = Update_Type_To_String_Message.find(updatedRec.updateType);
      if (search != std::end(Update_Type_To_String_Message)) {
        std::cout << swoc::bwprint(text, search->second, updatedRec.recName) << '\n';
      } else {
        std::cout << "Oops we don't know how to handle the update status for '" << updatedRec.recName << "' ["
                  << updatedRec.updateType << "]\n";
      }
    }
  } catch (std::exception const &ex) {
    std::cout << swoc::bwprint(text, "Unexpected error found {}", ex.what());
  }
}
//-----------------------------------------------------------------------------------------------------------------------------------

void
ConfigStatusPrinter::write_output(YAML::Node const &result)
{
  auto const &response = result.as<shared::rpc::RecordLookUpResponse>();
  std::string text, recordName;
  try {
    for (auto &&recordInfo : response.recordList) {
      recordName = recordInfo.name;
      if (recordName == "proxy.process.version.server.long") {
        std::cout << "Version: " << recordInfo.currentValue << "\n";
      } else if (recordName == "proxy.process.proxy.start_time") {
        std::cout << swoc::bwprint(text, "{}: {}\n", "Started at", FloatDate(recordInfo.currentValue, "%a %d %b %Y %H:%M:%S"));
      } else if (recordName == "proxy.process.proxy.reconfigure_time") {
        std::cout << swoc::bwprint(text, "{}: {}\n", "Reconfigured at", FloatDate(recordInfo.currentValue, "%a %d %b %Y %H:%M:%S"));
      } else if (recordName == "proxy.process.proxy.reconfigure_required") {
        std::cout << "Reconfigure required: " << ((recordInfo.currentValue == "1") ? "yes" : "no") << "\n";
      } else if (recordName == "proxy.process.proxy.restart_required") {
        std::cout << "Restart required: " << ((recordInfo.currentValue == "1") ? "yes" : "no") << "\n";
      }
    }
  } catch (...) {
    std::cout << recordName << ": <unable to read the value>" << "\n";
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
RecordDescribePrinter::write_output(YAML::Node const &result)
{
  auto const &response = result.as<shared::rpc::RecordLookUpResponse>();
  std::string text;
  for (auto &&recordInfo : response.recordList) {
    if (!recordInfo.registered) {
      std::cout << recordInfo.name
                << ": Unrecognized configuration value. Record is a configuration name/value but is not registered\n";
      continue;
    }
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Name", recordInfo.name);
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Current Value ", recordInfo.currentValue);
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Default Value ", recordInfo.defaultValue);
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Record Type ", rec_labelof(recordInfo.rclass));
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Data Type ", recordInfo.dataType);

    std::visit(swoc::meta::vary{
                 [&](shared::rpc::RecordLookUpResponse::RecordParamInfo::ConfigMeta const &meta) {
                   std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Access Control ", rec_accessof(meta.accessType));
                   std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Update Type ", rec_updateof(meta.updateType));
                   std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Update Status ", meta.updateStatus);
                   std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Source ", rec_sourceof(meta.source));

                   std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Syntax Check ", meta.checkExpr);
                 },
                 [&](shared::rpc::RecordLookUpResponse::RecordParamInfo::StatMeta const &meta) {
                   // This may not be what we want, as for a metric we may not need to print all the same info. In that case
                   // just create a new printer for this.
                   std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Persist Type ", meta.persistType);
                 },
               },
               recordInfo.meta);

    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Overridable", (recordInfo.overridable ? "yes" : "no"));
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Version ", recordInfo.version);
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Order ", recordInfo.order);
    std::cout << swoc::bwprint(text, "{:16s}: {}\n", "Raw Stat Block ", recordInfo.rsb);
  }

  // also print errors.
  print_record_error_list(response.errorList);
}
//------------------------------------------------------------------------------------------------------------------------------------
void
GetHostStatusPrinter::write_output(YAML::Node const &result)
{
  auto resp = result.as<HostStatusLookUpResponse>();

  if (resp.statusList.size() > 0) {
    for (auto &&host : resp.statusList) {
      std::cout << host.hostName << " " << host.status << '\n';
    }
    std::cout << '\n';
  }

  for (auto &&e : resp.errorList) {
    std::cout << e << '\n';
  }
}

//------------------------------------------------------------------------------------------------------------------------------------
void
SetHostStatusPrinter::write_output([[maybe_unused]] YAML::Node const &result)
{
  // do nothing.
}
//------------------------------------------------------------------------------------------------------------------------------------
void
HostDBStatusPrinter::write_output(YAML::Node const &result)
{
  write_output_json(result["data"] ? result["data"] : result);
}
//-------------------------------------------------------------------------------------------------------------------------------------
void
CacheDiskStoragePrinter::write_output(YAML::Node const &result)
{
  auto my_print = [](auto const &disk) {
    std::cout << "Device: " << disk.path << '\n';
    std::cout << "Status: " << disk.status << '\n';
    std::cout << "Error Count: " << disk.errorCount << '\n';
  };

  auto const &resp = result.as<DeviceStatusInfoResponse>();
  auto        iter = std::begin(resp.data);
  my_print(*iter);
  ++iter;
  for (; iter != std::end(resp.data); ++iter) {
    std::cout << "---\n";
    my_print(*iter);
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
CacheDiskStorageOfflinePrinter::write_output(YAML::Node const &result)
{
  for (auto &&item : result) {
    if (auto n = item["has_online_storage_left"]) {
      bool any_left = n.as<bool>();
      if (!any_left) {
        std::cout << "No more online storage left" << helper::try_extract<std::string>(n, "path") << '\n';
      }
    }
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
RPCAPIPrinter::write_output(YAML::Node const &result)
{
  if (auto methods = result["methods"]) {
    std::cout << "Methods:\n";
    for (auto &&m : methods) {
      std::cout << "- " << m.as<std::string>() << '\n';
    }
  }
  if (auto notifications = result["notifications"]) {
    std::cout << "Notifications:\n";
    for (auto &&m : notifications) {
      std::cout << "- " << m.as<std::string>() << '\n';
    }
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
ServerStatusPrinter::write_output(YAML::Node const &result)
{
  write_output_json(result["data"] ? result["data"] : result);
}
//-------------------------------------------------------------------------------------------------------------------------------------
