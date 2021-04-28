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

#include "Http2Config.h"

////
// Http2ConfigParams
//
Http2ConfigParams::Http2ConfigParams()
{
  REC_EstablishStaticConfigInt32U(max_concurrent_streams_in, "proxy.config.http2.max_concurrent_streams_in");
  REC_EstablishStaticConfigInt32U(min_concurrent_streams_in, "proxy.config.http2.min_concurrent_streams_in");
  REC_EstablishStaticConfigInt32U(max_active_streams_in, "proxy.config.http2.max_active_streams_in");
  REC_EstablishStaticConfigInt32U(stream_priority_enabled, "proxy.config.http2.stream_priority_enabled");
  REC_EstablishStaticConfigInt32U(initial_window_size, "proxy.config.http2.initial_window_size_in");
  REC_EstablishStaticConfigInt32U(max_frame_size, "proxy.config.http2.max_frame_size");
  REC_EstablishStaticConfigInt32U(header_table_size, "proxy.config.http2.header_table_size");
  REC_EstablishStaticConfigInt32U(max_header_list_size, "proxy.config.http2.max_header_list_size");
  REC_EstablishStaticConfigInt32U(accept_no_activity_timeout, "proxy.config.http2.accept_no_activity_timeout");
  REC_EstablishStaticConfigInt32U(no_activity_timeout_in, "proxy.config.http2.no_activity_timeout_in");
  REC_EstablishStaticConfigInt32U(active_timeout_in, "proxy.config.http2.active_timeout_in");
  REC_EstablishStaticConfigInt32U(push_diary_size, "proxy.config.http2.push_diary_size");
  REC_EstablishStaticConfigInt32U(zombie_timeout_in, "proxy.config.http2.zombie_debug_timeout_in");
  REC_EstablishStaticConfigFloat(stream_error_rate_threshold, "proxy.config.http2.stream_error_rate_threshold");
  REC_EstablishStaticConfigInt32U(max_settings_per_frame, "proxy.config.http2.max_settings_per_frame");
  REC_EstablishStaticConfigInt32U(max_settings_per_minute, "proxy.config.http2.max_settings_per_minute");
  REC_EstablishStaticConfigInt32U(max_settings_frames_per_minute, "proxy.config.http2.max_settings_frames_per_minute");
  REC_EstablishStaticConfigInt32U(max_ping_frames_per_minute, "proxy.config.http2.max_ping_frames_per_minute");
  REC_EstablishStaticConfigInt32U(max_priority_frames_per_minute, "proxy.config.http2.max_priority_frames_per_minute");
  REC_EstablishStaticConfigFloat(min_avg_window_update, "proxy.config.http2.min_avg_window_update");
  REC_EstablishStaticConfigInt32U(con_slow_log_threshold, "proxy.config.http2.connection.slow.log.threshold");
  REC_EstablishStaticConfigInt32U(stream_slow_log_threshold, "proxy.config.http2.stream.slow.log.threshold");
  REC_EstablishStaticConfigInt32U(header_table_size_limit, "proxy.config.http2.header_table_size_limit");
  REC_EstablishStaticConfigInt32U(write_buffer_block_size, "proxy.config.http2.write_buffer_block_size");
  REC_EstablishStaticConfigFloat(write_size_threshold, "proxy.config.http2.write_size_threshold");
  REC_EstablishStaticConfigInt32U(write_time_threshold, "proxy.config.http2.write_time_threshold");
}

////
// Http2Config
//
void
Http2Config::startup()
{
  _config_update_handler = std::make_unique<ConfigUpdateHandler<Http2Config>>();

  // dynamic configs
  _config_update_handler->attach("proxy.config.http2.max_concurrent_streams_in");
  _config_update_handler->attach("proxy.config.http2.min_concurrent_streams_in");
  _config_update_handler->attach("proxy.config.http2.max_active_streams_in");
  _config_update_handler->attach("proxy.config.http2.stream_priority_enabled");
  _config_update_handler->attach("proxy.config.http2.initial_window_size_in");
  _config_update_handler->attach("proxy.config.http2.max_frame_size");
  _config_update_handler->attach("proxy.config.http2.header_table_size");
  _config_update_handler->attach("proxy.config.http2.max_header_list_size");
  _config_update_handler->attach("proxy.config.http2.accept_no_activity_timeout");
  _config_update_handler->attach("proxy.config.http2.no_activity_timeout_in");
  _config_update_handler->attach("proxy.config.http2.active_timeout_in");
  _config_update_handler->attach("proxy.config.http2.push_diary_size");
  _config_update_handler->attach("proxy.config.http2.zombie_debug_timeout_in");
  _config_update_handler->attach("proxy.config.http2.stream_error_rate_threshold");
  _config_update_handler->attach("proxy.config.http2.max_settings_per_frame");
  _config_update_handler->attach("proxy.config.http2.max_settings_per_minute");
  _config_update_handler->attach("proxy.config.http2.max_settings_frames_per_minute");
  _config_update_handler->attach("proxy.config.http2.max_ping_frames_per_minute");
  _config_update_handler->attach("proxy.config.http2.max_priority_frames_per_minute");
  _config_update_handler->attach("proxy.config.http2.min_avg_window_update");
  _config_update_handler->attach("proxy.config.http2.connection.slow.log.threshold");
  _config_update_handler->attach("proxy.config.http2.stream.slow.log.threshold");
  _config_update_handler->attach("proxy.config.http2.header_table_size_limit");
  _config_update_handler->attach("proxy.config.http2.write_buffer_block_size");
  _config_update_handler->attach("proxy.config.http2.write_size_threshold");
  _config_update_handler->attach("proxy.config.http2.write_time_threshold");

  reconfigure();
}

void
Http2Config::reconfigure()
{
  Http2ConfigParams *params = new Http2ConfigParams();
  _config_id                = configProcessor.set(_config_id, params);
}

Http2ConfigParams *
Http2Config::acquire()
{
  return static_cast<Http2ConfigParams *>(configProcessor.get(_config_id));
}

void
Http2Config::release(Http2ConfigParams *params)
{
  configProcessor.release(_config_id, params);
}
