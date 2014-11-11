/** @file

  This file implements the LogConfig object.

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

#include "libts.h"
#include "I_Layout.h"

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include "ink_platform.h"
#include "ink_file.h"

#include "Main.h"
#include "List.h"
#include "InkXml.h"

#include "Log.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogFile.h"
#include "LogBuffer.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogUtils.h"
#include "SimpleTokenizer.h"

#include "LogCollationAccept.h"
#include "LogPredefined.h"

#define DISK_IS_CONFIG_FULL_MESSAGE \
    "Access logging to local log directory suspended - " \
    "configured space allocation exhausted."
#define DISK_IS_ACTUAL_FULL_MESSAGE \
    "Access logging to local log directory suspended - " \
    "no more space on the logging partition."
#define DISK_IS_CONFIG_LOW_MESSAGE \
    "Access logging to local log directory suspended - " \
    "configured space allocation almost exhausted."
#define DISK_IS_ACTUAL_LOW_MESSAGE \
    "Access logging to local log directory suspended - partition space is low."
#define DUP_FORMAT_MESSAGE \
    "Format named %s already exists; duplicate format names are not allowed."

#define PARTITION_HEADROOM_MB 	10

void
LogConfig::setup_default_values()
{
  const unsigned int bufSize = 512;
  char name[bufSize];
  if (!gethostname(name, bufSize)) {
    ink_strlcpy(name, "unknown_host_name", sizeof(name));
  }
  hostname = ats_strdup(name);

  log_buffer_size = (int) (10 * LOG_KILOBYTE);
  max_secs_per_buffer = 5;
  max_space_mb_for_logs = 100;
  max_space_mb_for_orphan_logs = 25;
  max_space_mb_headroom = 10;
  logfile_perm = 0644;
  logfile_dir = ats_strdup(".");

  separate_icp_logs = 1;
  separate_host_logs = false;

  squid_log_enabled = true;
  squid_log_is_ascii = true;
  squid_log_name = ats_strdup("squid");
  squid_log_header = NULL;

  common_log_enabled = false;
  common_log_is_ascii = true;
  common_log_name = ats_strdup("common");
  common_log_header = NULL;

  extended_log_enabled = false;
  extended_log_is_ascii = true;
  extended_log_name = ats_strdup("extended");
  extended_log_header = NULL;

  extended2_log_enabled = false;
  extended2_log_is_ascii = true;
  extended2_log_name = ats_strdup("extended2");
  extended2_log_header = NULL;

  collation_mode = Log::NO_COLLATION;
  collation_host = ats_strdup("none");
  collation_port = 0;
  collation_host_tagged = false;
  collation_preproc_threads = 1;
  collation_secret = ats_strdup("foobar");
  collation_retry_sec = 0;
  collation_max_send_buffers = 0;

  rolling_enabled = Log::NO_ROLLING;
  rolling_interval_sec = 86400; // 24 hours
  rolling_offset_hr = 0;
  rolling_size_mb = 10;
  auto_delete_rolled_files = true;
  roll_log_files_now = false;

  custom_logs_enabled = false;

/* The default values for the search log                         */

  search_log_enabled = false;
/* Comma separated filter names. These Filetrs are defined in    */
/* log_xml.config file. One such filter defines to reject URLs   */
/* with .gif extension. These filters are added to search log    */
/* object.                                                       */
  search_log_filters = NULL;
  search_rolling_interval_sec = 86400;  // 24 hours
  search_server_ip_addr = 0;
  search_server_port = 8080;
  search_top_sites = 100;
/* Comma separated filter names. These Filetrs are defined in    */
/* records.config or thru UI. These are applied to the search log*/
/* while parsing to generate top 'n' site summary file. The URLs */
/* containing these strings will not be parsed.                  */
  search_url_filter = NULL;
/* Logging system captures all the URLs in this log file.        */
  search_log_file_one = ats_strdup("search_log1");
/* Logging system captures only cache miss URLs to this log file.*/
  search_log_file_two = ats_strdup("search_log2");

  sampling_frequency = 1;
  file_stat_frequency = 16;
  space_used_frequency = 900;

  use_orphan_log_space_value = false;

  ascii_buffer_size = 4 * 9216;
  max_line_size = 9216;         // size of pipe buffer for SunOS 5.6
}

void *
LogConfig::reconfigure_mgmt_variables(void * /* token ATS_UNUSED */, char * /* data_raw ATS_UNUSED */,
                                      int /* data_len ATS_UNUSED */)
{
  Note("received log reconfiguration event, rolling now");
  Log::config->roll_log_files_now = true;
  return NULL;
}

void
LogConfig::read_configuration_variables()
{
  int val;
  char *ptr;

  val = (int) REC_ConfigReadInteger("proxy.config.log.log_buffer_size");
  if (val > 0) {
    log_buffer_size = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.max_secs_per_buffer");
  if (val > 0) {
    max_secs_per_buffer = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.max_space_mb_for_logs");
  if (val > 0) {
    max_space_mb_for_logs = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.max_space_mb_for_orphan_logs");
  if (val > 0) {
    max_space_mb_for_orphan_logs = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.max_space_mb_headroom");
  if (val > 0) {
    max_space_mb_headroom = val;
  }

  // TODO: We should mover this "parser" to lib/ts
  ptr = REC_ConfigReadString("proxy.config.log.logfile_perm");
  if (ptr && strlen(ptr) == 9) {
    logfile_perm = 0;
    char *c = ptr;
    if (*c == 'r')
      logfile_perm |= S_IRUSR;
    c++;
    if (*c == 'w')
      logfile_perm |= S_IWUSR;
    c++;
    if (*c == 'x')
      logfile_perm |= S_IXUSR;
    c++;
    if (*c == 'r')
      logfile_perm |= S_IRGRP;
    c++;
    if (*c == 'w')
      logfile_perm |= S_IWGRP;
    c++;
    if (*c == 'x')
      logfile_perm |= S_IXGRP;
    c++;
    if (*c == 'r')
      logfile_perm |= S_IROTH;
    c++;
    if (*c == 'w')
      logfile_perm |= S_IWOTH;
    c++;
    if (*c == 'x')
      logfile_perm |= S_IXOTH;
    ats_free(ptr);
  }

  ptr = REC_ConfigReadString("proxy.config.log.hostname");
  if (ptr != NULL) {
    ats_free(hostname);
    hostname = ptr;
  }

  ats_free(logfile_dir);
  logfile_dir = RecConfigReadLogDir();

  if (access(logfile_dir, R_OK | W_OK | X_OK) == -1) {
    // Try 'system_root_dir/var/log/trafficserver' directory
    fprintf(stderr,"unable to access log directory '%s': %d, %s\n",
            logfile_dir, errno, strerror(errno));
    fprintf(stderr,"please set 'proxy.config.log.logfile_dir'\n");
    _exit(1);
  }

  //
  // for each predefined logging format, we need to know:
  //   - whether logging in that format is enabled
  //   - if we're logging to a file, what the name and format (ASCII,
  //     BINARY) are
  //   - what header should be written down at the start of each file for
  //     this format
  // this is accomplished with four config variables per format:
  //   "proxy.config.log.<format>_log_enabled" INT
  //   "proxy.config.log.<format>_log_is_ascii" INT
  //   "proxy.config.log.<format>_log_name" STRING
  //   "proxy.config.log.<format>_log_header" STRING
  //


  // SQUID
  val = (int) REC_ConfigReadInteger("proxy.config.log.squid_log_enabled");
  squid_log_enabled = (val > 0);

  val = (int) REC_ConfigReadInteger("proxy.config.log.squid_log_is_ascii");
  squid_log_is_ascii = (val > 0);

  ptr = REC_ConfigReadString("proxy.config.log.squid_log_name");
  if (ptr != NULL) {
    ats_free(squid_log_name);
    squid_log_name = ptr;
  }

  ptr = REC_ConfigReadString("proxy.config.log.squid_log_header");
  if (ptr != NULL) {
    ats_free(squid_log_header);
    squid_log_header = ptr;
  }

  // COMMON
  val = (int) REC_ConfigReadInteger("proxy.config.log.common_log_enabled");
  common_log_enabled = (val > 0);

  val = (int) REC_ConfigReadInteger("proxy.config.log.common_log_is_ascii");
  common_log_is_ascii = (val > 0);

  ptr = REC_ConfigReadString("proxy.config.log.common_log_name");
  if (ptr != NULL) {
    ats_free(common_log_name);
    common_log_name = ptr;
  }

  ptr = REC_ConfigReadString("proxy.config.log.common_log_header");
  if (ptr != NULL) {
    ats_free(common_log_header);
    common_log_header = ptr;
  }

  // EXTENDED
  val = (int) REC_ConfigReadInteger("proxy.config.log.extended_log_enabled");
  extended_log_enabled = (val > 0);

  val = (int) REC_ConfigReadInteger("proxy.config.log.extended_log_is_ascii");
  extended_log_is_ascii = (val > 0);

  ptr = REC_ConfigReadString("proxy.config.log.extended_log_name");
  if (ptr != NULL) {
    ats_free(extended_log_name);
    extended_log_name = ptr;
  }

  ptr = REC_ConfigReadString("proxy.config.log.extended_log_header");
  if (ptr != NULL) {
    ats_free(extended_log_header);
    extended_log_header = ptr;
  }

  // EXTENDED2
  val = (int) REC_ConfigReadInteger("proxy.config.log.extended2_log_enabled");
  extended2_log_enabled = (val > 0);

  val = (int) REC_ConfigReadInteger("proxy.config.log.extended2_log_is_ascii");
  extended2_log_is_ascii = (val > 0);

  ptr = REC_ConfigReadString("proxy.config.log.extended2_log_name");
  if (ptr != NULL) {
    ats_free(extended2_log_name);
    extended2_log_name = ptr;
  }

  ptr = REC_ConfigReadString("proxy.config.log.extended2_log_header");
  if (ptr != NULL) {
    ats_free(extended2_log_header);
    extended2_log_header = ptr;
  }


  // SPLITTING
  // 0 means no splitting
  // 1 means splitting
  // for icp
  //   -1 means filter out (do not log and do not create split file)
  val = (int) REC_ConfigReadInteger("proxy.config.log.separate_icp_logs");
  separate_icp_logs = (val > 0);

  val = (int) REC_ConfigReadInteger("proxy.config.log.separate_host_logs");
  separate_host_logs = (val > 0);


  // COLLATION
  val = (int) REC_ConfigReadInteger("proxy.local.log.collation_mode");
  // do not restrict value so that error message is logged if
  // collation_mode is out of range
  collation_mode = val;

  ptr = REC_ConfigReadString("proxy.config.log.collation_host");
  if (ptr != NULL) {
    ats_free(collation_host);
    collation_host = ptr;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.collation_port");
  if (val >= 0) {
    collation_port = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.collation_host_tagged");
  collation_host_tagged = (val > 0);

  val = (int) REC_ConfigReadInteger("proxy.config.log.collation_preproc_threads");
  if (val > 0 && val <= 128) {
    collation_preproc_threads = val;
  }

  ptr = REC_ConfigReadString("proxy.config.log.collation_secret");
  if (ptr != NULL) {
    ats_free(collation_secret);
    collation_secret = ptr;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.collation_retry_sec");
  if (val >= 0) {
    collation_retry_sec = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.collation_max_send_buffers");
  if (val >= 0) {
    collation_max_send_buffers = val;
  }


  // ROLLING

  // we don't check for valid values of rolling_enabled, rolling_interval_sec,
  // rolling_offset_hr, or rolling_size_mb because the LogObject takes care of this
  //
  rolling_interval_sec = (int) REC_ConfigReadInteger("proxy.config.log.rolling_interval_sec");
  rolling_offset_hr = (int) REC_ConfigReadInteger("proxy.config.log.rolling_offset_hr");
  rolling_size_mb = (int) REC_ConfigReadInteger("proxy.config.log.rolling_size_mb");

  val = (int) REC_ConfigReadInteger("proxy.config.log.rolling_enabled");
  if (LogRollingEnabledIsValid(val)) {
    rolling_enabled = (Log::RollingEnabledValues)val;
  } else {
    Warning("invalid value '%d' for '%s', disabling log rolling", val, "proxy.config.log.rolling_enabled");
    rolling_enabled = Log::NO_ROLLING;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.auto_delete_rolled_files");
  auto_delete_rolled_files = (val > 0);

  // CUSTOM LOGGING
  val = (int) REC_ConfigReadInteger("proxy.config.log.custom_logs_enabled");
  custom_logs_enabled = (val > 0);

  // PERFORMANCE
  val = (int) REC_ConfigReadInteger("proxy.config.log.sampling_frequency");
  if (val > 0) {
    sampling_frequency = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.file_stat_frequency");
  if (val > 0) {
    file_stat_frequency = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.space_used_frequency");
  if (val > 0) {
    space_used_frequency = val;
  }

  // ASCII BUFFER
  val = (int) REC_ConfigReadInteger("proxy.config.log.ascii_buffer_size");
  if (val > 0) {
    ascii_buffer_size = val;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.max_line_size");
  if (val > 0) {
    max_line_size = val;
  }

/* The following variables are initialized after reading the     */
/* variable values from records.config                           */

  val = (int) REC_ConfigReadInteger("proxy.config.log.search_log_enabled");
  if (Log::logging_mode == Log::LOG_MODE_FULL)
    search_log_enabled = (val > 0);

/*                                                               */
/* The following collects the filter names to be added to search */
/* log object. User can define the filter to exclude URLs with   */
/* certain file extensions from being logged.                    */
/*                                                               */
  ptr = REC_ConfigReadString("proxy.config.log.search_log_filters");
  if (ptr != NULL) {
    search_log_filters = ptr;
  }

/*                                                               */
/* This filter information is used while parsing the search log  */
/* to generate top 'n' site sorted URL summary file.             */
/* This log file contains top 'n' number of frequently accessed  */
/* sites. This file is sent to search server mentioned by the    */
/* pair ip-address & port.                                       */
/*                                                               */
  ptr = REC_ConfigReadString("proxy.config.log.search_url_filter");
  if (ptr != NULL) {
    search_url_filter = ptr;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.search_top_sites");
  if (val > 0) {
    search_top_sites = val;
  }

  ptr = REC_ConfigReadString("proxy.config.log.search_server_ip_addr");
  if (ptr != NULL) {
    unsigned int ipaddr;
    ipaddr = inet_addr(ptr);
    if (ipaddr > 0) {
      search_server_ip_addr = ipaddr;
    } else
      search_server_ip_addr = 0;
  }

  val = (int) REC_ConfigReadInteger("proxy.config.log.search_server_port");
  if (val > 0) {
    search_server_port = val;
  }

/*  Rolling interval is taken care in LogObject.                 */
  val = (int) REC_ConfigReadInteger("proxy.config.log.search_rolling_interval_sec");
  if (val > 0) {
    search_rolling_interval_sec = val;
  }
}

/*-------------------------------------------------------------------------
  LogConfig::LogConfig

  Read the logging configuration variables from the config file and
  initialize the LogConfig member variables.  Assign some meaningful
  default value if we get garbage back from the config file.
  -------------------------------------------------------------------------*/

// TODO: Is UINT_MAX here really correct?
LogConfig::LogConfig()
  : initialized(false),
    reconfiguration_needed(false),
    logging_space_exhausted(false), m_space_used(0), m_partition_space_left((int64_t) UINT_MAX),
    m_log_collation_accept(NULL),
    m_dir_entry(NULL),
    m_pDir(NULL),
    m_disk_full(false),
    m_disk_low(false), m_partition_full(false), m_partition_low(false), m_log_directory_inaccessible(false)
{
  // Setup the default values for all LogConfig public variables so that
  // a LogConfig object is valid upon return from the constructor even
  // if no configuration file is read
  //
  setup_default_values();
}

/*-------------------------------------------------------------------------
  LogConfig::~LogConfig

  Delete all config variable strings.
  -------------------------------------------------------------------------*/

LogConfig::~LogConfig()
{

// we don't delete the log collation accept because it may be transferred
// to another LogConfig object
//
//    delete m_log_collation_accept;

  ats_free(hostname);
  ats_free(logfile_dir);
  ats_free(squid_log_name);
  ats_free(squid_log_header);
  ats_free(common_log_name);
  ats_free(common_log_header);
  ats_free(extended_log_name);
  ats_free(extended_log_header);
  ats_free(extended2_log_name);
  ats_free(extended2_log_header);
  ats_free(collation_host);
  ats_free(collation_secret);
  ats_free(search_log_file_one);
  ats_free(search_log_file_two);
  ats_free(m_dir_entry);
}


/*-------------------------------------------------------------------------
  LogConfig::setup_collation
  -------------------------------------------------------------------------*/

void
LogConfig::setup_collation(LogConfig * prev_config)
{
  // Set-up the collation status, but only if collation is enabled and
  // there are valid entries for the collation host and port.
  //
  if (collation_mode < Log::NO_COLLATION || collation_mode >= Log::N_COLLATION_MODES) {
    Note("Invalid value %d for proxy.local.log.collation_mode"
         " configuration variable (valid range is from %d to %d)\n"
         "Log collation disabled", collation_mode, Log::NO_COLLATION, Log::N_COLLATION_MODES - 1);
  } else if (collation_mode == Log::NO_COLLATION) {
    // if the previous configuration had a collation accept, delete it
    //
    if (prev_config && prev_config->m_log_collation_accept) {
      delete prev_config->m_log_collation_accept;
      prev_config->m_log_collation_accept = NULL;
    }
  } else {
    if (!collation_port) {
      Note("Cannot activate log collation, %d is an invalid collation port", collation_port);
    } else if (collation_mode > Log::COLLATION_HOST && strcmp(collation_host, "none") == 0) {
      Note("Cannot activate log collation, \"%s\" is an invalid collation host", collation_host);
    } else {
      if (collation_mode == Log::COLLATION_HOST) {

        ink_assert(m_log_collation_accept == 0);

        if (prev_config && prev_config->m_log_collation_accept) {
          if (prev_config->collation_port == collation_port) {
            m_log_collation_accept = prev_config->m_log_collation_accept;
          } else {
            delete prev_config->m_log_collation_accept;
          }
        }

        if (!m_log_collation_accept) {
          Log::collation_port = collation_port;
          m_log_collation_accept = new LogCollationAccept(collation_port);
        }
        Debug("log", "I am a collation host listening on port %d.", collation_port);
      } else {
        Debug("log", "I am a collation client (%d)."
              " My collation host is %s:%d", collation_mode, collation_host, collation_port);
      }

      Debug("log", "using iocore log collation");
      if (collation_host_tagged) {
        LogFormat::turn_tagging_on();
      } else {
        LogFormat::turn_tagging_off();
      }
    }
  }
}

/*-------------------------------------------------------------------------
  LogConfig::init
  -------------------------------------------------------------------------*/

void
LogConfig::init(LogConfig * prev_config)
{
  LogObject * errlog = NULL;

  ink_assert(!initialized);

  setup_collation(prev_config);

  update_space_used();

  // create log objects
  //
  if (Log::transaction_logging_enabled()) {
    setup_log_objects();
  }

  // ----------------------------------------------------------------------
  // Construct a new error log object candidate.
  if (Log::error_logging_enabled()) {
    PreDefinedFormatInfo * info;

    Debug("log", "creating predefined error log object");
    info = MakePredefinedErrorLog(this);
    errlog = this->create_predefined_object(info, 0, NULL);
    errlog->set_fmt_timestamps();
    delete info;
  } else {
    Log::error_log = NULL;
  }

  if (prev_config) {
    // Transfer objects from previous configuration.
    transfer_objects(prev_config);

    // After transferring objects, we are going to keep either the new error log or the old one. Figure out
    // which one we are keeping and make that the global ...
    if (Log::error_log) {
      errlog = this->log_object_manager.find_by_format_name(Log::error_log->m_format->name());
    }
  }

  ink_atomic_swap(&Log::error_log, errlog);

  // determine if we should use the orphan log space value or not
  // we use it if all objects are collation clients, or if some are and
  // the specified space for collation is larger than that for local files
  //
  size_t num_collation_clients = log_object_manager.get_num_collation_clients();
  use_orphan_log_space_value =
    (num_collation_clients == 0 ? false :
     (log_object_manager.get_num_objects() == num_collation_clients ? true :
      max_space_mb_for_orphan_logs > max_space_mb_for_logs));

  initialized = true;
}

/*-------------------------------------------------------------------------
  LogConfig::display

  Dump the values for the current LogConfig object.
  -------------------------------------------------------------------------*/

void
LogConfig::display(FILE * fd)
{
  fprintf(fd, "-----------------------------\n");
  fprintf(fd, "--- Logging Configuration ---\n");
  fprintf(fd, "-----------------------------\n");
  fprintf(fd, "Config variables:\n");
  fprintf(fd, "   log_buffer_size = %d\n", log_buffer_size);
  fprintf(fd, "   max_secs_per_buffer = %d\n", max_secs_per_buffer);
  fprintf(fd, "   max_space_mb_for_logs = %d\n", max_space_mb_for_logs);
  fprintf(fd, "   max_space_mb_for_orphan_logs = %d\n", max_space_mb_for_orphan_logs);
  fprintf(fd, "   use_orphan_log_space_value = %d\n", use_orphan_log_space_value);
  fprintf(fd, "   max_space_mb_headroom = %d\n", max_space_mb_headroom);
  fprintf(fd, "   hostname = %s\n", hostname);
  fprintf(fd, "   logfile_dir = %s\n", logfile_dir);
  fprintf(fd, "   logfile_perm = 0%o\n", logfile_perm);
  fprintf(fd, "   squid_log_enabled = %d\n", squid_log_enabled);
  fprintf(fd, "   squid_log_is_ascii = %d\n", squid_log_is_ascii);
  fprintf(fd, "   squid_log_name = %s\n", squid_log_name);
  fprintf(fd, "   squid_log_header = %s\n", squid_log_header ? squid_log_header : "<no header defined>");
  fprintf(fd, "   common_log_enabled = %d\n", common_log_enabled);
  fprintf(fd, "   common_log_is_ascii = %d\n", common_log_is_ascii);
  fprintf(fd, "   common_log_name = %s\n", common_log_name);
  fprintf(fd, "   common_log_header = %s\n", common_log_header ? common_log_header : "<no header defined>");
  fprintf(fd, "   extended_log_enabled = %d\n", extended_log_enabled);
  fprintf(fd, "   extended_log_is_ascii = %d\n", extended_log_is_ascii);
  fprintf(fd, "   extended_log_name = %s\n", extended_log_name);
  fprintf(fd, "   extended_log_header = %s\n", extended_log_header ? extended_log_header : "<no header defined>");
  fprintf(fd, "   extended2_log_enabled = %d\n", extended2_log_enabled);
  fprintf(fd, "   extended2_log_is_ascii = %d\n", extended2_log_is_ascii);
  fprintf(fd, "   extended2_log_name = %s\n", extended2_log_name);
  fprintf(fd, "   extended2_log_header = %s\n", extended2_log_header ? extended2_log_header : "<no header defined>");
  fprintf(fd, "   separate_icp_logs = %d\n", separate_icp_logs);
  fprintf(fd, "   separate_host_logs = %d\n", separate_host_logs);
  fprintf(fd, "   collation_mode = %d\n", collation_mode);
  fprintf(fd, "   collation_host = %s\n", collation_host);
  fprintf(fd, "   collation_port = %d\n", collation_port);
  fprintf(fd, "   collation_host_tagged = %d\n", collation_host_tagged);
  fprintf(fd, "   collation_preproc_threads = %d\n", collation_preproc_threads);
  fprintf(fd, "   collation_secret = %s\n", collation_secret);
  fprintf(fd, "   rolling_enabled = %d\n", rolling_enabled);
  fprintf(fd, "   rolling_interval_sec = %d\n", rolling_interval_sec);
  fprintf(fd, "   rolling_offset_hr = %d\n", rolling_offset_hr);
  fprintf(fd, "   rolling_size_mb = %d\n", rolling_size_mb);
  fprintf(fd, "   auto_delete_rolled_files = %d\n", auto_delete_rolled_files);
  fprintf(fd, "   sampling_frequency = %d\n", sampling_frequency);
  fprintf(fd, "   file_stat_frequency = %d\n", file_stat_frequency);
  fprintf(fd, "   space_used_frequency = %d\n", space_used_frequency);

  fprintf(fd, "\n");
  fprintf(fd, "************ Log Objects (%u objects) ************\n", (unsigned int)log_object_manager.get_num_objects());
  log_object_manager.display(fd);

  fprintf(fd, "************ Global Filter List (%u filters) ************\n", global_filter_list.count());
  global_filter_list.display(fd);

  fprintf(fd, "************ Global Format List (%u formats) ************\n", global_format_list.count());
  global_format_list.display(fd);
}

/*                                                               */
/* The user defined filters are added to the search_one          */
/* log object. These filters are defined to filter the images    */
/* and vedio file etc., URLs. These filters depends on the       */
/* end-user's search requirements.                               */
/*                                                               */
void
LogConfig::add_filters_to_search_log_object(const char *format_name)
{
  LogObject *obj;

  obj = log_object_manager.find_by_format_name(format_name);

  /* Apply the user defined filters on to newly created LogObject */
  SimpleTokenizer tok(search_log_filters, ',');
  char *filter_name;
  while (filter_name = tok.getNext(), filter_name != 0) {
    LogFilter *f;
    f = global_filter_list.find_by_name(filter_name);
    if (!f) {
      Warning("Filter %s not in the global filter list; cannot add to this LogObject", filter_name);
    } else {
      obj->add_filter(f);
    }
  }

}

//-----------------------------------------------------------------------------
// Create one object for each of the entries in the pre-defined info list
// and apply the specified filter(s) to it.
//
// Normally, only one pre-defined format has been selected on the config file
// so this function creates a single object in such case.
//
// This function adds the pre-defined objects to the global_object_list.
//

LogObject *
LogConfig::create_predefined_object(const PreDefinedFormatInfo * pdi, size_t num_filters,
                                                  LogFilter ** filter, const char *filt_name, bool force_extension)
{
  const char *obj_fname;
  char obj_filt_fname[PATH_NAME_MAX];

  ink_release_assert(pdi != NULL);

  if (filt_name) {
    ink_string_concatenate_strings_n(obj_filt_fname, PATH_NAME_MAX, pdi->filename, "-", filt_name, NULL);
    obj_fname = obj_filt_fname;
  } else {
    obj_fname = pdi->filename;
  }

  if (force_extension) {
    switch (pdi->filefmt) {
      case LOG_FILE_ASCII:
        ink_string_append(obj_filt_fname, (char *)LOG_FILE_ASCII_OBJECT_FILENAME_EXTENSION, PATH_NAME_MAX);
        break;
      case LOG_FILE_BINARY:
        ink_string_append(obj_filt_fname, (char *)LOG_FILE_BINARY_OBJECT_FILENAME_EXTENSION, PATH_NAME_MAX);
        break;
      default:
        break;
    }
  }

  // create object with filters
  //
  LogObject *obj;
  obj = new LogObject(pdi->format, logfile_dir, obj_fname,
                      pdi->filefmt, pdi->header, (Log::RollingEnabledValues)rolling_enabled,
                      collation_preproc_threads, rolling_interval_sec,
                      rolling_offset_hr, rolling_size_mb);

  if (pdi->collatable) {
    if (collation_mode == Log::SEND_STD_FMTS || collation_mode == Log::SEND_STD_AND_NON_XML_CUSTOM_FMTS) {

      LogHost *loghost = new LogHost(obj->get_full_filename(), obj->get_signature());
      ink_assert(loghost != NULL);

      loghost->set_name_port(collation_host, collation_port);
      obj->add_loghost(loghost, false);
    }
  }

  for (size_t i = 0; i < num_filters; ++i) {
    obj->add_filter(filter[i]);
  }

  // give object to object manager
  if (log_object_manager.manage_object(obj) != LogObjectManager::NO_FILENAME_CONFLICTS) {
    delete obj;
    return NULL;
  }

  return obj;
}

void
LogConfig::create_predefined_objects_with_filter(const PreDefinedFormatList & predef, size_t nfilters,
                                                  LogFilter ** filters, const char * filt_name, bool force_extension)
{
  PreDefinedFormatInfo *pdi;

  for (pdi = predef.formats.head; pdi != NULL; pdi = (pdi->link).next) {
    this->create_predefined_object(pdi, nfilters, filters, filt_name, force_extension);
  }
}


//-----------------------------------------------------------------------------
// split_by_protocol
//
// This function creates the objects needed to log different protocols on
// their own file if any of the "separate_xxx_logs" config. variable is set.
//
// Upon return, the pf_list argument holds the filters that reject the
// protocols for which objects have been created. This filter list is used
// to create the rest of the pre defined log objects.
//
// As input, this function requires a list wilh all information regarding
// pre-defined formats.
//
LogFilter *
LogConfig::split_by_protocol(const PreDefinedFormatList & predef)
{
  if (!separate_icp_logs) {
    return NULL;
  }
  // http MUST be last entry
  enum { icp=0,
         http
  };

  int64_t value[] = { LOG_ENTRY_ICP,
                    LOG_ENTRY_HTTP
  };
  const char *name[] = { "icp", "http" };
  const char *filter_name[] = { "__icp__", "__http__" };
  int64_t filter_val[http];    // protocols to reject
  size_t n = 0;

  LogField *etype_field = Log::global_field_list.find_by_symbol("etype");
  ink_assert(etype_field);

  if (separate_icp_logs) {
    if (separate_icp_logs == 1) {
      LogFilter * filter[1];

      filter[0] = new LogFilterInt(filter_name[icp], etype_field, LogFilter::ACCEPT, LogFilter::MATCH, value[icp]);
      create_predefined_objects_with_filter(predef, countof(filter), filter, name[icp]);
      delete filter[0];
    }
    filter_val[n++] = value[icp];
  }

  // At this point, separate objects for all protocols except http
  // have been created if requested. We now add to the argument list
  // the filters needed to reject the protocols that have already
  // been taken care of. Note that we do not test for http since
  // there is no "separate_http_logs" config variable and thus http
  // could not have been taken care of at this point
  //

  if (n > 0) {
    return new LogFilterInt("__reject_protocols__", etype_field, LogFilter::REJECT, LogFilter::MATCH, n, filter_val);
  }
  return NULL;
}

size_t
LogConfig::split_by_hostname(const PreDefinedFormatList & predef, LogFilter * reject_protocol_filter)
{
  size_t n_hosts;
  char **host = read_log_hosts_file(&n_hosts);  // allocates memory for array

  if (n_hosts) {

    size_t num_filt = 0;
    LogFilter *rp_ah[2];        // rejected protocols + accepted host
    LogField *shn_field = Log::global_field_list.find_by_symbol("shn");
    ink_assert(shn_field);

    if (reject_protocol_filter) {
      rp_ah[num_filt++] = reject_protocol_filter;
    }

    for (size_t i = 0; i < n_hosts; ++i) {

      // add a filter that accepts the specified hostname
      //
      char filter_name[LOG_MAX_FORMAT_LINE + 12];
      snprintf(filter_name, LOG_MAX_FORMAT_LINE, "__accept_%s__", host[i]);
      rp_ah[num_filt] = new LogFilterString(filter_name, shn_field, LogFilter::ACCEPT,
                                            LogFilter::CASE_INSENSITIVE_CONTAIN, host[i]);

      create_predefined_objects_with_filter(predef, num_filt + 1, rp_ah, host[i], true);
      delete rp_ah[num_filt];
    }

    LogFilter *rp_rh[2];        // rejected protocols + rejected hosts

    num_filt = 0;

    if (reject_protocol_filter) {
      rp_rh[num_filt++] = reject_protocol_filter;
    }

    rp_rh[num_filt] = new LogFilterString("__reject_hosts__", shn_field, LogFilter::REJECT,
                                          LogFilter::CASE_INSENSITIVE_CONTAIN, n_hosts, host);

    // create the "catch-all" object that contains logs for all
    // hosts other than those specified in the hosts file and for
    // those protocols that do not have their own file
    //
    create_predefined_objects_with_filter(predef, num_filt + 1, rp_rh);
    delete rp_rh[num_filt];

    delete[]host;               // deallocate memory allocated by
    // read_log_hosts_file
  }
  // host is not allocated unless n_hosts > 0
  // coverity[leaked_storage]
  return n_hosts;
}

//-----------------------------------------------------------------------------
// setup_log_objects
//
// Construct:
//
// -All objects necessary to log the pre defined formats considering
//  protocol and host splitting.
// -All custom objects.
//
// Upon return from this function:
// - global_object_list has the aforementioned objects
// - global_format_list has all the pre-determined log formats (e.g. squid,
//   common, etc.)
// - global_filter_list has all custom filters
//   Note that the filters necessary to do log splitting for the pre defined
//   format are kept private (e.g., they do not go to the global_filter_list).
//
void
LogConfig::setup_log_objects()
{
  Debug("log", "creating objects...");

  // ----------------------------------------------------------------------
  // Construct the LogObjects for the pre-defined formats.

  // gather the config information for the pre-defined formats
  //
  PreDefinedFormatList predef;

  predef.init(this);

  // do protocol splitting
  //
  LogFilter *reject_protocol_filter = split_by_protocol(predef);

  // do host splitting
  //
  size_t num_hosts = 0;
  if (separate_host_logs) {
    num_hosts = split_by_hostname(predef, reject_protocol_filter);
  }

  if (num_hosts == 0) {
    // if no host splitting was requested, or if host splitting
    // was not successful (e.g. empty log_hosts.config file) then
    // create the "catch-all" object that contains logs for all
    // protocols that do not have their own file, and for all hosts
    //
    LogFilter *f[1];
    f[0] = reject_protocol_filter;
    create_predefined_objects_with_filter(predef, countof(f), f);
  }

  delete reject_protocol_filter;

  // ----------------------------------------------------------------------
  // Construct the LogObjects for the custom formats

  global_filter_list.clear();

  /*                                                               */
  /* create search Log Objects here.                               */
  /* The following creates the search_one, search_two log objects  */
  /* For search_one log object user defines filters in xml config  */
  /* file. The names of these filters have to be given in          */
  /* records.config file                                           */
  /*                                                               */
  if (search_log_enabled) {
    Debug("log", "creating search log object");
    /* Read xml configuration for search log from memory.            */
    read_xml_log_config(1);
  }

  if (custom_logs_enabled) {
    /* Read xml configuration from logs_xml.config file.             */
    read_xml_log_config(0);
  }

  /*                                                               */
  /* Add the user defined filters to search_one log object.        */
  /*                                                               */
  if (search_log_enabled) {
    if (search_log_filters) {
      add_filters_to_search_log_object("m_search_one");
      add_filters_to_search_log_object("m_search_two");
    }
  }
  // open local pipes so readers can see them
  //
  log_object_manager.open_local_pipes();

  if (is_debug_tag_set("log")) {
    log_object_manager.display();
  }
}

/*-------------------------------------------------------------------------
  LogConfig::reconfigure

  This is the manager callback for any logging config variable change.
  Since we want to access the config variables to build a new config
  object, but can't from this function (big lock technology in the
  manager), we'll just set a flag and call the real reconfiguration
  function from the logging thread.
  -------------------------------------------------------------------------*/

int
LogConfig::reconfigure(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                       RecData /* data ATS_UNUSED */, void * /* cookie ATS_UNUSED */)
{
  Debug("log-config", "Reconfiguration request accepted");
  Log::config->reconfiguration_needed = true;
  return 0;
}

/*-------------------------------------------------------------------------
  LogConfig::register_config_callbacks

  This static function is called by Log::init to register the config update
  function for each of the logging configuration variables.
  -------------------------------------------------------------------------*/

void
LogConfig::register_config_callbacks()
{
  static const char * names[] = {
    "proxy.config.log.log_buffer_size",
    "proxy.config.log.max_secs_per_buffer",
    "proxy.config.log.max_space_mb_for_logs",
    "proxy.config.log.max_space_mb_for_orphan_logs",
    "proxy.config.log.max_space_mb_headroom",
    "proxy.config.log.logfile_perm",
    "proxy.config.log.hostname",
    "proxy.config.log.logfile_dir",
    "proxy.config.log.squid_log_enabled",
    "proxy.config.log.squid_log_is_ascii",
    "proxy.config.log.squid_log_name",
    "proxy.config.log.squid_log_header",
    "proxy.config.log.common_log_enabled",
    "proxy.config.log.common_log_is_ascii",
    "proxy.config.log.common_log_name",
    "proxy.config.log.common_log_header",
    "proxy.config.log.extended_log_enabled",
    "proxy.config.log.extended_log_is_ascii",
    "proxy.config.log.extended_log_name",
    "proxy.config.log.extended_log_header",
    "proxy.config.log.extended2_log_enabled",
    "proxy.config.log.extended2_log_is_ascii",
    "proxy.config.log.extended2_log_name",
    "proxy.config.log.extended2_log_header",
    "proxy.config.log.separate_icp_logs",
    "proxy.config.log.separate_host_logs",
    "proxy.local.log.collation_mode",
    "proxy.config.log.collation_host",
    "proxy.config.log.collation_port",
    "proxy.config.log.collation_host_tagged",
    "proxy.config.log.collation_secret",
    "proxy.config.log.collation_retry_sec",
    "proxy.config.log.collation_max_send_buffers",
    "proxy.config.log.rolling_enabled",
    "proxy.config.log.rolling_interval_sec",
    "proxy.config.log.rolling_offset_hr",
    "proxy.config.log.rolling_size_mb",
    "proxy.config.log.auto_delete_rolled_files",
    "proxy.config.log.custom_logs_enabled",
    "proxy.config.log.xml_config_file",
    "proxy.config.log.hosts_config_file",
    "proxy.config.log.sampling_frequency",
    "proxy.config.log.file_stat_frequency",
    "proxy.config.log.space_used_frequency",
    "proxy.config.log.search_rolling_interval_sec",
    "proxy.config.log.search_log_enabled",
    "proxy.config.log.search_top_sites",
    "proxy.config.log.search_server_ip_addr",
    "proxy.config.log.search_server_port",
    "proxy.config.log.search_url_filter",
  };


  for (unsigned i = 0; i < countof(names); ++i) {
    REC_RegisterConfigUpdateFunc(names[i], &LogConfig::reconfigure, NULL);
  }
}

/*-------------------------------------------------------------------------
  LogConfig::register_stat_callbacks

  This static function is called by Log::init to register the stat update
  function for each of the logging stats variables.
  -------------------------------------------------------------------------*/

void
LogConfig::register_stat_callbacks()
{
  //
  // events
  //
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_error_ok",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_error_ok_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_error_skip",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_error_skip_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_error_aggr",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_error_aggr_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_error_full",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_error_full_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_error_fail",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_error_fail_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_access_ok",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_access_ok_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_access_skip",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_access_skip_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_access_aggr",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_access_aggr_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_access_full",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_access_full_stat, RecRawStatSyncCount);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.event_log_access_fail",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_event_log_access_fail_stat, RecRawStatSyncCount);
  //
  // number vs bytes of logs
  //
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.num_sent_to_network",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_num_sent_to_network_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.num_lost_before_sent_to_network",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_num_lost_before_sent_to_network_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.num_received_from_network",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_num_received_from_network_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.num_flush_to_disk",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_num_flush_to_disk_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.num_lost_before_flush_to_disk",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log_stat_num_lost_before_flush_to_disk_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_lost_before_preproc",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_lost_before_preproc_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_sent_to_network",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_sent_to_network_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_lost_before_sent_to_network",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_lost_before_sent_to_network_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_received_from_network",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_received_from_network_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_flush_to_disk",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_flush_to_disk_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_lost_before_flush_to_disk",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_lost_before_flush_to_disk_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_written_to_disk",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_written_to_disk_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.bytes_lost_before_written_to_disk",
                     RECD_INT, RECP_PERSISTENT, (int) log_stat_bytes_lost_before_written_to_disk_stat, RecRawStatSyncSum);
  //
  // I/O
  //
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.log_files_open",
                     RECD_COUNTER, RECP_NON_PERSISTENT, (int) log_stat_log_files_open_stat, RecRawStatSyncSum);
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log.log_files_space_used",
                     RECD_INT, RECP_NON_PERSISTENT, (int) log_stat_log_files_space_used_stat, RecRawStatSyncSum);
}

/*-------------------------------------------------------------------------
  LogConfig::register_mgmt_callbacks

  This static function is called by Log::init to register the mgmt callback
  function for each of the logging mgmt messages.
  -------------------------------------------------------------------------*/

void
LogConfig::register_mgmt_callbacks()
{
  RecRegisterManagerCb(REC_EVENT_ROLL_LOG_FILES, &LogConfig::reconfigure_mgmt_variables, NULL);
}


/*-------------------------------------------------------------------------
  LogConfig::space_to_write

  This function returns true if there is enough disk space to write the
  given number of bytes, false otherwise.
  -------------------------------------------------------------------------*/

bool LogConfig::space_to_write(int64_t bytes_to_write)
{
  int64_t config_space, partition_headroom;
  int64_t logical_space_used, physical_space_left;
  bool space;

  config_space = (int64_t) get_max_space_mb() * LOG_MEGABYTE;
  partition_headroom = (int64_t) PARTITION_HEADROOM_MB * LOG_MEGABYTE;

  logical_space_used = m_space_used + bytes_to_write;
  physical_space_left = m_partition_space_left - (int64_t) bytes_to_write;

  space = ((logical_space_used<config_space) && (physical_space_left> partition_headroom));

  Debug("logspace", "logical space used %" PRId64 ", configured space %" PRId64
      ", physical space left %" PRId64 ", partition headroom %" PRId64 ", space %s available",
      logical_space_used, config_space, physical_space_left, partition_headroom,
      space ? "is" : "is not");

  return space;
}

/*-------------------------------------------------------------------------
  LogConfig::update_space_used

  Update the m_space_used variable by reading the logging dir and counting
  the total bytes being occupied by files.  If we've used too much space
  (space_used > max_space - headroom) then start deleting some files (if
  auto_delete_rolled_files is set) to make room. Finally, update the
  space_used stat.

  This routine will only be executed SINGLE-THREADED, either by the main
  thread when a LogConfig is initialized, or by the event thread during the
  periodic space check.
  -------------------------------------------------------------------------*/

static int
delete_candidate_compare(const LogDeleteCandidate * a, const LogDeleteCandidate * b)
{
  return ((int) (a->mtime - b->mtime));
}

void
LogConfig::update_space_used()
{
  // no need to update space used if log directory is inaccessible
  //
  if (m_log_directory_inaccessible)
    return;

  static const int MAX_CANDIDATES = 128;
  LogDeleteCandidate candidates[MAX_CANDIDATES];
  int i, victim, candidate_count;
  int64_t total_space_used, partition_space_left;
  char path[MAXPATHLEN];
  int sret;
  struct dirent *result;
  struct stat sbuf;
  DIR *ld;

  // check if logging directory has been specified
  //
  if (!logfile_dir) {
    const char *msg = "Logging directory not specified";
    Error("%s", msg);
    LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, "%s", msg);
    m_log_directory_inaccessible = true;
    return;
  }
  // check if logging directory exists and is searchable readable & writable
  //
  int err;
  do {
    err = access(logfile_dir, R_OK | W_OK | X_OK);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0) {
    const char *msg = "Error accessing logging directory %s: %s.";
    Error(msg, logfile_dir, strerror(errno));
    LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, logfile_dir, strerror(errno));
    m_log_directory_inaccessible = true;
    return;
  }

  ld =::opendir(logfile_dir);
  if (ld == NULL) {
    const char *msg = "Error opening logging directory %s to perform a space check: %s.";
    Error(msg, logfile_dir, strerror(errno));
    LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, logfile_dir, strerror(errno));
    m_log_directory_inaccessible = true;
    return;
  }

  if (!m_dir_entry) {
    long name_max = pathconf(logfile_dir, _PC_NAME_MAX);

    // pathconf should not fail after access and opendir have succeeded
    //
    ink_release_assert(name_max > 0);

    m_dir_entry = (struct dirent *)ats_malloc(sizeof(struct dirent) + name_max + 1);
  }

  total_space_used = 0LL;

  candidate_count = 0;

  while (readdir_r(ld, m_dir_entry, &result) == 0) {

    if (!result) {
      break;
    }

    snprintf(path, MAXPATHLEN, "%s/%s", logfile_dir, m_dir_entry->d_name);

    sret =::stat(path, &sbuf);
    if (sret != -1 && S_ISREG(sbuf.st_mode)) {

      total_space_used += (int64_t) sbuf.st_size;

      if (auto_delete_rolled_files && LogFile::rolled_logfile(m_dir_entry->d_name) && candidate_count < MAX_CANDIDATES) {
        //
        // then add this entry to the candidate list
        //
        candidates[candidate_count].name = ats_strdup(path);
        candidates[candidate_count].size = (int64_t) sbuf.st_size;
        candidates[candidate_count].mtime = sbuf.st_mtime;
        candidate_count++;
      }
    }
  }
  ::closedir(ld);

  //
  // Now check the partition to see if there is enough *actual* space.
  //
  partition_space_left = m_partition_space_left;

  struct statvfs fs;

  if (::statvfs(logfile_dir, &fs) >= 0) {
    partition_space_left = (int64_t) fs.f_bavail * (int64_t) fs.f_bsize;
  }

  //
  // Update the config variables for space used/left
  //
  m_space_used = total_space_used;
  m_partition_space_left = partition_space_left;
  RecSetRawStatSum(log_rsb, log_stat_log_files_space_used_stat, m_space_used);
  RecSetRawStatCount(log_rsb, log_stat_log_files_space_used_stat, 1);

  Debug("logspace", "%" PRId64 " bytes being used for logs", m_space_used);
  Debug("logspace", "%" PRId64 " bytes left on partition", m_partition_space_left);

  //
  // Now that we have an accurate picture of the amount of space being
  // used by logging, we can see if we're running low on space.  If so,
  // we might consider deleting some files that are stored in the
  // candidate array.
  //
  // To delete oldest files first, we'll sort our candidate array by
  // timestamps, making the oldest files first in the array (thus first
  // selected).
  //

  int64_t max_space = (int64_t) get_max_space_mb() * LOG_MEGABYTE;
  int64_t headroom = (int64_t) max_space_mb_headroom * LOG_MEGABYTE;

  if (candidate_count > 0 && !space_to_write(headroom)) {

    Debug("logspace", "headroom reached, trying to clear space ...");
    Debug("logspace", "sorting %d delete candidates ...", candidate_count);
    qsort(candidates, candidate_count, sizeof(LogDeleteCandidate),
          (int (*)(const void *, const void *)) delete_candidate_compare);

    for (victim = 0; victim < candidate_count; victim++) {

      if (space_to_write(headroom + log_buffer_size)) {
        Debug("logspace", "low water mark reached; stop deleting");
        break;
      }

      Debug("logspace", "auto-deleting %s", candidates[victim].name);

      if (unlink(candidates[victim].name) < 0) {
        Note("Traffic Server was Unable to auto-delete rolled "
             "logfile %s: %s.", candidates[victim].name, strerror(errno));
      } else {
        Debug("logspace", "The rolled logfile, %s, was auto-deleted; "
               "%" PRId64 " bytes were reclaimed.", candidates[victim].name, candidates[victim].size);
        m_space_used -= candidates[victim].size;
        m_partition_space_left += candidates[victim].size;
      }
    }
  }
  //
  // Clean up the candidate array
  //
  for (i = 0; i < candidate_count; i++) {
    ats_free(candidates[i].name);
  }

  //
  // Now that we've updated the m_space_used value, see if we need to
  // issue any alarms or warnings about space
  //


  if (!space_to_write(headroom)) {
    if (!logging_space_exhausted)
      Note("Logging space exhausted, any logs writing to local disk will be dropped!");

    logging_space_exhausted = true;
    //
    // Despite our best efforts, we still can't write to the disk.
    // Find out why and set/clear warnings.
    //
    // First, are we out of space based on configuration?
    //
    if (m_space_used >= max_space) {
      if (!m_disk_full) {
        m_disk_full = true;
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, DISK_IS_CONFIG_FULL_MESSAGE);
        Warning(DISK_IS_CONFIG_FULL_MESSAGE);
      }
    }
    //
    // How about out of actual space on the partition?
    //
    else if (m_partition_space_left <= 0) {
      if (!m_partition_full) {
        m_partition_full = true;
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, DISK_IS_ACTUAL_FULL_MESSAGE);
        Warning(DISK_IS_ACTUAL_FULL_MESSAGE);
      }
    }
    //
    // How about being within the headroom limit?
    //
    else if (m_space_used + headroom >= max_space) {
      if (!m_disk_low) {
        m_disk_low = true;
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, DISK_IS_CONFIG_LOW_MESSAGE);
        Warning(DISK_IS_CONFIG_LOW_MESSAGE);
      }
    } else {
      if (!m_partition_low) {
        m_partition_low = true;
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, DISK_IS_ACTUAL_LOW_MESSAGE);
        Warning(DISK_IS_ACTUAL_LOW_MESSAGE);
      }
    }
  } else {
    //
    // We have enough space to log again; clear any previous messages
    //
    if (logging_space_exhausted)
      Note("Logging space is no longer exhausted.");

    logging_space_exhausted = false;
    if (m_disk_full || m_partition_full) {
      Note("Logging disk is no longer full; access logging to local log directory resumed.");
      m_disk_full = false;
      m_partition_full = false;
    }
    if (m_disk_low || m_partition_low) {
      Note("Logging disk is no longer low; access logging to local log directory resumed.");
      m_disk_low = false;
      m_partition_low = false;
    }
  }
}


/*-------------------------------------------------------------------------
  LogConfig::read_xml_log_config

  This is a new routine for reading the XML-based log config file.
  -------------------------------------------------------------------------*/

static char xml_config_buffer[] = "<LogFilter> \
                                  <Name = \"reject_gif\"/> \
                                  <Action = \"REJECT\"/> \
                                  <Condition = \"cqup CASE_INSENSITIVE_CONTAIN .gif\"/> \
                                  </LogFilter>\
                                              \
                                  <LogFilter> \
                                  <Name = \"reject_jpg\"/> \
                                  <Action = \"REJECT\"/> \
                                  <Condition = \"cqup CASE_INSENSITIVE_CONTAIN .jpg\"/> \
                                  </LogFilter>\
                                              \
                                  <LogFilter> \
                                  <Name      = \"only_cache_miss\"/> \
                                  <Action    = \"ACCEPT\"/> \
                                  <Condition = \"crc MATCH TCP_MISS\"/> \
                                  </LogFilter> \
                                              \
                                  <LogFormat> \
                                  <Name = \"m_search_one\"/> \
                                  <Format = \"%%<cquc> %%<cqtt>\"/> \
                                  </LogFormat> \
                                               \
                                  <LogFormat> \
                                  <Name = \"m_search_two\"/> \
                                  <Format = \"%%<cquc> %%<crc>\"/> \
                                  </LogFormat> \
                                               \
                                  <LogObject> \
                                  <Filename = \"search_log1\"/> \
                                  <Format = \"m_search_one\"/> \
                                  <Filters = \"reject_gif\", \"reject_jpg\"/> \
                                  <RollingIntervalSec = \"%d\"/> \
                                  </LogObject> \
                                               \
                                  <LogObject> \
                                  <Filename = \"search_log2\"/> \
                                  <Format = \"m_search_two\"/> \
                                  <Filters = \"reject_gif\", \"reject_jpg\", \"only_cache_miss\"/> \
                                  <RollingIntervalSec = \"%d\"/> \
                                  </LogObject>";
void
LogConfig::read_xml_log_config(int from_memory)
{
  ats_scoped_str config_path;

  if (!from_memory) {
    config_path = RecConfigReadConfigPath("proxy.config.log.xml_config_file", "logs_xml.config");
  }

  InkXmlConfigFile log_config(config_path ? (const char *)config_path : "memory://builtin");

  if (!from_memory) {
    Debug("log-config", "Reading log config file %s", (const char *)config_path);
    Debug("xml", "%s is an XML-based config file", (const char *)config_path);

    if (log_config.parse() < 0) {
      Note("Error parsing log config file %s; ensure that it is XML-based", (const char *)config_path);
      return;
    }

    if (is_debug_tag_set("xml")) {
      log_config.display();
    }
  } else {
    int filedes[2];
    int nbytes = sizeof(xml_config_buffer);
    const size_t ptr_size = nbytes + 20;
    char *ptr = (char *)ats_malloc(ptr_size);

    if (pipe(filedes) != 0) {
      Note("xml parsing: Error in Opening a pipe");
      ats_free(ptr);
      return;
    }

    snprintf(ptr, ptr_size, xml_config_buffer, search_rolling_interval_sec, search_rolling_interval_sec);
    nbytes = strlen(ptr);
    if (write(filedes[1], ptr, nbytes) != nbytes) {
      Note("Error in writing to pipe.");
      ats_free(ptr);
      close(filedes[1]);
      close(filedes[0]);
      return;
    }
    ats_free(ptr);
    close(filedes[1]);

    if (log_config.parse(filedes[0]) < 0) {
      Note("Error parsing log config info from memory.");
      return;
    }
  }


  //
  // At this point, the XMl file has been parsed into a list of
  // InkXmlObjects.  We'll loop through them and add the information to
  // our current configuration.  Expected object names include:
  //
  //     LogFormat
  //     LogFilter
  //     LogObject
  //

  InkXmlObject *xobj;
  InkXmlAttr *xattr;

  for (xobj = log_config.first(); xobj; xobj = log_config.next(xobj)) {

    Debug("xml", "XmlObject: %s", xobj->object_name());

    if (strcmp(xobj->object_name(), "LogFormat") == 0) {

      //
      // Manditory attributes: Name, Format
      // Optional attributes : Interval (for aggregate operators)
      //
      NameList name;
      NameList format;
      NameList interval;

      for (xattr = xobj->first(); xattr; xattr = xobj->next(xattr)) {
        Debug("xml", "XmlAttr  : <%s,%s>", xattr->tag(), xattr->value());

        if (strcasecmp(xattr->tag(), "Name") == 0) {
          name.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Format") == 0) {
          format.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Interval") == 0) {
          interval.enqueue(xattr->value());
        } else {
          Note("Unknown attribute %s for %s; ignoring", xattr->tag(), xobj->object_name());
        }
      }

      // check integrity constraints
      //
      if (name.count() == 0) {
        Note("'Name' attribute missing for LogFormat object");
        continue;
      }
      if (format.count() == 0) {
        Note("'Format' attribute missing for LogFormat object");
        continue;
      }
      if (name.count() > 1) {
        Note("Multiple values for 'Name' attribute in %s; using the first one", xobj->object_name());
      }
      if (format.count() > 1) {
        Note("Multiple values for 'Format' attribute in %s; using the first one", xobj->object_name());
      }

      char *format_str = format.dequeue();
      char *name_str = name.dequeue();
      unsigned interval_num = 0;

      // if the format_str contains any of the aggregate operators,
      // we need to ensure that an interval was specified.
      //
      if (LogField::fieldlist_contains_aggregates(format_str)) {
        if (interval.count() == 0) {
          Note("'Interval' attribute missing for LogFormat object"
               " %s that contains aggregate operators: %s", name_str, format_str);
          continue;
        } else if (interval.count() > 1) {
          Note("Multiple values for 'Interval' attribute in %s; using the first one", xobj->object_name());
        }
        // interval
        //
        interval_num = ink_atoui(interval.dequeue());
      } else if (interval.count() > 0) {
        Note("Format %s has no aggregates, ignoring 'Interval' attribute.", name_str);
      }
      // create new format object and place onto global list
      //
      LogFormat *fmt = new LogFormat(name_str, format_str, interval_num);

      ink_assert(fmt != NULL);
      if (fmt->valid()) {
        global_format_list.add(fmt, false);

        if (is_debug_tag_set("xml")) {
          printf("The following format was added to the global format list\n");
          fmt->displayAsXML(stdout);
        }
      } else {
        Note("Format named \"%s\" will not be active; not a valid format", fmt->name()? fmt->name() : "");
        delete fmt;
      }
    }

    else if (strcmp(xobj->object_name(), "LogFilter") == 0) {
      int i;

      // Mandatory attributes: Name, Action, Condition
      // Optional attributes : none
      //
      NameList name;
      NameList condition;
      NameList action;

      for (xattr = xobj->first(); xattr; xattr = xobj->next(xattr)) {
        Debug("xml", "XmlAttr  : <%s,%s>", xattr->tag(), xattr->value());

        if (strcasecmp(xattr->tag(), "Name") == 0) {
          name.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Action") == 0) {
          action.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Condition") == 0) {
          condition.enqueue(xattr->value());
        } else {
          Note("Unknown attribute %s for %s; ignoring", xattr->tag(), xobj->object_name());
        }
      }

      // check integrity constraints
      //
      if (name.count() == 0) {
        Note("'Name' attribute missing for LogFilter object");
        continue;
      }
      if (action.count() == 0) {
        Note("'Action' attribute missing for LogFilter object");
        continue;
      }
      if (condition.count() == 0) {
        Note("'Condition' attribute missing for LogFilter object");
        continue;
      }
      if (name.count() > 1) {
        Note("Multiple values for 'Name' attribute in %s; using the first one", xobj->object_name());
      }
      if (action.count() > 1) {
        Note("Multiple values for 'Action' attribute in %s; using the first one", xobj->object_name());
      }
      if (condition.count() > 1) {
        Note("Multiple values for 'Condition' attribute in %s; using the first one", xobj->object_name());
      }

      char *filter_name = name.dequeue();

      // convert the action string to an enum value and validate it
      //
      char *action_str = action.dequeue();
      LogFilter::Action act = LogFilter::REJECT;        /* lv: make gcc happy */
      for (i = 0; i < LogFilter::N_ACTIONS; i++) {
        if (strcasecmp(action_str, LogFilter::ACTION_NAME[i]) == 0) {
          act = (LogFilter::Action) i;
          break;
        }
      }

      if (i == LogFilter::N_ACTIONS) {
        Warning("%s is not a valid filter action value; cannot create filter %s.", action_str, filter_name);
        continue;
      }
      // parse condition string and validate its fields
      //
      char *cond_str = condition.dequeue();

      SimpleTokenizer tok(cond_str);

      if (tok.getNumTokensRemaining() < 3) {
        Warning("Invalid condition syntax \"%s\"; cannot create filter %s.", cond_str, filter_name);
        continue;
      }

      char *field_str = tok.getNext();
      char *oper_str = tok.getNext();
      char *val_str = tok.getRest();

      // validate field symbol
      //
      if (strlen(field_str) > 2 && field_str[0] == '%' && field_str[1] == '<') {
        Debug("xml", "Field symbol has <> form: %s", field_str);
        char *end = field_str;
        while (*end && *end != '>')
          end++;
        *end = '\0';
        field_str += 2;
        Debug("xml", "... now field symbol is %s", field_str);
      }

      LogField *logfield = Log::global_field_list.find_by_symbol(field_str);
      if (!logfield) {
        // check for container fields
        if (*field_str == '{') {
          Note("%s appears to be a container field", field_str);
          char *fname_end = strchr(field_str, '}');
          if (NULL != fname_end) {
            char *fname = field_str + 1;
            *fname_end = 0;          // changes '}' to '\0'
            char *cname = fname_end + 1;     // start of container symbol
            Note("Found Container Field: Name = %s, symbol = %s", fname, cname);
            LogField::Container container = LogField::valid_container_name(cname);
            if (container == LogField::NO_CONTAINER) {
              Warning("%s is not a valid container; cannot create filter %s.", cname, filter_name);
              continue;
            } else {
              logfield = new LogField(fname, container);
              ink_assert(logfield != NULL);
            }
          } else {
            Warning("Invalid container field specification: no trailing '}' in %s cannot create filter %s.", field_str, filter_name);
            continue;
          }
        }
      }

      if (!logfield) {
        Warning("%s is not a valid field; cannot create filter %s.", field_str, filter_name);
        continue;
      }
      // convert the operator string to an enum value and validate it
      //
      LogFilter::Operator oper = LogFilter::MATCH;
      for (i = 0; i < LogFilter::N_OPERATORS; ++i) {
        if (strcasecmp(oper_str, LogFilter::OPERATOR_NAME[i]) == 0) {
          oper = (LogFilter::Operator) i;
          break;
        }
      }

      if (i == LogFilter::N_OPERATORS) {
        Warning("%s is not a valid operator; cannot create filter %s.", oper_str, filter_name);
        continue;
      }
      // now create the correct LogFilter
      //
      LogFilter *filter = NULL;
      LogField::Type field_type = logfield->type();

      switch (field_type) {

      case LogField::sINT:

        filter = new LogFilterInt(filter_name, logfield, act, oper, val_str);
        break;

      case LogField::dINT:

        Warning("Internal error: invalid field type (double int); cannot create filter %s.", filter_name);
        continue;

      case LogField::STRING:

        filter = new LogFilterString(filter_name, logfield, act, oper, val_str);
        break;

      case LogField::IP:

        filter = new LogFilterIP(filter_name, logfield, act, oper, val_str);
        break;

      default:

        Warning("Internal error: unknown field type %d; cannot create filter %s.", field_type, filter_name);
        continue;
      }

      ink_assert(filter);

      if (filter->get_num_values() == 0) {

        Warning("\"%s\" does not specify any valid values; cannot create filter %s.", val_str, filter_name);
        delete filter;

      } else {

        // add filter to global filter list
        //
        global_filter_list.add(filter, false);

        if (is_debug_tag_set("xml")) {
          printf("The following filter was added to the global filter list\n");
          filter->display_as_XML();
        }
      }
    }

    else if (strcasecmp(xobj->object_name(), "LogObject") == 0) {

      NameList format;          // mandatory
      NameList filename;        // mandatory
      NameList mode;
      NameList filters;
      NameList protocols;
      NameList serverHosts;
      NameList collationHosts;
      NameList header;
      NameList rollingEnabled;
      NameList rollingIntervalSec;
      NameList rollingOffsetHr;
      NameList rollingSizeMb;

      for (xattr = xobj->first(); xattr; xattr = xobj->next(xattr)) {
        Debug("xml", "XmlAttr  : <%s,%s>", xattr->tag(), xattr->value());
        if (strcasecmp(xattr->tag(), "Format") == 0) {
          format.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Filename") == 0) {
          filename.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Mode") == 0) {
          mode.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Filters") == 0) {
          filters.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Protocols") == 0) {
          protocols.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "ServerHosts") == 0) {
          serverHosts.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "CollationHosts") == 0) {
          collationHosts.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "Header") == 0) {
          header.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "RollingEnabled") == 0) {
          rollingEnabled.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "RollingIntervalSec") == 0) {
          rollingIntervalSec.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "RollingOffsetHr") == 0) {
          rollingOffsetHr.enqueue(xattr->value());
        } else if (strcasecmp(xattr->tag(), "RollingSizeMb") == 0) {
          rollingSizeMb.enqueue(xattr->value());
        } else {
          Note("Unknown attribute %s for %s; ignoring", xattr->tag(), xobj->object_name());
        }
      }

      // check integrity constraints
      //
      if (format.count() == 0) {
        Note("'Format' attribute missing for LogObject object");
        continue;
      }
      if (filename.count() == 0) {
        Note("'Filename' attribute missing for LogObject object");
        continue;
      }

      if (format.count() > 1) {
        Note("Multiple values for 'Format' attribute in %s; using the first one", xobj->object_name());
      }
      if (filename.count() > 1) {
        Note("Multiple values for 'Filename' attribute in %s; using the first one", xobj->object_name());
      }
      if (mode.count() > 1) {
        Note("Multiple values for 'Mode' attribute in %s; using the first one", xobj->object_name());
      }
      if (filters.count() > 1) {
        Note("Multiple values for 'Filters' attribute in %s; using the first one", xobj->object_name());
      }
      if (protocols.count() > 1) {
        Note("Multiple values for 'Protocols' attribute in %s; using the first one", xobj->object_name());
      }
      if (serverHosts.count() > 1) {
        Note("Multiple values for 'ServerHosts' attribute in %s; using the first one", xobj->object_name());
      }
      if (collationHosts.count() > 1) {
        Note("Multiple values for 'CollationHosts' attribute in %s; using the first one", xobj->object_name());
      }
      if (header.count() > 1) {
        Note("Multiple values for 'Header' attribute in %s; using the first one", xobj->object_name());
      }
      if (rollingEnabled.count() > 1) {
        Note("Multiple values for 'RollingEnabled' attribute in %s; using the first one", xobj->object_name());
      }
      if (rollingIntervalSec.count() > 1) {
        Note("Multiple values for 'RollingIntervalSec' attribute in %s; using the first one", xobj->object_name());
      }
      if (rollingOffsetHr.count() > 1) {
        Note("Multiple values for 'RollingOffsetHr' attribute in %s; using the first one", xobj->object_name());
      }
      if (rollingSizeMb.count() > 1) {
        Note("Multiple values for 'RollingSizeMb' attribute in %s; using the first one", xobj->object_name());
      }
      // create new LogObject and start adding to it
      //

      char *fmt_name = format.dequeue();
      LogFormat *fmt = global_format_list.find_by_name(fmt_name);
      if (!fmt) {
        Warning("Format %s not in the global format list; cannot create LogObject", fmt_name);
        continue;
      }
      // file format
      //
      LogFileFormat file_type = LOG_FILE_ASCII;      // default value
      if (mode.count()) {
        char *mode_str = mode.dequeue();
        file_type = (strncasecmp(mode_str, "bin", 3) == 0 ||
                     (mode_str[0] == 'b' && mode_str[1] == 0) ?
                     LOG_FILE_BINARY : (strcasecmp(mode_str, "ascii_pipe") == 0 ? LOG_FILE_PIPE : LOG_FILE_ASCII));
      }
      // rolling
      //
      char *rollingEnabled_str = rollingEnabled.dequeue();
      int obj_rolling_enabled = rollingEnabled_str ? ink_atoui(rollingEnabled_str) : rolling_enabled;

      char *rollingIntervalSec_str = rollingIntervalSec.dequeue();
      int obj_rolling_interval_sec = rollingIntervalSec_str ? ink_atoui(rollingIntervalSec_str) : rolling_interval_sec;

      char *rollingOffsetHr_str = rollingOffsetHr.dequeue();
      int obj_rolling_offset_hr = rollingOffsetHr_str ? ink_atoui(rollingOffsetHr_str) : rolling_offset_hr;

      char *rollingSizeMb_str = rollingSizeMb.dequeue();
      int obj_rolling_size_mb = rollingSizeMb_str ? ink_atoui(rollingSizeMb_str) : rolling_size_mb;

      if (!LogRollingEnabledIsValid(obj_rolling_enabled)) {
        Warning("Invalid log rolling value '%d' in log object %s", obj_rolling_enabled, xobj->object_name());
      }

      // create the new object
      //
      LogObject *obj = new LogObject(fmt, logfile_dir,
                                     filename.dequeue(),
                                     file_type,
                                     header.dequeue(),
                                     (Log::RollingEnabledValues)obj_rolling_enabled,
                                     collation_preproc_threads,
                                     obj_rolling_interval_sec,
                                     obj_rolling_offset_hr,
                                     obj_rolling_size_mb);

      // filters
      //
      char *filters_str = filters.dequeue();
      if (filters_str) {
        SimpleTokenizer tok(filters_str, ',');
        char *filter_name;
        while (filter_name = tok.getNext(), filter_name != 0) {
          LogFilter *f;
          f = global_filter_list.find_by_name(filter_name);
          if (!f) {
            Warning("Filter %s not in the global filter list; cannot add to this LogObject", filter_name);
          } else {
            obj->add_filter(f);
          }
        }
      }
      // protocols
      //
      char *protocols_str = protocols.dequeue();
      if (protocols_str) {

        LogField *etype_field = Log::global_field_list.find_by_symbol("etype");
        ink_assert(etype_field);

        SimpleTokenizer tok(protocols_str, ',');
        size_t n = tok.getNumTokensRemaining();

        if (n) {
          int64_t *val_array = new int64_t[n];
          size_t numValid = 0;
          char *t;
          while (t = tok.getNext(), t != NULL) {
            if (strcasecmp(t, "icp") == 0) {
              val_array[numValid++] = LOG_ENTRY_ICP;
            } else if (strcasecmp(t, "http") == 0) {
              val_array[numValid++] = LOG_ENTRY_HTTP;
            }
          }

          if (numValid == 0) {
            Warning("No valid protocol value(s) (%s) for Protocol "
                    "field in definition of XML LogObject.\nObject will log all protocols.", protocols_str);
          } else {
            if (numValid < n) {
              Warning("There are invalid protocol values (%s) in"
                      " the Protocol field of XML LogObject.\n"
                      "Only %zu out of %zu values will be used.", protocols_str, numValid, n);
            }

            LogFilterInt protocol_filter("__xml_protocol__",
                                         etype_field, LogFilter::ACCEPT, LogFilter::MATCH, numValid, val_array);
            obj->add_filter(&protocol_filter);
          }
          delete[] val_array;
        } else {
          Warning("No value(s) in Protocol field of XML object, object will log all protocols.");
        }
      }
      // server hosts
      //
      char *serverHosts_str = serverHosts.dequeue();
      if (serverHosts_str) {

        LogField *shn_field = Log::global_field_list.find_by_symbol("shn");
        ink_assert(shn_field);

        LogFilterString server_host_filter("__xml_server_hosts__",
                                           shn_field, LogFilter::ACCEPT, LogFilter::CASE_INSENSITIVE_CONTAIN, serverHosts_str);

        if (server_host_filter.get_num_values() == 0) {
          Warning("No valid server host value(s) (%s) for Protocol "
                  "field in definition of XML LogObject.\nObject will log all servers.", serverHosts_str);
        } else {
          obj->add_filter(&server_host_filter);
        }
      }
      // collation hosts
      //
      char *collationHosts_str = collationHosts.dequeue();
      if (collationHosts_str) {
        char *host;
        SimpleTokenizer tok(collationHosts_str, ',');
        while (host = tok.getNext(), host != 0) {

          LogHost *prev = NULL;
          char *failover_str;
          SimpleTokenizer failover_tok(host, '|'); // split failover hosts

          while (failover_str = failover_tok.getNext(), failover_str != 0) {
            LogHost *lh = new LogHost(obj->get_full_filename(), obj->get_signature());

            if (lh->set_name_or_ipstr(failover_str)) {
              Warning("Could not set \"%s\" as collation host", host);
              delete lh;
            } else if (!prev){
              obj->add_loghost(lh, false);
              prev = lh;
            } else {
              prev->failover_link.next = lh;
              prev = lh;
            }
          }
        }
      }
      // now the object is complete; give it to the object manager
      //
      log_object_manager.manage_object(obj);
    }

    else {
      Note("Unknown XML config object for logging: %s", xobj->object_name());
    }
  }
}

/*-------------------------------------------------------------------------
  LogConfig::read_log_hosts_file

  This routine will read the log_hosts.config file to build a set of
  filters for splitting logs based on hostname fields that match the
  entries in this file.
  -------------------------------------------------------------------------*/

char **
LogConfig::read_log_hosts_file(size_t * num_hosts)
{
  ats_scoped_str config_path(RecConfigReadConfigPath("proxy.config.log.hosts_config_file", "log_hosts.config"));
  char line[LOG_MAX_FORMAT_LINE];
  char **hosts = NULL;

  Debug("log-config", "Reading log hosts from %s", (const char *)config_path);

  size_t nhosts = 0;
  int fd = open(config_path, O_RDONLY);
  if (fd < 0) {
    Warning("Traffic Server can't open %s for reading log hosts for splitting: %s.", (const char *)config_path, strerror(errno));
  } else {
    //
    // First, count the number of hosts in the file
    //
    while (ink_file_fd_readline(fd, LOG_MAX_FORMAT_LINE, line) > 0) {
      //
      // Ignore blank Lines and lines that begin with a '#'.
      //
      if (*line == '\n' || *line == '#') {
        continue;
      }
      ++nhosts;
    }
    //
    // Now read the hosts from the file and set-up the array entries.
    //
    if (nhosts) {
      if (lseek(fd, 0, SEEK_SET) != 0) {
        Warning("lseek failed on file %s: %s", (const char *)config_path, strerror(errno));
        nhosts = 0;
      } else {
        hosts = new char *[nhosts];
        ink_assert(hosts != NULL);

        size_t i = 0;
        while (ink_file_fd_readline(fd, LOG_MAX_FORMAT_LINE, line) > 0) {
          //
          // Ignore blank Lines and lines that begin with a '#'.
          //
          if (*line == '\n' || *line == '#') {
            continue;
          }
          LogUtils::strip_trailing_newline(line);
          hosts[i] = ats_strdup(line);
          ++i;
        }
        ink_assert(i == nhosts);
      }
    }
    close(fd);
  }
  *num_hosts = nhosts;
  return hosts;
}
