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
  struct Options {
    enum class Format {
      LEGACY = 0, // Legacy format, mimics the old traffic_ctl output
      PRETTY,     // Enhanced printing messages. (in case you would like to generate them)
      JSON,       // Json formatting
      RECORDS,    // only valid for configs, but it's handy to have it here.
      DATA_REQ,   // Print json request + default format
      DATA_RESP,  // Print json response + default format
      DATA_ALL    // Print json request and response + default format
    };
    Options() = default;
    Options(Format fmt) : _format(fmt) {}
    Format _format{Format::LEGACY}; //!< selected(passed) format.
  };

  /// Printer constructor. Needs the format as it will be used by derived classes.
  BasePrinter(Options opts) : _printOpt(opts) {}

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

  virtual void write_output(std::string_view output);
  virtual void write_debug(std::string_view output);

  /// Format getters.
  Options::Format get_format() const;
  bool print_req_msg() const;
  bool print_resp_msg() const;
  bool is_json_format() const;
  bool is_legacy_format() const;
  bool is_records_format() const;
  bool is_pretty_format() const;

protected:
  Options _printOpt;
};
inline BasePrinter::Options::Format
BasePrinter::get_format() const
{
  return _printOpt._format;
}

inline bool
BasePrinter::print_req_msg() const
{
  return get_format() == Options::Format::DATA_ALL || get_format() == Options::Format::DATA_REQ;
}

inline bool
BasePrinter::print_resp_msg() const
{
  return get_format() == Options::Format::DATA_ALL || get_format() == Options::Format::DATA_RESP;
}

inline bool
BasePrinter::is_json_format() const
{
  return get_format() == Options::Format::JSON;
}

inline bool
BasePrinter::is_legacy_format() const
{
  return get_format() == Options::Format::LEGACY;
}
inline bool
BasePrinter::is_records_format() const
{
  return get_format() == Options::Format::RECORDS;
}
inline bool
BasePrinter::is_pretty_format() const
{
  return get_format() == Options::Format::PRETTY;
}
//------------------------------------------------------------------------------------------------------------------------------------
class GenericPrinter : public BasePrinter
{
  void
  write_output(YAML::Node const &result) override
  {
    /* muted */
  }

public:
  GenericPrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class RecordPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_legacy(RecordLookUpResponse const &result);
  void write_output_pretty(RecordLookUpResponse const &result);

public:
  RecordPrinter(Options opt) : BasePrinter(opt) { _printAsRecords = is_records_format(); }

protected:
  bool _printAsRecords{false};
};

class MetricRecordPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  MetricRecordPrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class DiffConfigPrinter : public RecordPrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_pretty(YAML::Node const &result);

public:
  DiffConfigPrinter(BasePrinter::Options opt) : RecordPrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class ConfigReloadPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_pretty(YAML::Node const &result);

public:
  ConfigReloadPrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class RecordDescribePrinter : public BasePrinter
{
  void write_output_legacy(RecordLookUpResponse const &result);
  void write_output_pretty(RecordLookUpResponse const &result);
  void write_output(YAML::Node const &result) override;

public:
  RecordDescribePrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class GetHostStatusPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  GetHostStatusPrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class SetHostStatusPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  SetHostStatusPrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class CacheDiskStoragePrinter : public BasePrinter
{
  void write_output_pretty(YAML::Node const &result);
  void write_output(YAML::Node const &result) override;

public:
  CacheDiskStoragePrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class CacheDiskStorageOfflinePrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;
  void write_output_pretty(YAML::Node const &result);

public:
  CacheDiskStorageOfflinePrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class RPCAPIPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  RPCAPIPrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------