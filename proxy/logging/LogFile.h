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

#include <cstdarg>
#include <cstdio>

#include "ts/ink_platform.h"
#include "LogBufferSink.h"

class LogSock;
class LogBuffer;
struct LogBufferHeader;
class LogObject;
class BaseLogFile;
class BaseMetaInfo;

/*-------------------------------------------------------------------------
  LogFile
  -------------------------------------------------------------------------*/

class LogFile : public LogBufferSink, public RefCountObj
{
public:
  LogFile(const char *name, const char *header, LogFileFormat format, uint64_t signature, size_t ascii_buffer_size = 4 * 9216,
          size_t max_line_size = 9216);
  LogFile(const LogFile &);
  ~LogFile() override;

  enum {
    LOG_FILE_NO_ERROR = 0,
    LOG_FILE_NO_PIPE_READERS,
    LOG_FILE_COULD_NOT_CREATE_PIPE,
    LOG_FILE_PIPE_MODE_NOT_SUPPORTED,
    LOG_FILE_COULD_NOT_OPEN_FILE,
    LOG_FILE_FILESYSTEM_CHECKS_FAILED
  };

  int preproc_and_try_delete(LogBuffer *lb) override;

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

  static int write_ascii_logbuffer(LogBufferHeader *buffer_header, int fd, const char *path, const char *alt_format = nullptr);
  int write_ascii_logbuffer3(LogBufferHeader *buffer_header, const char *alt_format = nullptr);
  static bool rolled_logfile(char *file);
  static bool exists(const char *pathname);

  void display(FILE *fd = stdout);
  int open_file();

  off_t
  get_size_bytes() const
  {
    if (m_file_format == LOG_FILE_PIPE)
      return 0;
    else if (m_log)
      return m_log->get_size_bytes();
    else
      return 0;
  }

public:
  bool is_open();
  void close_file();
  void check_fd();
  int get_fd();
  static int writeln(char *data, int len, int fd, const char *path);

public:
  LogFileFormat m_file_format;

private:
  char *m_name;

public:
  BaseLogFile *m_log; // BaseLogFile backs the actual file on disk
  char *m_header;
  uint64_t m_signature;       // signature of log object stored
  size_t m_ascii_buffer_size; // size of ascii buffer
  size_t m_max_line_size;     // size of longest log line (record)
  int m_fd;                   // this could back m_log or a pipe, depending on the situation

public:
  Link<LogFile> link;
  // noncopyable
  LogFile &operator=(const LogFile &) = delete;

private:
  // -- member functions not allowed --
  LogFile();
};
