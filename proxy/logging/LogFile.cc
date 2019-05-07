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
 LogFile.cc


 ***************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/SimpleTokenizer.h"
#include "tscore/ink_file.h"

#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "P_EventSystem.h"
#include "I_Machine.h"

#include "tscore/BaseLogFile.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogBuffer.h"
#include "LogFile.h"
#include "LogObject.h"
#include "LogUtils.h"
#include "LogConfig.h"
#include "Log.h"

/*-------------------------------------------------------------------------
  LogFile::LogFile

  This constructor builds a LogFile object given the path, filename, header,
  and logfile format type.  This is the common way to create a new logfile.
  -------------------------------------------------------------------------*/

LogFile::LogFile(const char *name, const char *header, LogFileFormat format, uint64_t signature, size_t ascii_buffer_size,
                 size_t max_line_size)
  : m_file_format(format),
    m_name(ats_strdup(name)),
    m_header(ats_strdup(header)),
    m_signature(signature),
    m_max_line_size(max_line_size)
{
  if (m_file_format != LOG_FILE_PIPE) {
    m_log = new BaseLogFile(name, m_signature);
    m_log->set_hostname(Machine::instance()->hostname);
  } else {
    m_log = nullptr;
  }

  m_fd                = -1;
  m_ascii_buffer_size = (ascii_buffer_size < max_line_size ? max_line_size : ascii_buffer_size);

  Debug("log-file", "exiting LogFile constructor, m_name=%s, this=%p", m_name, this);
}

/*-------------------------------------------------------------------------
  LogFile::LogFile

  This (copy) contructor builds a LogFile object from another LogFile object.
  -------------------------------------------------------------------------*/

LogFile::LogFile(const LogFile &copy)
  : RefCountObj(copy),
    m_file_format(copy.m_file_format),
    m_name(ats_strdup(copy.m_name)),
    m_header(ats_strdup(copy.m_header)),
    m_signature(copy.m_signature),
    m_ascii_buffer_size(copy.m_ascii_buffer_size),
    m_max_line_size(copy.m_max_line_size),
    m_fd(copy.m_fd)
{
  ink_release_assert(m_ascii_buffer_size >= m_max_line_size);

  if (copy.m_log) {
    m_log = new BaseLogFile(*(copy.m_log));
  } else {
    m_log = nullptr;
  }

  Debug("log-file", "exiting LogFile copy constructor, m_name=%s, this=%p", m_name, this);
}
/*-------------------------------------------------------------------------
  LogFile::~LogFile
  -------------------------------------------------------------------------*/

LogFile::~LogFile()
{
  Debug("log-file", "entering LogFile destructor, this=%p", this);
  delete m_log;
  ats_free(m_header);
  ats_free(m_name);
  Debug("log-file", "exiting LogFile destructor, this=%p", this);
}

/*-------------------------------------------------------------------------
  LogFile::change_name
  -------------------------------------------------------------------------*/

void
LogFile::change_name(const char *new_name)
{
  ats_free(m_name);
  if (m_log) {
    m_log->change_name(new_name);
  }
  m_name = ats_strdup(new_name);
}

/*-------------------------------------------------------------------------
  LogFile::change_header
  -------------------------------------------------------------------------*/

void
LogFile::change_header(const char *header)
{
  ats_free(m_header);
  m_header = ats_strdup(header);
}

/*-------------------------------------------------------------------------
  LogFile::open

  Open the logfile for append access.  This will create a logfile if the
  file does not already exist.
  -------------------------------------------------------------------------*/

int
LogFile::open_file()
{
  // whatever we want to open should have a name
  ink_assert(m_name != nullptr);

  // is_open() takes into account if we're using BaseLogFile or a naked fd
  if (is_open()) {
    return LOG_FILE_NO_ERROR;
  }

  bool file_exists = LogFile::exists(m_name);

  if (m_file_format == LOG_FILE_PIPE) {
    // setup pipe
    if (mkfifo(m_name, S_IRUSR | S_IWUSR | S_IRGRP) < 0) {
      if (errno != EEXIST) {
        Error("Could not create named pipe %s for logging: %s", m_name, strerror(errno));
        return LOG_FILE_COULD_NOT_CREATE_PIPE;
      }
    } else {
      Debug("log-file", "Created named pipe %s for logging", m_name);
    }

    // now open the pipe
    Debug("log-file", "attempting to open pipe %s", m_name);
    m_fd = ::open(m_name, O_WRONLY | O_NDELAY, 0);
    if (m_fd < 0) {
      Debug("log-file", "no readers for pipe %s", m_name);
      return LOG_FILE_NO_PIPE_READERS;
    }
  } else {
    if (m_log) {
      int status = m_log->open_file(Log::config->logfile_perm);
      if (status == BaseLogFile::LOG_FILE_COULD_NOT_OPEN_FILE) {
        return LOG_FILE_COULD_NOT_OPEN_FILE;
      }
    } else {
      return LOG_FILE_COULD_NOT_OPEN_FILE;
    }
  }

  //
  // If we've opened the file and it didn't already exist, then this is a
  // "new" file and we need to make some initializations.  This is the
  // time to write any headers and do any one-time initialization of the
  // file.
  //
  if (!file_exists) {
    if (m_file_format != LOG_FILE_BINARY && m_header && m_log) {
      Debug("log-file", "writing header to LogFile %s", m_name);
      writeln(m_header, strlen(m_header), fileno(m_log->m_fp), m_name);
    }
  }

  RecIncrRawStat(log_rsb, this_thread()->mutex->thread_holding, log_stat_log_files_open_stat, 1);

  Debug("log", "exiting LogFile::open_file(), file=%s presumably open", m_name);
  return LOG_FILE_NO_ERROR;
}

/*-------------------------------------------------------------------------
  LogFile::close

  Close the current logfile.
  -------------------------------------------------------------------------*/

void
LogFile::close_file()
{
  if (is_open()) {
    if (m_file_format == LOG_FILE_PIPE) {
      ::close(m_fd);
      Debug("log-file", "LogFile %s (fd=%d) is closed", m_name, m_fd);
      m_fd = -1;
    } else if (m_log) {
      m_log->close_file();
      Debug("log-file", "LogFile %s is closed", m_log->get_name());
    } else {
      Warning("LogFile %s is open but was not closed", m_name);
    }
  }

  RecIncrRawStat(log_rsb, this_thread()->mutex->thread_holding, log_stat_log_files_open_stat, -1);
}

/*-------------------------------------------------------------------------
 * LogFile::roll
 * This function is called by a LogObject to roll its files.
 *
 * Return 1 if file rolled, 0 otherwise
-------------------------------------------------------------------------*/
int
LogFile::roll(long interval_start, long interval_end)
{
  if (m_log) {
    // Due to commit 346b419 the BaseLogFile::close_file() is no longer called within BaseLogFile::roll().
    // For diagnostic log files, the rolling is implemented by renaming and destroying the BaseLogFile object
    // and then creating a new BaseLogFile object with the original filename. Due to possible race conditions
    // the old/new object swap happens within lock/unlock calls within Diags.cc.
    // For logging log files, the rolling is implemented by renaming the original file and closing it.
    // Afterwards, the LogFile object will re-open a new file with the original file name using the original object.
    // There is no need to protect against contention since the open/close/writes are all executed under a
    // single log flush thread.
    // Since these two methods of using BaseLogFile are not compatible, we perform the logging log file specific
    // close file operation here within the containing LogFile object.
    if (m_log->roll(interval_start, interval_end)) {
      m_log->close_file();
      return 1;
    }
  }

  return 0;
}

/*-------------------------------------------------------------------------
  LogFile::preproc_and_try_delete

  preprocess the given buffer data before write to target file
  and try to delete it when its reference become zero.
  -------------------------------------------------------------------------*/
int
LogFile::preproc_and_try_delete(LogBuffer *lb)
{
  int ret = -1;
  LogBufferHeader *buffer_header;

  if (lb == nullptr) {
    Note("Cannot write LogBuffer to LogFile %s; LogBuffer is NULL", m_name);
    return -1;
  }

  ink_atomic_increment(&lb->m_references, 1);

  if ((buffer_header = lb->header()) == nullptr) {
    Note("Cannot write LogBuffer to LogFile %s; LogBufferHeader is NULL", m_name);
    goto done;
  }
  if (buffer_header->entry_count == 0) {
    // no bytes to write
    Note("LogBuffer with 0 entries for LogFile %s, nothing to write", m_name);
    goto done;
  }

  //
  // If the start time for this file has yet to be established, then grab
  // the low_timestamp from the given LogBuffer.  Then, we always set the
  // end time to the high_timestamp, so it's always up to date.
  //
  if (m_log) {
    if (!m_log->m_start_time) {
      m_log->m_start_time = buffer_header->low_timestamp;
    }
    m_log->m_end_time = buffer_header->high_timestamp;
  }

  if (m_file_format == LOG_FILE_BINARY) {
    //
    // Ok, now we need to write the binary buffer to the file, and we
    // can do so in one swift write.  The question is, do we write the
    // LogBufferHeader with each buffer or not?  The answer is yes.
    // Even though we'll be puttint down redundant data (things that
    // don't change between buffers), it's not worth trying to separate
    // out the buffer-dependent data from the buffer-independent data.
    //
    LogFlushData *flush_data = new LogFlushData(this, lb);

    ProxyMutex *mutex = this_thread()->mutex.get();

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_num_flush_to_disk_stat, lb->header()->entry_count);

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_flush_to_disk_stat, lb->header()->byte_count);

    ink_atomiclist_push(Log::flush_data_list, flush_data);

    Log::flush_notify->signal();

    //
    // LogBuffer will be deleted in flush thread
    //
    return 0;
  } else if (m_file_format == LOG_FILE_ASCII || m_file_format == LOG_FILE_PIPE) {
    write_ascii_logbuffer3(buffer_header);
    ret = 0;
  } else {
    Note("Cannot write LogBuffer to LogFile %s; invalid file format: %d", m_name, m_file_format);
  }

done:
  LogBuffer::destroy(lb);
  return ret;
}

/*-------------------------------------------------------------------------
  LogFile::write_ascii_logbuffer

  This routine takes the given LogBuffer and writes it (in ASCII) to the
  given file descriptor.  Written as a stand-alone function, it can be
  called from either the local LogBuffer::write routine from inside of the
  proxy, or from an external program (since it is a static function).  The
  return value is the number of bytes written.
  -------------------------------------------------------------------------*/

int
LogFile::write_ascii_logbuffer(LogBufferHeader *buffer_header, int fd, const char *path, const char *alt_format)
{
  ink_assert(buffer_header != nullptr);
  ink_assert(fd >= 0);

  char fmt_buf[LOG_MAX_FORMATTED_BUFFER];
  char fmt_line[LOG_MAX_FORMATTED_LINE];
  LogBufferIterator iter(buffer_header);
  LogEntryHeader *entry_header;
  int fmt_buf_bytes  = 0;
  int fmt_line_bytes = 0;
  int bytes          = 0;

  LogFormatType format_type;
  char *fieldlist_str;
  char *printf_str;

  switch (buffer_header->version) {
  case LOG_SEGMENT_VERSION:
    format_type = (LogFormatType)buffer_header->format_type;

    fieldlist_str = buffer_header->fmt_fieldlist();
    printf_str    = buffer_header->fmt_printf();
    break;

  default:
    Note("Invalid LogBuffer version %d in write_ascii_logbuffer; "
         "current version is %d",
         buffer_header->version, LOG_SEGMENT_VERSION);
    return 0;
  }

  while ((entry_header = iter.next())) {
    fmt_line_bytes = LogBuffer::to_ascii(entry_header, format_type, &fmt_line[0], LOG_MAX_FORMATTED_LINE, fieldlist_str, printf_str,
                                         buffer_header->version, alt_format);
    ink_assert(fmt_line_bytes > 0);

    if (fmt_line_bytes > 0) {
      if ((fmt_line_bytes + fmt_buf_bytes) >= LOG_MAX_FORMATTED_BUFFER) {
        if (!Log::config->logging_space_exhausted) {
          bytes += writeln(fmt_buf, fmt_buf_bytes, fd, path);
        }
        fmt_buf_bytes = 0;
      }
      ink_assert(fmt_buf_bytes < LOG_MAX_FORMATTED_BUFFER);
      ink_assert(fmt_line_bytes < LOG_MAX_FORMATTED_BUFFER - fmt_buf_bytes);
      memcpy(&fmt_buf[fmt_buf_bytes], fmt_line, fmt_line_bytes);
      fmt_buf_bytes += fmt_line_bytes;
      ink_assert(fmt_buf_bytes < LOG_MAX_FORMATTED_BUFFER);
      fmt_buf[fmt_buf_bytes] = '\n'; // keep entries separate
      fmt_buf_bytes += 1;
    }
  }
  if (fmt_buf_bytes > 0) {
    if (!Log::config->logging_space_exhausted) {
      ink_assert(fmt_buf_bytes < LOG_MAX_FORMATTED_BUFFER);
      bytes += writeln(fmt_buf, fmt_buf_bytes, fd, path);
    }
  }

  return bytes;
}

int
LogFile::write_ascii_logbuffer3(LogBufferHeader *buffer_header, const char *alt_format)
{
  Debug("log-file",
        "entering LogFile::write_ascii_logbuffer3 for %s "
        "(this=%p)",
        m_name, this);
  ink_assert(buffer_header != nullptr);

  ProxyMutex *mutex = this_thread()->mutex.get();
  LogBufferIterator iter(buffer_header);
  LogEntryHeader *entry_header;
  int fmt_entry_count = 0;
  int fmt_buf_bytes   = 0;
  int total_bytes     = 0;

  LogFormatType format_type;
  char *fieldlist_str;
  char *printf_str;
  char *ascii_buffer;

  switch (buffer_header->version) {
  case LOG_SEGMENT_VERSION:
    format_type   = (LogFormatType)buffer_header->format_type;
    fieldlist_str = buffer_header->fmt_fieldlist();
    printf_str    = buffer_header->fmt_printf();
    break;

  default:
    Note("Invalid LogBuffer version %d in write_ascii_logbuffer; "
         "current version is %d",
         buffer_header->version, LOG_SEGMENT_VERSION);
    return 0;
  }

  while ((entry_header = iter.next())) {
    fmt_entry_count = 0;
    fmt_buf_bytes   = 0;

    if (m_file_format == LOG_FILE_PIPE) {
      ascii_buffer = (char *)ats_malloc(m_max_line_size);
    } else {
      ascii_buffer = (char *)ats_malloc(m_ascii_buffer_size);
    }

    // fill the buffer with as many records as possible
    //
    do {
      if (entry_header->entry_len >= m_max_line_size) {
        Warning("Log is too long(%" PRIu32 "), it would be truncated. max_len:%zu", entry_header->entry_len, m_max_line_size);
      }

      int bytes = LogBuffer::to_ascii(entry_header, format_type, &ascii_buffer[fmt_buf_bytes], m_max_line_size - 1, fieldlist_str,
                                      printf_str, buffer_header->version, alt_format);

      if (bytes > 0) {
        fmt_buf_bytes += bytes;
        ascii_buffer[fmt_buf_bytes] = '\n';
        ++fmt_buf_bytes;
        ++fmt_entry_count;
      } else {
        Note("Failed to convert LogBuffer to ascii, have dropped (%" PRIu32 ") bytes.", entry_header->entry_len);

        RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_num_lost_before_flush_to_disk_stat, fmt_entry_count);

        RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_lost_before_flush_to_disk_stat, fmt_buf_bytes);
      }
      // if writing to a pipe, fill the buffer with a single
      // record to avoid as much as possible overflowing the
      // pipe buffer
      //
      if (m_file_format == LOG_FILE_PIPE) {
        break;
      }

      if (m_ascii_buffer_size - fmt_buf_bytes < m_max_line_size) {
        break;
      }
    } while ((entry_header = iter.next()));

    // send the buffer to flush thread
    //
    LogFlushData *flush_data = new LogFlushData(this, ascii_buffer, fmt_buf_bytes);

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_num_flush_to_disk_stat, fmt_entry_count);

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_flush_to_disk_stat, fmt_buf_bytes);

    ink_atomiclist_push(Log::flush_data_list, flush_data);

    Log::flush_notify->signal();

    total_bytes += fmt_buf_bytes;
  }

  return total_bytes;
}

bool
LogFile::rolled_logfile(char *file)
{
  return BaseLogFile::rolled_logfile(file);
}

bool
LogFile::exists(const char *pathname)
{
  return BaseLogFile::exists(pathname);
}

/*-------------------------------------------------------------------------
  LogFile::writeln

  This function will make sure the following data is written to the
  output file (m_fd) with a trailing newline.
  -------------------------------------------------------------------------*/

int
LogFile::writeln(char *data, int len, int fd, const char *path)
{
  int total_bytes = 0;

  if (len > 0 && data && fd >= 0) {
    struct iovec wvec[2];
    memset(&wvec, 0, sizeof(iovec));
    memset(&wvec[1], 0, sizeof(iovec));
    int bytes_this_write, vcnt = 1;

#if defined(solaris)
    wvec[0].iov_base = (caddr_t)data;
#else
    wvec[0].iov_base = (void *)data;
#endif
    wvec[0].iov_len = (size_t)len;

    if (data[len - 1] != '\n') {
#if defined(solaris)
      wvec[1].iov_base = (caddr_t) "\n";
#else
      wvec[1].iov_base = (void *)"\n";
#endif
      wvec[1].iov_len = (size_t)1;
      vcnt++;
    }

    if ((bytes_this_write = (int)::writev(fd, (const struct iovec *)wvec, vcnt)) < 0) {
      Warning("An error was encountered in writing to %s: %s.", ((path) ? path : "logfile"), strerror(errno));
    } else {
      total_bytes = bytes_this_write;
    }
  }
  return total_bytes;
}

/*-------------------------------------------------------------------------
  LogFile::check_fd

  This routine will occasionally stat the current logfile to make sure that
  it really does exist.  The easiest way to do this is to close the file
  and re-open it, which will create the file if it doesn't already exist.

  Failure to open the logfile will generate a manager alarm and a Warning.
  -------------------------------------------------------------------------*/

void
LogFile::check_fd()
{
  static bool failure_last_call    = false;
  static unsigned stat_check_count = 1;

  if ((stat_check_count % Log::config->file_stat_frequency) == 0) {
    //
    // It's time to see if the file really exists.  If we can't see
    // the file (via access), then we'll close our descriptor and
    // attept to re-open it, which will create the file if it's not
    // there.
    //
    if (m_name && !LogFile::exists(m_name)) {
      close_file();
    }
    stat_check_count = 0;
  }
  stat_check_count++;

  int err = open_file();
  // XXX if open_file() returns, LOG_FILE_FILESYSTEM_CHECKS_FAILED, raise a more informative alarm ...
  if (err != LOG_FILE_NO_ERROR && err != LOG_FILE_NO_PIPE_READERS) {
    if (!failure_last_call) {
      LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, "Traffic Server could not open logfile %s.", m_name);
      Warning("Traffic Server could not open logfile %s: %s.", m_name, strerror(errno));
    }
    failure_last_call = true;
    return;
  }

  failure_last_call = false;
}

void
LogFile::display(FILE *fd)
{
  fprintf(fd, "Logfile: %s, %s\n", get_name(), (is_open()) ? "file is open" : "file is not open");
}

bool
LogFile::is_open()
{
  if (m_file_format == LOG_FILE_PIPE) {
    return m_fd >= 0;
  } else {
    return m_log && m_log->is_open();
  }
}

/*
 * Returns the fd of the entity (pipe or regular file ) that this object is
 * representing
 *
 * Returns -1 on error, the correct fd otherwise
 */
int
LogFile::get_fd()
{
  if (m_file_format == LOG_FILE_PIPE) {
    return m_fd;
  } else if (m_log && m_log->m_fp) {
    return fileno(m_log->m_fp);
  } else {
    return -1;
  }
}
