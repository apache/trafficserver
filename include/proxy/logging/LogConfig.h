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
#include "records/RecProcess.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/logging/LogObject.h"
#include "proxy/logging/RolledLogDeleter.h"
#include "swoc/MemSpan.h"
#include "tsutil/Metrics.h"

using ts::Metrics;

struct LogsStatsBlock {
  Metrics::Counter::AtomicType *event_log_error_ok;
  Metrics::Counter::AtomicType *event_log_error_skip;
  Metrics::Counter::AtomicType *event_log_error_aggr;
  Metrics::Counter::AtomicType *event_log_error_full;
  Metrics::Counter::AtomicType *event_log_error_fail;
  Metrics::Counter::AtomicType *event_log_access_ok;
  Metrics::Counter::AtomicType *event_log_access_skip;
  Metrics::Counter::AtomicType *event_log_access_aggr;
  Metrics::Counter::AtomicType *event_log_access_full;
  Metrics::Counter::AtomicType *event_log_access_fail;
  Metrics::Counter::AtomicType *num_sent_to_network;
  Metrics::Counter::AtomicType *num_lost_before_sent_to_network;
  Metrics::Counter::AtomicType *num_received_from_network;
  Metrics::Counter::AtomicType *num_flush_to_disk;
  Metrics::Counter::AtomicType *num_lost_before_flush_to_disk;
  Metrics::Counter::AtomicType *bytes_lost_before_preproc;
  Metrics::Counter::AtomicType *bytes_sent_to_network;
  Metrics::Counter::AtomicType *bytes_lost_before_sent_to_network;
  Metrics::Counter::AtomicType *bytes_received_from_network;
  Metrics::Counter::AtomicType *bytes_flush_to_disk;
  Metrics::Counter::AtomicType *bytes_lost_before_flush_to_disk;
  Metrics::Counter::AtomicType *bytes_written_to_disk;
  Metrics::Counter::AtomicType *bytes_lost_before_written_to_disk;
  Metrics::Gauge::AtomicType   *log_files_open;
  Metrics::Gauge::AtomicType   *log_files_space_used;
};

extern LogsStatsBlock log_rsb;

struct dirent;

/*-------------------------------------------------------------------------
  this object keeps the state of the logging configuration variables.  upon
  construction, the log configuration file is read and the logging
  variables are initialized.

  the "global" logconfig object is kept as a static pointer in the log
  class, called "config", and changed whenever the configuration variables
  are changed in the config file, using log::change_configuration().

  to add a new config variable:
     1. add a line in records.yaml for the new config variable.
        the name in records.yaml should be "proxy.config.log.xxx".
     2. create a member variable to store the current value.
        the name of the member variable should be "xxx".
     3. if the member variable is a string, add a delete for it in the
        destructor, logconfig::~logconfig.
     4. initialize the member variable in logconfig::setup_default_values
     5. update the member variable from the records.yaml file
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

  bool space_to_write(int64_t bytes_to_write) const;

  bool
  space_is_short() const
  {
    return !space_to_write(max_space_mb_headroom * LOG_MEGABYTE);
  };

  void
  increment_space_used(int bytes)
  {
    m_space_used           += bytes;
    m_partition_space_left -= bytes;
  }

  void update_space_used();
  void read_configuration_variables();

  // CVR This is the mgmt callback function, hence all the strange arguments
  static void reconfigure_mgmt_variables(swoc::MemSpan<void>);

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

  bool    initialized             = false;
  bool    reconfiguration_needed  = false;
  bool    logging_space_exhausted = false;
  int64_t m_space_used            = 0;
  int64_t m_partition_space_left;
  bool    roll_log_files_now; // signal that files must be rolled

  LogObjectManager log_object_manager;

  LogFilterList filter_list;
  LogFormatList format_list;

  uint32_t log_buffer_size       = 10 * LOG_KILOBYTE;
  bool     log_fast_buffer       = false;
  int      max_secs_per_buffer   = 5;
  int      max_space_mb_for_logs = 100;
  int      max_space_mb_headroom = 10;
  int      logfile_perm          = 0644;

  int preproc_threads = 1;

  Log::RollingEnabledValues rolling_enabled          = Log::NO_ROLLING;
  int                       rolling_interval_sec     = 86400;
  int                       rolling_offset_hr        = 0;
  int                       rolling_size_mb          = 10;
  int                       rolling_min_count        = 0;
  int                       rolling_max_count        = 0;
  bool                      rolling_allow_empty      = false;
  bool                      auto_delete_rolled_files = false;

  int sampling_frequency   = 1;
  int file_stat_frequency  = 16;
  int space_used_frequency = 900;

  int ascii_buffer_size         = 4 * 9216;
  int max_line_size             = 9216;
  int logbuffer_max_iobuf_index = BUFFER_SIZE_INDEX_32K;

  char *hostname           = nullptr;
  char *logfile_dir        = nullptr;
  char *error_log_filename = nullptr;

private:
  bool evaluate_config();

  bool m_disk_full                  = false;
  bool m_disk_low                   = false;
  bool m_partition_full             = false;
  bool m_partition_low              = false;
  bool m_log_directory_inaccessible = false;

  RolledLogDeleter rolledLogDeleter;

  // noncopyable
  // -- member functions not allowed --
  LogConfig(const LogConfig &)            = delete;
  LogConfig &operator=(const LogConfig &) = delete;
};
