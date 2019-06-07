/** @file

  A brief file description

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

#include "tscore/ink_config.h"
#include "RecordsConfig.h"

#if TS_USE_REMOTE_UNWINDING
#define MGMT_CRASHLOG_HELPER "traffic_crashlog"
#else
#define MGMT_CRASHLOG_HELPER NULL
#endif

//-------------------------------------------------------------------------
// RecordsConfig
//-------------------------------------------------------------------------

// clang-format off
static const RecordElement RecordsConfig[] =
{
  //##############################################################################
  //#
  //# records.config items
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.product_company", RECD_STRING, "Apache Software Foundation", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.product_vendor", RECD_STRING, "Apache", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.product_name", RECD_STRING, "Traffic Server", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.proxy_name", RECD_STRING, BUILD_MACHINE, RECU_DYNAMIC, RR_REQUIRED, RECC_STR, ".+", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.bin_path", RECD_STRING, TS_BUILD_BINDIR, RECU_NULL, RR_REQUIRED, RECC_NULL, nullptr, RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.proxy_binary", RECD_STRING, "traffic_server", RECU_NULL, RR_REQUIRED, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.manager_binary", RECD_STRING, "traffic_manager", RECU_NULL, RR_REQUIRED, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.proxy_binary_opts", RECD_STRING, "-M", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.env_prep", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  // Jira TS-21
  {RECT_CONFIG, "proxy.config.local_state_dir", RECD_STRING, TS_BUILD_RUNTIMEDIR, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.syslog_facility", RECD_STRING, "LOG_DAEMON", RECU_RESTART_TM, RR_NULL, RECC_STR, ".*", RECA_NULL}
  ,
  //# Negative core limit means max out limit
  {RECT_CONFIG, "proxy.config.core_limit", RECD_INT, "-1", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.crash_log_helper", RECD_STRING, MGMT_CRASHLOG_HELPER, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  // 0 - Disabled, 1 - enabled for important pages (e.g. cache directory), 2 - enabled for all pages
  {RECT_CONFIG, "proxy.config.mlock_enabled", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  //# 0 = disable (seconds)
  {RECT_CONFIG, "proxy.config.dump_mem_info_frequency", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //# 0 = disable
  {RECT_CONFIG, "proxy.config.http_ui_enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-3]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.max_disk_errors", RECD_INT, "5", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.output.logfile", RECD_STRING, "traffic.out", RECU_RESTART_TM, RR_REQUIRED, RECC_NULL, nullptr,
   RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.output.logfile_perm", RECD_STRING, "rw-r--r--", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  // traffic.out rotation, default is 0 (aka rolling turned off) to preserve compatibility
  {RECT_CONFIG, "proxy.config.output.logfile.rolling_enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.output.logfile.rolling_interval_sec", RECD_INT, "3600", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.output.logfile.rolling_size_mb", RECD_INT, "100", RECU_DYNAMIC, RR_NULL, RECC_STR, "^0*[1-9][0-9]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.output.logfile.rolling_min_count", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^0*[1-9][0-9]*$", RECA_NULL}
  ,
  //# 0 = disable
  {RECT_CONFIG, "proxy.config.res_track_memory", RECD_INT, "0", RECU_RESTART_TS, RR_REQUIRED, RECC_INT,  "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.memory.max_usage", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_STR, "^-?[0-9]+$", RECA_NULL}
  ,
  //##############################################################################
  //# Traffic Server system settings
  //##############################################################################
  // The percent of the /proc/sys/fs/file-max value to set the RLIMIT_NOFILE cur/max to
  {RECT_CONFIG, "proxy.config.system.file_max_pct", RECD_FLOAT, "0.9", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_READ_ONLY}
  ,
  // Traffic Server Execution threads configuration
  // By default Traffic Server set number of execution threads equal to total CPUs
  {RECT_CONFIG, "proxy.config.exec_thread.autoconfig", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.exec_thread.autoconfig.scale", RECD_FLOAT, "1.5", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.exec_thread.limit", RECD_INT, "2", RECU_RESTART_TS, RR_NULL, RECC_INT, "[1-" TS_STR(TS_MAX_NUMBER_EVENT_THREADS) "]", RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.exec_thread.affinity", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-4]", RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.accept_threads", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-" TS_STR(TS_MAX_NUMBER_EVENT_THREADS) "]", RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.task_threads", RECD_INT, "2", RECU_RESTART_TS, RR_NULL, RECC_INT, "[1-" TS_STR(TS_MAX_NUMBER_EVENT_THREADS) "]", RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.thread.default.stacksize", RECD_INT, "1048576", RECU_RESTART_TS, RR_NULL, RECC_INT, "[131072-104857600]", RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.restart.active_client_threshold", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.restart.stop_listening", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.stop.shutdown_timeout", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.thread.max_heartbeat_mseconds", RECD_INT, "60", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1000]", RECA_READ_ONLY}
  ,

  //##############################################################################
  //#
  //# Support for SRV records
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.srv_enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Support for disabling check for Accept-* / Content-* header mismatch
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.http.cache.ignore_accept_mismatch", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ignore_accept_language_mismatch", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ignore_accept_encoding_mismatch", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ignore_accept_charset_mismatch", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  //
  // Websocket configs
  //
  {RECT_CONFIG, "proxy.config.http.websocket.max_number_of_connections", RECD_INT, "-1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //##############################################################################
  //#
  //# Redirection
  //#
  //# 1. number_of_redirections: The maximum number of redirections TS permits. Disabled if set to 0 (default)
  //# 2. proxy.config.http.redirect_use_orig_cache_key: Location Header if set to 0 (default), else use original request cache key
  //# 3. redirection_host_no_port: do not include default port in host header during redirection
  //# 4. post_copy_size: The maximum POST data size TS permits to copy
  //# 5. redirect.actions: How to handle redirects.
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.http.number_of_redirections", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.redirect_use_orig_cache_key", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.redirect_host_no_port", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.post_copy_size", RECD_INT, "2048", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.redirect.actions", RECD_STRING, "routable:follow", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Diagnostics
  //#
  //# Enable by setting proxy.config.diags.debug.enabled to 1
  //# Route each type of diagnostic with a string, each character representing:
  //#    O  stdout
  //#    E  stderr
  //#    S  syslog
  //#    L  diags.log
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.diags.debug.enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.debug.tags", RECD_STRING, "http|dns", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.debug.client_ip", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.action.enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.action.tags", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.show_location", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.diag", RECD_STRING, "E", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.debug", RECD_STRING, "E", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.status", RECD_STRING, "L", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.note", RECD_STRING, "L", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.warning", RECD_STRING, "L", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.error", RECD_STRING, "L", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.fatal", RECD_STRING, "L", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.alert", RECD_STRING, "L", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.output.emergency", RECD_STRING, "L", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.logfile_perm", RECD_STRING, "rw-r--r--", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  // diags.log rotation, default is 0 (aka rolling turned off) to preserve compatibility
  {RECT_CONFIG, "proxy.config.diags.logfile.rolling_enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.logfile.rolling_interval_sec", RECD_INT, "3600", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.logfile.rolling_size_mb", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_STR, "^0*[1-9][0-9]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.diags.logfile.rolling_min_count", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^0*[1-9][0-9]*$", RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Local Manager
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.lm.pserver_timeout_secs", RECD_INT, "1", RECU_RESTART_TM, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.lm.pserver_timeout_msecs", RECD_INT, "0", RECU_RESTART_TM, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.admin.autoconf.localhost_only", RECD_INT, "1", RECU_RESTART_TM, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.admin.number_config_bak", RECD_INT, "3", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.admin.user_id", RECD_STRING, TS_PKGSYSUSER, RECU_NULL, RR_REQUIRED, RECC_NULL, nullptr, RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.admin.cli_path", RECD_STRING, "cli", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.admin.api.restricted", RECD_INT, "0", RECU_RESTART_TM, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,

  //##############################################################################
  //#
  //# UDP configuration stuff: hidden variables
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.udp.free_cancelled_pkts_sec", RECD_INT, "10", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.udp.periodic_cleanup", RECD_INT, "10", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.udp.send_retries", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.udp.threads", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Process Manager
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.process_manager.timeout", RECD_INT, "5", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Alarm Configuration
  //#
  //##############################################################################
  //        #################################################################
  //        # execute alarm as "<abs_path>/<bin> "<MSG_STRING_FROM_PROXY>"" #
  //        #################################################################
  {RECT_CONFIG, "proxy.config.alarm.bin", RECD_STRING, "example_alarm_bin.sh", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.alarm.abs_path", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.alarm.script_runtime", RECD_INT, "5", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-300]", RECA_NULL}
  ,

  //        ###########
  //        # Parsing #
  //        ###########
  {RECT_CONFIG, "proxy.config.header.parse.no_host_url_redirect", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_STR, ".*", RECA_NULL}
  ,

  //##############################################################################
  //#
  //# HTTP Engine
  //#
  //##############################################################################
  //        ##########
  //        # basics #
  //        ##########
  //       #
  {RECT_CONFIG, "proxy.config.http.allow_half_open", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.enabled", RECD_INT, "1", RECU_RESTART_TM, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.server_ports", RECD_STRING, "8080 8080:ipv6", RECU_RESTART_TM, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.wait_for_cache", RECD_INT, "0", RECU_RESTART_TM, RR_NULL, RECC_INT, "[0-3]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.insert_request_via_str", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-4]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.insert_response_via_str", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-4]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.request_via_str", RECD_STRING, "ApacheTrafficServer/" PACKAGE_VERSION, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.response_via_str", RECD_STRING, "ApacheTrafficServer/" PACKAGE_VERSION, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.response_server_enabled", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.response_server_str", RECD_STRING, "ATS/" PACKAGE_VERSION, RECU_DYNAMIC, RR_NULL, RECC_NULL, ".*", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.no_dns_just_forward_to_parent", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.uncacheable_requests_bypass_parent", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.no_origin_server_dns", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.use_client_target_addr", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.use_client_source_port", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.keep_alive_enabled_in", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.keep_alive_enabled_out", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.keep_alive_post_out", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.chunking_enabled", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.chunking.size", RECD_INT, "4096", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.flow_control.enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.flow_control.high_water", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.flow_control.low_water", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.post.check.content_length.enabled", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.strict_uri_parsing", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  //       # Send http11 requests
  //       #
  //       #   0 - Never
  //       #   1 - Always
  //       #   2 - if the server has returned http1.1 before
  //       #   3 - if the client request is 1.1 & the server
  //       #         has returned 1.1 before
  //       #
  {RECT_CONFIG, "proxy.config.http.send_http11_requests", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.send_100_continue_response", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.disallow_post_100_continue", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.server_session_sharing.match", RECD_STRING, "both", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.server_session_sharing.pool", RECD_STRING, "thread", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.default_buffer_size", RECD_INT, "8", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.default_buffer_water_mark", RECD_INT, "32768", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.enable_http_info", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.server_max_connections", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.per_server.connection.max", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.per_server.connection.match", RECD_STRING, "ip", RECU_DYNAMIC, RR_NULL, RECC_STR, "^(?:ip|host|both|none)$", RECA_NULL}
        ,
  {RECT_CONFIG, "proxy.config.http.per_server.connection.alert_delay", RECD_INT, "60", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
        ,
  {RECT_CONFIG, "proxy.config.http.per_server.connection.queue_size", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^-?[0-9]+$", RECA_NULL}
    ,
  {RECT_CONFIG, "proxy.config.http.per_server.connection.queue_delay", RECD_INT, "100", RECU_DYNAMIC, RR_NULL, RECC_STR, "^-?[0-9]+$", RECA_NULL}
        ,
  {RECT_CONFIG, "proxy.config.http.per_server.min_keep_alive", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.attach_server_session_to_client", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.max_connections_in", RECD_INT, "30000", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.max_connections_active_in", RECD_INT, "10000", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,

  //       ###########################
  //       # HTTP referrer filtering #
  //       ###########################
  {RECT_CONFIG, "proxy.config.http.referer_filter", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.referer_format_redirect", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.referer_default_redirect", RECD_STRING, "http://www.example.com/", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.auth_server_session_private", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.max_post_size", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "^[0-9]+$", RECA_NULL}
  ,
  //        ##############################
  //        # parent proxy configuration #
  //        ##############################
  {RECT_CONFIG, "proxy.config.http.parent_proxies", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_STR, ".*", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.parent_proxy.file", RECD_STRING, "parent.config", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.parent_proxy.retry_time", RECD_INT, "300", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //# Parent fail threshold is the number of request that must fail within
  //#  the retry window for the parent to be marked down
  {RECT_CONFIG, "proxy.config.http.parent_proxy.fail_threshold", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.parent_proxy.total_connect_attempts", RECD_INT, "4", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.parent_proxy.per_parent_connect_attempts", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.parent_proxy.connect_attempts_timeout", RECD_INT, "30", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.parent_proxy.mark_down_hostdb", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.parent_proxy.self_detect", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.forward.proxy_auth_to_parent", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //        ###################################
  //        # NO DNS DOC IN CACHE             #
  //        ###################################
  {RECT_CONFIG, "proxy.config.http.doc_in_cache_skip_dns", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,

  //        ###################################
  //        # HTTP connection timeouts (secs) #
  //        ###################################
  //       #
  //       # out: proxy -> os connection
  //       # in : ua -> proxy connection
  //       #
  {RECT_CONFIG, "proxy.config.http.keep_alive_no_activity_timeout_in", RECD_INT, "120", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.keep_alive_no_activity_timeout_out", RECD_INT, "120", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.websocket.no_activity_timeout", RECD_INT, "600", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.websocket.active_timeout", RECD_INT, "3600", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.transaction_no_activity_timeout_in", RECD_INT, "30", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.transaction_no_activity_timeout_out", RECD_INT, "30", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.transaction_active_timeout_in", RECD_INT, "900", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.transaction_active_timeout_out", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.accept_no_activity_timeout", RECD_INT, "120", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.background_fill_active_timeout", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.background_fill_completed_threshold", RECD_FLOAT, "0.0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //        ##################################
  //        # origin server connect attempts #
  //        ##################################
  {RECT_CONFIG, "proxy.config.http.connect_attempts_max_retries", RECD_INT, "3", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.connect_attempts_max_retries_dead_server", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.connect_attempts_rr_retries", RECD_INT, "3", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.connect_attempts_timeout", RECD_INT, "30", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.post_connect_attempts_timeout", RECD_INT, "1800", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.down_server.cache_time", RECD_INT, "60", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.down_server.abort_threshold", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.negative_revalidating_enabled", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.negative_revalidating_lifetime", RECD_INT, "1800", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.negative_caching_enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.negative_caching_lifetime", RECD_INT, "1800", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.negative_caching_list", RECD_STRING, "204 305 403 404 405 414 500 501 502 503 504", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //        #########################
  //        # proxy users variables #
  //        #########################
  {RECT_CONFIG, "proxy.config.http.anonymize_remove_from", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.anonymize_remove_referer", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.anonymize_remove_user_agent", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.anonymize_remove_cookie", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.anonymize_remove_client_ip", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.insert_client_ip", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.anonymize_other_header_list", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_STR, ".*", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.insert_squid_x_forwarded_for", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.insert_forwarded", RECD_STRING, "none", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.proxy_protocol_whitelist", RECD_STRING, "none", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.insert_age_in_response", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.enable_http_stats", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.allow_multi_range", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  // This defaults to a special invalid value so the HTTP transaction handling code can tell that it was not explicitly set.
  {RECT_CONFIG, "proxy.config.http.normalize_ae", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,

  //        ####################################################
  //        # Global User-Agent header                         #
  //        ####################################################
  {RECT_CONFIG, "proxy.config.http.global_user_agent_header", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_STR, ".*", RECA_NULL}
  ,

  //        ############
  //        # security #
  //        ############
  {RECT_CONFIG, "proxy.config.http.request_header_max_size", RECD_INT, "131072", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.response_header_max_size", RECD_INT, "131072", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.push_method_enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,

  //        #################
  //        # cache control #
  //        #################
  {RECT_CONFIG, "proxy.config.http.cache.http", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.generation", RECD_INT, "-1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  // Enabling this setting allows the proxy to cache empty documents. This currently requires
  // that the response has a Content-Length: header, with a value of "0".
  {RECT_CONFIG, "proxy.config.http.cache.allow_empty_doc", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL }
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ignore_client_no_cache", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ignore_client_cc_max_age", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ims_on_client_no_cache", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ignore_server_no_cache", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       # cache responses to cookies has 4 options
  //       #
  //       #  0 - do not cache any responses to cookies
  //       #  1 - cache for any content-type (ignore cookies)
  //       #  2 - cache only for image types
  //       #  3 - cache for all but text content-types
  //       #  4 - cache for all but text content-types except OS response without "Set-Cookie" or with "Cache-Control: public"
  {RECT_CONFIG, "proxy.config.http.cache.cache_responses_to_cookies", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-4]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.ignore_authentication", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.cache_urls_that_look_dynamic", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.post_method", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.max_open_read_retries", RECD_INT, "-1", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.open_read_retry_time", RECD_INT, "10", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.max_open_write_retries", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       #  open_write_fail_action has 3 options:
  //       #
  //       #  0 - default. disable cache and goto origin
  //       #  1 - return error if cache miss
  //       #  2 - serve stale until proxy.config.http.cache.max_stale_age, then goto origin, if revalidate
  //       #  3 - return error if cache miss or serve stale until proxy.config.http.cache.max_stale_age, then goto origin, if revalidate
  //       #  4 - return error if cache miss or if revalidate
  {RECT_CONFIG, "proxy.config.http.cache.open_write_fail_action", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       #  when_to_revalidate has 4 options:
  //       #
  //       #  0 - default. use use cache directives or heuristic
  //       #  1 - stale if heuristic
  //       #  2 - always stale (always revalidate)
  //       #  3 - never stale
  //       #
  {RECT_CONFIG, "proxy.config.http.cache.when_to_revalidate", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //
  //       #  required headers: three options
  //       #
  //       #  0 - No required headers to make document cachable
  //       #  1 - at least, "Last-Modified:" header required
  //       #  2 - explicit lifetime required, "Expires:" or "Cache-Control:"
  //       #
  {RECT_CONFIG, "proxy.config.http.cache.required_headers", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.max_stale_age", RECD_INT, "604800", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.range.lookup", RECD_INT, "1", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.range.write", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //        ########################
  //        # heuristic expiration #
  //        ########################
  {RECT_CONFIG, "proxy.config.http.cache.heuristic_min_lifetime", RECD_INT, "3600", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.heuristic_max_lifetime", RECD_INT, "86400", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.heuristic_lm_factor", RECD_FLOAT, "0.10", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.guaranteed_min_lifetime", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.cache.guaranteed_max_lifetime", RECD_INT, "31536000", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //        ###################
  //        # Error Reporting #
  //        ###################
  {RECT_CONFIG, "proxy.config.http.errors.log_error_pages", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http.slow.log.threshold", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Customizable User Response Pages
  //#
  //##############################################################################
  //# 1 - enable customizable user response pages in only the "default" directory
  //# 2 - enable language-targeted user response pages
  //# 3 - enable host-targeted user response pages
  {RECT_CONFIG, "proxy.config.body_factory.enable_customizations", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT,
    "[1-3]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.body_factory.enable_logging", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.body_factory.template_sets_dir", RECD_STRING, "body_factory", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[^[:space:]]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.body_factory.response_max_size", RECD_INT, "8192", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //# 0 - never suppress generated responses
  //# 1 - always suppress generated responses
  //# 2 - suppress responses for intercepted traffic
  {RECT_CONFIG, "proxy.config.body_factory.response_suppression_mode", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.body_factory.template_base", RECD_STRING, "NONE", RECU_DYNAMIC, RR_NULL, RECC_STR, ".*", RECA_NULL}
  ,
  //##############################################################################
  //#
  //# SOCKS Processor
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.socks.socks_needed", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.socks_version", RECD_INT, "4", RECU_RESTART_TS, RR_NULL, RECC_INT, "[4-5]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.socks_config_file", RECD_STRING, "socks.config", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.socks_timeout", RECD_INT, "100", RECU_RESTART_TS, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.server_connect_timeout", RECD_INT, "10", RECU_RESTART_TS, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.per_server_connection_attempts", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.connection_attempts", RECD_INT, "4", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.server_retry_timeout", RECD_INT, "300", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.default_servers", RECD_STRING, "", RECU_RESTART_TS, RR_NULL, RECC_STR, "^([^[:space:]]+:[0-9]+;?)*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.server_retry_time", RECD_INT, "300", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.server_fail_threshold", RECD_INT, "2", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.accept_enabled", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.accept_port", RECD_INT, "1080", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-65535]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.socks.http_port", RECD_INT, "80", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# I/O Subsystem
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.io.max_buffer_size", RECD_INT, "32768", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Net Subsystem
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.net.connections_throttle", RECD_INT, "30000", RECU_RESTART_TS, RR_REQUIRED, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.listen_backlog", RECD_INT, "-1", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  // This option takes different defaults depending on features / platform. TODO: This should use the
  // autoconf stuff probably ?
  {RECT_CONFIG, "proxy.config.net.defer_accept", RECD_INT,
#ifdef TCP_DEFER_ACCEPT
   "45",
#else
   "1",
#endif
   RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-65535]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_recv_buffer_size_in", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_send_buffer_size_in", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_option_flag_in", RECD_INT, "0x5", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_packet_mark_in", RECD_INT, "0x0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_packet_tos_in", RECD_INT, "0x0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_recv_buffer_size_out", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_send_buffer_size_out", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_option_flag_out", RECD_INT, "0x1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_packet_mark_out", RECD_INT, "0x0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_packet_tos_out", RECD_INT, "0x0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_mss_in", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.poll_timeout", RECD_INT, "10", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.default_inactivity_timeout", RECD_INT, "86400", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.inactivity_check_frequency", RECD_INT, "1", RECU_RESTART_TM, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.event_period", RECD_INT, "10", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.accept_period", RECD_INT, "10", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.retry_delay", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.throttle_delay", RECD_INT, "50", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.sock_option_tfo_queue_size_in", RECD_INT, "10000", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.tcp_congestion_control_in", RECD_STRING, "", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.net.tcp_congestion_control_out", RECD_STRING, "", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Hit Evacuation
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.cache.hit_evacuate_percent", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.hit_evacuate_size_limit", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //##############################################################################
  //#
  //# Cache
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.cache.storage_filename", RECD_STRING, "storage.config", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.control.filename", RECD_STRING, "cache.config", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.ip_allow.filename", RECD_STRING, "ip_allow.config", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.hosting_filename", RECD_STRING, "hosting.config", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.volume_filename", RECD_STRING, "volume.config", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.permit.pinning", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  //  # default the ram cache size to AUTO_SIZE (-1)
  //  # alternatively: 20971520 (20MB)
  {RECT_CONFIG, "proxy.config.cache.ram_cache.size", RECD_INT, "-1", RECU_RESTART_TS, RR_NULL, RECC_STR, "^-?[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.ram_cache.algorithm", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.ram_cache.use_seen_filter", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.ram_cache.compress", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-3]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.ram_cache.compress_percent", RECD_INT, "90", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //  # how often should the directory be synced (seconds)
  {RECT_CONFIG, "proxy.config.cache.dir.sync_frequency", RECD_INT, "60", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.hostdb.disable_reverse_lookup", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.select_alternate", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.ram_cache_cutoff", RECD_INT, "4194304", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //  # The maximum number of alternates that are allowed for any given URL.
  //  # (0 disables the maximum number of alts check)
  {RECT_CONFIG, "proxy.config.cache.limits.http.max_alts", RECD_INT, "5", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.force_sector_size", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.target_fragment_size", RECD_INT, "1048576", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //  # The maximum size of a document that will be stored in the cache.
  //  # (0 disables the maximum document size check)
  {RECT_CONFIG, "proxy.config.cache.max_doc_size", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.min_average_object_size", RECD_INT, "8000", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.threads_per_disk", RECD_INT, "8", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.agg_write_backlog", RECD_INT, "5242880", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.enable_checksum", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.alt_rewrite_max_size", RECD_INT, "4096", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.enable_read_while_writer", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.mutex_retry_delay", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.read_while_writer.max_retries", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.cache.read_while_writer_retry.delay", RECD_INT, "50", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# DNS
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.dns.lookup_timeout", RECD_INT, "20", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.retries", RECD_INT, "5", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-9]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.search_default_domains", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.failover_number", RECD_INT, "5", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.failover_period", RECD_INT, "60", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.max_dns_in_flight", RECD_INT, "2048", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.validate_query_name", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.splitDNS.enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.splitdns.filename", RECD_STRING, "splitdns.config", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.nameservers", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.local_ipv6", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.local_ipv4", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.resolv_conf", RECD_STRING, "/etc/resolv.conf", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.round_robin_nameservers", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.dedicated_thread", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.dns.connection_mode", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.ip_resolve", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# HostDB
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.hostdb", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL}
  ,
  //       # up to 511 characters, may not be changed while running
  {RECT_CONFIG, "proxy.config.hostdb.filename", RECD_STRING, "host.db", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       # in entries, may not be changed while running
  {RECT_CONFIG, "proxy.config.hostdb.max_count", RECD_INT, "-1", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.round_robin_max_count", RECD_INT, "16", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.storage_path", RECD_STRING, TS_BUILD_CACHEDIR, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.max_size", RECD_INT, "10M", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.partitions", RECD_INT, "64", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       # in minutes (all three)
  //       #  0 = obey, 1 = ignore, 2 = min(X,ttl), 3 = max(X,ttl)
  {RECT_CONFIG, "proxy.config.hostdb.ttl_mode", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-3]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.lookup_timeout", RECD_INT, "30", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.timeout", RECD_INT, "86400", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.verify_after", RECD_INT, "720", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.fail.timeout", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.re_dns_on_reload", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.serve_stale_for", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       # move entries to the owner on a lookup?
  {RECT_CONFIG, "proxy.config.hostdb.migrate_on_demand", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       # round-robin addresses for single clients
  //       # (can cause authentication problems)
  {RECT_CONFIG, "proxy.config.hostdb.strict_round_robin", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.timed_round_robin", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //       # how often should the hostdb be synced (seconds)
  {RECT_CONFIG, "proxy.config.cache.hostdb.sync_frequency", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.host_file.path", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.hostdb.host_file.interval", RECD_INT, "86400", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //##########################################################################
  //#
  //# HTTP
  //#
  //##########################################################################
  //##########################################################################
  //        ###########
  //        # CONNECT #
  //        ###########
  {RECT_CONFIG, "proxy.config.http.connect_ports", RECD_STRING, "443", RECU_DYNAMIC, RR_NULL, RECC_STR, "^(\\*|[[:digit:][:space:]]+)$", RECA_NULL}
  ,
  //        ##########################
  //        # Various update periods #
  //        ##########################
  // Periods of update threads
  {RECT_CONFIG, "proxy.config.config_update_interval_ms", RECD_INT, "3000", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.raw_stat_sync_interval_ms", RECD_INT, "5000", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.remote_sync_interval_ms", RECD_INT, "5000", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //        ###########
  //        # Parsing #
  //        ###########
  //##############################################################################
  //#
  //# New Logging Config
  //#
  //##############################################################################
  //# possible values for logging_enabled
  //# 0: no logging at all
  //# 1: log errors only
  //# 2: full logging
  {RECT_CONFIG, "proxy.config.log.logging_enabled", RECD_INT, "3", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-4]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.log_buffer_size", RECD_INT, "9216", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.max_secs_per_buffer", RECD_INT, "5", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.max_space_mb_for_logs", RECD_INT, "25000", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.max_space_mb_headroom", RECD_INT, "1000", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.hostname", RECD_STRING, "localhost", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.logfile_dir", RECD_STRING, TS_BUILD_LOGDIR, RECU_DYNAMIC, RR_NULL, RECC_STR, "^[^[:space:]]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.logfile_perm", RECD_STRING, "rw-r--r--", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.config.filename", RECD_STRING, "logging.yaml", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.preproc_threads", RECD_INT, "1", RECU_DYNAMIC, RR_REQUIRED, RECC_INT, "[1-128]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.rolling_enabled", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-4]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.rolling_interval_sec", RECD_INT, "86400", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.rolling_offset_hr", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.rolling_size_mb", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_STR, "^0*[1-9][0-9]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.rolling_min_count", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^0*[1-9][0-9]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.auto_delete_rolled_files", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.sampling_frequency", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.space_used_frequency", RECD_INT, "2", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.file_stat_frequency", RECD_INT, "32", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.ascii_buffer_size", RECD_INT, "36864", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.log.max_line_size", RECD_INT, "9216", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  // How often periodic tasks get executed in the Log.cc infrastructure
  {RECT_CONFIG, "proxy.config.log.periodic_tasks_interval", RECD_INT, "5", RECU_DYNAMIC, RR_NULL, RECC_NULL, "^[0-9]+$", RECA_NULL}
  ,

  //##############################################################################
  //#
  //# Reverse Proxy
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.reverse_proxy.enabled", RECD_INT, "1", RECU_DYNAMIC, RR_REQUIRED, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.url_remap.filename", RECD_STRING, "remap.config", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.url_remap.remap_required", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.url_remap.pristine_host_hdr", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,

  //##############################################################################
  //#
  //# SSL Termination
  //#
  //##############################################################################
  {RECT_CONFIG, "proxy.config.ssl.server.session_ticket.enable", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.TLSv1", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.TLSv1_1", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  // Disable this when using some versions of OpenSSL that causes crashes. See TS-2355.
  {RECT_CONFIG, "proxy.config.ssl.TLSv1_2", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.TLSv1_3", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.TLSv1", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.TLSv1_1", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.TLSv1_2", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.TLSv1_3", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.cipher_suite", RECD_STRING, "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.cipher_suite", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.honor_cipher_order", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.certification_level", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.cert.path", RECD_STRING, TS_BUILD_SYSCONFDIR, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.cert_chain.filename", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.multicert.filename", RECD_STRING, "ssl_multicert.config", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.multicert.exit_on_load_fail", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL}
,
  {RECT_CONFIG, "proxy.config.ssl.servername.filename", RECD_STRING, "sni.yaml", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.ticket_key.filename", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.private_key.path", RECD_STRING, TS_BUILD_SYSCONFDIR, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.CA.cert.filename", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_STR, "^[^[:space:]]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.CA.cert.path", RECD_STRING, TS_BUILD_SYSCONFDIR, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.verify.server", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-2]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.verify.server.policy", RECD_STRING, "DISABLED", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.verify.server.properties", RECD_STRING, "ALL", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.cert.filename", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_STR, "^[^[:space:]]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.cert.path", RECD_STRING, TS_BUILD_SYSCONFDIR, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.private_key.filename", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_STR, "^[^[:space:]]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.private_key.path", RECD_STRING, TS_BUILD_SYSCONFDIR, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.CA.cert.filename", RECD_STRING, nullptr, RECU_DYNAMIC, RR_NULL, RECC_STR, "^[^[:space:]]*$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.CA.cert.path", RECD_STRING, TS_BUILD_SYSCONFDIR, RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.sni_policy", RECD_STRING, "host", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.session_cache", RECD_INT, "2", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.session_cache.size", RECD_INT, "102400", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.session_cache.num_buckets", RECD_INT, "256", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.session_cache.skip_cache_on_bucket_contention", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.max_record_size", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, "[0-16383]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.session_cache.timeout", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.session_cache.auto_clear", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.hsts_max_age", RECD_INT, "-1", RECU_DYNAMIC, RR_NULL, RECC_STR, "^-?[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.hsts_include_subdomains", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.allow_client_renegotiation", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.dhparams_file", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.handshake_timeout_in", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-65535]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.cert.load_elevated", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.ssl.server.groups_list", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.groups_list", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //##############################################################################
  //#
  //# OCSP (Online Certificate Status Protocol) Stapling Configuration
  //#
  //##############################################################################
  //        # Enable OCSP stapling. Disabled by default.
  {RECT_CONFIG, "proxy.config.ssl.ocsp.enabled", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  //        # Number of seconds before an OCSP response expires in the stapling cache. 3600s (1 hour) by default.
  {RECT_CONFIG, "proxy.config.ssl.ocsp.cache_timeout", RECD_INT, "3600", RECU_DYNAMIC, RR_NULL, RECC_NULL, "^[0-9]+$", RECA_NULL}
  ,
  //        # Timeout for queries to OCSP responders. 10s by default.
  {RECT_CONFIG, "proxy.config.ssl.ocsp.request_timeout", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_NULL, "^[0-9]+$", RECA_NULL}
  ,
  //        # Update period for stapling caches. 60s (1 min) by default.
  {RECT_CONFIG, "proxy.config.ssl.ocsp.update_period", RECD_INT, "60", RECU_DYNAMIC, RR_NULL, RECC_NULL, "^[0-9]+$", RECA_NULL}
  ,
  //        # Base path for OCSP prefetched responses
  {RECT_CONFIG, "proxy.config.ssl.ocsp.response.path", RECD_STRING, TS_BUILD_SYSCONFDIR, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //##############################################################################
  //#
  //# Configuration for TLSv1.3 and above
  //#
  //##############################################################################
  // The default value (nullptr) means the default value of TLS stack will be used.
  // - e.g. OpenSSL : "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256"
  {RECT_CONFIG, "proxy.config.ssl.server.TLSv1_3.cipher_suites", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.ssl.client.TLSv1_3.cipher_suites", RECD_STRING, nullptr, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //############################################################################
  //#
  //# WCCP
  //#
  //############################################################################
  {RECT_LOCAL, "proxy.config.wccp.addr", RECD_STRING, "", RECU_RESTART_TM, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.wccp.services", RECD_STRING, "", RECU_RESTART_TM, RR_NULL, RECC_NULL, nullptr, RECA_NULL }
  ,

  //##############################################################################
  //# Plug-in Configuration
  //##############################################################################
  //# Directory in which to find plugins
  {RECT_CONFIG, "proxy.config.plugin.plugin_dir", RECD_STRING, TS_BUILD_LIBEXECDIR, RECU_RESTART_TS, RR_NULL, RECC_NULL, nullptr, RECA_READ_ONLY}
  ,
  {RECT_CONFIG, "proxy.config.plugin.load_elevated", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_INT, "[0-1]", RECA_READ_ONLY}
  ,

  // Interim configuration setting for obeying keepalive requests on internal
  // (PluginVC) sessions. See TS-4960 and friends.
  {RECT_LOCAL, "proxy.config.http.keepalive_internal_vc", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL},

  //##############################################################################
  //#
  //# Local Manager Specific Records File
  //#
  //# <RECORD-TYPE> <NAME> <TYPE> <VALUE (till end of line)>
  //#
  //# *NOTE*: All NODE Records must be placed continuously!
  //#
  //# Add NODE       Records Here
  //##############################################################################
  {RECT_NODE, "proxy.node.hostname_FQ", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.hostname", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //#
  //# Restart Stats
  //#
  {RECT_NODE, "proxy.node.restarts.manager.start_time", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.restarts.proxy.start_time", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.restarts.proxy.cache_ready_time", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.restarts.proxy.stop_time", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.restarts.proxy.restart_count", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  //#
  //# Manager Version Info
  //#
  {RECT_NODE, "proxy.node.version.manager.short", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.version.manager.long", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.version.manager.build_number", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.version.manager.build_time", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.version.manager.build_date", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.version.manager.build_machine", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_NODE, "proxy.node.version.manager.build_person", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //#
  //# SSL parent proxying info
  //#
  //# Set the value of this variable to 1 if this node
  //#  is also the default parent for all ssl requests
  //#  in a cluster. Setting the value to 1 will prevent
  //#  SSL requests from this proxy to a parent from
  //#  self-looping.
  //#
  {RECT_LOCAL, "proxy.local.http.parent_proxy.disable_connect_tunneling", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  {RECT_CONFIG, "proxy.config.http.forward_connect_method", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,

  //############
  //#
  //# HTTP/2 global configuration.
  //#
  //############
  {RECT_CONFIG, "proxy.config.http2.stream_priority_enabled", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_INT, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.max_concurrent_streams_in", RECD_INT, "100", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.min_concurrent_streams_in", RECD_INT, "10", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.max_active_streams_in", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.initial_window_size_in", RECD_INT, "1048576", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.max_frame_size", RECD_INT, "16384", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.header_table_size", RECD_INT, "4096", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.max_header_list_size", RECD_INT, "131072", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.accept_no_activity_timeout", RECD_INT, "120", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.no_activity_timeout_in", RECD_INT, "120", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.active_timeout_in", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.push_diary_size", RECD_INT, "256", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.zombie_debug_timeout_in", RECD_INT, "0", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.stream_error_rate_threshold", RECD_FLOAT, "0.1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.max_settings_per_frame", RECD_INT, "7", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.http2.max_settings_per_minute", RECD_INT, "14", RECU_DYNAMIC, RR_NULL, RECC_STR, "^[0-9]+$", RECA_NULL}
  ,

  //# Add LOCAL Records Here
  {RECT_LOCAL, "proxy.local.incoming_ip_to_bind", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_LOCAL, "proxy.local.outgoing_ip_to_bind", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //# Librecords based stats system (new as of v2.1.3)
  {RECT_CONFIG, "proxy.config.stat_api.max_stats_allowed", RECD_INT, "256", RECU_RESTART_TS, RR_NULL, RECC_INT, "[256-1000]", RECA_NULL}
  ,

  //############
  //#
  //# Per-thread freelist / allocator controls
  //#
  //############
  /* this should be renamed in 6.0 */
  {RECT_CONFIG, "proxy.config.allocator.thread_freelist_size", RECD_INT, "512", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.allocator.thread_freelist_low_watermark", RECD_INT, "32", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.allocator.hugepages", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL}
  ,
  {RECT_CONFIG, "proxy.config.allocator.dontdump_iobuffers", RECD_INT, "1", RECU_RESTART_TS, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL}
  ,

  //############
  //#
  //# Eric's super cool remap processor
  //#
  //############
  {RECT_CONFIG, "proxy.config.remap.num_remap_threads", RECD_INT, "0", RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL}
  ,

  //###########
  //#
  //# Temporary and esoteric values.
  //#
  //###########
  {RECT_CONFIG, "proxy.config.cache.http.compatibility.4-2-0-fixup", RECD_INT, "1", RECU_DYNAMIC, RR_NULL, RECC_NULL, nullptr, RECA_NULL},

  // Controls for TLS ASYN_JOBS and engine loading
  {RECT_CONFIG, "proxy.config.ssl.async.handshake.enabled", RECD_INT, "0", RECU_RESTART_TS, RR_NULL, RECC_NULL, "[0-1]", RECA_NULL},
  {RECT_CONFIG, "proxy.config.ssl.engine.conf_file", RECD_STRING, nullptr, RECU_NULL, RR_NULL, RECC_NULL, nullptr, RECA_NULL},
};
// clang-format on

void
RecordsConfigIterate(RecordElementCallback callback, void *data)
{
  for (unsigned i = 0; i < countof(RecordsConfig); ++i) {
    callback(&RecordsConfig[i], data);
  }
}
