/** @file

  Config.h - configuration file support

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

#include <map>
#include <string>
#include <sstream>
#include <mutex>

struct fromstring {
  fromstring(const std::string &string) : _string(string) {}
  template <typename _T> operator _T() const
  {
    _T t;
    std::istringstream ss(_string);
    ss >> t;
    return t;
  }

protected:
  std::string _string;
};

class Config
{
public:
  virtual bool loadConfig(const std::string &filename);

  static Config &
  getSingleton()
  {
    static Config onlyInstance;
    return onlyInstance;
  }

  bool getValue(const std::string &category, const std::string &key, std::string &value);

  template <typename _T>
  bool
  getValue(const std::string &category, const std::string &key, _T &value)
  {
    std::string strvalue;
    if (!getValue(category, key, strvalue))
      return false;
    value = fromstring(strvalue);
    return true;
  }

  template <typename _T>
  _T
  returnValue(const std::string &category, const std::string &key, _T default_value)
  {
    std::string strvalue;
    if (!getValue(category, key, strvalue))
      return default_value;
    return fromstring(strvalue);
  }

  bool configHasChanged();

private:
  // make sure the unit tests can access internals
  friend class sslSessionReuseConfigTest;

  Config();
  virtual ~Config();

  bool loadConfigOnChange();

  static const int cCheckDivisor = 5;

  // returns true if the mtime is newer than m_lastmtime
  // forcefully changes m_lastmtime.
  bool setLastConfigChange();

  std::string m_filename;
  std::map<std::string, std::string> m_config;
  std::mutex m_yconfigLock;
  bool m_noConfig;
  bool m_alreadyLoaded;
  time_t m_lastCheck;
  time_t m_lastmtime;
};
