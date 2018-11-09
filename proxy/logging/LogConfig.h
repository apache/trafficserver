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

#pragma once

#include <string_view>
#include <string>

#include "tscore/IntrusiveHashMap.h"
#include "tscore/ink_platform.h"
#include "records/P_RecProcess.h"
#include "ProxyConfig.h"
#include "LogObject.h"

/* Instead of enumerating the stats in DynamicStats.h, each module needs
   to enumerate its stats separately and register them with librecords
   */
enum {
  // Logging Events
  log_stat_event_log_error_ok_stat,
  log_stat_event_log_error_skip_stat,
  log_stat_event_log_error_aggr_stat,
  log_stat_event_log_error_full_stat,
  log_stat_event_log_error_fail_stat,

  log_stat_event_log_access_ok_stat,
  log_stat_event_log_access_skip_stat,
  log_stat_event_log_access_aggr_stat,
  log_stat_event_log_access_full_stat,
  log_stat_event_log_access_fail_stat,

  // Logging Data
  log_stat_num_sent_to_network_stat,
  log_stat_num_lost_before_sent_to_network_stat,
  log_stat_num_received_from_network_stat,
  log_stat_num_flush_to_disk_stat,
  log_stat_num_lost_before_flush_to_disk_stat,

  log_stat_bytes_lost_before_preproc_stat,
  log_stat_bytes_sent_to_network_stat,
  log_stat_bytes_lost_before_sent_to_network_stat,
  log_stat_bytes_received_from_network_stat,

  log_stat_bytes_flush_to_disk_stat,
  log_stat_bytes_lost_before_flush_to_disk_stat,
  log_stat_bytes_written_to_disk_stat,
  log_stat_bytes_lost_before_written_to_disk_stat,

  // Logging I/O
  log_stat_log_files_open_stat,
  log_stat_log_files_space_used_stat,

  log_stat_count
};

extern RecRawStatBlock *log_rsb;

struct dirent;
struct LogCollationAccept;

/*-------------------------------------------------------------------------
  LogDeleteCandidate, LogDeletingInfo&Descriptor
  -------------------------------------------------------------------------*/

struct LogDeleteCandidate {
  std::string name;
  int64_t size;
  time_t mtime;

  LogDeleteCandidate(char *p_name, int64_t st_size, time_t st_time) : name(p_name), size(st_size), mtime(st_time) {}
};

struct LogDeletingInfo {
  std::string name;
  int min_count;
  std::vector<LogDeleteCandidate> candidates;

  LogDeletingInfo *_next{nullptr};
  LogDeletingInfo *_prev{nullptr};

  LogDeletingInfo(const char *type, int limit) : name(type), min_count(limit) {}
  LogDeletingInfo(std::string_view type, int limit) : name(type), min_count(limit) {}

  void
  clear()
  {
    candidates.clear();
  }
};

struct LogDeletingInfoDescriptor {
  using key_type   = std::string_view;
  using value_type = LogDeletingInfo;

  static key_type
  key_of(value_type *value)
  {
    return value->name;
  }

  static bool
  equal(key_type const &lhs, key_type const &rhs)
  {
    return lhs == rhs;
  }

  static value_type *&
  next_ptr(value_type *value)
  {
    return value->_next;
  }

  static value_type *&
  prev_ptr(value_type *value)
  {
    return value->_prev;
  }

  static constexpr std::hash<std::string_view> hasher{};

  static auto
  hash_of(key_type s) -> decltype(hasher(s))
  {
    return hasher(s);
  }
};

/*-------------------------------------------------------------------------
  this object keeps the state of the logging configuraion variables.  upon
  construction, the log configuration file is read and the logging
  variables are initialized.

  the "global" logconfig object is kept as a static pointer in the log
  class, called "config", and changed whenever the configuration variables
  are changed in the config file, using log::change_configuration().

  to add a new config variable:
     1. add a line in records.config for the new config variable.
        the name in records.config should be "proxy.config.log.xxx".
     2. create a member variable to store the current value.
        the name of the member variable should be "xxx".
     3. if the member variable is a string, add a delete for it in the
        destructor, logconfig::~logconfig.
     4. initialize the member variable in logconfig::setup_default_values
     5. update the member variable from the records.config file
        in logconfig::read_configuration_variables() using a call to
        configreadinteger or configreadstring.
     6. add a line in the logconfig::register_config_callbacks() function
        for this new variable if it is exposed in the GUI
  -------------------------------------------------------------------------*/

class LogConfig : public ConfigInfo
{
public:
  LogConfig();
  ~LogConfig() override;

  void init(LogConfig *previous_config = nullptr);
  void display(FILE *fd = stdout);
  void setup_log_objects();

  static int reconfigure(const char *name, RecDataT data_type, RecData data, void *cookie);

  static void register_config_callbacks();
  static void register_stat_callbacks();
  static void register_mgmt_callbacks();

  bool space_to_write(int64_t bytes_to_write) const;

  bool
  am_collation_host() const
  {
    return collation_mode == Log::COLLATION_HOST;
  }

  bool
  space_is_short() const
  {
    return !space_to_write(max_space_mb_headroom * LOG_MEGABYTE);
  };

  void
  increment_space_used(int bytes)
  {
    m_space_used += bytes;
    m_partition_space_left -= bytes;
  }

  void update_space_used();
  void read_configuration_variables();

  // CVR This is the mgmt callback function, hence all the strange arguments
  static void *reconfigure_mgmt_variables(void *token, char *data_raw, int data_len);

  int
  get_max_space_mb() const
  {
    return (use_orphan_log_space_value ? max_space_mb_for_orphan_logs : max_space_mb_for_logs);
  }

  void
  transfer_objects(LogConfig *old_config)
  {
    log_object_manager.transfer_objects(old_config->log_object_manager);
  }

  bool
  has_api_objects() const
  {
    return log_object_manager.has_api_objects();
  }

public:
  bool initialized;
  bool reconfiguration_needed;
  bool logging_space_exhausted;
  int64_t m_space_used;
  int64_t m_partition_space_left;
  bool roll_log_files_now; // signal that files must be rolled

  LogObjectManager log_object_manager;

  LogFilterList filter_list;
  LogFormatList format_list;

  int log_buffer_size;
  int max_secs_per_buffer;
  int max_space_mb_for_logs;
  int max_space_mb_for_orphan_logs;
  int max_space_mb_headroom;
  int logfile_perm;
  int collation_mode;
  int collation_port;
  bool collation_host_tagged;
  int collation_preproc_threads;
  int collation_retry_sec;
  int collation_max_send_buffers;
  Log::RollingEnabledValues rolling_enabled;
  int rolling_interval_sec;
  int rolling_offset_hr;
  int rolling_size_mb;
  int rolling_min_count;
  bool auto_delete_rolled_files;

  IntrusiveHashMap<LogDeletingInfoDescriptor> deleting_info;

  int sampling_frequency;
  int file_stat_frequency;
  int space_used_frequency;

  int ascii_buffer_size;
  int max_line_size;

  char *hostname;
  char *logfile_dir;
  char *collation_host;
  char *collation_secret;

private:
  bool evaluate_config();

  void setup_default_values();
  void setup_collation(LogConfig *prev_config);

private:
  // if true, use max_space_mb_for_orphan_logs to determine the amount
  // of space that logging can use, otherwise use max_space_mb_for_logs
  //
  bool use_orphan_log_space_value;

  LogCollationAccept *m_log_collation_accept;

  bool m_disk_full;
  bool m_disk_low;
  bool m_partition_full;
  bool m_partition_low;
  bool m_log_directory_inaccessible;

  // noncopyable
  // -- member functions not allowed --
  LogConfig(const LogConfig &) = delete;
  LogConfig &operator=(const LogConfig &) = delete;
};
