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

#include "tscore/ink_platform.h"
#include "Log.h"
#include "LogFile.h"
#include "LogFormat.h"
#include "LogFilter.h"
#include "LogBuffer.h"
#include "LogAccess.h"
#include "LogFilter.h"
#include <vector>

/*-------------------------------------------------------------------------
  LogObject

  This is a new addition to the Traffic Server logging as of the 3.1
  (Panda) release.  This object corresponds to the new type of logfile
  entity that will be the focal point for all logging, replacing the role
  of the LogFormat object.  The LogObject will contain information about
  the format being used, the physical file  attached, and any filters that
  are in place.  The global logging configuration for a traffic server will
  consist of a list of LogObjects.
  -------------------------------------------------------------------------*/

#define LOG_FILE_ASCII_OBJECT_FILENAME_EXTENSION ".log"
#define LOG_FILE_BINARY_OBJECT_FILENAME_EXTENSION ".blog"
#define LOG_FILE_PIPE_OBJECT_FILENAME_EXTENSION ".pipe"

#define FLUSH_ARRAY_SIZE (512 * 4)

#define LOG_OBJECT_ARRAY_DELTA 8

#define ACQUIRE_API_MUTEX(_f)   \
  ink_mutex_acquire(_APImutex); \
  Debug("log-api-mutex", _f)

#define RELEASE_API_MUTEX(_f)   \
  ink_mutex_release(_APImutex); \
  Debug("log-api-mutex", _f)

class LogBufferManager
{
private:
  ASLL(LogBuffer, write_link) write_list;
  int _num_flush_buffers = 0;

public:
  LogBufferManager() {}
  inline void
  add_to_flush_queue(LogBuffer *buffer)
  {
    write_list.push(buffer);
    ink_atomic_increment(&_num_flush_buffers, 1);
  }

  size_t preproc_buffers(LogBufferSink *sink);
};

// LogObject is atomically reference counted, and the reference count is always owned by
// one or more LogObjectManagers.
class LogObject : public RefCountObj
{
public:
  enum LogObjectFlags {
    BINARY                   = 1,
    WRITES_TO_PIPE           = 4,
    LOG_OBJECT_FMT_TIMESTAMP = 8, // always format a timestamp into each log line (for raw text logs)
  };

  // BINARY: log is written in binary format (rather than ascii)
  // WRITES_TO_PIPE: object writes to a named pipe rather than to a file

  LogObject(const LogFormat *format, const char *log_dir, const char *basename, LogFileFormat file_format, const char *header,
            Log::RollingEnabledValues rolling_enabled, int flush_threads, int rolling_interval_sec = 0, int rolling_offset_hr = 0,
            int rolling_size_mb = 0, bool auto_created = false, int rolling_max_count = 0, int rolling_min_count = 0,
            bool reopen_after_rolling = false, int pipe_buffer_size = 0);
  LogObject(LogObject &);
  ~LogObject() override;

  void add_filter(LogFilter *filter, bool copy = true);
  void set_filter_list(const LogFilterList &list, bool copy = true);

  inline void
  set_fmt_timestamps()
  {
    m_flags |= LOG_OBJECT_FMT_TIMESTAMP;
  }

  int log(LogAccess *lad, const char *text_entry = nullptr);

  /** Log the @a text_entry.
   *
   * @param lad Log accessor.
   * @param text_entry Literal text to log.
   * @return Result - value from Log::ReturnCodeFlags.
   *
   * @see Log::ReturnCodeFlags.
   */
  int log(LogAccess *lad, std::string_view text_entry);

  int va_log(LogAccess *lad, const char *fmt, va_list ap);

  unsigned roll_files(long time_now = 0);

  inline int
  add_to_flush_queue(LogBuffer *buffer)
  {
    int idx = m_buffer_manager_idx++ % m_flush_threads;

    m_buffer_manager[idx].add_to_flush_queue(buffer);

    return idx;
  }

  inline size_t
  preproc_buffers(int idx = -1)
  {
    size_t nfb;

    if (idx == -1) {
      idx = m_buffer_manager_idx++ % m_flush_threads;
    }

    nfb = m_buffer_manager[idx].preproc_buffers(m_logFile.get());

    return nfb;
  }

  void check_buffer_expiration(long time_now);

  void display(FILE *fd = stdout);
  static uint64_t compute_signature(LogFormat *format, char *filename, unsigned int flags);

  inline const char *
  get_original_filename() const
  {
    return m_filename;
  }
  inline const char *
  get_full_filename() const
  {
    return (m_alt_filename ? m_alt_filename : m_filename);
  }
  inline const char *
  get_base_filename() const
  {
    return m_basename;
  }

  off_t get_file_size_bytes();

  inline uint64_t
  get_signature() const
  {
    return m_signature;
  }

  inline int
  get_rolling_interval() const
  {
    return m_rolling_interval_sec;
  }

  inline void
  set_log_file_header(const char *header)
  {
    m_logFile->change_header(header);
  }

  inline void
  set_rolling_enabled(Log::RollingEnabledValues rolling_enabled)
  {
    _setup_rolling(rolling_enabled, m_rolling_interval_sec, m_rolling_offset_hr, m_rolling_size_mb);
  }

  inline void
  set_rolling_interval_sec(int rolling_interval_sec)
  {
    _setup_rolling(m_rolling_enabled, rolling_interval_sec, m_rolling_offset_hr, m_rolling_size_mb);
  }

  inline void
  set_rolling_offset_hr(int rolling_offset_hr)
  {
    _setup_rolling(m_rolling_enabled, m_rolling_interval_sec, rolling_offset_hr, m_rolling_size_mb);
  }

  inline void
  set_rolling_size_mb(int rolling_size_mb)
  {
    _setup_rolling(m_rolling_enabled, m_rolling_interval_sec, m_rolling_offset_hr, rolling_size_mb);
  }

  inline bool
  writes_to_pipe() const
  {
    return (m_flags & WRITES_TO_PIPE) ? true : false;
  }
  inline bool
  writes_to_disk()
  {
    return (m_logFile && !(m_flags & WRITES_TO_PIPE) ? true : false);
  }

  inline unsigned int
  get_flags() const
  {
    return m_flags;
  }

  void rename(char *new_name);

  inline bool
  has_alternate_name() const
  {
    return (m_alt_filename ? true : false);
  }

  inline const char *
  get_format_string()
  {
    return (m_format ? m_format->format_string() : "<none>");
  }

  inline void
  force_new_buffer()
  {
    _checkout_write(nullptr, 0);
  }

  bool operator==(LogObject &rhs);

public:
  LogFormat *m_format;
  Ptr<LogFile> m_logFile;
  LogFilterList m_filter_list;

private:
  char *m_basename; // the name of the file associated
  // with this object, relative to
  // the logging directory
  char *m_filename; // the full path of the file associated
  // with this object
  char *m_alt_filename; // the full path of the file used
  // instead of m_filename if the latter
  // could not be used because of
  // name conflicts

  unsigned int m_flags; // diverse object flags (see above)
  uint64_t m_signature; // INK_MD5 signature for object

  Log::RollingEnabledValues m_rolling_enabled;
  int m_flush_threads;        // number of flush threads
  int m_rolling_interval_sec; // time interval between rolls
  // 0 means no rolling
  int m_rolling_offset_hr;     //
  int m_rolling_size_mb;       // size at which the log file rolls
  long m_last_roll_time;       // the last time this object rolled its files
  int m_max_rolled;            // maximum number of rolled logs to be kept, 0 no limit
  int m_min_rolled;            // minimum number of rolled logs to be kept, 0 no limit
  bool m_reopen_after_rolling; // reopen log file after rolling (normally it is just renamed and closed)

  head_p m_log_buffer; // current work buffer
  unsigned m_buffer_manager_idx;
  LogBufferManager *m_buffer_manager;

  int m_pipe_buffer_size;

  void generate_filenames(const char *log_dir, const char *basename, LogFileFormat file_format);
  void _setup_rolling(Log::RollingEnabledValues rolling_enabled, int rolling_interval_sec, int rolling_offset_hr,
                      int rolling_size_mb);
  unsigned _roll_files(long interval_start, long interval_end);

  LogBuffer *_checkout_write(size_t *write_offset, size_t write_size);

  // noncopyable
  LogObject(const LogObject &) = delete;
  LogObject &operator=(const LogObject &) = delete;

private:
  // -- member functions not allowed --
  LogObject();
};

/*-------------------------------------------------------------------------
  TextLog
  -------------------------------------------------------------------------*/

class TextLogObject : public LogObject
{
public:
  inkcoreapi TextLogObject(const char *name, const char *log_dir, bool timestamps, const char *header,
                           Log::RollingEnabledValues rolling_enabled, int flush_threads, int rolling_interval_sec,
                           int rolling_offset_hr, int rolling_size_mb, int rolling_max_count, int rolling_min_count,
                           bool reopen_after_rolling);

  inkcoreapi int write(const char *format, ...) TS_PRINTFLIKE(2, 3);
  inkcoreapi int va_write(const char *format, va_list ap);

  static const LogFormat *textfmt;
};

/*-------------------------------------------------------------------------
  LogObjectManager

  A log object manager keeps track of log objects and is responsible for
  their deletion
  -------------------------------------------------------------------------*/

class LogObjectManager
{
public:
  // error status
  //
  enum {
    NO_FILENAME_CONFLICTS = 0,
    ERROR_ACCESSING_LOG_FILE,
    ERROR_DETERMINING_FILE_INFO,
    CANNOT_SOLVE_FILENAME_CONFLICTS,
    ERROR_DOING_FILESYSTEM_CHECKS
  };

private:
  typedef std::vector<LogObject *> LogObjectList;

  LogObjectList _objects;    // array of configured objects
  LogObjectList _APIobjects; // array of API objects

public:
  ink_mutex *_APImutex; // synchronize access to array of API objects
private:
  int _manage_object(LogObject *log_object, bool is_api_object, int maxConflicts);
  static bool _has_internal_filename_conflict(const char *filename, LogObjectList &objects);
  int _solve_filename_conflicts(LogObject *log_obj, int maxConflicts);
  int _solve_internal_filename_conflicts(LogObject *log_obj, int maxConflicts, int fileNum = 0);
  void _filename_resolution_abort(const char *fname);

public:
  LogObjectManager();
  ~LogObjectManager();

  // we don't define a destructor because the objects that the
  // LogObjectManager manages are either passed along to another
  // manager or moved to the list of inactive objects to be destroyed

  int
  manage_object(LogObject *logObject, int maxConflicts = 99)
  {
    return _manage_object(logObject, false, maxConflicts);
  }

  int
  manage_api_object(LogObject *logObject, int maxConflicts = 99)
  {
    return _manage_object(logObject, true, maxConflicts);
  }

  // return success
  bool unmanage_api_object(LogObject *logObject);

  // Flush the buffers on all the managed log objects.
  void flush_all_objects();

  LogObject *get_object_with_signature(uint64_t signature);
  void check_buffer_expiration(long time_now);

  unsigned roll_files(long time_now);
  void reopen_moved_log_files();

  int log(LogAccess *lad);
  void display(FILE *str = stdout);
  void add_filter_to_all(LogFilter *filter);
  LogObject *find_by_format_name(const char *name) const;
  size_t preproc_buffers(int idx);
  void open_local_pipes();
  void transfer_objects(LogObjectManager &mgr);

  bool
  has_api_objects() const
  {
    return _APIobjects.size() > 0;
  }
  unsigned
  get_num_objects() const
  {
    return _objects.size();
  }
};

inline bool
LogObject::operator==(LogObject &old)
{
  return (get_signature() == old.get_signature() && m_logFile && old.m_logFile &&
          strcmp(m_logFile->get_name(), old.m_logFile->get_name()) == 0 && (m_filter_list == old.m_filter_list) &&
          (m_rolling_interval_sec == old.m_rolling_interval_sec && m_rolling_offset_hr == old.m_rolling_offset_hr &&
           m_rolling_size_mb == old.m_rolling_size_mb && m_reopen_after_rolling == old.m_reopen_after_rolling &&
           m_max_rolled == old.m_max_rolled && m_min_rolled == old.m_min_rolled));
}

inline off_t
LogObject::get_file_size_bytes()
{
  return m_logFile->get_size_bytes();
}
