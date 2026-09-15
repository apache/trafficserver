/** @file

  config.cc - config file support

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

#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ts/ts.h>
#include <ts/apidefs.h>

#include "Config.h"
#include "common.h"
#include "tscpp/util/TextView.h"

Config::Config()
{
  m_filename = "";
  m_config.clear();
  m_noConfig      = false;
  m_alreadyLoaded = false;
  m_lastCheck     = 0;
  m_lastmtime     = 0;
}

Config::~Config() = default;

namespace
{
bool
readConfig(const std::string &filename, std::map<std::string, std::string> &config)
{
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }
  struct stat info;
  if (fstat(fd, &info) != 0 || info.st_size < 0) {
    close(fd);
    return false;
  }

  std::string config_data(static_cast<size_t>(info.st_size), '\0');
  size_t offset = 0;
  while (offset < config_data.size()) {
    ssize_t n = read(fd, config_data.data() + offset, config_data.size() - offset);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      close(fd);
      return false;
    }
    offset += static_cast<size_t>(n);
  }
  close(fd);

  ts::TextView content(config_data);
  while (content) {
    ts::TextView line = content.take_prefix_at('\n');
    if (line.empty() || '#' == *line) {
      continue;
    }
    line.ltrim_if(&isspace);
    ts::TextView field = line.take_prefix_at('=');
    std::string field_name;
    std::string value;
    if (!field.empty()) {
      field_name.assign(field.data(), field.size());
    }
    if (!line.empty()) {
      value.assign(line.data(), line.size());
    }
    TSDebug(PLUGIN, "%.*s=%.*s", static_cast<int>(field_name.size()), field_name.c_str(), static_cast<int>(value.size()),
            value.c_str());
    if (!field_name.empty()) {
      config[field_name] = value;
    }
  }
  return true;
}
} // namespace

bool
Config::loadConfig(const std::string &filename)
{
  {
    std::lock_guard<std::mutex> lock(m_yconfigLock);
    if (m_alreadyLoaded || m_loading) {
      return m_alreadyLoaded;
    }
    m_filename = filename;
    m_loading  = true;
  }

  // Parse without the lock, then publish the complete map under the lock.
  // Readers see the previous map (empty during the first load), never a partial one.
  std::map<std::string, std::string> config;
  bool success = readConfig(filename, config);

  std::lock_guard<std::mutex> lock(m_yconfigLock);
  m_loading = false;
  if (success) {
    m_config.swap(config);
    m_noConfig      = false;
    m_alreadyLoaded = true;
  }
  return success;
}

bool
Config::setLastConfigChange()
{
  struct stat s;
  time_t oldLastmtime = m_lastmtime;

  memset(&s, 0, sizeof(s));
  if (stat(m_filename.c_str(), &s) != 0) {
    return false;
  }

  m_lastmtime = s.st_mtime;

  if (s.st_mtime > oldLastmtime) {
    return true;
  }
  return false;
}

bool
Config::configHasChanged()
{
  std::lock_guard<std::mutex> lock(m_yconfigLock);
  return !m_loading && checkConfigChange();
}

bool
Config::checkConfigChange()
{
  time_t checkTime = time(nullptr) / cCheckDivisor;

  if (m_lastCheck != checkTime) {
    m_lastCheck = checkTime;
    return setLastConfigChange();
  }
  return false;
}

bool
Config::loadConfigOnChange()
{
  std::string filename;
  {
    std::lock_guard<std::mutex> lock(m_yconfigLock);
    if (m_loading || m_filename.empty()) {
      return true;
    }
    if (!checkConfigChange()) {
      return true;
    }
    m_alreadyLoaded = false;
    filename        = m_filename;
  }
  return loadConfig(filename);
}

bool
Config::getValue(const std::string &category, const std::string &key, std::string &value)
{
  if (loadConfigOnChange()) {
    std::lock_guard<std::mutex> lock(m_yconfigLock);
    if (!m_noConfig) {
      auto it = m_config.find(category + "." + key);
      if (it != m_config.end()) {
        value = it->second;
      }
    }
  }
  return !value.empty();
}
