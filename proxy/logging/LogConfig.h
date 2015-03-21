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


#ifndef LOG_CONFIG_H
#define LOG_CONFIG_H

#include "libts.h"
#include "P_RecProcess.h"
#include "ProxyConfig.h"

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
struct PreDefinedFormatList;
struct PreDefinedFormatInfo;

/*-------------------------------------------------------------------------
  LogConfig

  This object keeps the state of the logging configuraion variables.  Upon
  construction, the log configuration file is read and the logging
  variables are initialized.

  The "global" LogConfig object is kept as a static pointer in the Log
  class, called "config", and changed whenever the configuration variables
  are changed in the config file, using Log::change_configuration().

  To add a new config variable:
     1. Add a line in records.config for the new config variable.
        The name in records.config should be "proxy.config.log.xxx".
     2. Create a member variable to store the current value.
        The name of the member variable should be "xxx".
     3. If the member variable is a string, add a delete for it in the
        destructor, LogConfig::~LogConfig.
     4. Initialize the member variable in LogConfig::setup_default_values
     5. Update the member variable from the records.config file
        in LogConfig::read_configuration_variables() using a call to
        ConfigReadInteger or ConfigReadString.
     6. Add a line in the LogConfig::register_config_callbacks() function
        for this new variable if it is exposed in the GUI
  -------------------------------------------------------------------------*/

class LogConfig : public ConfigInfo
{
public:
  LogConfig();
  ~LogConfig();

  void init(LogConfig *previous_config = 0);
  void display(FILE *fd = stdout);
  void setup_log_objects();

  static int reconfigure(const char *name, RecDataT data_type, RecData data, void *cookie);

  static void register_config_callbacks();
  static void register_stat_callbacks();
  static void register_mgmt_callbacks();

  bool space_to_write(int64_t bytes_to_write);

  bool
  am_collation_host() const
  {
    return collation_mode == Log::COLLATION_HOST;
  }
  bool
  space_is_short()
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
  get_max_space_mb()
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
  LogFilterList global_filter_list;
  LogFormatList global_format_list;

  int log_buffer_size;
  int max_secs_per_buffer;
  int max_space_mb_for_logs;
  int max_space_mb_for_orphan_logs;
  int max_space_mb_headroom;
  int logfile_perm;
  bool squid_log_enabled;
  bool squid_log_is_ascii;
  bool common_log_enabled;
  bool common_log_is_ascii;
  bool extended_log_enabled;
  bool extended_log_is_ascii;
  bool extended2_log_enabled;
  bool extended2_log_is_ascii;
  bool separate_icp_logs;
  bool separate_host_logs;
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
  bool auto_delete_rolled_files;
  bool custom_logs_enabled;

  bool search_log_enabled;
  int search_rolling_interval_sec;
  unsigned int search_server_ip_addr;
  int search_server_port;
  int search_top_sites;
  char *search_log_filters;
  char *search_url_filter;
  char *search_log_file_one;
  char *search_log_file_two;

  int sampling_frequency;
  int file_stat_frequency;
  int space_used_frequency;

  int ascii_buffer_size;
  int max_line_size;

  char *hostname;
  char *logfile_dir;
  char *squid_log_name;
  char *squid_log_header;
  char *common_log_name;
  char *common_log_header;
  char *extended_log_name;
  char *extended_log_header;
  char *extended2_log_name;
  char *extended2_log_header;
  char *collation_host;
  char *collation_secret;

private:
  void read_xml_log_config(int from_memory);
  char **read_log_hosts_file(size_t *nhosts);

  void setup_default_values();
  void setup_collation(LogConfig *prev_config);
  LogFilter *split_by_protocol(const PreDefinedFormatList &pre_def_info_list);
  size_t split_by_hostname(const PreDefinedFormatList &pre_def_info_list, LogFilter *reject_protocol);
  LogObject *create_predefined_object(const PreDefinedFormatInfo *pdi, size_t nfilters, LogFilter **filters,
                                      const char *filt_name = 0, bool force_extension = false);
  void create_predefined_objects_with_filter(const PreDefinedFormatList &pre_def_info_list, size_t nfilters, LogFilter **filters,
                                             const char *filt_name = 0, bool force_extension = false);

  void add_filters_to_search_log_object(const char *format_name);

private:
  // if true, use max_space_mb_for_orphan_logs to determine the amount
  // of space that logging can use, otherwise use max_space_mb_for_logs
  //
  bool use_orphan_log_space_value;

  LogCollationAccept *m_log_collation_accept;

  struct dirent *m_dir_entry;
  char *m_pDir;
  bool m_disk_full;
  bool m_disk_low;
  bool m_partition_full;
  bool m_partition_low;
  bool m_log_directory_inaccessible;

private:
  // -- member functions not allowed --
  LogConfig(const LogConfig &);
  LogConfig &operator=(const LogConfig &);
};

/*-------------------------------------------------------------------------
  LogDeleteCandidate
  -------------------------------------------------------------------------*/

struct LogDeleteCandidate {
  time_t mtime;
  char *name;
  int64_t size;
};

#endif
