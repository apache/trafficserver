/** @file

  Base class for log files

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

#include <cstdarg>
#include <cstdio>
#include <sys/time.h>
#include <cstdint>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "ink_memory.h"
#include "ink_string.h"
#include "ink_file.h"
#include "ink_cap.h"
#include "ink_time.h"
#include "SimpleTokenizer.h"

#define LOGFILE_ROLLED_EXTENSION ".old"
#define LOGFILE_SEPARATOR_STRING "_"
#define LOGFILE_DEFAULT_PERMS (0644)
#define LOGFILE_ROLL_MAXPATHLEN 4096
#define BASELOGFILE_DEBUG_MODE \
  0 // change this to 1 to enable debug messages
    // TODO find a way to enable this from autotools

typedef enum {
  LL_Debug = 0, // process does not die
  LL_Note,      // process does not die
  LL_Warning,   // process does not die
  LL_Error,     // process does not die
  LL_Fatal,     // causes process termination
} LogLogPriorityLevel;

#define log_log_trace(...)                         \
  do {                                             \
    if (BASELOGFILE_DEBUG_MODE)                    \
      BaseLogFile::log_log(LL_Debug, __VA_ARGS__); \
  } while (0)

#define log_log_error(...)                         \
  do {                                             \
    if (BASELOGFILE_DEBUG_MODE)                    \
      BaseLogFile::log_log(LL_Error, __VA_ARGS__); \
  } while (0)

/*
 *
 * BaseMetaInfo class
 *
 * Used to store persistent information between ATS instances
 *
 */
class BaseMetaInfo
{
public:
  enum {
    DATA_FROM_METAFILE = 1, // metadata was read (or attempted to)
    // from metafile
    VALID_CREATION_TIME = 2, // creation time is valid
    VALID_SIGNATURE     = 4, // signature is valid
    // (i.e., creation time only)
    FILE_OPEN_SUCCESSFUL = 8 // metafile was opened successfully
  };

  enum {
    BUF_SIZE = 640 // size of read/write buffer
  };

private:
  char *_filename;                // the name of the meta file
  time_t _creation_time;          // file creation time
  uint64_t _log_object_signature; // log object signature
  int _flags;                     // metainfo status flags
  char _buffer[BUF_SIZE];         // read/write buffer

  void _read_from_file();
  void _write_to_file();
  void _build_name(const char *filename);

public:
  BaseMetaInfo(const char *filename) : _flags(0)
  {
    _build_name(filename);
    _read_from_file();
  }

  BaseMetaInfo(char *filename, time_t creation) : _creation_time(creation), _log_object_signature(0), _flags(VALID_CREATION_TIME)
  {
    _build_name(filename);
    _write_to_file();
  }

  BaseMetaInfo(char *filename, time_t creation, uint64_t signature)
    : _creation_time(creation), _log_object_signature(signature), _flags(VALID_CREATION_TIME | VALID_SIGNATURE)
  {
    _build_name(filename);
    _write_to_file();
  }

  ~BaseMetaInfo() { ats_free(_filename); }
  bool
  get_creation_time(time_t *time)
  {
    if (_flags & VALID_CREATION_TIME) {
      *time = _creation_time;
      return true;
    } else {
      return false;
    }
  }

  bool
  get_log_object_signature(uint64_t *signature)
  {
    if (_flags & VALID_SIGNATURE) {
      *signature = _log_object_signature;
      return true;
    } else {
      return false;
    }
  }

  bool
  data_from_metafile() const
  {
    return (_flags & DATA_FROM_METAFILE ? true : false);
  }

  bool
  file_open_successful()
  {
    return (_flags & FILE_OPEN_SUCCESSFUL ? true : false);
  }
};

/*
 *
 * BaseLogFile Class
 *
 */
class BaseLogFile
{
public:
  // member functions
  BaseLogFile()        = delete;
  BaseLogFile &operator=(const BaseLogFile &) = delete;
  BaseLogFile(const char *name);
  BaseLogFile(const char *name, uint64_t sig);
  BaseLogFile(const BaseLogFile &);
  ~BaseLogFile();
  int roll();
  int roll(long interval_start, long interval_end);
  static bool rolled_logfile(char *path);
  static bool exists(const char *pathname);
  int open_file(int perm = -1);
  void close_file();
  void change_name(const char *new_name);
  void display(FILE *fd = stdout);
  const char *
  get_name() const
  {
    return m_name;
  }
  bool
  is_open()
  {
    return (m_fp != nullptr);
  }
  off_t
  get_size_bytes() const
  {
    return m_bytes_written;
  }
  bool
  is_init()
  {
    return m_is_init;
  }
  const char *
  get_hostname() const
  {
    return m_hostname;
  }
  void
  set_hostname(const char *hn)
  {
    m_hostname = ats_strdup(hn);
  }

  static void log_log(LogLogPriorityLevel priority, const char *format, ...);

  // member variables
  enum {
    LOG_FILE_NO_ERROR = 0,
    LOG_FILE_COULD_NOT_OPEN_FILE,
  };

  FILE *m_fp               = nullptr;
  long m_start_time        = time(nullptr);
  long m_end_time          = 0L;
  uint64_t m_bytes_written = 0;

private:
  // member functions
  int timestamp_to_str(long timestamp, char *buf, int size);

  // member variables
  ats_scoped_str m_name;
  ats_scoped_str m_hostname;
  bool m_is_regfile         = false;
  bool m_is_init            = false;
  BaseMetaInfo *m_meta_info = nullptr;
  uint64_t m_signature      = 0;
  bool m_has_signature      = false;
};
