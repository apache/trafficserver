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
#include <string_view>
#include <yaml-cpp/yaml.h>

#include "shared/rpc/RPCRequests.h"
#include <swoc/BufferWriter.h>

/** Format wrapper for floating point time stamps represented as strings.
 * If the time isn't provided, the current epoch time is used. If the format string isn't
 * provided a format like "2017 Jun 29 14:11:29" is used.
 */
struct FloatDate {
  std::string_view _src;
  std::string_view _fmt;

  /** Constructor.
   *
   * @param src A string's representation of a floating point timestamp.
   * @param fmt The format specification for the date string.
   */
  FloatDate(std::string_view src, std::string_view fmt) : _src{src}, _fmt{fmt} {}
};

/** Format a timestamp wrapped in a @c FloatDate.
 *
 * @param w Output.
 * @param spec Format specifier.
 * @param wrap Timestamp string_view wrapper.
 * @return @a w
 */
swoc::BufferWriter &bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, FloatDate const &wrap);

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
    enum FormatFlags {
      NOT_SET      = 0,      // nothing set.
      JSON         = 1 << 0, // Json formatting
      RECORDS      = 1 << 1, // only valid for configs, but it's handy to have it here.
      RPC          = 1 << 2, // Print JSONRPC request and response + default output.
      SHOW_DEFAULT = 1 << 3  // Add the default values alongside with the actual value.
    };
    Options() = default;
    Options(FormatFlags flags) : _format(flags) {}
    mutable FormatFlags _format{FormatFlags::NOT_SET}; //!< selected(passed) format.
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
  void write_output(shared::rpc::JSONRPCResponse const &response);

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

  virtual void write_output(std::string_view output) const;
  virtual void write_debug(std::string_view output) const;

  /// FormatFlags getters.
  Options::FormatFlags get_format() const;
  bool                 print_rpc_message() const;
  bool                 is_json_format() const;
  bool                 is_records_format() const;
  bool                 should_include_default() const;

protected:
  void    write_output_json(YAML::Node const &node) const;
  Options _printOpt;
};

constexpr enum BasePrinter::Options::FormatFlags
operator|(const enum BasePrinter::Options::FormatFlags rhs, const enum BasePrinter::Options::FormatFlags lhs)
{
  return static_cast<BasePrinter::Options::FormatFlags>(static_cast<uint32_t>(rhs) | static_cast<uint32_t>(lhs));
}

constexpr enum BasePrinter::Options::FormatFlags &
operator|=(BasePrinter::Options::FormatFlags &rhs, BasePrinter::Options::FormatFlags lhs)
{
  return rhs = rhs | lhs;
}

constexpr enum BasePrinter::Options::FormatFlags
operator&(BasePrinter::Options::FormatFlags rhs, BasePrinter::Options::FormatFlags lhs)
{
  return static_cast<BasePrinter::Options::FormatFlags>(static_cast<uint32_t>(rhs) & static_cast<uint32_t>(lhs));
}

inline BasePrinter::Options::FormatFlags
BasePrinter::get_format() const
{
  return _printOpt._format;
}

inline bool
BasePrinter::print_rpc_message() const
{
  return _printOpt._format & Options::FormatFlags::RPC;
}

inline bool
BasePrinter::is_json_format() const
{
  return _printOpt._format & Options::FormatFlags::JSON;
}

inline bool
BasePrinter::is_records_format() const
{
  return _printOpt._format & Options::FormatFlags::RECORDS;
}
inline bool
BasePrinter::should_include_default() const
{
  return _printOpt._format & Options::FormatFlags::SHOW_DEFAULT;
}
//------------------------------------------------------------------------------------------------------------------------------------
class GenericPrinter : public BasePrinter
{
  void
  write_output([[maybe_unused]] YAML::Node const &result) override
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

public:
  RecordPrinter(Options opt) : BasePrinter(opt) {}

protected:
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

public:
  DiffConfigPrinter(BasePrinter::Options opt) : RecordPrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class ConfigReloadPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  ConfigReloadPrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class ConfigShowFileRegistryPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  using BasePrinter::BasePrinter;
};
//------------------------------------------------------------------------------------------------------------------------------------
class ConfigSetPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  using BasePrinter::BasePrinter;
};
//------------------------------------------------------------------------------------------------------------------------------------
class ConfigStatusPrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

public:
  using BasePrinter::BasePrinter;
};
//------------------------------------------------------------------------------------------------------------------------------------
class RecordDescribePrinter : public BasePrinter
{
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
  void write_output(YAML::Node const &result) override;

public:
  CacheDiskStoragePrinter(BasePrinter::Options opt) : BasePrinter(opt) {}
};
//------------------------------------------------------------------------------------------------------------------------------------
class CacheDiskStorageOfflinePrinter : public BasePrinter
{
  void write_output(YAML::Node const &result) override;

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
