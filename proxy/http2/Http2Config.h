/** @file

  Configs for HTTP/2

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

#include "ProxyConfig.h"

struct Http2ConfigParams : public ConfigInfo {
  Http2ConfigParams();

  // noncopyable
  Http2ConfigParams(const Http2ConfigParams &) = delete;
  Http2ConfigParams &operator=(const Http2ConfigParams &) = delete;

  // Config Params
  uint32_t max_concurrent_streams_in      = 100;
  uint32_t min_concurrent_streams_in      = 10;
  uint32_t max_active_streams_in          = 0;
  bool throttling                         = false;
  uint32_t stream_priority_enabled        = 0;
  uint32_t initial_window_size            = 65535;
  uint32_t max_frame_size                 = 16384;
  uint32_t header_table_size              = 4096;
  uint32_t max_header_list_size           = 4294967295;
  uint32_t accept_no_activity_timeout     = 120;
  uint32_t no_activity_timeout_in         = 120;
  uint32_t active_timeout_in              = 0;
  uint32_t push_diary_size                = 256;
  uint32_t zombie_timeout_in              = 0;
  float stream_error_rate_threshold       = 0.1;
  uint32_t max_settings_per_frame         = 7;
  uint32_t max_settings_per_minute        = 14;
  uint32_t max_settings_frames_per_minute = 14;
  uint32_t max_ping_frames_per_minute     = 60;
  uint32_t max_priority_frames_per_minute = 120;
  float min_avg_window_update             = 2560.0;
  uint32_t con_slow_log_threshold         = 0;
  uint32_t stream_slow_log_threshold      = 0;
  uint32_t header_table_size_limit        = 65536;
  uint32_t write_buffer_block_size        = 262144;
  float write_size_threshold              = 0.5;
  uint32_t write_time_threshold           = 100;
};

class Http2Config
{
public:
  using scoped_config = ConfigProcessor::scoped_config<Http2Config, Http2ConfigParams>;

  static void startup();

  // ConfigUpdateContinuation interface
  static void reconfigure();

  // ConfigProcessor::scoped_config interface
  static Http2ConfigParams *acquire();
  static void release(Http2ConfigParams *params);

private:
  inline static int _config_id = 0;
  inline static std::unique_ptr<ConfigUpdateHandler<Http2Config>> _config_update_handler;
};
