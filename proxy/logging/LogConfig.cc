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

/*************************************************************************** 
 LogConfig.cc

 This file implements the LogConfig object.
 
 ***************************************************************************/
#include "inktomi++.h"

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#include <dirent.h>

#if (HOST_OS == linux)
#include <sys/statfs.h>
#elif (HOST_OS == solaris)
#include <sys/statfs.h>
#include <sys/statvfs.h>
#elif (HOST_OS != freebsd)
#include <sys/statvfs.h>
#endif  // linux 

#include "ink_platform.h"

#include "Main.h"
#include "List.h"
#include "InkXml.h"

#include "LogFormatType.h"
#include "LogLimits.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogFile.h"
#include "LogBuffer.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogUtils.h"
#include "Log.h"
#include "SimpleTokenizer.h"

#if defined(IOCORE_LOG_COLLATION)
#include "LogCollationAccept.h"
#endif

#if (HOST_OS == linux)
#include <sys/vfs.h>
#else
extern "C"
{
  int statvfs(const char *, struct statvfs *);
};
#endif

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

char *ink_prepare_dir(char *logfile_dir);

void
LogConfig::setup_default_values()
{
  const unsigned int bufSize = 512;
  char name[bufSize];
  if (!gethostname(name, bufSize)) {
    ink_strncpy(name, "unknown_host_name", sizeof(name));
  }
  hostname = xstrdup(name);

  log_buffer_size = (int) (10 * LOG_KILOBYTE);
  max_entries_per_buffer = 100;
  max_secs_per_buffer = 5;
  max_space_mb_for_logs = 100;
  max_space_mb_for_orphan_logs = 25;
  max_space_mb_headroom = 10;
  logfile_perm = 0644;
  logfile_dir = xstrdup(".");

  separate_icp_logs = 1;
  separate_mixt_logs = -1;
  separate_host_logs = FALSE;

  squid_log_enabled = TRUE;
  xuid_logging_enabled = TRUE;
  squid_log_is_ascii = TRUE;
  squid_log_name = xstrdup("squid");
  squid_log_header = NULL;

  common_log_enabled = FALSE;
  common_log_is_ascii = TRUE;
  common_log_name = xstrdup("common");
  common_log_header = NULL;

  extended_log_enabled = FALSE;
  extended_log_is_ascii = TRUE;
  extended_log_name = xstrdup("extended");
  extended_log_header = NULL;

  extended2_log_enabled = FALSE;
  extended2_log_is_ascii = TRUE;
  extended2_log_name = xstrdup("extended2");
  extended2_log_header = NULL;

  collation_mode = NO_COLLATION;
  collation_host = xstrdup("none");
  collation_port = 0;
  collation_host_tagged = FALSE;
  collation_secret = xstrdup("foobar");
  collation_retry_sec = 0;
  collation_max_send_buffers = 0;

  rolling_enabled = NO_ROLLING;
  rolling_interval_sec = 86400; // 24 hours
  rolling_offset_hr = 0;
  rolling_size_mb = 10;
  auto_delete_rolled_files = TRUE;
  roll_log_files_now = FALSE;

  custom_logs_enabled = FALSE;
  xml_logs_config = FALSE;
  config_file = xstrdup("logs.config");
  xml_config_file = xstrdup("logs_xml.config");
  hosts_config_file = xstrdup("log_hosts.config");

/* The default values for the search log                         */

  search_log_enabled = FALSE;
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
  search_log_file_one = xstrdup("search_log1");
/* Logging system captures only cache miss URLs to this log file.*/
  search_log_file_two = xstrdup("search_log2");

  sampling_frequency = 1;
  file_stat_frequency = 16;
  space_used_frequency = 900;

  use_orphan_log_space_value = false;

  ascii_buffer_size = 4 * 9216;
  max_line_size = 9216;         // size of pipe buffer for SunOS 5.6
  overspill_report_count = 500;
}

void *
LogConfig::reconfigure_mgmt_variables(void *token, char *data_raw, int data_len)
{
  Note("[TrafficServer:LogConfig] : Roll_Log_Files event received. rolling now");
  Log::config->roll_log_files_now = true;
  return NULL;
}

void
LogConfig::read_configuration_variables()
{
  int val;
  char *ptr;
  struct stat s;
  int err;

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.log_buffer_size");
  if (val > 0) {
    log_buffer_size = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.max_entries_per_buffer");
  if (val > 0) {
    max_entries_per_buffer = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.max_secs_per_buffer");
  if (val > 0) {
    max_secs_per_buffer = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.max_space_mb_for_logs");
  if (val > 0) {
    max_space_mb_for_logs = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.max_space_mb_for_" "orphan_logs");
  if (val > 0) {
    max_space_mb_for_orphan_logs = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.max_space_mb_headroom");
  if (val > 0) {
    max_space_mb_headroom = val;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.logfile_perm");
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
    xfree(ptr);
  }

  ptr = LOG_ConfigReadString("proxy.config.log2.hostname");
  if (ptr != NULL) {
    xfree(hostname);
    hostname = ptr;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.logfile_dir");
  if (ptr != NULL) {
    xfree(logfile_dir);
    logfile_dir = ptr;
    if ((err = stat(logfile_dir, &s)) < 0) {
      xfree(logfile_dir);
      logfile_dir = NULL;
      if ((err = stat(system_log_dir, &s)) < 0) {
        // Try 'system_root_dir/var/log/trafficserver' directory
        snprintf(system_log_dir, sizeof(system_log_dir), "%s%s%s%s%s%s%s",
                 system_root_dir, DIR_SEP,"var",DIR_SEP,"log",DIR_SEP,"trafficserver");
        if ((err = stat(system_log_dir, &s)) < 0) {
          fprintf(stderr,"unable to stat() log dir'%s': %d %d, %s\n", 
                  system_log_dir, err, errno, strerror(errno));
          fprintf(stderr,"please set 'proxy.config.log2.logfile_dir'\n");
          _exit(1);
        } 

      } 
      logfile_dir = xstrdup(system_log_dir);
    }
  }


  //
  // for each predefined logging format, we need to know:
  //   - whether logging in that format is enabled
  //   - if we're logging to a file, what the name and format (ASCII,
  //     BINARY) are
  //   - what header should be written down at the start of each file for
  //     this format
  // this is accomplished with four config variables per format:
  //   "proxy.config.log2.<format>_log_enabled" INT
  //   "proxy.config.log2.<format>_log_is_ascii" INT
  //   "proxy.config.log2.<format>_log_name" STRING
  //   "proxy.config.log2.<format>_log_header" STRING
  //


  // SQUID
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.squid_log_enabled");
  if (val == 0 || val == 1) {
    squid_log_enabled = val;
  };

  // X-UID logging enabled.
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.xuid_logging_enabledq");
  if (val == 0 || val == 1) {
    xuid_logging_enabled = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.squid_log_is_ascii");
  if (val == 0 || val == 1) {
    squid_log_is_ascii = val;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.squid_log_name");
  if (ptr != NULL) {
    xfree(squid_log_name);
    squid_log_name = ptr;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.squid_log_header");
  if (ptr != NULL) {
    xfree(squid_log_header);
    squid_log_header = ptr;
  };

  // COMMON
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.common_log_enabled");
  if (val == 0 || val == 1) {
    common_log_enabled = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.common_log_is_ascii");
  if (val == 0 || val == 1) {
    common_log_is_ascii = val;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.common_log_name");
  if (ptr != NULL) {
    xfree(common_log_name);
    common_log_name = ptr;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.common_log_header");
  if (ptr != NULL) {
    xfree(common_log_header);
    common_log_header = ptr;
  };

  // EXTENDED
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.extended_log_enabled");
  if (val == 0 || val == 1) {
    extended_log_enabled = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.extended_log_is_ascii");
  if (val == 0 || val == 1) {
    extended_log_is_ascii = val;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.extended_log_name");
  if (ptr != NULL) {
    xfree(extended_log_name);
    extended_log_name = ptr;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.extended_log_header");
  if (ptr != NULL) {
    xfree(extended_log_header);
    extended_log_header = ptr;
  };

  // EXTENDED2
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.extended2_log_enabled");
  if (val == 0 || val == 1) {
    extended2_log_enabled = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.extended2_log_is_ascii");
  if (val == 0 || val == 1) {
    extended2_log_is_ascii = val;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.extended2_log_name");
  if (ptr != NULL) {
    xfree(extended2_log_name);
    extended2_log_name = ptr;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.extended2_log_header");
  if (ptr != NULL) {
    xfree(extended2_log_header);
    extended2_log_header = ptr;
  };


  // SPLITTING
  // 0 means no splitting
  // 1 means splitting
  // for icp and mixt 
  //   -1 means filter out (do not log and do not create split file)
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.separate_icp_logs");
  if (val == 0 || val == 1 || val == -1) {
    separate_icp_logs = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.separate_mixt_logs");
  if (val == 0 || val == 1 || val == -1) {
    separate_mixt_logs = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.separate_host_logs");
  if (val == 0 || val == 1) {
    separate_host_logs = val;
  };


  // COLLATION
  val = (int) LOG_LocalReadInteger("proxy.local.log2.collation_mode");
  // do not restrict value so that error message is logged if 
  // collation_mode is out of range
  collation_mode = val;

  ptr = LOG_ConfigReadString("proxy.config.log2.collation_host");
  if (ptr != NULL) {
    xfree(collation_host);
    collation_host = ptr;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.collation_port");
  if (val >= 0) {
    collation_port = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.collation_host_tagged");
  if (val == 0 || val == 1) {
    collation_host_tagged = val;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.collation_secret");
  if (ptr != NULL) {
    xfree(collation_secret);
    collation_secret = ptr;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.collation_retry_sec");
  if (val >= 0) {
    collation_retry_sec = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.collation_max_send_buffers");
  if (val >= 0) {
    collation_max_send_buffers = val;
  };


  // ROLLING

  // we don't check for valid values of rolling_enabled, rolling_interval_sec,
  // rolling_offset_hr, or rolling_size_mb because the LogObject takes care of this
  //
  rolling_enabled = (int) LOG_ConfigReadInteger("proxy.config.log2.rolling_enabled");

  rolling_interval_sec = (int) LOG_ConfigReadInteger("proxy.config.log2.rolling_interval_sec");

  rolling_offset_hr = (int) LOG_ConfigReadInteger("proxy.config.log2.rolling_offset_hr");

  rolling_size_mb = (int) LOG_ConfigReadInteger("proxy.config.log2.rolling_size_mb");

  val = (int) LOG_ConfigReadInteger("proxy.config.log2." "auto_delete_rolled_files");
  if (val == 0 || val == 1) {
    auto_delete_rolled_files = val;
  };

  // CUSTOM LOGGING
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.custom_logs_enabled");
  if (val == 0 || val == 1) {
    custom_logs_enabled = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.xml_logs_config");
  if (val == 0 || val == 1) {
    xml_logs_config = val;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.config_file");
  if (ptr != NULL) {
    xfree(config_file);
    config_file = ptr;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.xml_config_file");
  if (ptr != NULL) {
    xfree(xml_config_file);
    xml_config_file = ptr;
  };

  ptr = LOG_ConfigReadString("proxy.config.log2.hosts_config_file");
  if (ptr != NULL) {
    xfree(hosts_config_file);
    hosts_config_file = ptr;
  };

  // PERFORMANCE
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.sampling_frequency");
  if (val > 0) {
    sampling_frequency = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.file_stat_frequency");
  if (val > 0) {
    file_stat_frequency = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.space_used_frequency");
  if (val > 0) {
    space_used_frequency = val;
  };

  // ASCII BUFFER
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.ascii_buffer_size");
  if (val > 0) {
    ascii_buffer_size = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.max_line_size");
  if (val > 0) {
    max_line_size = val;
  };

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.overspill_report_count");
  if (val > 0) {
    overspill_report_count = val;
  };

/* The following variables are initialized after reading the     */
/* variable values from records.config                           */

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.search_log_enabled");
  if (val == 0 || val == 1) {
    if (Log::logging_mode == Log::FULL_LOGGING)
      search_log_enabled = val;
  }

/*                                                               */
/* The following collects the filter names to be added to search */
/* log object. User can define the filter to exclude URLs with   */
/* certain file extensions from being logged.                    */
/*                                                               */
  ptr = LOG_ConfigReadString("proxy.config.log2.search_log_filters");
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
  ptr = LOG_ConfigReadString("proxy.config.log2.search_url_filter");
  if (ptr != NULL) {
    search_url_filter = ptr;
  }

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.search_top_sites");
  if (val > 0) {
    search_top_sites = val;
  }

  ptr = LOG_ConfigReadString("proxy.config.log2.search_server_ip_addr");
  if (ptr != NULL) {
    unsigned int ipaddr;
    ipaddr = inet_addr(ptr);
    if (ipaddr > 0) {
      search_server_ip_addr = ipaddr;
    } else
      search_server_ip_addr = 0;
  }

  val = (int) LOG_ConfigReadInteger("proxy.config.log2.search_server_port");
  if (val > 0) {
    search_server_port = val;
  }

/*  Rolling interval is taken care in LogObject.                 */
  val = (int) LOG_ConfigReadInteger("proxy.config.log2.search_rolling_interval_sec");
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

LogConfig::LogConfig()
:
initialized(false),
reconfiguration_needed(false),
logging_space_exhausted(false), m_space_used(0), m_partition_space_left((ink64) UINT_MAX),
#if defined (IOCORE_LOG_COLLATION)
  m_log_collation_accept(NULL),
#endif
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

#if defined(IOCORE_LOG_COLLATION)
// we don't delete the log collation accept because it may be transferred
// to another LogConfig object
//
//    delete m_log_collation_accept;
#endif

  xfree(hostname);
  xfree(logfile_dir);
  xfree(squid_log_name);
  xfree(squid_log_header);
  xfree(common_log_name);
  xfree(common_log_header);
  xfree(extended_log_name);
  xfree(extended_log_header);
  xfree(extended2_log_name);
  xfree(extended2_log_header);
  xfree(collation_host);
  xfree(collation_secret);
  xfree(config_file);
  xfree(xml_config_file);
  xfree(hosts_config_file);
  xfree(search_log_file_one);
  xfree(search_log_file_two);
  xfree(m_dir_entry);
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
  if (collation_mode<NO_COLLATION || collation_mode>= N_COLLATION_MODES) {
    Note("Invalid value %d for proxy.local.log2.collation_mode"
         " configuration variable (valid range is from %d to %d)\n"
         "Log collation disabled", collation_mode, NO_COLLATION, N_COLLATION_MODES - 1);
  } else if (collation_mode == NO_COLLATION) {
    // if the previous configuration had a collation accept, delete it
    // 
    if (prev_config && prev_config->m_log_collation_accept) {
      delete prev_config->m_log_collation_accept;
      prev_config->m_log_collation_accept = NULL;
    }
  } else {
    if (!collation_port) {
      Note("Cannot activate log collation, %d is and invalid " "collation port", collation_port);
    } else if (collation_mode > COLLATION_HOST && strcmp(collation_host, "none") == 0) {
      Note("Cannot activate log collation, \"%s\" is and invalid " "collation host", collation_host);
    } else {
      if (collation_mode == COLLATION_HOST) {
#if defined(IOCORE_LOG_COLLATION)

        ink_debug_assert(m_log_collation_accept == 0);

        if (prev_config && prev_config->m_log_collation_accept) {
          if (prev_config->collation_port == collation_port) {
            m_log_collation_accept = prev_config->m_log_collation_accept;
          } else {
            delete prev_config->m_log_collation_accept;
          }
        }

        if (!m_log_collation_accept) {
          Log::collation_port = collation_port;
          m_log_collation_accept = NEW(new LogCollationAccept(collation_port));
        }
#else
        // since we are the collation host, we need to signal the
        // collate_cond variable so that our collation thread wakes up.
        //
        ink_cond_signal(&Log::collate_cond);
#endif
        Debug("log2", "I am a collation host listening on port %d.", collation_port);
      } else {
        Debug("log2", "I am a collation client (%d)."
              " My collation host is %s:%d", collation_mode, collation_host, collation_port);
      }

#ifdef IOCORE_LOG_COLLATION
      Debug("log2", "using iocore log collation");
#else
      Debug("log2", "using socket log collation");
#endif
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
  ink_debug_assert(!initialized);

  setup_collation(prev_config);

  update_space_used();

  // setup the error log before the rest of the log objects since
  // we don't do filename conflict checking for it
  //
  TextLogObject *old_elog = Log::error_log;
  TextLogObject *new_elog = 0;

  // swap new error log for old error log unless 
  // -there was no error log and we don't want one
  // -there was an error log, and the new one is identical
  //  (the logging directory did not change)
  //
  if (!((!old_elog && !Log::error_logging_enabled()) ||
        (old_elog && Log::error_logging_enabled() &&
         (prev_config ? !strcmp(prev_config->logfile_dir, logfile_dir) : 0)))) {
    if (Log::error_logging_enabled()) {
      new_elog =
        NEW(new TextLogObject("error.log", logfile_dir, true, NULL,
                              rolling_enabled, rolling_interval_sec, rolling_offset_hr, rolling_size_mb));
      if (new_elog->do_filesystem_checks() < 0) {
        const char *msg = "The log file %s did not pass filesystem checks. " "No output will be produced for this log";
        Error(msg, new_elog->get_full_filename());
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, new_elog->get_full_filename());
        delete new_elog;
        new_elog = 0;
      }
    }
    ink_atomic_swap_ptr((void *) &Log::error_log, new_elog);
    if (old_elog) {
      old_elog->force_new_buffer();
      Log::add_to_inactive(old_elog);
    }
  }
  // create log objects
  //
  if (Log::transaction_logging_enabled()) {
    setup_log_objects();
  }
  // transfer objects from previous configuration
  //
  if (prev_config) {
    transfer_objects(prev_config);
  }
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
  fprintf(fd, "   max_entries_per_buffer = %d\n", max_entries_per_buffer);
  fprintf(fd, "   max_secs_per_buffer = %d\n", max_secs_per_buffer);
  fprintf(fd, "   max_space_mb_for_logs = %d\n", max_space_mb_for_logs);
  fprintf(fd, "   max_space_mb_for_orphan_logs = %d\n", max_space_mb_for_orphan_logs);
  fprintf(fd, "   use_orphan_log_space_value = %d\n", use_orphan_log_space_value);
  fprintf(fd, "   max_space_mb_headroom = %d\n", max_space_mb_headroom);
  fprintf(fd, "   hostname = %s\n", hostname);
  fprintf(fd, "   logfile_dir = %s\n", logfile_dir);
  fprintf(fd, "   logfile_perm = 0%o\n", logfile_perm);
  fprintf(fd, "   config_file = %s\n", config_file);
  fprintf(fd, "   xml_config_file = %s\n", xml_config_file);
  fprintf(fd, "   hosts_config_file = %s\n", hosts_config_file);
  fprintf(fd, "   squid_log_enabled = %d\n", squid_log_enabled);
  fprintf(fd, "   xuid_logging_enabled = %d\n", xuid_logging_enabled);
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
  fprintf(fd, "   separate_mixt_logs = %d\n", separate_mixt_logs);
  fprintf(fd, "   separate_host_logs = %d\n", separate_host_logs);
  fprintf(fd, "   collation_mode = %d\n", collation_mode);
  fprintf(fd, "   collation_host = %s\n", collation_host);
  fprintf(fd, "   collation_port = %d\n", collation_port);
  fprintf(fd, "   collation_host_tagged = %d\n", collation_host_tagged);
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

//-----------------------------------------------------------------------------
// setup_pre_defined_info 
//
// This function adds all the pre defined formats to the global_format_list
// and gathers the information for the active formats in a single place
// (an entry in a PreDefinedFormatInfo list)
//
void
LogConfig::setup_pre_defined_info(PreDefinedFormatInfoList * preDefInfoList)
{
  LogFormat *fmt;
  PreDefinedFormatInfo *pdfi;

  Log::config->xuid_logging_enabled = xuid_logging_enabled;
  fmt = NEW(new LogFormat(SQUID_LOG));

  ink_assert(fmt != 0);
  global_format_list.add(fmt, false);
  Debug("log2", "squid format added to the global format list");


  if (squid_log_enabled) {
    pdfi = NEW(new PreDefinedFormatInfo(fmt, squid_log_name, squid_log_is_ascii, squid_log_header));
    preDefInfoList->enqueue(pdfi);
  }

  fmt = NEW(new LogFormat(COMMON_LOG));
  ink_assert(fmt != 0);
  global_format_list.add(fmt, false);
  Debug("log2", "common format added to the global format list");

  if (common_log_enabled) {
    pdfi = NEW(new PreDefinedFormatInfo(fmt, common_log_name, common_log_is_ascii, common_log_header));
    preDefInfoList->enqueue(pdfi);
  }

  fmt = NEW(new LogFormat(EXTENDED_LOG));
  ink_assert(fmt != 0);
  global_format_list.add(fmt, false);
  Debug("log2", "extended format added to the global format list");

  if (extended_log_enabled) {
    pdfi = NEW(new PreDefinedFormatInfo(fmt, extended_log_name, extended_log_is_ascii, extended_log_header));
    preDefInfoList->enqueue(pdfi);
  }

  fmt = NEW(new LogFormat(EXTENDED2_LOG));
  ink_assert(fmt != 0);
  global_format_list.add(fmt, false);
  Debug("log2", "extended2 format added to the global format list");

  if (extended2_log_enabled) {
    pdfi = NEW(new PreDefinedFormatInfo(fmt, extended2_log_name, extended2_log_is_ascii, extended2_log_header));
    preDefInfoList->enqueue(pdfi);
  }

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
      Warning("Filter %s not in the global filter list; " "cannot add to this LogObject", filter_name);
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
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

void
LogConfig::create_pre_defined_objects_with_filter(const PreDefinedFormatInfoList & pre_def_info_list, size_t num_filters,
                                                  LogFilter ** filter, const char *filt_name, bool force_extension)
{
  PreDefinedFormatInfo *pdi;
  for (pdi = pre_def_info_list.head; pdi != NULL; pdi = (pdi->link).next) {

    char *obj_fname;
    char obj_filt_fname[PATH_MAX];
    if (filt_name) {
      ink_string_concatenate_strings_n(obj_filt_fname, PATH_MAX, pdi->filename, "-", filt_name, NULL);
      obj_fname = obj_filt_fname;
    } else {
      obj_fname = pdi->filename;
    }

    if (force_extension) {
      ink_string_append(obj_filt_fname,
                        (char *) (pdi->is_ascii ?
                                  ASCII_LOG_OBJECT_FILENAME_EXTENSION :
                                  BINARY_LOG_OBJECT_FILENAME_EXTENSION), PATH_MAX);
    }
    // create object with filters
    //
    LogObject *obj;
    obj = NEW(new LogObject(pdi->format, logfile_dir, obj_fname,
                            pdi->is_ascii ? ASCII_LOG : BINARY_LOG,
                            pdi->header, rolling_enabled, rolling_interval_sec, rolling_offset_hr, rolling_size_mb));

    if (collation_mode == SEND_STD_FMTS || collation_mode == SEND_STD_AND_NON_XML_CUSTOM_FMTS) {

      LogHost *loghost = NEW(new LogHost(obj->get_full_filename(),
                                         obj->get_signature()));
      ink_assert(loghost != NULL);

      loghost->set_name_port(collation_host, collation_port);
      obj->add_loghost(loghost, false);
    }

    for (size_t i = 0; i < num_filters; ++i) {
      obj->add_filter(filter[i]);
    }

    // give object to object manager
    //
    log_object_manager.manage_object(obj);
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
LogConfig::split_by_protocol(const PreDefinedFormatInfoList & pre_def_info_list)
{
  if (!(separate_icp_logs || separate_mixt_logs)) {
    return NULL;
  }
  // http MUST be last entry
  enum
  { icp=0, mixt, http };
  unsigned value[] = { LOG_ENTRY_ICP,
    LOG_ENTRY_MIXT, LOG_ENTRY_HTTP
  };
  const char *name[] = { "icp", "mixt", "http" };
  const char *filter_name[] = { "__icp__", "__mixt__", "__http__" };
  unsigned filter_val[http];    // protocols to reject
  size_t n = 0;

  LogFilter *filter[1];
  LogField *etype_field = Log::global_field_list.find_by_symbol("etype");
  ink_assert(etype_field);

  if (separate_icp_logs) {
    if (separate_icp_logs == 1) {
      filter[0] = NEW(new LogFilterInt(filter_name[icp], etype_field, LogFilter::ACCEPT, LogFilter::MATCH, value[icp]));
      create_pre_defined_objects_with_filter(pre_def_info_list, 1, filter, name[icp]);
      delete filter[0];
    }
    filter_val[n++] = value[icp];
  }

  if (separate_mixt_logs) {
    if (separate_mixt_logs == 1) {
      filter[0] = NEW(new LogFilterInt(filter_name[mixt],
                                       etype_field, LogFilter::ACCEPT, LogFilter::MATCH, value[mixt]));
      create_pre_defined_objects_with_filter(pre_def_info_list, 1, filter, name[mixt]);
      delete filter[0];
    }
    filter_val[n++] = value[mixt];
  }
  // At this point, separate objects for all protocols except http
  // have been created if requested. We now add to the argument list
  // the filters needed to reject the protocols that have already
  // been taken care of. Note that we do not test for http since
  // there is no "separate_http_logs" config variable and thus http
  // could not have been taken care of at this point
  //

  if (n > 0) {
    return (NEW(new LogFilterInt("__reject_protocols__", etype_field,
                                 LogFilter::REJECT, LogFilter::MATCH, n, filter_val)));
  } else {
    return NULL;
  }
}

size_t
  LogConfig::split_by_hostname(const PreDefinedFormatInfoList & pre_def_info_list, LogFilter * reject_protocol_filter)
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
      ink_snprintf(filter_name, LOG_MAX_FORMAT_LINE, "__accept_%s__", host[i]);
      rp_ah[num_filt] =
        NEW(new LogFilterString(filter_name,
                                shn_field, LogFilter::ACCEPT, LogFilter::CASE_INSENSITIVE_CONTAIN, host[i]));

      create_pre_defined_objects_with_filter(pre_def_info_list, num_filt + 1, rp_ah, host[i], true);
      delete rp_ah[num_filt];
    }

    LogFilter *rp_rh[2];        // rejected protocols + rejected hosts

    num_filt = 0;

    if (reject_protocol_filter) {
      rp_rh[num_filt++] = reject_protocol_filter;
    }

    rp_rh[num_filt] =
      NEW(new LogFilterString("__reject_hosts__",
                              shn_field, LogFilter::REJECT, LogFilter::CASE_INSENSITIVE_CONTAIN, n_hosts, host));

    // create the "catch-all" object that contains logs for all
    // hosts other than those specified in the hosts file and for
    // those protocols that do not have their own file
    //
    create_pre_defined_objects_with_filter(pre_def_info_list, num_filt + 1, rp_rh);
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
  Debug("log2", "creating objects...");

  // ----------------------------------------------------------------------
  // Construct the LogObjects for the pre-defined formats.

  // gather the config information for the pre-defined formats
  //
  PreDefinedFormatInfoList pre_def_info_list;
  setup_pre_defined_info(&pre_def_info_list);

  // do protocol splitting
  // 
  LogFilter *reject_protocol_filter = split_by_protocol(pre_def_info_list);

  // do host splitting
  //
  size_t num_hosts = 0;
  if (separate_host_logs) {
    num_hosts = split_by_hostname(pre_def_info_list, reject_protocol_filter);
  }

  if (num_hosts == 0) {
    // if no host splitting was requested, or if host splitting
    // was not successful (e.g. empty log_hosts.config file) then
    // create the "catch-all" object that contains logs for all
    // protocols that do not have their own file, and for all hosts
    //
    LogFilter *f[1];
    f[0] = reject_protocol_filter;
    create_pre_defined_objects_with_filter(pre_def_info_list, 1, f);
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
    Debug("log2", "creating search log object");
    /* Read xml configuration for search log from memory.            */
    read_xml_log_config(1);
  }

  if (custom_logs_enabled) {

    if (xml_logs_config) {
      /* Read xml configuration from logs_xml.config file.             */
      read_xml_log_config(0);
    } else {
      read_old_log_config();
    }
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

  if (is_debug_tag_set("log2")) {
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
LogConfig::reconfigure(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  printf("x\n");
  Debug("log2-config", "Reconfiguration request accepted");
  Log::config->reconfiguration_needed = true;
  return 0;
}

void
LogConfig::register_configs()
{
  //##############################################################################
  //#
  //# New Logging Config
  //#
  //##############################################################################
  //# possible values for logging_enabled
  //# 0: no logging at all
  //# 1: log errors only
  //# 2: full logging

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.logging_enabled", 2, RECU_DYNAMIC, RECC_INT, "[0-4]");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.log_buffer_size", 9216, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.max_entries_per_buffer", 100, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.max_secs_per_buffer", 5, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG,
                       "proxy.config.log2.max_space_mb_for_logs", 2000, RECU_DYNAMIC, RECC_STR, "^[0-9]+$");

  RecRegisterConfigInt(RECT_CONFIG,
                       "proxy.config.log2.max_space_mb_for_orphan_logs", 25, RECU_DYNAMIC, RECC_STR, "^[0-9]+$");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.max_space_mb_headroom", 10, RECU_DYNAMIC, RECC_STR, "^[0-9]+$");

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.hostname", "localhost", RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.logfile_dir", "./logs", RECU_DYNAMIC, RECC_STR, "^[^[:space:]]+$");

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.logfile_perm", "rw-r--r--", RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.custom_logs_enabled", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.xml_logs_config", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.config_file", "logs.config", RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.xml_config_file", "logs_xml.config", RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.hosts_config_file", "log_hosts.config", RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.squid_log_enabled", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.xuid_logging_enabled", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.squid_log_is_ascii", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.squid_log_name", "squid", RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.squid_log_header", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.common_log_enabled", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.common_log_is_ascii", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.common_log_name", "common", RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.common_log_header", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.extended_log_enabled", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.extended_log_is_ascii", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.extended_log_name", "extended", RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.extended_log_header", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.extended2_log_enabled", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.extended2_log_is_ascii", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.extended2_log_name",
                          "extended2", RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.extended2_log_header", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.separate_icp_logs", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.separate_mixt_logs", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.separate_host_logs", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG,
                          "proxy.config.log2.collation_host", NULL, RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.collation_port", 0,      /* <log2_collation_port>, */
                       RECU_DYNAMIC, RECC_INT, "[0-65535]");

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.collation_secret", "foobar", RECU_DYNAMIC, RECC_STR, ".*");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.collation_host_tagged", 0, RECU_DYNAMIC, RECC_INT, "[0-1]");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.collation_retry_sec", 5, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.collation_max_send_buffers", 16, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.rolling_enabled", 1, RECU_DYNAMIC, RECC_INT, "[0-1]");

  RecRegisterConfigInt(RECT_CONFIG,
                       "proxy.config.log2.rolling_interval_sec", 86400, RECU_DYNAMIC, RECC_STR, "^[0-9]+$");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.rolling_offset_hr", 0, RECU_DYNAMIC, RECC_STR, "^[0-9]+$");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.rolling_size_mb", 10, RECU_DYNAMIC, RECC_STR, "^0*[1-9][0-9]*$");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.auto_delete_rolled_files", 1, RECU_DYNAMIC, RECC_INT, "[0-1]");

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.sampling_frequency", 1, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.space_used_frequency", 2, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.file_stat_frequency", 32, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.ascii_buffer_size", 36864, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.max_line_size", 9216, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.overspill_report_count", 500, RECU_DYNAMIC, RECC_NULL, NULL);
  /*                                                                             */
  /* Begin  HCL Modifications.                                                   */
  /*                                                                             */
  RecRegisterConfigInt(RECT_CONFIG,
                       "proxy.config.log2.search_rolling_interval_sec", 86400, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.search_log_enabled", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.search_server_ip_addr", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.search_server_port", 8080, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigInt(RECT_CONFIG, "proxy.config.log2.search_top_sites", 100, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.search_url_filter", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  RecRegisterConfigString(RECT_CONFIG, "proxy.config.log2.search_log_filters", NULL, RECU_DYNAMIC, RECC_NULL, NULL);
  /*                                                                             */
  /* End    HCL Modifications.                                                   */
  /*                                                                             */
}

/*-------------------------------------------------------------------------
  LogConfig::register_config_callbacks

  This static function is called by Log::init to register the config update
  function for each of the logging configuration variables.
  -------------------------------------------------------------------------*/

void
LogConfig::register_config_callbacks()
{
  // Note: variables that are not exposed in the UI are commented out
  //
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.log_buffer_size", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.max_entries_per_buffer", &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.max_secs_per_buffer",
//                            &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.max_space_mb_for_logs", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.max_space_mb_for_orphan_logs", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.max_space_mb_headroom", &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.logfile_perm",
//                            &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.hostname",
//                            &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.logfile_dir", &LogConfig::reconfigure, NULL);

  // SQUID
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.squid_log_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.xuid_logging_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.squid_log_is_ascii", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.squid_log_name", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.squid_log_header", &LogConfig::reconfigure, NULL);

  // COMMON
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.common_log_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.common_log_is_ascii", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.common_log_name", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.common_log_header", &LogConfig::reconfigure, NULL);

  // EXTENDED 
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended_log_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended_log_is_ascii", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended_log_name", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended_log_header", &LogConfig::reconfigure, NULL);

  // EXTENDED2
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended2_log_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended2_log_is_ascii", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended2_log_name", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.extended2_log_header", &LogConfig::reconfigure, NULL);

  // SPLITTING
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.separate_icp_logs", &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.separate_mixt_logs",
//                                  &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.separate_host_logs", &LogConfig::reconfigure, NULL);

  // COLLATION
  LOG_RegisterLocalUpdateFunc("proxy.local.log2.collation_mode", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.collation_host", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.collation_port", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.collation_host_tagged", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.collation_secret", &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.collation_retry_sec",
//                                  &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.collation_max_send_buffers",
//                                  &LogConfig::reconfigure, NULL);

  // ROLLING
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.rolling_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.rolling_interval_sec", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.rolling_offset_hr", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.rolling_size_mb", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.auto_delete_rolled_files", &LogConfig::reconfigure, NULL);

  // CUSTOM LOGGING
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.custom_logs_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.xml_logs_config", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.config_file", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.xml_config_file", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.hosts_config_file", &LogConfig::reconfigure, NULL);

  // PERFORMANCE
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.sampling_frequency",
//                            &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.file_stat_frequency",
//                            &LogConfig::reconfigure, NULL);
//    LOG_RegisterConfigUpdateFunc ("proxy.config.log2.space_used_frequency",
//                            &LogConfig::reconfigure, NULL);

/* These are the call back function connectivities               */
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.search_rolling_interval_sec", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.search_log_enabled", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.search_top_sites", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.search_server_ip_addr", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.search_server_port", &LogConfig::reconfigure, NULL);
  LOG_RegisterConfigUpdateFunc("proxy.config.log2.search_url_filter", &LogConfig::reconfigure, NULL);

}

/*-------------------------------------------------------------------------
  LogConfig::register_stat_callbacks

  This static function is called by Log::init to register the stat update
  function for each of the logging stats variables.
  -------------------------------------------------------------------------*/

void
LogConfig::register_stat_callbacks()
{
#define LOG_CLEAR_DYN_STAT(x) \
do { \
	RecSetRawStatSum(log_rsb, x, 0); \
	RecSetRawStatCount(log_rsb, x, 0); \
} while (0);
  //
  // bytes moved
  //
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.bytes_buffered",
                     RECD_INT, RECP_PERSISTENT, (int) log2_stat_bytes_buffered_stat, RecRawStatSyncSum);

  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.bytes_written_to_disk",
                     RECD_INT, RECP_PERSISTENT, (int) log2_stat_bytes_written_to_disk_stat, RecRawStatSyncSum);

  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.bytes_sent_to_network",
                     RECD_INT, RECP_PERSISTENT, (int) log2_stat_bytes_sent_to_network_stat, RecRawStatSyncSum);

  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.bytes_received_from_network",
                     RECD_INT, RECP_PERSISTENT, (int) log2_stat_bytes_received_from_network_stat, RecRawStatSyncSum);

  //
  // I/O
  //
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.log_files_open",
                     RECD_COUNTER, RECP_NON_PERSISTENT, (int) log2_stat_log_files_open_stat, RecRawStatSyncSum);
  LOG_CLEAR_DYN_STAT(log2_stat_log_files_open_stat);

  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.log_files_space_used",
                     RECD_INT, RECP_NON_PERSISTENT, (int) log2_stat_log_files_space_used_stat, RecRawStatSyncSum);

  //
  // events
  //
  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.event_log_error",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log2_stat_event_log_error_stat, RecRawStatSyncCount);

  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.event_log_access",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log2_stat_event_log_access_stat, RecRawStatSyncCount);

  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.event_log_access_fail",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log2_stat_event_log_access_fail_stat, RecRawStatSyncCount);

  RecRegisterRawStat(log_rsb, RECT_PROCESS,
                     "proxy.process.log2.event_log_access_skip",
                     RECD_COUNTER, RECP_PERSISTENT, (int) log2_stat_event_log_access_skip_stat, RecRawStatSyncCount);
}

/*-------------------------------------------------------------------------
  LogConfig::register_mgmt_callbacks

  This static function is called by Log::init to register the mgmt callback
  function for each of the logging mgmt messages.
  -------------------------------------------------------------------------*/

void
LogConfig::register_mgmt_callbacks()
{
  LOG_RegisterMgmtCallback(REC_EVENT_ROLL_LOG_FILES, &LogConfig::reconfigure_mgmt_variables, NULL);
}


/*------------------------------------------------------------------------- 
  LogConfig::space_to_write

  This function returns true if there is enough disk space to write the
  given number of bytes, false otherwise.
  -------------------------------------------------------------------------*/

bool LogConfig::space_to_write(ink64 bytes_to_write)
{
  ink64
    config_space,
    partition_headroom;
  ink64
    logical_space_used,
    physical_space_left;
  bool
    space;

  config_space = (ink64) get_max_space_mb() * LOG_MEGABYTE;
  partition_headroom = (ink64) PARTITION_HEADROOM_MB *
    LOG_MEGABYTE;

  logical_space_used = m_space_used + bytes_to_write;
  physical_space_left = m_partition_space_left - (ink64) bytes_to_write;

  space = ((logical_space_used<config_space) && (physical_space_left> partition_headroom));

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
  ink64 total_space_used, partition_space_left;
  char path[MAXPATHLEN];
  int sret;
  struct dirent *result;
  struct stat sbuf;
  DIR *ld;

  // check if logging directory has been specified
  //
  if (!logfile_dir) {
    const char *msg = "Logging directory not specified";
    Error(msg);
    LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg);
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
    const char *msg = "Error opening logging directory %s to perform a space " "check: %s.";
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

    m_dir_entry = (struct dirent *) xmalloc(sizeof(struct dirent) + name_max + 1);
    ink_assert(m_dir_entry != NULL);
  }

  total_space_used = 0LL;

  candidate_count = 0;

  while (ink_readdir_r(ld, m_dir_entry, &result) == 0) {

    if (!result) {
      break;
    }

    ink_snprintf(path, MAXPATHLEN, "%s/%s", logfile_dir, m_dir_entry->d_name);

    sret =::stat(path, &sbuf);
    if (sret != -1 && S_ISREG(sbuf.st_mode)) {

      total_space_used += (ink64) sbuf.st_size;

      if (auto_delete_rolled_files && LogFile::rolled_logfile(m_dir_entry->d_name) && candidate_count < MAX_CANDIDATES) {
        //
        // then add this entry to the candidate list
        //
        candidates[candidate_count].name = xstrdup(path);
        candidates[candidate_count].size = (ink64) sbuf.st_size;
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
#if (HOST_OS == solaris)
  struct statvfs fs;
  ::memset(&fs, 0, sizeof(fs));
  int ret =::statvfs(logfile_dir, &fs);
#else
  struct statfs fs;
  ::memset(&fs, 0, sizeof(fs));
  int ret =::statfs(logfile_dir, &fs);
#endif
  if (ret >= 0) {
    partition_space_left = (ink64) fs.f_bavail * (ink64) fs.f_bsize;
  }

  //
  // Update the config variables for space used/left
  //
  m_space_used = total_space_used;
  m_partition_space_left = partition_space_left;
  LOG_SET_DYN_STAT(log2_stat_log_files_space_used_stat, 1, m_space_used);

  Debug("log2space", "%lld bytes being used for logs", m_space_used);
  Debug("log2space", "%lld bytes left on parition", m_partition_space_left);

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

  ink64 max_space = (ink64) get_max_space_mb() * LOG_MEGABYTE;
  ink64 headroom = (ink64) max_space_mb_headroom * LOG_MEGABYTE;

  if (candidate_count > 0 && !space_to_write(headroom)) {

    Debug("log2space", "headroom reached, trying to clear space ...");
    Debug("log2space", "sorting %d delete candidates ...", candidate_count);
    qsort(candidates, candidate_count, sizeof(LogDeleteCandidate),
          (int (*)(const void *, const void *)) delete_candidate_compare);

    for (victim = 0; victim < candidate_count; victim++) {

      if (space_to_write(headroom + log_buffer_size)) {
        Debug("log2space", "low water mark reached; stop deleting");
        break;
      }

      Debug("log2space", "auto-deleting %s", candidates[victim].name);

      if (unlink(candidates[victim].name) < 0) {
        Note("Traffic Server was Unable to auto-delete rolled "
             "logfile %s: %s.", candidates[victim].name, strerror(errno));
      } else {
        Status("The rolled logfile, %s, was auto-deleted; "
               "%lld bytes were reclaimed.", candidates[victim].name, candidates[victim].size);
        m_space_used -= candidates[victim].size;
        m_partition_space_left += candidates[victim].size;
      }
    }
  }
  //
  // Clean up the candidate array
  //
  for (i = 0; i < candidate_count; i++) {
    xfree(candidates[i].name);
  }

  //
  // Now that we've updated the m_space_used value, see if we need to 
  // issue any alarms or warnings about space
  //

#if 0
  // CVR See if we need to roll the files based on the configured rolling 
  // log dir size - 11445
  if ((m_space_used >= ((ink64) max_space_mb_for_rolling * LOG_MEGABYTE)) && rolling_based_on_directory_size_enabled) {

    long now;
    now = LogUtils::timestamp();
    log_object_manager.roll_files_size_exceed(now);
    rolling_based_on_directory_size_enabled = false;

    LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, ROLLING_LIMIT_EXCEEDED_MESSAGE);
    Warning(ROLLING_LIMIT_EXCEEDED_MESSAGE);
  }
#endif // if 0

  if (!space_to_write(headroom)) {
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
    logging_space_exhausted = false;
    if (m_disk_full || m_partition_full) {
      Note("Logging disk is no longer full; " "access logging to local log directory resumed.");
      m_disk_full = false;
      m_partition_full = false;
    }
    if (m_disk_low || m_partition_low) {
      Note("Logging disk is no longer low; " "access logging to local log directory resumed.");
      m_disk_low = false;
      m_partition_low = false;
    }
  }
}


/*-------------------------------------------------------------------------
  LogConfig::read_old_log_config
  -------------------------------------------------------------------------*/

void
LogConfig::read_old_log_config()
{
  char config_path[PATH_MAX];
  char line[LOG_MAX_FORMAT_LINE];
  char copy[LOG_MAX_FORMAT_LINE];
  int line_num = 0;

  if (config_file == NULL) {
    Note("No log config file to read");
    return;
  }

  ink_snprintf(config_path, PATH_MAX, "%s/%s", system_config_directory, config_file);

  Debug("log2-config", "Reading log config file %s", config_path);

  int fd =::open(config_path, O_RDONLY);
  if (fd < 0) {
    Warning("Traffic Server can't open %s for reading custom " "formats: %s.", config_path, strerror(errno));
    return;
  }

  while (ink_file_fd_readline(fd, LOG_MAX_FORMAT_LINE, line) > 0) {

    //
    // Ignore blank Lines and lines that begin with a '#'.
    //
    line_num++;
    if (*line == '\n' || *line == '#') {
      continue;
    }
    //
    // Tokenize the line; ':' is the field separator character
    //
    LogUtils::strip_trailing_newline(line);
    ink_strncpy(copy, line, sizeof(copy));
    char *entry_type = strtok(copy, ":");

    if (entry_type) {

      // format
      // 
      if (strcasecmp(entry_type, "format") == 0) {

        char *file_name = NULL;
        char *file_header = NULL;
        LogFileFormat file_type;

        LogFormat *new_format = LogFormat::format_from_specification(line, &file_name, &file_header, &file_type);

        bool valid_format = false;
        if (new_format != NULL) {
          if (global_format_list.find_by_name(new_format->name())) {
            Status(DUP_FORMAT_MESSAGE, new_format->name());
            valid_format = false;
          } else {
            valid_format = true;
          }
        }

        if (valid_format) {
          LogObject *obj = NEW(new LogObject(new_format,
                                             logfile_dir,
                                             file_name, file_type,
                                             file_header,
                                             rolling_enabled,
                                             rolling_interval_sec,
                                             rolling_offset_hr,
                                             rolling_size_mb));

          if (collation_mode == SEND_NON_XML_CUSTOM_FMTS || collation_mode == SEND_STD_AND_NON_XML_CUSTOM_FMTS) {

            LogHost *loghost = NEW(new LogHost(obj->get_full_filename(),
                                               obj->get_signature()));
            ink_assert(loghost != NULL);

            loghost->set_name_port(collation_host, collation_port);
            obj->add_loghost(loghost, false);

          }

          global_format_list.add(new_format, false);

          // give object to object manager
          //
          log_object_manager.manage_object(obj);
        }

        xfree(file_name);
        xfree(file_header);

        if (valid_format) {
          continue;
        }
      }
      // filter
      //
      if (strcasecmp(entry_type, "filter") == 0) {
        char *fmt_name = NULL;
        LogFilter *filter = LogFilter::filter_from_specification(line, global_format_list, Log::global_field_list,
                                                                 &fmt_name);
        if (filter) {
          if (global_filter_list.find_by_name(filter->name())) {
            Warning("Ignoring duplicate filter definition at " "line %d", line_num);
            delete filter;
          } else {
            LogObject *obj;
            if (strcmp(fmt_name, "_global_") == 0) {
              // apply filter to all objects
              //
              log_object_manager.add_filter_to_all(filter);
            } else {
              // apply filter to object with format fmt_name
              //
              obj = log_object_manager.find_by_format_name(fmt_name);
              if (!obj) {
                Warning("There is no format named %s for " "filter at line %d", fmt_name, line_num);
                delete filter;
                xfree(fmt_name);
                continue;
              } else {
                obj->add_filter(filter);
              }
            }
          }
          if (fmt_name) {
            xfree(fmt_name);
          }
          global_filter_list.add(filter, false);
          continue;
        }
        if (fmt_name) {
          xfree(fmt_name);
        }
      }
    }

    Warning("Traffic Server failed to parse line %d of the " "logging config file: %s.", line_num, config_path);
  }

  ::close(fd);
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
  char config_path[PATH_MAX];

  if (from_memory) {
    ink_snprintf(config_path, PATH_MAX, "%s", "from_memory");
    Debug("log2", "Reading from memory %s", config_path);
  } else {
    if (xml_config_file == NULL) {
      Note("No log config file to read");
      return;
    }
    ink_snprintf(config_path, PATH_MAX, "%s/%s", system_config_directory, xml_config_file);
  }


  Debug("log2-config", "Reading log config file %s", config_path);
  Debug("xml", "%s is an XML-based config file", config_path);

  InkXmlConfigFile log_config(config_path);

  if (!from_memory) {

    if (log_config.parse() < 0) {
      Note("Error parsing log config file; ensure that it is XML-based.");
      return;
    }

    if (is_debug_tag_set("xml")) {
      log_config.display();
    }
  } else {
    int filedes[2];
    int nbytes = sizeof(xml_config_buffer);
    const size_t ptr_size = nbytes + 20;
    char *ptr = (char *) xmalloc(ptr_size);

    if (pipe(filedes) != 0) {
      Note("xml parsing: Error in Opening a pipe\n");
      return;
    }

    snprintf(ptr, ptr_size, xml_config_buffer, search_rolling_interval_sec, search_rolling_interval_sec);
    nbytes = strlen(ptr);
    if (write(filedes[1], ptr, nbytes) != nbytes) {
      Note("Error in writing to pipe.");
      xfree(ptr);
      close(filedes[1]);
      close(filedes[0]);
      return;
    }
    xfree(ptr);
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
        Note("Multiple values for 'Name' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (format.count() > 1) {
        Note("Multiple values for 'Format' attribute in %s; " "using the first one", xobj->object_name());
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
          Note("Multiple values for 'Interval' attribute in %s; " "using the first one", xobj->object_name());
        }
        // interval 
        //
        interval_num = ink_atoui(interval.dequeue());
      } else if (interval.count() > 0) {
        Note("Format %s has no aggregates, ignoring 'Interval'" " attribute.", name_str);
      }
      // create new format object and place onto global list
      //
      LogFormat *fmt = NEW(new LogFormat(name_str, format_str,
                                         interval_num));
      ink_assert(fmt != NULL);
      if (fmt->valid()) {
        global_format_list.add(fmt, false);

        if (is_debug_tag_set("xml")) {
          printf("The following format was added to the global " "format list\n");
          fmt->displayAsXML(stdout);
        }
      } else {
        Note("Format named \"%s\" will not be active;" " not a valid format", fmt->name()? fmt->name() : "");
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
        Note("Multiple values for 'Name' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (action.count() > 1) {
        Note("Multiple values for 'Action' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (condition.count() > 1) {
        Note("Multiple values for 'Condition' attribute in %s; " "using the first one", xobj->object_name());
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
        Warning("%s is not a valid filter action value; " "cannot create filter %s.", action_str, filter_name);
        continue;
      }
      // parse condition string and validate its fields
      //
      char *cond_str = condition.dequeue();

      SimpleTokenizer tok(cond_str);

      if (tok.getNumTokensRemaining() < 3) {
        Warning("Invalid condition syntax \"%s\"; " "cannot create filter %s.", cond_str, filter_name);
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
        Warning("%s is not a valid field; " "cannot create filter %s.", field_str, filter_name);
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
        Warning("%s is not a valid operator; " "cannot create filter %s.", oper_str, filter_name);
        continue;
      }
      // now create the correct LogFilter
      //
      LogFilter *filter = NULL;
      LogField::Type field_type = logfield->type();

      switch (field_type) {

      case LogField::sINT:

        filter = NEW(new LogFilterInt(filter_name, logfield, act, oper, val_str));
        break;

      case LogField::dINT:

        Warning("Internal error: invalid field type (double int); " "cannot create filter %s.", filter_name);
        continue;

      case LogField::STRING:

        filter = NEW(new LogFilterString(filter_name, logfield, act, oper, val_str));
        break;

      default:

        Warning("Internal error: unknown field type %d; " "cannot create filter %s.", field_type, filter_name);
        continue;
      }

      ink_assert(filter);

      if (filter->get_num_values() == 0) {

        Warning("\"%s\" does not specify any valid values; " "cannot create filter %s.", val_str, filter_name);
        delete filter;

      } else {

        // add filter to global filter list
        //
        global_filter_list.add(filter, false);

        if (is_debug_tag_set("xml")) {
          printf("The following filter was added to " "the global filter list\n");
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
        Note("Multiple values for 'Format' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (filename.count() > 1) {
        Note("Multiple values for 'Filename' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (mode.count() > 1) {
        Note("Multiple values for 'Mode' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (filters.count() > 1) {
        Note("Multiple values for 'Filters' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (protocols.count() > 1) {
        Note("Multiple values for 'Protocols' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (serverHosts.count() > 1) {
        Note("Multiple values for 'ServerHosts' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (collationHosts.count() > 1) {
        Note("Multiple values for 'CollationHosts' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (header.count() > 1) {
        Note("Multiple values for 'Header' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (rollingEnabled.count() > 1) {
        Note("Multiple values for 'RollingEnabled' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (rollingIntervalSec.count() > 1) {
        Note("Multiple values for 'RollingIntervalSec' attribute " "in %s; using the first one", xobj->object_name());
      }
      if (rollingOffsetHr.count() > 1) {
        Note("Multiple values for 'RollingOffsetHr' attribute in %s; " "using the first one", xobj->object_name());
      }
      if (rollingSizeMb.count() > 1) {
        Note("Multiple values for 'RollingSizeMb' attribute in %s; " "using the first one", xobj->object_name());
      }
      // create new LogObject and start adding to it
      //

      char *fmt_name = format.dequeue();
      LogFormat *fmt = global_format_list.find_by_name(fmt_name);
      if (!fmt) {
        Warning("Format %s not in the global format list; " "cannot create LogObject", fmt_name);
        continue;
      }
      // file format
      //
      LogFileFormat file_type = ASCII_LOG;      // default value
      if (mode.count()) {
        char *mode_str = mode.dequeue();
        file_type = (strncasecmp(mode_str, "bin", 3) == 0 ||
                     (mode_str[0] == 'b' && mode_str[1] == 0) ?
                     BINARY_LOG : (strcasecmp(mode_str, "ascii_pipe") == 0 ? ASCII_PIPE : ASCII_LOG));
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

      // create the new object
      //
      LogObject *obj = NEW(new LogObject(fmt, logfile_dir,
                                         filename.dequeue(),
                                         file_type,
                                         header.dequeue(),
                                         obj_rolling_enabled,
                                         obj_rolling_interval_sec,
                                         obj_rolling_offset_hr,
                                         obj_rolling_size_mb));

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
            Warning("Filter %s not in the global filter list; " "cannot add to this LogObject", filter_name);
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
          unsigned *val_array = NEW(new unsigned[n]);
          size_t numValid = 0;
          char *t;
          while (t = tok.getNext(), t != NULL) {
            if (strcasecmp(t, "icp") == 0) {
              val_array[numValid++] = LOG_ENTRY_ICP;
            } else if (strcasecmp(t, "mixt") == 0) {
              val_array[numValid++] = LOG_ENTRY_MIXT;
            } else if (strcasecmp(t, "http") == 0) {
              val_array[numValid++] = LOG_ENTRY_HTTP;
            }
          }

          if (numValid == 0) {
            Warning("No valid protocol value(s) (%s) for Protocol "
                    "field in definition of XML LogObject.\n" "Object will log all protocols.", protocols_str);
          } else {
            if (numValid < n) {
              Warning("There are invalid protocol values (%s) in"
                      " the Protocol field of XML LogObject.\n"
                      "Only %u out of %u values will be used.", protocols_str, numValid, n);
            }

            LogFilterInt protocol_filter("__xml_protocol__",
                                         etype_field, LogFilter::ACCEPT, LogFilter::MATCH, numValid, val_array);
            obj->add_filter(&protocol_filter);
          }
          delete[] val_array;
        } else {
          Warning("No value(s) in Protocol field of XML object, " "object will log all protocols.");
        }
      }
      // server hosts
      //
      char *serverHosts_str = serverHosts.dequeue();
      if (serverHosts_str) {

        LogField *shn_field = Log::global_field_list.find_by_symbol("shn");
        ink_assert(shn_field);

        LogFilterString
          server_host_filter("__xml_server_hosts__",
                             shn_field, LogFilter::ACCEPT, LogFilter::CASE_INSENSITIVE_CONTAIN, serverHosts_str);

        if (server_host_filter.get_num_values() == 0) {
          Warning("No valid server host value(s) (%s) for Protocol "
                  "field in definition of XML LogObject.\n" "Object will log all servers.", serverHosts_str);
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

          LogHost *lh = NEW(new LogHost(obj->get_full_filename(),
                                        obj->get_signature()));
          if (lh->set_name_or_ipstr(host)) {
            Warning("Could not set \"%s\" as collation host", host);
            delete lh;
          } else {
            obj->add_loghost(lh, false);
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
  char config_path[PATH_MAX];
  char line[LOG_MAX_FORMAT_LINE];
  char **hosts = NULL;

  ink_snprintf(config_path, PATH_MAX, "%s/%s", system_config_directory, hosts_config_file);

  Debug("log2-config", "Reading log hosts from %s", config_path);

  size_t nhosts = 0;
  int fd = open(config_path, O_RDONLY);
  if (fd < 0) {
    Warning("Traffic Server can't open %s for reading log hosts " "for splitting: %s.", config_path, strerror(errno));
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
        Warning("lseek failed on file %s: %s", config_path, strerror(errno));
        nhosts = 0;
      } else {
        hosts = NEW(new char *[nhosts]);
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
          hosts[i] = strdup(line);
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
