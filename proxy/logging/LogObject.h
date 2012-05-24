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



#ifndef LOG_OBJECT_H
#define LOG_OBJECT_H

#include "libts.h"
#include "Log.h"
#include "LogFile.h"
#include "LogFormat.h"
#include "LogFilter.h"
#include "LogHost.h"
#include "LogBuffer.h"
#include "LogAccess.h"
#include "LogFilter.h"
#include "SimpleTokenizer.h"

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

#define ASCII_LOG_OBJECT_FILENAME_EXTENSION ".log"
#define BINARY_LOG_OBJECT_FILENAME_EXTENSION ".blog"
#define ASCII_PIPE_OBJECT_FILENAME_EXTENSION ".pipe"

#define FLUSH_ARRAY_SIZE (512*4)

#define LOG_OBJECT_ARRAY_DELTA 8

#define ACQUIRE_API_MUTEX(_f) \
ink_mutex_acquire(_APImutex); \
Debug("log-api-mutex", _f)

#define RELEASE_API_MUTEX(_f) \
ink_mutex_release(_APImutex); \
Debug("log-api-mutex", _f)

class LogBufferManager
{
  private:
    ASLL(LogBuffer, write_link) write_list;
    int _num_flush_buffers;

  public:
    LogBufferManager() : _num_flush_buffers(0) { }

    void add_to_flush_queue(LogBuffer *buffer) {
    write_list.push(buffer);
    ink_atomic_increment(&_num_flush_buffers, 1);
    }

    size_t flush_buffers(LogBufferSink *sink);
};

class LogObject
{
public:
  enum LogObjectFlags
  {
    BINARY = 1,
    REMOTE_DATA = 2,
    WRITES_TO_PIPE = 4
  };

  // BINARY: log is written in binary format (rather than ascii)
  // REMOTE_DATA: object receives data from remote collation clients, so
  //              it should not be destroyed during a reconfiguration
  // WRITES_TO_PIPE: object writes to a named pipe rather than to a file

  LogObject(LogFormat *format, const char *log_dir, const char *basename,
                 LogFileFormat file_format, const char *header,
                 int rolling_enabled, int rolling_interval_sec = 0,
                 int rolling_offset_hr = 0, int rolling_size_mb = 0);
  LogObject(LogObject &);
  virtual ~LogObject();

  void add_filter(LogFilter * filter, bool copy = true);
  void set_filter_list(const LogFilterList & list, bool copy = true);
  void add_loghost(LogHost * host, bool copy = true);

  void set_remote_flag() { m_flags |= REMOTE_DATA; };

  int log(LogAccess * lad, char *text_entry = NULL);

  int roll_files(long time_now = 0);

  void add_to_flush_queue(LogBuffer * buffer) { m_buffer_manager.add_to_flush_queue(buffer); }

  size_t flush_buffers()
  {
    size_t nfb;

    if (m_logFile) {
      nfb = m_buffer_manager.flush_buffers(m_logFile);
    } else {
      nfb = m_buffer_manager.flush_buffers(&m_host_list);
    }
    return nfb;
  }

  void check_buffer_expiration(long time_now);

  void display(FILE * fd = stdout);
  void displayAsXML(FILE * fd = stdout, bool extended = false);
  static uint64_t compute_signature(LogFormat * format, char *filename, unsigned int flags);

  char *get_original_filename() const { return m_filename; }
  char *get_full_filename() const { return (m_alt_filename ? m_alt_filename : m_filename); }
  char *get_base_filename() const { return m_basename; }

  off_t get_file_size_bytes();

  uint64_t get_signature() const { return m_signature; }

  int get_rolling_interval() const { return m_rolling_interval_sec; }

  void set_log_file_header(const char *header) { m_logFile->change_header(header); }

  void set_rolling_enabled(int rolling_enabled)
  {
    _setup_rolling(rolling_enabled, m_rolling_interval_sec, m_rolling_offset_hr, m_rolling_size_mb);
  }

  void set_rolling_interval_sec(int rolling_interval_sec)
  {
    _setup_rolling(m_rolling_enabled, rolling_interval_sec, m_rolling_offset_hr, m_rolling_size_mb);
  }

 void set_rolling_offset_hr(int rolling_offset_hr)
  {
    _setup_rolling(m_rolling_enabled, m_rolling_interval_sec, rolling_offset_hr, m_rolling_size_mb);
  }

  void set_rolling_size_mb(int rolling_size_mb)
  {
    m_rolling_size_mb = rolling_size_mb;
  }

  bool is_collation_client() const { return (m_logFile ? false : true); }
  bool receives_remote_data() const { return m_flags & REMOTE_DATA ? true : false; }
  bool writes_to_pipe() const { return m_flags & WRITES_TO_PIPE ? true : false; }
  bool writes_to_disk() { return (m_logFile && !(m_flags & WRITES_TO_PIPE) ? true : false); }

  unsigned int get_flags() const { return m_flags; }

  void rename(char *new_name);

  bool has_alternate_name() const { return (m_alt_filename ? true : false); }

  const char *get_format_string() { return (m_format ? m_format->format_string() : "<none>"); }

  void force_new_buffer() {
    _checkout_write(NULL, 0);
  }

  bool operator==(LogObject & rhs);
  int do_filesystem_checks();

public:
  LogFormat * m_format;
  LogFile *m_logFile;
  LogFilterList m_filter_list;
  LogHostList m_host_list;

private:
  char *m_basename;             // the name of the file associated
  // with this object, relative to
  // the logging directory
  char *m_filename;             // the full path of the file associated
  // with this object
  char *m_alt_filename;         // the full path of the file used
  // instead of m_filename if the latter
  // could not be used because of
  // name conflicts

  unsigned int m_flags;         // diverse object flags (see above)
  uint64_t m_signature;           // INK_MD5 signature for object

  int m_rolling_enabled;
  int m_rolling_interval_sec;   // time interval between rolls
  // 0 means no rolling
  int m_rolling_offset_hr;      //
  int m_rolling_size_mb;        // size at which the log file rolls
  long m_last_roll_time;        // the last time this object rolled
  // its files

  int m_ref_count;

  volatile head_p m_log_buffer;     // current work buffer
  LogBufferManager m_buffer_manager;

  void generate_filenames(const char *log_dir, const char *basename, LogFileFormat file_format);
  void _setup_rolling(int rolling_enabled, int rolling_interval_sec, int rolling_offset_hr, int rolling_size_mb);
#ifndef TS_MICRO
  int _roll_files(long interval_start, long interval_end);
#endif

  LogBuffer *_checkout_write(size_t * write_offset, size_t write_size);

private:
  // -- member functions not allowed --
  LogObject();
  LogObject & operator=(const LogObject &);
};

/*-------------------------------------------------------------------------
  TextLog
  -------------------------------------------------------------------------*/

class TextLogObject:public LogObject
{
public:
  inkcoreapi TextLogObject(const char *name, const char *log_dir,
                           bool timestamps, const char *header,
                           int rolling_enabled, int rolling_interval_sec = 0,
                           int rolling_offset_hr = 0, int rolling_size_mb = 0);

  inkcoreapi int write(const char *format, ...);
  inkcoreapi int va_write(const char *format, va_list ap);

private:
    bool m_timestamps;
};

/*-------------------------------------------------------------------------
  RefCounter
  -------------------------------------------------------------------------*/
class RefCounter
{
public:
  RefCounter(int *count)
    : m_count(count)
  {
    ink_atomic_increment(m_count, 1);
  }

  ~RefCounter() {
    ink_atomic_increment(m_count, -1);
  }

private:
  int *m_count;
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
  enum
  {
    NO_FILENAME_CONFLICTS = 0,
    ERROR_ACCESSING_LOG_FILE,
    ERROR_DETERMINING_FILE_INFO,
    CANNOT_SOLVE_FILENAME_CONFLICTS,
    ERROR_DOING_FILESYSTEM_CHECKS
  };

private:

  LogObject ** _objects;      // array of objects managed
  size_t _numObjects;           // the number of objects managed
  size_t _maxObjects;           // the maximum capacity of the array
  // of objects managed

  LogObject **_APIobjects;      // array of API objects
  size_t _numAPIobjects;        // the number of API objects managed
  size_t _maxAPIobjects;        // the maximum capacity of the array
  // of API objects managed

public:
    ink_mutex * _APImutex;      // synchronize access to array of API
  // objects
private:

  int _manage_object(LogObject * log_object, bool is_api_object, int maxConflicts);
#ifndef TS_MICRO
  static bool _has_internal_filename_conflict(char *filename, uint64_t signature, LogObject ** objects, int numObjects);
#endif                          // TS_MICRO
  int _solve_filename_conflicts(LogObject * log_obj, int maxConflicts);
  int _solve_internal_filename_conflicts(LogObject * log_obj, int maxConflicts, int fileNum = 0);
  void _add_object(LogObject * object);
  void _add_api_object(LogObject * object);
  int _roll_files(long time_now, bool roll_only_if_needed);

public:
 LogObjectManager()
   : _numObjects(0), _maxObjects(LOG_OBJECT_ARRAY_DELTA), _numAPIobjects(0), _maxAPIobjects(LOG_OBJECT_ARRAY_DELTA)
  {
    _objects = new LogObject *[_maxObjects];
    _APIobjects = new LogObject *[_maxAPIobjects];
    _APImutex = NEW(new ink_mutex);
    ink_mutex_init(_APImutex, "_APImutex");
  }

  ~LogObjectManager() {
    for (unsigned int i = 0; i < _maxObjects; i++) {
      delete _objects[i];
    }
    delete[] _objects;

    for (unsigned int i = 0; i < _maxAPIobjects; i++) {
      delete _APIobjects[i];
    }
    delete[] _APIobjects;

    delete _APImutex;
  }

  // we don't define a destructor because the objects that the
  // LogObjectManager manages are either passed along to another
  // manager or moved to the list of inactive objects to be destroyed

  int manage_object(LogObject * logObject, int maxConflicts = 99) {
    return _manage_object(logObject, false, maxConflicts);
  }

  int manage_api_object(LogObject * logObject, int maxConflicts = 99) {
    return _manage_object(logObject, true, maxConflicts);
  }

  // return success
  bool unmanage_api_object(LogObject * logObject);

  LogObject *get_object_with_signature(uint64_t signature);
  void check_buffer_expiration(long time_now);
  size_t get_num_objects() const { return _numObjects; }

  int roll_files(long time_now);

  int log(LogAccess * lad);
  void display(FILE * str = stdout);
  void add_filter_to_all(LogFilter * filter);
  LogObject *find_by_format_name(const char *name);
  size_t flush_buffers();
  void open_local_pipes();
  void transfer_objects(LogObjectManager & mgr);

  bool has_api_objects() const  { return (_numAPIobjects > 0); }

  size_t get_num_collation_clients();
};

inline int LogObjectManager::roll_files(long time_now)
{
    int num_rolled = 0;
    for (size_t i=0; i < _numObjects; i++) {
      num_rolled += _objects[i]->roll_files(time_now);
    }
    return num_rolled;
};

inline int
LogObjectManager::log(LogAccess * lad)
{
  int ret = 0;
  for (size_t i = 0; i < _numObjects; i++) {
    ret |= _objects[i]->log(lad);
  }
  return ret;
}

inline void
LogObjectManager::display(FILE * str)
{
  for (size_t i = 0; i < _numObjects; i++) {
    _objects[i]->display(str);
  }
}

inline LogObject *
LogObjectManager::find_by_format_name(const char *name)
{
  for (size_t i = 0; i < _numObjects; i++) {
    if (_objects[i]->m_format->name_id() == LogFormat::id_from_name(name)) {
      return _objects[i];
    }
  }
  return NULL;
}

inline size_t
LogObjectManager::get_num_collation_clients()
{
  size_t coll_clients = 0;
  for (size_t i = 0; i < _numObjects; i++) {
    if (_objects[i]->is_collation_client()) {
      ++coll_clients;
    }
  }
  return coll_clients;
}

inline bool
LogObject::operator==(LogObject & old)
{
  if (!receives_remote_data() && !old.receives_remote_data()) {
    return (get_signature() == old.get_signature() &&
            (is_collation_client() && old.is_collation_client()?
             m_host_list == old.m_host_list :
             m_logFile && old.m_logFile &&
             strcmp(m_logFile->get_name(), old.m_logFile->get_name()) == 0) &&
            (m_filter_list == old.m_filter_list) &&
            (m_rolling_interval_sec == old.m_rolling_interval_sec &&
             m_rolling_offset_hr == old.m_rolling_offset_hr && m_rolling_size_mb == old.m_rolling_size_mb));
  }
  return false;
}

inline off_t
LogObject::get_file_size_bytes()
{
  if (m_logFile) {
    return m_logFile->get_size_bytes();
  } else {
    off_t max_size = 0;
    LogHost *host;
    for (host = m_host_list.first(); host; host = m_host_list.next(host)) {
      LogFile *orphan_logfile = host->get_orphan_logfile();
      if (orphan_logfile) {
        off_t s = orphan_logfile->get_size_bytes();
        if (s > max_size)
          max_size = s;
      }
    }
    return max_size;
  }
}

#endif
