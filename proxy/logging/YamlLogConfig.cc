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

#include "YamlLogConfig.h"
#include "YamlLogConfigDecoders.h"

#include "LogConfig.h"
#include "LogObject.h"

#include "tscore/EnumDescriptor.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <memory>

bool
YamlLogConfig::parse(const char *cfgFilename)
{
  bool result;
  try {
    result = loadLogConfig(cfgFilename);
  } catch (std::exception &ex) {
    Error("%s", ex.what());
    result = false;
  }
  return result;
}

bool
YamlLogConfig::loadLogConfig(const char *cfgFilename)
{
  YAML::Node config = YAML::LoadFile(cfgFilename);

  if (config.IsNull()) {
    return false;
  }

  if (!config.IsMap()) {
    Error("malformed logging.yaml file; expected a map");
    return false;
  }

  if (config["logging"]) {
    config = config["logging"];
  } else {
    Error("malformed logging.yaml file; expected a toplevel 'logging' node");
    return false;
  }

  auto formats = config["formats"];
  for (auto const &node : formats) {
    auto fmt = node.as<std::unique_ptr<LogFormat>>().release();
    if (fmt->valid()) {
      cfg->format_list.add(fmt, false);

      if (is_debug_tag_set("log")) {
        printf("The following format was added to the global format list\n");
        fmt->display(stdout);
      }
    } else {
      Note("Format named \"%s\" will not be active; not a valid format", fmt->name() ? fmt->name() : "");
      delete fmt;
    }
  }

  auto filters = config["filters"];
  for (auto const &node : filters) {
    auto filter = node.as<std::unique_ptr<LogFilter>>().release();

    if (filter) {
      cfg->filter_list.add(filter, false);

      if (is_debug_tag_set("xml")) {
        printf("The following filter was added to the global filter list\n");
        filter->display(stdout);
      }
    }
  }

  auto logs = config["logs"];
  for (auto const &node : logs) {
    auto obj = decodeLogObject(node);
    if (obj) {
      cfg->log_object_manager.manage_object(obj);
    }
  }
  return true;
}

TsEnumDescriptor ROLLING_MODE_TEXT = {{{"none", 0}, {"time", 1}, {"size", 2}, {"both", 3}, {"any", 4}}};
TsEnumDescriptor ROLLING_MODE_LUA  = {
  {{"log.roll.none", 0}, {"log.roll.time", 1}, {"log.roll.size", 2}, {"log.roll.both", 3}, {"log.roll.any", 4}}};

std::set<std::string> valid_log_object_keys = {
  "filename",          "format",          "mode",    "header",   "rolling_enabled", "rolling_interval_sec",
  "rolling_offset_hr", "rolling_size_mb", "filters", "min_count"};

LogObject *
YamlLogConfig::decodeLogObject(const YAML::Node &node)
{
  for (auto const &item : node) {
    if (std::none_of(valid_log_object_keys.begin(), valid_log_object_keys.end(),
                     [&item](std::string s) { return s == item.first.as<std::string>(); })) {
      throw YAML::ParserException(item.Mark(), "log: unsupported key '" + item.first.as<std::string>() + "'");
    }
  }

  if (!node["format"]) {
    throw YAML::ParserException(node.Mark(), "missing 'format' argument");
  }
  std::string format = node["format"].as<std::string>();

  if (!node["filename"]) {
    throw YAML::ParserException(node.Mark(), "missing 'filename' argument");
  }

  std::string header;
  if (node["header"]) {
    header = node["header"].as<std::string>();
  }

  std::string filename = node["filename"].as<std::string>();
  LogFormat *fmt       = cfg->format_list.find_by_name(format.c_str());
  if (!fmt) {
    Error("Format %s is not a known format; cannot create LogObject", format.c_str());
    return nullptr;
  }

  // file format
  LogFileFormat file_type = LOG_FILE_ASCII; // default value
  if (node["mode"]) {
    std::string mode = node["mode"].as<std::string>();
    file_type        = (0 == strncasecmp(mode.c_str(), "bin", 3) || (1 == mode.size() && mode[0] == 'b') ?
                   LOG_FILE_BINARY :
                   (0 == strcasecmp(mode.c_str(), "ascii_pipe") ? LOG_FILE_PIPE : LOG_FILE_ASCII));
  }

  int obj_rolling_enabled      = cfg->rolling_enabled;
  int obj_rolling_interval_sec = cfg->rolling_interval_sec;
  int obj_rolling_offset_hr    = cfg->rolling_offset_hr;
  int obj_rolling_size_mb      = cfg->rolling_size_mb;
  int obj_min_count            = cfg->rolling_min_count;

  if (node["rolling_enabled"]) {
    auto value          = node["rolling_enabled"].as<std::string>();
    obj_rolling_enabled = ROLLING_MODE_TEXT.get(value);
    if (obj_rolling_enabled < 0) {
      obj_rolling_enabled = ROLLING_MODE_LUA.get(value);
      if (obj_rolling_enabled < 0) {
        obj_rolling_enabled = node["rolling_enabled"].as<int>();
        if (obj_rolling_enabled < Log::NO_ROLLING || obj_rolling_enabled > Log::ROLL_ON_TIME_AND_SIZE) {
          throw YAML::ParserException(node["rolling_enabled"].Mark(), "unknown value " + value);
        }
      }
    }
  }
  if (node["rolling_interval_sec"]) {
    obj_rolling_interval_sec = node["rolling_interval_sec"].as<int>();
  }
  if (node["rolling_offset_hr"]) {
    obj_rolling_offset_hr = node["rolling_offset_hr"].as<int>();
  }
  if (node["rolling_size_mb"]) {
    obj_rolling_size_mb = node["rolling_size_mb"].as<int>();
  }
  if (node["min_count"]) {
    obj_min_count = node["min_count"].as<int>();
  }
  if (!LogRollingEnabledIsValid(obj_rolling_enabled)) {
    Warning("Invalid log rolling value '%d' in log object", obj_rolling_enabled);
  }

  auto logObject = new LogObject(fmt, Log::config->logfile_dir, filename.c_str(), file_type, header.c_str(),
                                 (Log::RollingEnabledValues)obj_rolling_enabled, Log::config->preproc_threads,
                                 obj_rolling_interval_sec, obj_rolling_offset_hr, obj_rolling_size_mb);

  // Generate LogDeletingInfo entry for later use
  std::string ext;
  switch (file_type) {
  case LOG_FILE_ASCII:
    ext = LOG_FILE_ASCII_OBJECT_FILENAME_EXTENSION;
    break;
  case LOG_FILE_PIPE:
    ext = LOG_FILE_PIPE_OBJECT_FILENAME_EXTENSION;
    break;
  case LOG_FILE_BINARY:
    ext = LOG_FILE_BINARY_OBJECT_FILENAME_EXTENSION;
    break;
  default:
    break;
  }
  cfg->deleting_info.insert(new LogDeletingInfo(filename + ext, ((obj_min_count == 0) ? INT_MAX : obj_min_count)));

  // filters
  auto filters = node["filters"];
  if (!filters) {
    return logObject;
  }

  if (!filters.IsSequence()) {
    throw YAML::ParserException(filters.Mark(), "'filters' should be a list");
  }

  for (auto const &filter : filters) {
    std::string filter_name = filter.as<std::string>().c_str();
    LogFilter *f            = cfg->filter_list.find_by_name(filter_name.c_str());
    if (!f) {
      Warning("Filter %s is not a known filter; cannot add to this LogObject", filter_name.c_str());
    } else {
      logObject->add_filter(f);
    }
  }

  return logObject;
}
