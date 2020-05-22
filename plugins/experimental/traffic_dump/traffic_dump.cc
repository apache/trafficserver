/** @traffic_dump.cc
  Plugin Traffic Dump captures traffic on a per session basis. A sampling ratio can be set via plugin.config or traffic_ctl to dump
  one out of n sessions. The dump file schema can be found
  https://github.com/apache/trafficserver/tree/master/tests/tools/lib/replay_schema.json
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

#include <getopt.h>

#include "global_variables.h"
#include "session_data.h"
#include "transaction_data.h"

namespace traffic_dump
{
/// Handle LIFECYCLE_MSG from traffic_ctl.
static int
global_message_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_LIFECYCLE_MSG: {
    TSPluginMsg *msg = static_cast<TSPluginMsg *>(edata);
    static constexpr std::string_view PLUGIN_PREFIX("traffic_dump."_sv);

    std::string_view tag(msg->tag, strlen(msg->tag));
    if (tag.substr(0, PLUGIN_PREFIX.size()) == PLUGIN_PREFIX) {
      tag.remove_prefix(PLUGIN_PREFIX.size());
      if (tag == "sample") {
        const auto new_sample_size = static_cast<int64_t>(strtol(static_cast<char const *>(msg->data), nullptr, 0));
        TSDebug(debug_tag, "TS_EVENT_LIFECYCLE_MSG: Received Msg to change sample size to %" PRId64 "bytes", new_sample_size);
        SessionData::set_sample_pool_size(new_sample_size);
      } else if (tag == "reset") {
        TSDebug(debug_tag, "TS_EVENT_LIFECYCLE_MSG: Received Msg to reset disk usage counter");
        SessionData::reset_disk_usage();
      } else if (tag == "limit") {
        const auto new_max_disk_usage = static_cast<int64_t>(strtol(static_cast<char const *>(msg->data), nullptr, 0));
        TSDebug(debug_tag, "TS_EVENT_LIFECYCLE_MSG: Received Msg to change max disk usage to %" PRId64 "bytes", new_max_disk_usage);
        SessionData::set_max_disk_usage(new_max_disk_usage);
      }
    }
    return TS_SUCCESS;
  }
  default:
    TSDebug(debug_tag, "session_aio_handler(): unhandled events %d", event);
    return TS_ERROR;
  }
}

} // namespace traffic_dump

void
TSPluginInit(int argc, char const *argv[])
{
  TSDebug(traffic_dump::debug_tag, "initializing plugin");
  TSPluginRegistrationInfo info;

  info.plugin_name   = "traffic_dump";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to register plugin.", traffic_dump::debug_tag);
    return;
  }

  bool sensitive_fields_were_specified = false;
  traffic_dump::sensitive_fields_t user_specified_fields;
  ts::file::path log_dir{traffic_dump::SessionData::default_log_directory};
  int64_t sample_pool_size = traffic_dump::SessionData::default_sample_pool_size;
  int64_t max_disk_usage   = traffic_dump::SessionData::default_max_disk_usage;
  std::string sni_filter;

  /// Commandline options
  static const struct option longopts[] = {
    {"logdir", required_argument, nullptr, 'l'},     {"sample", required_argument, nullptr, 's'},
    {"limit", required_argument, nullptr, 'm'},      {"sensitive-fields", required_argument, nullptr, 'f'},
    {"sni-filter", required_argument, nullptr, 'n'}, {nullptr, no_argument, nullptr, 0}};
  int opt = 0;
  while (opt >= 0) {
    opt = getopt_long(argc, const_cast<char *const *>(argv), "l:", longopts, nullptr);
    switch (opt) {
    case 'f': {
      // --sensitive-fields takes a comma-separated list of HTTP fields that
      // are sensitive.  The field values for these fields will be replaced
      // with generic traffic_dump generated data.
      //
      // If this option is not used, then the default values in
      // default_sensitive_fields is used. If this option is used, then it
      // replaced the default sensitive fields with the user-supplied list of
      // sensitive fields.
      sensitive_fields_were_specified = true;
      ts::TextView input_filter_fields{std::string_view{optarg}};
      ts::TextView filter_field;
      while (!(filter_field = input_filter_fields.take_prefix_at(',')).empty()) {
        filter_field.trim_if(&isspace);
        if (filter_field.empty()) {
          continue;
        }
        user_specified_fields.emplace(filter_field);
      }
      break;
    }
    case 'n': {
      // --sni-filter is used to filter sessions based upon an SNI.
      sni_filter = std::string(optarg);
      break;
    }
    case 'l': {
      log_dir = ts::file::path{optarg};
      break;
    }
    case 's': {
      sample_pool_size = static_cast<int64_t>(std::strtol(optarg, nullptr, 0));
      break;
    }
    case 'm': {
      max_disk_usage = static_cast<int64_t>(std::strtol(optarg, nullptr, 0));
    }
    case -1:
    case '?':
      break;

    default:
      TSDebug(traffic_dump::debug_tag, "Unexpected options.");
      TSError("[%s] Unexpected options error.", traffic_dump::debug_tag);
      return;
    }
  }
  if (!log_dir.is_absolute()) {
    log_dir = ts::file::path(TSInstallDirGet()) / log_dir;
  }
  if (sni_filter.empty()) {
    if (!traffic_dump::SessionData::init(log_dir.view(), max_disk_usage, sample_pool_size)) {
      TSError("[%s] Failed to initialize session state.", traffic_dump::debug_tag);
      return;
    }
  } else {
    if (!traffic_dump::SessionData::init(log_dir.view(), max_disk_usage, sample_pool_size, sni_filter)) {
      TSError("[%s] Failed to initialize session state with an SNI filter.", traffic_dump::debug_tag);
      return;
    }
  }

  if (sensitive_fields_were_specified) {
    if (!traffic_dump::TransactionData::init(std::move(user_specified_fields))) {
      TSError("[%s] Failed to initialize transaction state with user-specified fields.", traffic_dump::debug_tag);
      return;
    }
  } else {
    // The user did not provide their own list of sensitive fields. Use the
    // default.
    if (!traffic_dump::TransactionData::init()) {
      TSError("[%s] Failed to initialize transaction state.", traffic_dump::debug_tag);
      return;
    }
  }

  TSCont message_continuation = TSContCreate(traffic_dump::global_message_handler, nullptr);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, message_continuation);
  return;
}
