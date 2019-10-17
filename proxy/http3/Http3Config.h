/** @file
 *
 *  HTTP/3 Config
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "ProxyConfig.h"

class Http3ConfigParams : public ConfigInfo
{
public:
  Http3ConfigParams(){};
  ~Http3ConfigParams(){};

  void initialize();

  uint32_t header_table_size() const;
  uint32_t max_header_list_size() const;
  uint32_t qpack_blocked_streams() const;
  uint32_t num_placeholders() const;
  uint32_t max_settings() const;

private:
  uint32_t _header_table_size     = 0;
  uint32_t _max_header_list_size  = 0;
  uint32_t _qpack_blocked_streams = 0;
  uint32_t _num_placeholders      = 0;
  uint32_t _max_settings          = 10;
};

class Http3Config
{
public:
  static void startup();
  static void reconfigure();
  static Http3ConfigParams *acquire();
  static void release(Http3ConfigParams *params);

  using scoped_config = ConfigProcessor::scoped_config<Http3Config, Http3ConfigParams>;

private:
  static int _config_id;
};
