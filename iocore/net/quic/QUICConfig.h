/** @file
 *
 *  A brief file description
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

class QUICConfigParams : public ConfigInfo
{
public:
  void initialize();

  uint32_t no_activity_timeout_in() const;
  uint32_t initial_max_data() const;
  uint32_t initial_max_stream_data() const;
  uint32_t initial_max_stream_id() const;
  uint32_t server_id() const;

private:
  // FIXME Fill appropriate values
  uint32_t _no_activity_timeout_in  = 0;
  uint32_t _initial_max_data        = 100; // in units of 1024 octets
  uint32_t _initial_max_stream_data = 2048;
  uint32_t _initial_max_stream_id   = 100;
  uint32_t _server_id               = 0;
};

class QUICConfig
{
public:
  static void startup();
  static void reconfigure();
  static QUICConfigParams *acquire();
  static void release(QUICConfigParams *params);

  using scoped_config = ConfigProcessor::scoped_config<QUICConfig, QUICConfigParams>;

private:
  static int _config_id;
};
