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
#include "CtrlPrinters.h"

#include <iostream>
#include <unordered_map>
#include <string_view>

#include "jsonrpc/ctrl_yaml_codecs.h"
#include "tscpp/util/ts_meta.h"
#include <tscore/BufferWriter.h>
#include "PrintUtils.h"

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
    YAML::Emitter out;
    out << YAML::DoubleQuoted << YAML::Flow;
    out << response.fullMsg;
    write_output(std::string_view{out.c_str()});
    return;
  }

  if (response.is_error() && this->is_pretty_format()) {
    // we print the error in this case. Already formatted.
    std::cout << response.error.as<shared::rpc::JSONRPCError>();
    return;
  }

  if (!response.result.IsNull()) {
    // on you!
    // Found convinient to let the derived class deal with the specifics.
    write_output(response.result);
  }
}

void
BasePrinter::write_output(std::string_view output)
{
  std::cout << output << '\n';
}

void
BasePrinter::write_debug(std::string_view output)
{
  std::cout << output << '\n';
}

//------------------------------------------------------------------------------------------------------------------------------------
void
RecordPrinter::write_output(YAML::Node const &result)
{
  auto response = result.as<shared::rpc::RecordLookUpResponse>();
  if (is_legacy_format()) {
    write_output_legacy(response);
  } else {
    write_output_pretty(response);
  }
}
void
RecordPrinter::write_output_legacy(shared::rpc::RecordLookUpResponse const &response)
{
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
      std::cout << ts::bwprint(text, "{} {} {} {} # default: {}\n", rec_labelof(recordInfo.rclass), recordInfo.name,
                               recordInfo.dataType, recordInfo.currentValue, recordInfo.defaultValue);
    }
  }
  // we print errors if found.
  print_record_error_list(response.errorList);
}
void
RecordPrinter::write_output_pretty(shared::rpc::RecordLookUpResponse const &response)
{
  write_output_legacy(response);
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
        std::cout << ts::bwprint(text, "{} has changed\n", recordInfo.name);
        std::cout << ts::bwprint(text, "\tCurrent Value: {}\n", currentValue);
        std::cout << ts::bwprint(text, "\tDefault Value: {}\n", defaultValue);
      } else {
        std::cout << ts::bwprint(text, "{} {} {} {} # default: {}\n", rec_labelof(recordInfo.rclass), recordInfo.name,
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
ConfigSetPrinter::write_output(YAML::Node const &result)
{
  // we match the legacy format, the only one supported for now.
  static const std::unordered_map<std::string, std::string> Update_Type_To_String_Message = {
    {"0", "Set {}"},                                                                                           // UNDEFINED
    {"1", "Set {}, please wait 10 seconds for traffic server to sync configuration, restart is not required"}, // DYNAMIC
    {"2", "Set {}, restart required"},                                                                         // RESTART_TS
    {"3", "Set {}, restart required"} // RESTART TM, we take care of this in case we get it from TS.
  };
  std::string text;
  try {
    auto const &response = result.as<ConfigSetRecordResponse>();
    for (auto &&updatedRec : response.data) {
      if (auto search = Update_Type_To_String_Message.find(updatedRec.updateType);
          search != std::end(Update_Type_To_String_Message)) {
        std::cout << ts::bwprint(text, search->second, updatedRec.recName) << '\n';
      } else {
        std::cout << "Oops we don't know how to handle the update status for '" << updatedRec.recName << "' ["
                  << updatedRec.updateType << "]\n";
      }
    }
  } catch (std::exception const &ex) {
    std::cout << ts::bwprint(text, "Unexpected error found {}", ex.what());
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
RecordDescribePrinter::write_output(YAML::Node const &result)
{
  auto const &response = result.as<shared::rpc::RecordLookUpResponse>();
  if (is_legacy_format()) {
    write_output_legacy(response);
  } else {
    write_output_pretty(response);
  }
}

void
RecordDescribePrinter::write_output_legacy(shared::rpc::RecordLookUpResponse const &response)
{
  std::string text;
  for (auto &&recordInfo : response.recordList) {
    if (!recordInfo.registered) {
      std::cout << recordInfo.name
                << ": Unrecognized configuration value. Record is a configuration name/value but is not registered\n";
      continue;
    }
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Name", recordInfo.name);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Current Value ", recordInfo.currentValue);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Default Value ", recordInfo.defaultValue);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Record Type ", rec_labelof(recordInfo.rclass));
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Data Type ", recordInfo.dataType);

    std::visit(ts::meta::overloaded{
                 [&](shared::rpc::RecordLookUpResponse::RecordParamInfo::ConfigMeta const &meta) {
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Access Control ", rec_accessof(meta.accessType));
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Update Type ", rec_updateof(meta.updateType));
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Update Status ", meta.updateStatus);
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Source ", rec_sourceof(meta.source));

                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Syntax Check ", meta.checkExpr);
                 },
                 [&](shared::rpc::RecordLookUpResponse::RecordParamInfo::StatMeta const &meta) {
                   // This may not be what we want, as for a metric we may not need to print all the same info. In that case
                   // just create a new printer for this.
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Persist Type ", meta.persistType);
                 },
               },
               recordInfo.meta);

    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Overridable", (recordInfo.overridable ? "yes" : "no"));
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Version ", recordInfo.version);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Order ", recordInfo.order);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Raw Stat Block ", recordInfo.rsb);
  }

  // also print errors.
  print_record_error_list(response.errorList);
}

void
RecordDescribePrinter::write_output_pretty(shared::rpc::RecordLookUpResponse const &response)
{
  // we default for legacy.
  write_output_legacy(response);
}
//------------------------------------------------------------------------------------------------------------------------------------
void
GetHostStatusPrinter::write_output(YAML::Node const &result)
{
  auto response = result.as<shared::rpc::RecordLookUpResponse>();
  for (auto &&recordInfo : response.recordList) {
    std::cout << recordInfo.name << " " << recordInfo.currentValue << '\n';
  }
  for (auto &&e : response.errorList) {
    std::cout << "Failed  to fetch " << e.recordName << '\n';
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
  // do nothing.
  if (!is_legacy_format()) {
    write_output_pretty(result);
  }
}
void
CacheDiskStoragePrinter::write_output_pretty(YAML::Node const &result)
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
  if (!is_legacy_format()) {
    write_output_pretty(result);
  }
}
void
CacheDiskStorageOfflinePrinter::write_output_pretty(YAML::Node const &result)
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
