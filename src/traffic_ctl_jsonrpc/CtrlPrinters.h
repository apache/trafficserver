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
#pragma once

#include <iostream>
#include <yaml-cpp/yaml.h>

#include "jsonrpc/RPCRequests.h"

//------------------------------------------------------------------------------------------------------------------------------------
///
/// Base class that implements the basic output format.
///
/// Every command will print out specific details depending on the nature of the message. This base class models the basic API.
/// @c _format member should be set when the object is created, this member should be used to  decide the way we want to generate
/// the output and possibly(TODO) where we want it(stdout, stderr, etc.). If no output is needed GenericPrinter can be used which is
/// muted.
class BasePrinter
{
public:
  /// This enum maps the --format flag coming from traffic_ctl. (also --records is included here, see comments down below.)
  enum class Format {
    LEGACY = 0, // Legacy format, mimics the old traffic_ctl output
    PRETTY,     // Enhanced printing messages. (in case you would like to generate them)
    RECORDS     // only valid for configs, but it's handy to have it here.
  };

  /// Printer constructor. Needs the format as it will be used by derived classes.
  BasePrinter(Format fmt) : _format(fmt) {}
  BasePrinter()          = default;
  virtual ~BasePrinter() = default;

  ///
  /// Function that will generate the expected output based on the response result.
  ///
  /// If the response contains any high level error, it will be print and the the specific derived class @c write_output() will not
  /// be called.
  /// @param response the  server response.
  ///
  void write_output(specs::JSONRPCResponse const &response);

  ///
  /// Write output based on the response values.
  ///
  /// Implement this one so you deal with the expected output, @c _format will be already set to the right
  /// selected type so you can decide what to print.
  ///
  /// @param result jsonrpc result structure. No format specified by us, it's the one specified by the actual jsonrpc
  ///               response.
  ///
  virtual void write_output(YAML::Node const &result) = 0;

  virtual void
  write_output(std::string_view output)
  {
    std::cout << output;
  }

protected:
  /// handy function that checks if the selected format is legacy.
  bool
  is_format_legacy()
  {
    return _format == Format::LEGACY;
  }

  Format _format{Format::LEGACY}; //!< selected(passed) format.
};
//------------------------------------------------------------------------------------------------------------------------------------
class GenericPrinter : public BasePrinter
{
  void
  write_output(YAML::Node const &result) override
  {
    /* muted */
  }

public:
  GenericPrinter() : BasePrinter() {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class RecordPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_legacy(RecordLookUpResponse const &result);
  void write_output_pretty(RecordLookUpResponse const &result);

public:
  RecordPrinter(BasePrinter::Format fmt) : BasePrinter(fmt) { _printAsRecords = (_format == Format::RECORDS); }

protected:
  bool _printAsRecords{false};
};

class MetricRecordPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  MetricRecordPrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class DiffConfigPrinter : public RecordPrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_pretty(YAML::Node const &result);

public:
  DiffConfigPrinter(BasePrinter::Format fmt) : RecordPrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class ConfigReloadPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_pretty(YAML::Node const &result);

public:
  ConfigReloadPrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class RecordDescribePrinter : public BasePrinter
{
  void write_output_legacy(RecordLookUpResponse const &result);
  void write_output_pretty(RecordLookUpResponse const &result);
  void write_output(YAML::Node const &result) override;

public:
  RecordDescribePrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class GetHostStatusPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  GetHostStatusPrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class SetHostStatusPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  SetHostStatusPrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class CacheDiskStoragePrinter : public BasePrinter
{
  void write_output_pretty(YAML::Node const &result);
  void write_output(YAML::Node const &result) override;

public:
  CacheDiskStoragePrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class CacheDiskStorageOfflinePrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_pretty(YAML::Node const &result);

public:
  CacheDiskStorageOfflinePrinter(BasePrinter::Format fmt) : BasePrinter(fmt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class RPCAPIPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;
};
//------------------------------------------------------------------------------------------------------------------------------------