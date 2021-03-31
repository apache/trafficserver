/** @file

  Configs for PreWarming Tunnel

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

#include "HttpConfig.h"

struct PreWarmConfigParams : public ConfigInfo {
  PreWarmConfigParams();

  // noncopyable
  PreWarmConfigParams(const HttpConfigParams &) = delete;
  PreWarmConfigParams &operator=(const HttpConfigParams &) = delete;

  // Config Params
  int8_t enabled         = 0;
  int8_t algorithm       = 0;
  int64_t event_period   = 0;
  int64_t max_stats_size = 0;
};

class PreWarmConfig
{
public:
  using scoped_config = ConfigProcessor::scoped_config<PreWarmConfig, PreWarmConfigParams>;

  static void startup();

  // ConfigUpdateContinuation interface
  static void reconfigure();

  // ConfigProcessor::scoped_config interface
  static PreWarmConfigParams *acquire();
  static void release(PreWarmConfigParams *params);

private:
  inline static int _config_id = 0;
  inline static std::unique_ptr<ConfigUpdateHandler<PreWarmConfig>> _config_update_handler;
};
