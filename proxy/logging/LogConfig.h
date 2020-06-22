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

#include "tscore/ink_platform.h"
#include "records/P_RecProcess.h"
#include "ProxyConfig.h"
#include "LogObject.h"
#include "RolledLogDeleter.h"
#include "tscpp/util/MemSpan.h"

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
  static void reconfigure_mgmt_variables(ts::MemSpan<void>);

  int
  get_max_space_mb() const
  {
    return max_space_mb_for_logs;
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

  /** Register rolled logs of logname for auto-deletion when there are space
   * constraints.
   *
   * @param[in] logname The name of the unrolled log to register, such as
   * "diags.log".
   *
   * @param[in] rolling_min_count The minimum amount of rolled logs of logname
   * to try to keep around. A value of 0 expresses a desire to keep all rolled
   * files, if possible.
   */
  void register_rolled_log_auto_delete(std::string_view logname, int rolling_min_count);

public:
  bool initialized             = false;
  bool reconfiguration_needed  = false;
  bool logging_space_exhausted = false;
  int64_t m_space_used         = 0;
  int64_t m_partition_space_left;
  bool roll_log_files_now; // signal that files must be rolled

  LogObjectManager log_object_manager;

  LogFilterList filter_list;
  LogFormatList format_list;

  int log_buffer_size;
  int max_secs_per_buffer;
  int max_space_mb_for_logs;
  int max_space_mb_headroom;
  int logfile_perm;

  int preproc_threads;

  Log::RollingEnabledValues rolling_enabled;
  int rolling_interval_sec;
  int rolling_offset_hr;
  int rolling_size_mb;
  int rolling_min_count;
  int rolling_max_count;
  bool rolling_allow_empty;
  bool auto_delete_rolled_files;

  int sampling_frequency;
  int file_stat_frequency;
  int space_used_frequency;

  int ascii_buffer_size;
  int max_line_size;
  int logbuffer_max_iobuf_index;

  char *hostname;
  char *logfile_dir;

private:
  bool evaluate_config();

  void setup_default_values();

private:
  bool m_disk_full                  = false;
  bool m_disk_low                   = false;
  bool m_partition_full             = false;
  bool m_partition_low              = false;
  bool m_log_directory_inaccessible = false;

  RolledLogDeleter rolledLogDeleter;

  // noncopyable
  // -- member functions not allowed --
  LogConfig(const LogConfig &) = delete;
  LogConfig &operator=(const LogConfig &) = delete;
};
