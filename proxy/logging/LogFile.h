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


#ifndef LOG_FILE_H
#define LOG_FILE_H

#include <stdarg.h>
#include <stdio.h>

#include "libts.h"
#include "LogBufferSink.h"

class LogSock;
class LogBuffer;
struct LogBufferHeader;
class LogObject;

#define LOGFILE_ROLLED_EXTENSION ".old"
#define LOGFILE_SEPARATOR_STRING "_"

/*-------------------------------------------------------------------------
  MetaInfo

  Meta information for LogFile
  -------------------------------------------------------------------------*/
class MetaInfo
{
public:
  enum {
    DATA_FROM_METAFILE = 1, // metadata was read (or attempted to)
    // from metafile
    VALID_CREATION_TIME = 2, // creation time is valid
    VALID_SIGNATURE = 4,     // signature is valid
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
  MetaInfo(const char *filename) : _flags(0)
  {
    _build_name(filename);
    _read_from_file();
  }

  MetaInfo(char *filename, time_t creation, uint64_t signature)
    : _creation_time(creation), _log_object_signature(signature), _flags(VALID_CREATION_TIME | VALID_SIGNATURE)
  {
    _build_name(filename);
    _write_to_file();
  }

  ~MetaInfo() { ats_free(_filename); }

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

/*-------------------------------------------------------------------------
  LogFile
  -------------------------------------------------------------------------*/

class LogFile : public LogBufferSink, public RefCountObj
{
public:
  LogFile(const char *name, const char *header, LogFileFormat format, uint64_t signature, size_t ascii_buffer_size = 4 * 9216,
          size_t max_line_size = 9216);
  LogFile(const LogFile &);
  ~LogFile();

  enum {
    LOG_FILE_NO_ERROR = 0,
    LOG_FILE_NO_PIPE_READERS,
    LOG_FILE_COULD_NOT_CREATE_PIPE,
    LOG_FILE_PIPE_MODE_NOT_SUPPORTED,
    LOG_FILE_COULD_NOT_OPEN_FILE,
    LOG_FILE_FILESYSTEM_CHECKS_FAILED
  };

  int preproc_and_try_delete(LogBuffer *lb);

  int roll(long interval_start, long interval_end);

  const char *
  get_name() const
  {
    return m_name;
  }

  void change_header(const char *header);
  void change_name(const char *new_name);

  LogFileFormat
  get_format() const
  {
    return m_file_format;
  }
  const char *
  get_format_name() const
  {
    return (m_file_format == LOG_FILE_BINARY ? "binary" : (m_file_format == LOG_FILE_PIPE ? "ascii_pipe" : "ascii"));
  }

  static int write_ascii_logbuffer(LogBufferHeader *buffer_header, int fd, const char *path, const char *alt_format = NULL);
  int write_ascii_logbuffer3(LogBufferHeader *buffer_header, const char *alt_format = NULL);
  static bool rolled_logfile(char *file);
  static bool exists(const char *pathname);

  void display(FILE *fd = stdout);
  int open_file();

  off_t
  get_size_bytes() const
  {
    return m_file_format != LOG_FILE_PIPE ? m_bytes_written : 0;
  };
  int
  do_filesystem_checks()
  {
    return 0;
  }; // TODO: this need to be tidy up when to redo the file checking

public:
  bool
  is_open()
  {
    return (m_fd >= 0);
  }
  void close_file();

  void check_fd();
  static int writeln(char *data, int len, int fd, const char *path);
  void read_metadata();

public:
  LogFileFormat m_file_format;

private:
  char *m_name;

public:
  char *m_header;
  uint64_t m_signature; // signature of log object stored
  MetaInfo *m_meta_info;

  size_t m_ascii_buffer_size; // size of ascii buffer
  size_t m_max_line_size;     // size of longest log line (record)

  int m_fd;
  long m_start_time;
  long m_end_time;
  volatile uint64_t m_bytes_written;
  off_t m_size_bytes; // current size of file in bytes

public:
  Link<LogFile> link;

private:
  // -- member functions not allowed --
  LogFile();
  LogFile &operator=(const LogFile &);
};

/***************************************************************************
 LogFileList IS NOT USED
****************************************************************************/

#endif
