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

#include "CtrlPrinters.h"
#include "jsonrpc/yaml_codecs.h"
#include "tscpp/util/ts_meta.h"
#include <tscore/BufferWriter.h>
#include "PrintUtils.h"

//------------------------------------------------------------------------------------------------------------------------------------

namespace
{
void
print_record_error_list(std::vector<RecordLookUpResponse::RecordError> const &errors)
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
BasePrinter::write_output(specs::JSONRPCResponse const &response)
{
  if (response.is_error() && _format == Format::PRETTY) {
    std::cout << response.error.as<specs::JSONRPCError>();
    return;
  }
  if (!response.result.IsNull()) {
    write_output(response.result);
  }
}

//------------------------------------------------------------------------------------------------------------------------------------
void
RecordPrinter::write_output(YAML::Node const &result)
{
  auto response = result.as<RecordLookUpResponse>();
  if (is_format_legacy()) {
    write_output_legacy(response);
  } else {
    write_output_pretty(response);
  }
}
void
RecordPrinter::write_output_legacy(RecordLookUpResponse const &response)
{
  std::string text;
  for (auto &&recordInfo : response.recordList) {
    if (!_printAsRecords) {
      std::cout << ts::bwprint(text, "{}: {}\n", recordInfo.name, recordInfo.currentValue);
    } else {
      std::cout << ts::bwprint(text, "{} {} {} {} # default: {}\n", rec_labelof(recordInfo.rclass), recordInfo.name,
                               recordInfo.dataType, recordInfo.currentValue, recordInfo.defaultValue);
    }
  }
}
void
RecordPrinter::write_output_pretty(RecordLookUpResponse const &response)
{
  write_output_legacy(response);
  print_record_error_list(response.errorList);
}
//------------------------------------------------------------------------------------------------------------------------------------
void
MetricRecordPrinter::write_output(YAML::Node const &result)
{
  std::string text;
  auto response = result.as<RecordLookUpResponse>();
  for (auto &&recordInfo : response.recordList) {
    std::cout << ts::bwprint(text, "{} {}\n", recordInfo.name, recordInfo.currentValue);
  }
}
//------------------------------------------------------------------------------------------------------------------------------------

void
DiffConfigPrinter::write_output(YAML::Node const &result)
{
  std::string text;
  auto response = result.as<RecordLookUpResponse>();
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

//------------------------------------------------------------------------------------------------------------------------------------
void
ConfigReloadPrinter::write_output(YAML::Node const &result)
{
}
//------------------------------------------------------------------------------------------------------------------------------------
void
RecordDescribePrinter::write_output(YAML::Node const &result)
{
  auto const &response = result.as<RecordLookUpResponse>();
  if (is_format_legacy()) {
    write_output_legacy(response);
  } else {
    write_output_pretty(response);
  }
}

void
RecordDescribePrinter::write_output_legacy(RecordLookUpResponse const &response)
{
  std::string text;
  for (auto &&recordInfo : response.recordList) {
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Name", recordInfo.name);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Current Value ", recordInfo.currentValue);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Default Value ", recordInfo.defaultValue);
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Record Type ", rec_labelof(recordInfo.rclass));
    std::cout << ts::bwprint(text, "{:16s}: {}\n", "Data Type ", recordInfo.dataType);

    std::visit(ts::meta::overloaded{
                 [&](RecordLookUpResponse::RecordParamInfo::ConfigMeta const &meta) {
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Access Control ", rec_accessof(meta.accessType));
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Update Type ", rec_updateof(meta.updateType));
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Update Status ", meta.updateStatus);
                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Source ", rec_sourceof(meta.source));

                   std::cout << ts::bwprint(text, "{:16s}: {}\n", "Syntax Check ", meta.checkExpr);
                 },
                 [&](RecordLookUpResponse::RecordParamInfo::StatMeta const &meta) {
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
}

void
RecordDescribePrinter::write_output_pretty(RecordLookUpResponse const &response)
{
  std::string text;

  write_output_legacy(response);
  print_record_error_list(response.errorList);
}
//------------------------------------------------------------------------------------------------------------------------------------
void
GetHostStatusPrinter::write_output(YAML::Node const &result)
{
  auto response = result.as<RecordLookUpResponse>();
  std::string text;
  for (auto &&recordInfo : response.recordList) {
    std::cout << ts::bwprint(text, "{} {}\n", recordInfo.name, recordInfo.currentValue);
  }
  for (auto &&e : response.errorList) {
    std::cout << ts::bwprint(text, "Failed  to fetch {}\n", e.recordName);
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
  if (!is_format_legacy()) {
    write_output_pretty(result);
  }
}
void
CacheDiskStoragePrinter::write_output_pretty(YAML::Node const &result)
{
  std::string text;
  auto my_print = [&text](auto const &disk) {
    std::cout << ts::bwprint(text, "Device: {}\n", disk.path);
    std::cout << ts::bwprint(text, "Status: {}\n", disk.status);
    std::cout << ts::bwprint(text, "Error Count: {}\n", disk.errorCount);
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
  if (!is_format_legacy()) {
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
        std::string text;
        std::cout << ts::bwprint(text, "No more online storage left. {}\n", try_extract<std::string>(n, "path"));
      }
    }
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
void
RPCAPIPrinter::write_output(YAML::Node const &result)
{
  std::string text;
  if (auto methods = result["methods"]) {
    std::cout << "Methods:\n";
    for (auto &&m : methods) {
      std::cout << ts::bwprint(text, "- {}\n", m.as<std::string>());
    }
  }
  if (auto notifications = result["notifications"]) {
    std::cout << "Notifications:\n";
    for (auto &&m : notifications) {
      std::cout << ts::bwprint(text, "- {}\n", m.as<std::string>());
    }
  }
}
//------------------------------------------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------------------------------------

std::ostream &
operator<<(std::ostream &os, const RecordLookUpResponse::RecordError &re)
{
  std::string text;
  os << ts::bwprint(text, "{:16s}: {}\n", "Record Name ", re.recordName);
  os << ts::bwprint(text, "{:16s}: {}\n", "Code", re.code);
  if (!re.message.empty()) {
    os << ts::bwprint(text, "{:16s}: {}\n", "Message", re.message);
  }
  return os;
}

namespace specs
{
std::ostream &
operator<<(std::ostream &os, const specs::JSONRPCError &err)
{
  std::string text;
  os << "Error found.\n";
  os << ts::bwprint(text, "code: {}\n", err.code);
  os << ts::bwprint(text, "message: {}\n", err.message);
  if (err.data.size() > 0) {
    os << "---\nAdditional error information found:\n";
    auto my_print = [&](auto const &e) {
      os << ts::bwprint(text, "+ code: {}\n", e.first);
      os << ts::bwprint(text, "+ message: {}\n", e.second);
    };

    auto iter = std::begin(err.data);

    my_print(*iter);
    ++iter;
    for (; iter != std::end(err.data); ++iter) {
      os << "---\n";
      my_print(*iter);
    }
  }

  return os;
}
} // namespace specs