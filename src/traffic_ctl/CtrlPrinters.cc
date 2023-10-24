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
#include "tscpp/util/ts_bw_format.h"

#include "traffic_ctl/CtrlPrinters.h"
#include "jsonrpc/ctrl_yaml_codecs.h"
#include "traffic_ctl/PrintUtils.h"

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
  if (errors.size()) {
    std::cout << "------------ Errors ----------\n";
    auto iter = std::begin(errors);
    if (iter != std::end(errors)) {
      std::cout << *iter;
    }
    ++iter;
    for (auto err = iter; err != std::end(errors); ++err) {
      std::cout << "--\n";
      std::cout << *err;
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
    // If an error is present, then as per the specs we can ignore the jsonrpc.result field, so we print the error and we are done
    // here!
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
  write_output(std::string_view{out.c_str()});
}
//------------------------------------------------------------------------------------------------------------------------------------
void
RecordPrinter::write_output(YAML::Node const &result)
{
  auto const &response = result.as<shared::rpc::RecordLookUpResponse>();
  std::string text;
  for (auto &&recordInfo : response.recordList) {
    if (!recordInfo.registered) {
      std::cout << recordInfo.name
                << ": Unrecognized configuration value. Record is a configuration name/value but is not registered\n";
      continue;
    }
    if (!_printAsRecords) {
      std::cout << recordInfo.name << ": " << recordInfo.currentValue << '\n';
    } else {
      std::cout << swoc::bwprint(text, "{} {} {} {} # default: {}\n", rec_labelof(recordInfo.rclass), recordInfo.name,
                                 recordInfo.dataType, recordInfo.currentValue, recordInfo.defaultValue);
    }
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
  std::string text;
  auto response = result.as<shared::rpc::RecordLookUpResponse>();
  for (auto &&recordInfo : response.recordList) {
    auto const &currentValue = recordInfo.currentValue;
    auto const &defaultValue = recordInfo.defaultValue;
    const bool hasChanged    = (currentValue != defaultValue);
    if (hasChanged) {
      if (!_printAsRecords) {
        std::cout << swoc::bwprint(text, "{} has changed\n", recordInfo.name);
        std::cout << swoc::bwprint(text, "\tCurrent Value: {}\n", currentValue);
        std::cout << swoc::bwprint(text, "\tDefault Value: {}\n", defaultValue);
      } else {
        std::cout << swoc::bwprint(text, "{} {} {} {} # default: {}\n", rec_labelof(recordInfo.rclass), recordInfo.name,
                                   recordInfo.dataType, recordInfo.currentValue, recordInfo.defaultValue);
      }
    }
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
ConfigReloadPrinter::write_output(YAML::Node const &result)
{
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
    {"2", "Set {}, restart required"                                                                        }, // RESTART_TS
    {"3", "Set {}, restart required"                                                                        }  // RESTART TM, we take care of this in case we get it from TS.
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
    std::cout << recordName << ": <unable to read the value>"
              << "\n";
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
SetHostStatusPrinter::write_output(YAML::Node const &result)
{
  // do nothing.
}
//------------------------------------------------------------------------------------------------------------------------------------

void
CacheDiskStoragePrinter::write_output(YAML::Node const &result)
{
  auto my_print = [](auto const &disk) {
    std::cout << "Device: " << disk.path << '\n';
    std::cout << "Status: " << disk.status << '\n';
    std::cout << "Error Count: " << disk.errorCount << '\n';
  };

  auto const &resp = result.as<DeviceStatusInfoResponse>();
  auto iter        = std::begin(resp.data);
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
//---------------------------------------------------------------------------------------------------------------------------------
