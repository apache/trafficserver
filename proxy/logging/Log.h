/** @file

  Logging

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

  @verbatim
  int Log::access (LogAccess *entry);

  The return value is Log::OK if all log objects successfully logged the
  entry. Otherwise, it has the following bits set to indicate what happened

  ret_val & Log::SKIP      - at least one object filtered the entry
                           - transaction logging has been disabled
                           - the logging system is sampling and it is
                             not yet time for a new sample
                           - no log objects have been defined
                           - entry to log is empty

  ret_val & Log::FAIL      an internal limit of the logging system was
                           exceeded preventing entry from being logged

  ret_val & Log::FULL      the logging space has been exhausted

  @endverbatim

  @section example Example usage of the API

  @code
  LogAccess entry(this);
  int ret = Log::access(&entry);
  @endcode

*/

#pragma once

#include <cstdarg>
#include "tscore/ink_platform.h"
#include "tscore/EventNotify.h"
#include "tscore/Regression.h"
#include "records/P_RecProcess.h"
#include "LogFile.h"
#include "LogBuffer.h"

#include <unordered_map>

class LogAccess;
class LogFieldList;
class LogFilterList;
class LogFormatList;
struct LogBufferHeader;
class LogFile;
class LogBuffer;
class LogFormat;
class LogObject;
class LogConfig;
class TextLogObject;

class LogFlushData
{
public:
  LINK(LogFlushData, link);
  Ptr<LogFile> m_logfile;
  LogBuffer *logbuffer = nullptr;
  void *m_data;
  int m_len;

  LogFlushData(LogFile *logfile, void *data, int len = -1) : m_logfile(logfile), m_data(data), m_len(len) {}
  ~LogFlushData()
  {
    switch (m_logfile->m_file_format) {
    case LOG_FILE_BINARY:
      logbuffer = static_cast<LogBuffer *>(m_data);
      LogBuffer::destroy(logbuffer);
      break;
    case LOG_FILE_ASCII:
    case LOG_FILE_PIPE:
      free(m_data);
      break;
    case N_LOGFILE_TYPES:
    default:
      ink_release_assert(!"Unknown file format type!");
    }
  }
};

/**
   This object exists to provide a namespace for the logging system.
   It contains all data types and global variables relevant to the
   logging system.  You can't actually create a Log object, so all
   members are static.
 */
class Log
{
public:
  // Prevent creation of any instances of this class.
  //
  Log() = delete;

  enum ReturnCodeFlags {
    LOG_OK = 1,
    SKIP   = 2,
    AGGR   = 4,
    FAIL   = 8,
    FULL   = 16,
  };

  enum LoggingMode {
    LOG_MODE_NONE = 0,
    LOG_MODE_ERRORS,       // log *only* errors
    LOG_MODE_TRANSACTIONS, // log *only* transactions
    LOG_MODE_FULL
  };

  enum InitFlags {
    FIELDS_INITIALIZED = 1,
    FULLY_INITIALIZED  = 2,
  };

  enum ConfigFlags {
    NO_REMOTE_MANAGEMENT = 1,
    LOGCAT               = 4,
  };

  enum RollingEnabledValues {
    NO_ROLLING = 0,
    ROLL_ON_TIME_ONLY,
    ROLL_ON_SIZE_ONLY,
    ROLL_ON_TIME_OR_SIZE,
    ROLL_ON_TIME_AND_SIZE,
    INVALID_ROLLING_VALUE
  };

  enum {
    MIN_ROLLING_INTERVAL_SEC = 30,   // 30 second minimum rolling interval
    MAX_ROLLING_INTERVAL_SEC = 86400 // 24 hrs rolling interval max
  };

  // main interface
  static void init(int configFlags = 0);
  static void init_fields();

  static bool
  transaction_logging_enabled()
  {
    return (logging_mode == LOG_MODE_FULL || logging_mode == LOG_MODE_TRANSACTIONS);
  }

  static bool
  error_logging_enabled()
  {
    return (logging_mode == LOG_MODE_FULL || logging_mode == LOG_MODE_ERRORS);
  }

  static int access(LogAccess *lad);
  static int va_error(const char *format, va_list ap);
  static int error(const char *format, ...) TS_PRINTFLIKE(1, 2);

  /////////////////////////////////////////////////////////////////////////
  // 'Wire tracing' enabled by source ip or by percentage of connections //
  /////////////////////////////////////////////////////////////////////////
  static void trace_in(const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, ...) TS_PRINTFLIKE(3, 4);
  static void trace_out(const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, ...) TS_PRINTFLIKE(3, 4);
  static void trace_va(bool in, const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, va_list ap);

  // public data members
  static LogObject *error_log;
  /** The latest fully initialized LogConfig.
   *
   * This is the safe, fully initialed LogConfig object to query against when
   * performing logging operations.
   */
  static LogConfig *config;
  static LogFieldList global_field_list;
  static std::unordered_map<std::string, LogField *> field_symbol_hash;
  static LogFormat *global_scrap_format;
  static LogObject *global_scrap_object;
  static LoggingMode logging_mode;

  // logging thread stuff
  static EventNotify *preproc_notify;
  static void *preproc_thread_main(void *args);
  static EventNotify *flush_notify;
  static InkAtomicList *flush_data_list;
  static void *flush_thread_main(void *args);

  static int preproc_threads;

  // reconfiguration stuff
  static void change_configuration();
  static int handle_logging_mode_change(const char *name, RecDataT data_type, RecData data, void *cookie);
  static int handle_periodic_tasks_int_change(const char *name, RecDataT data_type, RecData data, void *cookie);

  friend void RegressionTest_LogObjectManager_Transfer(RegressionTest *, int, int *);

private:
  static void periodic_tasks(long time_now);
  static void create_threads();
  static void init_when_enabled();

  static int init_status;
  static int config_flags;
  static bool logging_mode_changed;
  static uint32_t periodic_tasks_interval;
};

static inline bool
LogRollingEnabledIsValid(int enabled)
{
  return (enabled >= Log::NO_ROLLING || enabled < Log::INVALID_ROLLING_VALUE);
}

#define TraceIn(flag, ...)        \
  do {                            \
    if (unlikely(flag))           \
      Log::trace_in(__VA_ARGS__); \
  } while (0)

#define TraceOut(flag, ...)        \
  do {                             \
    if (unlikely(flag))            \
      Log::trace_out(__VA_ARGS__); \
  } while (0)
