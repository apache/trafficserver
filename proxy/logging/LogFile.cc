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

#include "libts.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Error.h"

#include "P_EventSystem.h"
#include "I_Machine.h"
#include "LogSock.h"

#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogBuffer.h"
#include "LogFile.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogUtils.h"
#include "LogConfig.h"
#include "Log.h"

/*-------------------------------------------------------------------------
  LogFile::LogFile

  This constructor builds a LogFile object given the path, filename, header,
  and logfile format type.  This is the common way to create a new logfile.
  -------------------------------------------------------------------------*/

LogFile::LogFile(const char *name, const char *header, LogFileFormat format,
                 uint64_t signature, size_t ascii_buffer_size, size_t max_line_size)
  : m_file_format(format),
    m_name(ats_strdup(name)),
    m_header(ats_strdup(header)),
    m_signature(signature),
    m_meta_info(NULL),
    m_max_line_size(max_line_size)
{
  delete m_meta_info;
  m_meta_info = NULL;
  m_fd = -1;
  m_start_time = 0L;
  m_end_time = 0L;
  m_bytes_written = 0;
  m_size_bytes = 0;
  m_ascii_buffer_size = (ascii_buffer_size < max_line_size ? max_line_size : ascii_buffer_size);

  Debug("log-file", "exiting LogFile constructor, m_name=%s, this=%p", m_name, this);
}

/*-------------------------------------------------------------------------
  LogFile::LogFile

  This (copy) contructor builds a LogFile object from another LogFile object.
  -------------------------------------------------------------------------*/

LogFile::LogFile (const LogFile& copy)
  : m_file_format (copy.m_file_format),
    m_name  (ats_strdup (copy.m_name)),
    m_header  (ats_strdup (copy.m_header)),
    m_signature (copy.m_signature),
    m_meta_info (NULL),
    m_ascii_buffer_size (copy.m_ascii_buffer_size),
    m_max_line_size (copy.m_max_line_size),
    m_fd (-1),
    m_start_time (0L),
    m_end_time (0L),
    m_bytes_written (0)
{
    ink_release_assert(m_ascii_buffer_size >= m_max_line_size);

    Debug("log-file", "exiting LogFile copy constructor, m_name=%s, this=%p",
          m_name, this);
}
/*-------------------------------------------------------------------------
  LogFile::~LogFile
  -------------------------------------------------------------------------*/

LogFile::~LogFile()
{
  Debug("log-file", "entering LogFile destructor, this=%p", this);
  close_file();

  ats_free(m_name);
  ats_free(m_header);
  delete m_meta_info;
  Debug("log-file", "exiting LogFile destructor, this=%p", this);
}

/*-------------------------------------------------------------------------
  LogFile::exists

  Returns true if the logfile already exists; false otherwise.
  -------------------------------------------------------------------------*/

bool LogFile::exists(const char *pathname)
{
  ink_assert(pathname != NULL);
  return (pathname && ::access(pathname, F_OK) == 0);
}

/*-------------------------------------------------------------------------
  LogFile::change_name
  -------------------------------------------------------------------------*/

void
LogFile::change_name(const char *new_name)
{
  ats_free(m_name);
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
  if (is_open()) {
    return LOG_FILE_NO_ERROR;
  }

  if (m_name && !strcmp(m_name, "stdout")) {
    m_fd = STDOUT_FILENO;
    return LOG_FILE_NO_ERROR;
  }
  //
  // Check to see if the file exists BEFORE we try to open it, since
  // opening it will also create it.
  //
  bool file_exists = LogFile::exists(m_name);

  if (file_exists) {
    if (!m_meta_info) {
      // This object must be fresh since it has not built its MetaInfo
      // so we create a new MetaInfo object that will read right away
      // (in the constructor) the corresponding metafile
      //
      m_meta_info = new MetaInfo(m_name);
    }
  } else {
    // The log file does not exist, so we create a new MetaInfo object
    //  which will save itself to disk right away (in the constructor)
    m_meta_info = new MetaInfo(m_name, LogUtils::timestamp(), m_signature);
  }

  int flags, perms;

  if (m_file_format == LOG_FILE_PIPE) {
    if (mkfifo(m_name, S_IRUSR | S_IWUSR) < 0) {
      if (errno != EEXIST) {
        Error("Could not create named pipe %s for logging: %s", m_name, strerror(errno));
        return LOG_FILE_COULD_NOT_CREATE_PIPE;
      }
    } else {
      Debug("log-file", "Created named pipe %s for logging", m_name);
    }
    flags = O_WRONLY | O_NDELAY;
    perms = 0;
  } else {
    flags = O_WRONLY | O_APPEND | O_CREAT;
    perms = Log::config->logfile_perm;
  }

  Debug("log-file", "attempting to open %s", m_name);
  m_fd =::open(m_name, flags, perms);

  if (m_fd < 0) {
    // if error happened because no process is reading the pipe don't
    // complain, otherwise issue an error message
    //
    if (errno != ENXIO) {
      Error("Error opening log file %s: %s", m_name, strerror(errno));
      return LOG_FILE_COULD_NOT_OPEN_FILE;
    }
    Debug("log-file", "no readers for pipe %s", m_name);
    return LOG_FILE_NO_PIPE_READERS;
  }

  int e = do_filesystem_checks();
  if (e != 0) {
    m_fd = -1;                  // reset to error condition
    return LOG_FILE_FILESYSTEM_CHECKS_FAILED;
  }

  // set m_bytes_written to force the rolling based on filesize.
  m_bytes_written = lseek( m_fd, 0, SEEK_CUR );

  Debug("log-file", "LogFile %s is now open (fd=%d)", m_name, m_fd);

  //
  // If we've opened the file and it didn't already exist, then this is a
  // "new" file and we need to make some initializations.  This is the
  // time to write any headers and do any one-time initialization of the
  // file.
  //
  if (!file_exists) {
    if (m_file_format != LOG_FILE_BINARY && m_header != NULL) {
      Debug("log-file", "writing header to LogFile %s", m_name);
      writeln(m_header, strlen(m_header), m_fd, m_name);
    }
  }

  RecIncrRawStat(log_rsb, this_thread()->mutex->thread_holding,
                 log_stat_log_files_open_stat, 1);

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
    ::close(m_fd);
    Debug("log-file", "LogFile %s (fd=%d) is closed", m_name, m_fd);
    m_fd = -1;

    RecIncrRawStat(log_rsb, this_thread()->mutex->thread_holding,
                   log_stat_log_files_open_stat, -1);
  }
}

/*-------------------------------------------------------------------------
  LogFile::rolled_logfile

  This function will return true if the given filename corresponds to a
  rolled logfile.  We make this determination based on the file extension.
  -------------------------------------------------------------------------*/

bool LogFile::rolled_logfile(char *path)
{
  const int
    target_len = (int) strlen(LOGFILE_ROLLED_EXTENSION);
  int
    len = (int) strlen(path);
  if (len > target_len) {
    char *
      str = &path[len - target_len];
    if (!strcmp(str, LOGFILE_ROLLED_EXTENSION)) {
      return true;
    }
  }
  return false;
}

/*-------------------------------------------------------------------------
  LogFile::roll

  This function is called by a LogObject to roll its files.  The
  tricky part to this routine is in coming up with the new file name,
  which contains the bounding timestamp interval for the entries
  within the file.

  Under normal operating conditions, this LogFile object was in existence
  for all writes to the file.  In this case, the LogFile members m_start_time
  and m_end_time will have the starting and ending times for the actual
  entries written to the file.

  On restart situations, it is possible to re-open an existing logfile,
  which means that the m_start_time variable will be later than the actual
  entries recorded in the file.  In this case, we'll use the creation time
  of the file, which should be recorded in the meta-information located on
  disk.

  If we can't use the meta-file, either because it's not there or because
  it's not valid, then we'll use timestamp 0 (Jan 1, 1970) as the starting
  bound.

  Return 1 if file rolled, 0 otherwise
  -------------------------------------------------------------------------*/

int
LogFile::roll(long interval_start, long interval_end)
{
  //
  // First, let's see if a roll is even needed.
  //
  if (m_name == NULL || !LogFile::exists(m_name)) {
    Debug("log-file", "Roll not needed for %s; file doesn't exist", (m_name) ? m_name : "no_name");
    return 0;
  }
  // Read meta info if needed (if file was not opened)
  //
  if (!m_meta_info) {
    m_meta_info = new MetaInfo(m_name);
  }
  //
  // Create the new file name, which consists of a timestamp and rolled
  // extension added to the previous file name.  The timestamp format is
  // ".%Y%m%d.%Hh%Mm%Ss-%Y%m%d.%Hh%Mm%Ss", where the two date/time values
  // represent the starting and ending times for entries in the rolled
  // log file.  In addition, we add the hostname.  So, the entire rolled
  // format is something like:
  //
  //    "squid.log.mymachine.19980712.12h00m00s-19980713.12h00m00s.old"
  //
  char roll_name[MAXPATHLEN];
  char start_time_ext[64];
  char end_time_ext[64];
  time_t start, end;

  //
  // Make sure the file is closed so we don't leak any descriptors.
  //
  close_file();

  //
  // Start with conservative values for the start and end bounds, then
  // try to refine.
  //
  start = 0L;
  end = (interval_end >= m_end_time) ? interval_end : m_end_time;

  if (m_meta_info->data_from_metafile()) {
    //
    // If the metadata came from the metafile, this means that
    // the file was preexisting, so we can't use m_start_time for
    // our starting bounds.  Instead, we'll try to use the file
    // creation time stored in the metafile (if it's valid and we can
    // read it).  If all else fails, we'll use 0 for the start time.
    //
    m_meta_info->get_creation_time(&start);
  } else {
    //
    // The logfile was not preexisting (normal case), so we'll use
    // earlier of the interval start time and the m_start_time.
    //
    // note that m_start_time is not the time of the first
    // transaction, but the time of the creation of the first log
    // buffer used by the file. These times may be different,
    // especially under light load, and using the m_start_time may
    // produce overlapping filenames (the problem is that we have
    // no easy way of keeping track of the timestamp of the first
    // transaction
    //
    start = (m_start_time < interval_start) ? m_start_time : interval_start;
  }

  //
  // Now that we have our timestamp values, convert them to the proper
  // timestamp formats and create the rolled file name.
  //
  LogUtils::timestamp_to_str((long) start, start_time_ext, 64);
  LogUtils::timestamp_to_str((long) end, end_time_ext, 64);
  snprintf(roll_name, MAXPATHLEN, "%s%s%s.%s-%s%s",
               m_name,
               LOGFILE_SEPARATOR_STRING,
               Machine::instance()->hostname, start_time_ext, end_time_ext, LOGFILE_ROLLED_EXTENSION);

  //
  // It may be possible that the file we want to roll into already
  // exists.  If so, then we need to add a version tag to the rolled
  // filename as well so that we don't clobber existing files.
  //

  int version = 1;
  while (LogFile::exists(roll_name)) {
    Note("The rolled file %s already exists; adding version "
         "tag %d to avoid clobbering the existing file.", roll_name, version);
    snprintf(roll_name, MAXPATHLEN, "%s%s%s.%s-%s.%d%s",
                 m_name,
                 LOGFILE_SEPARATOR_STRING,
                 Machine::instance()->hostname, start_time_ext, end_time_ext, version, LOGFILE_ROLLED_EXTENSION);
    version++;
  }

  //
  // It's now safe to rename the file.
  //

  if (::rename(m_name, roll_name) < 0) {
    Warning("Traffic Server could not rename logfile %s to %s, error %d: "
            "%s.", m_name, roll_name, errno, strerror(errno));
    return 0;
  }
  // reset m_start_time
  //
  m_start_time = 0;
  m_bytes_written = 0;

  Debug("log-file", "The logfile %s was rolled to %s.", m_name, roll_name);

  return 1;
}

/*-------------------------------------------------------------------------
  LogFile::preproc_and_try_delete

  preprocess the given buffer data before write to target file
  and try to delete it when its reference become zero.
  -------------------------------------------------------------------------*/
int
LogFile::preproc_and_try_delete(LogBuffer * lb)
{
  int ret = -1;
  LogBufferHeader *buffer_header;

  if (lb == NULL) {
    Note("Cannot write LogBuffer to LogFile %s; LogBuffer is NULL", m_name);
    return -1;
  }

  ink_atomic_increment(&lb->m_references, 1);

  if ((buffer_header = lb->header()) == NULL) {
    Note("Cannot write LogBuffer to LogFile %s; LogBufferHeader is NULL",
        m_name);
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
  if (!m_start_time)
    m_start_time = buffer_header->low_timestamp;
  m_end_time = buffer_header->high_timestamp;

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

    ProxyMutex *mutex = this_thread()->mutex;

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_num_flush_to_disk_stat,
                   lb->header()->entry_count);

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_flush_to_disk_stat,
                   lb->header()->byte_count);

    ink_atomiclist_push(Log::flush_data_list, flush_data);

    Log::flush_notify->signal();

    //
    // LogBuffer will be deleted in flush thread
    //
    return 0;
  }
  else if (m_file_format == LOG_FILE_ASCII || m_file_format == LOG_FILE_PIPE) {
    write_ascii_logbuffer3(buffer_header);
    ret = 0;
  }
  else {
    Note("Cannot write LogBuffer to LogFile %s; invalid file format: %d",
         m_name, m_file_format);
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
LogFile::write_ascii_logbuffer(LogBufferHeader * buffer_header, int fd, const char *path, const char *alt_format)
{
  ink_assert(buffer_header != NULL);
  ink_assert(fd >= 0);

  char fmt_buf[LOG_MAX_FORMATTED_BUFFER];
  char fmt_line[LOG_MAX_FORMATTED_LINE];
  LogBufferIterator iter(buffer_header);
  LogEntryHeader *entry_header;
  int fmt_buf_bytes = 0;
  int fmt_line_bytes = 0;
  int bytes = 0;

  LogFormatType format_type;
  char *fieldlist_str;
  char *printf_str;

  switch (buffer_header->version) {
  case LOG_SEGMENT_VERSION:
    format_type = (LogFormatType) buffer_header->format_type;

    fieldlist_str = buffer_header->fmt_fieldlist();
    printf_str = buffer_header->fmt_printf();
    break;

  default:
    Note("Invalid LogBuffer version %d in write_ascii_logbuffer; "
         "current version is %d", buffer_header->version, LOG_SEGMENT_VERSION);
    return 0;
  }

  while ((entry_header = iter.next())) {
    fmt_line_bytes = LogBuffer::to_ascii(entry_header, format_type,
                                         &fmt_line[0], LOG_MAX_FORMATTED_LINE,
                                         fieldlist_str, printf_str, buffer_header->version, alt_format);
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
      fmt_buf[fmt_buf_bytes] = '\n';    // keep entries separate
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
LogFile::write_ascii_logbuffer3(LogBufferHeader * buffer_header, const char *alt_format)
{
  Debug("log-file", "entering LogFile::write_ascii_logbuffer3 for %s " "(this=%p)", m_name, this);
  ink_assert(buffer_header != NULL);

  ProxyMutex *mutex = this_thread()->mutex;
  LogBufferIterator iter(buffer_header);
  LogEntryHeader *entry_header;
  int fmt_entry_count = 0;
  int fmt_buf_bytes = 0;
  int total_bytes = 0;

  LogFormatType format_type;
  char *fieldlist_str;
  char *printf_str;
  char *ascii_buffer;

  switch (buffer_header->version) {
  case LOG_SEGMENT_VERSION:
    format_type = (LogFormatType) buffer_header->format_type;
    fieldlist_str = buffer_header->fmt_fieldlist();
    printf_str = buffer_header->fmt_printf();
    break;

  default:
    Note("Invalid LogBuffer version %d in write_ascii_logbuffer; "
         "current version is %d", buffer_header->version, LOG_SEGMENT_VERSION);
    return 0;
  }

  while ((entry_header = iter.next())) {
    fmt_entry_count = 0;
    fmt_buf_bytes = 0;

    if (m_file_format == LOG_FILE_PIPE)
      ascii_buffer = (char *)malloc(m_max_line_size);
    else
      ascii_buffer = (char *)malloc(m_ascii_buffer_size);

    // fill the buffer with as many records as possible
    //
    do {
      if (entry_header->entry_len >= m_max_line_size) {
        Warning("Log is too long(%" PRIu32 "), it would be truncated. max_len:%zu",
                entry_header->entry_len, m_max_line_size);
      }

      int bytes = LogBuffer::to_ascii(entry_header, format_type,
                                      &ascii_buffer[fmt_buf_bytes],
                                      m_max_line_size - 1,
                                      fieldlist_str, printf_str,
                                      buffer_header->version,
                                      alt_format);

      if (bytes > 0) {
        fmt_buf_bytes += bytes;
        ascii_buffer[fmt_buf_bytes] = '\n';
        ++fmt_buf_bytes;
        ++fmt_entry_count;
      } else {
        Error("Failed to convert LogBuffer to ascii, have dropped (%" PRIu32 ") bytes.",
              entry_header->entry_len);

        RecIncrRawStat(log_rsb, mutex->thread_holding,
                       log_stat_num_lost_before_flush_to_disk_stat,
                       fmt_entry_count);

        RecIncrRawStat(log_rsb, mutex->thread_holding,
                       log_stat_bytes_lost_before_flush_to_disk_stat,
                       fmt_buf_bytes);
      }
      // if writing to a pipe, fill the buffer with a single
      // record to avoid as much as possible overflowing the
      // pipe buffer
      //
      if (m_file_format == LOG_FILE_PIPE)
        break;

      if (m_ascii_buffer_size - fmt_buf_bytes < m_max_line_size)
        break;
    } while ((entry_header = iter.next()));

    // send the buffer to flush thread
    //
    LogFlushData *flush_data = new LogFlushData(this, ascii_buffer, fmt_buf_bytes);

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_num_flush_to_disk_stat,
                   fmt_entry_count);

    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_flush_to_disk_stat,
                   fmt_buf_bytes);

    ink_atomiclist_push(Log::flush_data_list, flush_data);

    Log::flush_notify->signal();

    total_bytes += fmt_buf_bytes;
  }

  return total_bytes;
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
    wvec[0].iov_base = (caddr_t) data;
#else
    wvec[0].iov_base = (void *) data;
#endif
    wvec[0].iov_len = (size_t) len;

    if (data[len - 1] != '\n') {
#if defined(solaris)
      wvec[1].iov_base = (caddr_t) "\n";
#else
      wvec[1].iov_base = (void *) "\n";
#endif
      wvec[1].iov_len = (size_t) 1;
      vcnt++;
    }

    if ((bytes_this_write = (int)::writev(fd, (const struct iovec *) wvec, vcnt)) < 0) {
      Warning("An error was encountered in writing to %s: %s.", ((path) ? path : "logfile"), strerror(errno));
    } else
      total_bytes = bytes_this_write;
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
  static bool failure_last_call = false;
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
LogFile::display(FILE * fd)
{
  fprintf(fd, "Logfile: %s, %s\n", get_name(), (is_open())? "file is open" : "file is not open");
}

/***************************************************************************
 LogFileList IS NOT USED
****************************************************************************/


/****************************************************************************

  MetaInfo methods

*****************************************************************************/

void
MetaInfo::_build_name(const char *filename)
{
  int i = -1, l = 0;
  char c;
  while (c = filename[l], c != 0) {
    if (c == '/') {
      i = l;
    }
    ++l;
  }

  // 7 = 1 (dot at beginning) + 5 (".meta") + 1 (null terminating)
  //
  _filename = (char *)ats_malloc(l + 7);

  if (i < 0) {
    ink_string_concatenate_strings(_filename, ".", filename, ".meta", NULL);
  } else {
    memcpy(_filename, filename, i + 1);
    ink_string_concatenate_strings(&_filename[i + 1], ".", &filename[i + 1]
                                   , ".meta", NULL);
  }
}

void
MetaInfo::_read_from_file()
{
  _flags |= DATA_FROM_METAFILE;
  int fd = open(_filename, O_RDONLY);
  if (fd < 0) {
    Warning("Could not open metafile %s for reading: %s", _filename, strerror(errno));
  } else {
    _flags |= FILE_OPEN_SUCCESSFUL;
    SimpleTokenizer tok('=', SimpleTokenizer::OVERWRITE_INPUT_STRING);
    int line_number = 1;
    while (ink_file_fd_readline(fd, BUF_SIZE, _buffer) > 0) {
      tok.setString(_buffer);
      char *t = tok.getNext();
      if (t) {
        if (strcmp(t, "creation_time") == 0) {
          t = tok.getNext();
          if (t) {
            _creation_time = (time_t) ink_atoi64(t);
            _flags |= VALID_CREATION_TIME;
          }
        } else if (strcmp(t, "object_signature") == 0) {
          t = tok.getNext();
          if (t) {
            _log_object_signature = ink_atoi64(t);
            _flags |= VALID_SIGNATURE;
            Debug("log-meta", "MetaInfo::_read_from_file\n"
                  "\tfilename = %s\n"
                  "\tsignature string = %s\n" "\tsignature value = %" PRIu64 "", _filename, t, _log_object_signature);
          }
        } else if (line_number == 1) {
          ink_release_assert(!"no panda support");
        }
      }
      ++line_number;
    }
    close(fd);
  }
}

void
MetaInfo::_write_to_file()
{
  int fd = open(_filename, O_WRONLY | O_CREAT | O_TRUNC,
                Log::config->logfile_perm);

  if (fd <= 0) {
    Warning("Could not open metafile %s for writing: %s", _filename, strerror(errno));
  } else {
    int n;
    if (_flags & VALID_CREATION_TIME) {
      n = snprintf(_buffer, BUF_SIZE, "creation_time = %lu\n", (unsigned long) _creation_time);
      // TODO modify this runtime check so that it is not an assertion
      ink_release_assert(n <= BUF_SIZE);
      if (write(fd, _buffer, n) == -1) {
        Warning("Could not write creation_time");
      }
    }
    if (_flags & VALID_SIGNATURE) {
      n = snprintf(_buffer, BUF_SIZE, "object_signature = %" PRIu64 "\n", _log_object_signature);
      // TODO modify this runtime check so that it is not an assertion
      ink_release_assert(n <= BUF_SIZE);
      if (write(fd, _buffer, n) == -1) {
        Warning("Could not write object_signaure");
      }
      Debug("log-meta", "MetaInfo::_write_to_file\n"
            "\tfilename = %s\n"
            "\tsignature value = %" PRIu64 "\n" "\tsignature string = %s", _filename, _log_object_signature, _buffer);
    }
  }
  close(fd);
}
