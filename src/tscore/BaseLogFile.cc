/** @file

  Base class for log files implementation

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

#include "tscore/BaseLogFile.h"

/*
 * This constructor creates a BaseLogFile based on a given name.
 * This is the most common way BaseLogFiles are created.
 */
BaseLogFile::BaseLogFile(const char *name) : m_name(ats_strdup(name))
{
  log_log_trace("exiting BaseLogFile constructor, m_name=%s, this=%p\n", m_name.get(), this);
}

/*
 * This constructor creates a BaseLogFile based on a given name.
 * Similar to above constructor, but is overloaded with the object signature
 */
BaseLogFile::BaseLogFile(const char *name, uint64_t sig) : m_name(ats_strdup(name)), m_signature(sig), m_has_signature(true)
{
  log_log_trace("exiting BaseLogFile signature constructor, m_name=%s, m_signature=%ld, this=%p\n", m_name.get(), m_signature,
                this);
}

/*
 * This copy constructor creates a BaseLogFile based on a given copy.
 */
BaseLogFile::BaseLogFile(const BaseLogFile &copy)
  : m_start_time(copy.m_start_time),

    m_name(ats_strdup(copy.m_name)),
    m_hostname(ats_strdup(copy.m_hostname)),

    m_is_init(copy.m_is_init),

    m_signature(copy.m_signature),
    m_has_signature(copy.m_has_signature)
{
  log_log_trace("exiting BaseLogFile copy constructor, m_name=%s, this=%p\n", m_name.get(), this);
}

/*
 * Destructor.
 */
BaseLogFile::~BaseLogFile()
{
  log_log_trace("entering BaseLogFile destructor, m_name=%s, this=%p\n", m_name.get(), this);

  if (m_is_regfile) {
    close_file();
  } else {
    log_log_trace("not a regular file, not closing, m_name=%s, this=%p\n", m_name.get(), this);
  }

  log_log_trace("exiting BaseLogFile destructor, this=%p\n", this);
}

/*
 * This function is called by a client of BaseLogFile to roll the underlying
 * file  The tricky part to this routine is in coming up with the new file name,
 * which contains the bounding timestamp interval for the entries
 * within the file.

 * Under normal operating conditions, this BaseLogFile object was in existence
 * for all writes to the file.  In this case, the LogFile members m_start_time
 * and m_end_time will have the starting and ending times for the actual
 * entries written to the file.

 * On restart situations, it is possible to re-open an existing BaseLogFile,
 * which means that the m_start_time variable will be later than the actual
 * entries recorded in the file.  In this case, we'll use the creation time
 * of the file, which should be recorded in the meta-information located on
 * disk.

 * If we can't use the meta-file, either because it's not there or because
 * it's not valid, then we'll use timestamp 0 (Jan 1, 1970) as the starting
 * bound.

 * Return 1 if file rolled, 0 otherwise
 */
int
BaseLogFile::roll(long interval_start, long interval_end)
{
  // First, let's see if a roll is even needed.
  if (m_name == nullptr || !BaseLogFile::exists(m_name.get())) {
    log_log_trace("Roll not needed for %s; file doesn't exist\n", (m_name.get()) ? m_name.get() : "no_name\n");
    return 0;
  }

  // Then, check if this object is backing a regular file
  if (!m_is_regfile) {
    log_log_trace("Roll not needed for %s; not regular file\n", (m_name.get()) ? m_name.get() : "no_name\n");
    return 0;
  }

  // Read meta info if needed (if file was not opened)
  if (!m_meta_info) {
    m_meta_info = new BaseMetaInfo(m_name.get());
  }

  // Create the new file name, which consists of a timestamp and rolled
  // extension added to the previous file name.  The timestamp format is
  // ".%Y%m%d.%Hh%Mm%Ss-%Y%m%d.%Hh%Mm%Ss", where the two date/time values
  // represent the starting and ending times for entries in the rolled
  // log file.  In addition, we add the hostname.  So, the entire rolled
  // format is something like:
  //
  //    "squid.log.mymachine.19980712.12h00m00s-19980713.12h00m00s.old"
  char roll_name[LOGFILE_ROLL_MAXPATHLEN];
  char start_time_ext[64];
  char end_time_ext[64];
  time_t start, end;

  // Start with conservative values for the start and end bounds, then
  // try to refine.
  start = 0L;
  end   = (interval_end >= m_end_time) ? interval_end : m_end_time;

  if (m_meta_info->data_from_metafile()) {
    // If the metadata came from the metafile, this means that
    // the file was preexisting, so we can't use m_start_time for
    // our starting bounds.  Instead, we'll try to use the file
    // creation time stored in the metafile (if it's valid and we can
    // read it).  If all else fails, we'll use 0 for the start time.
    log_log_trace("in BaseLogFile::roll(..) used metadata starttime\n");
    m_meta_info->get_creation_time(&start);
  } else {
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
    log_log_trace("in BaseLogFile::roll(..), didn't use metadata starttime, used earliest available starttime\n");
    if (interval_start == 0) {
      start = m_start_time;
    } else {
      start = (m_start_time < interval_start) ? m_start_time : interval_start;
    }
  }
  log_log_trace("in BaseLogFile::roll(..), start = %ld, m_start_time = %ld, interval_start = %ld\n", start, m_start_time,
                interval_start);

  // Now that we have our timestamp values, convert them to the proper
  // timestamp formats and create the rolled file name.
  timestamp_to_str((long)start, start_time_ext, sizeof(start_time_ext));
  timestamp_to_str((long)end, end_time_ext, sizeof(start_time_ext));
  snprintf(roll_name, LOGFILE_ROLL_MAXPATHLEN, "%s%s%s.%s-%s%s", m_name.get(), (m_hostname.get() ? LOGFILE_SEPARATOR_STRING : ""),
           (m_hostname.get() ? m_hostname.get() : ""), start_time_ext, end_time_ext, LOGFILE_ROLLED_EXTENSION);

  // It may be possible that the file we want to roll into already
  // exists.  If so, then we need to add a version tag to the rolled
  // filename as well so that we don't clobber existing files.
  int version = 1;
  while (BaseLogFile::exists(roll_name)) {
    log_log_trace("The rolled file %s already exists; adding version "
                  "tag %d to avoid clobbering the existing file.\n",
                  roll_name, version);
    snprintf(roll_name, LOGFILE_ROLL_MAXPATHLEN, "%s%s%s.%s-%s.%d%s", m_name.get(),
             (m_hostname.get() ? LOGFILE_SEPARATOR_STRING : ""), (m_hostname.get() ? m_hostname.get() : ""), start_time_ext,
             end_time_ext, version, LOGFILE_ROLLED_EXTENSION);
    ++version;
  }

  // It's now safe to rename the file.
  if (::rename(m_name.get(), roll_name) < 0) {
    log_log_error("Traffic Server could not rename logfile %s to %s, error %d: "
                  "%s.\n",
                  m_name.get(), roll_name, errno, strerror(errno));
    return 0;
  }

  // reset m_start_time
  m_start_time    = 0;
  m_bytes_written = 0;

  log_log_trace("The logfile %s was rolled to %s.\n", m_name.get(), roll_name);

  return 1;
}

/*
 * The more convenient rolling function. Intended use is for less
 * critical logs such as diags.log or traffic.out, since _exact_
 * timestamps may be less important
 *
 * The function calls roll(long,long) with these parameters:
 * Start time is either 0 or creation time stored in the metafile,
 * whichever is greater
 *
 * End time is the current time
 *
 * Returns 1 on success, 0 otherwise
 */
int
BaseLogFile::roll()
{
  time_t start;
  time_t now = time(nullptr);

  if (!m_meta_info || !m_meta_info->get_creation_time(&start)) {
    start = 0L;
  }

  return roll(start, now);
}

/*
 * This function will return true if the given filename corresponds to a
 * rolled logfile.  We make this determination based on the file extension.
 */
bool
BaseLogFile::rolled_logfile(char *path)
{
  const int target_len = (int)strlen(LOGFILE_ROLLED_EXTENSION);
  int len              = (int)strlen(path);
  if (len > target_len) {
    char *str = &path[len - target_len];
    if (!strcmp(str, LOGFILE_ROLLED_EXTENSION)) {
      return true;
    }
  }
  return false;
}

/*
 * Returns if the provided file in 'pathname' exists or not
 */
bool
BaseLogFile::exists(const char *pathname)
{
  ink_assert(pathname != nullptr);
  return (pathname && ::access(pathname, F_OK) == 0);
}

/*
 * Opens the BaseLogFile and associated BaseMetaInfo on disk if it exists
 * Returns relevant exit status
 */
int
BaseLogFile::open_file(int perm)
{
  log_log_trace("BaseLogFile: entered open_file()\n");
  if (is_open()) {
    return LOG_FILE_NO_ERROR;
  }

  if (!m_name.get()) {
    log_log_error("BaseLogFile: m_name is nullptr, aborting open_file()\n");
    return LOG_FILE_COULD_NOT_OPEN_FILE;
  } else if (!strcmp(m_name.get(), "stdout")) {
    log_log_trace("BaseLogFile: stdout opened\n");
    m_fp      = stdout;
    m_is_init = true;
    return LOG_FILE_NO_ERROR;
  } else if (!strcmp(m_name.get(), "stderr")) {
    log_log_trace("BaseLogFile: stderr opened\n");
    m_fp      = stderr;
    m_is_init = true;
    return LOG_FILE_NO_ERROR;
  }

  // means this object is representing a real file on disk
  m_is_regfile = true;

  // Check to see if the file exists BEFORE we try to open it, since
  // opening it will also create it.
  bool file_exists = BaseLogFile::exists(m_name.get());

  if (file_exists) {
    if (!m_meta_info) {
      // This object must be fresh since it has not built its MetaInfo
      // so we create a new MetaInfo object that will read right away
      // (in the constructor) the corresponding metafile
      m_meta_info = new BaseMetaInfo(m_name.get());
    }
  } else {
    // The log file does not exist, so we create a new MetaInfo object
    //  which will save itself to disk right away (in the constructor)
    if (m_has_signature) {
      m_meta_info = new BaseMetaInfo(m_name.get(), (long)time(nullptr), m_signature);
    } else {
      m_meta_info = new BaseMetaInfo(m_name.get(), (long)time(nullptr));
    }
  }

  // open actual log file (not metainfo)
  log_log_trace("BaseLogFile: attempting to open %s\n", m_name.get());

  m_fp = elevating_fopen(m_name.get(), "a+");

  // error check
  if (m_fp) {
    setlinebuf(m_fp);
  } else {
    log_log_error("Error opening log file %s: %s\n", m_name.get(), strerror(errno));
    log_log_error("Actual error: %s\n", (errno == EINVAL ? "einval" : "something else"));
    return LOG_FILE_COULD_NOT_OPEN_FILE;
  }

  // set permissions if necessary
  if (perm != -1) {
    // means LogFile passed in some permissions we need to set
    log_log_trace("BaseLogFile attempting to change %s's permissions to %o\n", m_name.get(), perm);
    if (elevating_chmod(m_name.get(), perm) != 0) {
      log_log_error("Error changing logfile=%s permissions: %s\n", m_name.get(), strerror(errno));
    }
  }

  // set m_bytes_written to force the rolling based on file size.
  m_bytes_written = fseek(m_fp, 0, SEEK_CUR);

  log_log_trace("BaseLogFile %s is now open (fd=%d)\n", m_name.get(), fileno(m_fp));
  m_is_init = true;
  return LOG_FILE_NO_ERROR;
}

/*
 * Closes actual log file, not metainfo
 */
void
BaseLogFile::close_file()
{
  if (is_open()) {
    fclose(m_fp);
    log_log_trace("BaseLogFile %s is closed\n", m_name.get());
    m_fp      = nullptr;
    m_is_init = false;
  }
}

/*
 * Changes names of the actual log file (not metainfo)
 */
void
BaseLogFile::change_name(const char *new_name)
{
  m_name = ats_strdup(new_name);
}

void
BaseLogFile::display(FILE *fd)
{
  fprintf(fd, "Logfile: %s, %s\n", get_name(), (is_open()) ? "file is open" : "file is not open");
}

/*
 * Lowest level internal logging facility for BaseLogFile
 *
 * Since BaseLogFiles can potentially be created before the bootstrap
 * instance of Diags is ready, we cannot simply call something like Debug().
 * However, we still need to log the creation of BaseLogFile, since the
 * information is still useful. This function will print out log messages
 * into traffic.out if we happen to be bootstrapping Diags. Since
 * traffic_manager redirects stdout/stderr into traffic.out, that
 * redirection is inherited by way of exec()/fork() all the way here.
 *
 * TODO use Debug() for non bootstrap instances
 */
void
BaseLogFile::log_log(LogLogPriorityLevel priority, const char *format, ...)
{
  va_list args;

  const char *priority_name = nullptr;
  FILE *output              = stdout;
  switch (priority) {
  case LL_Debug:
    priority_name = "DEBUG";
    break;
  case LL_Note:
    priority_name = "NOTE";
    break;
  case LL_Warning:
    priority_name = "WARNING";
    output        = stderr;
    break;
  case LL_Error:
    priority_name = "ERROR";
    output        = stderr;
    break;
  case LL_Fatal:
    priority_name = "FATAL";
    output        = stderr;
    break;
  default:
    priority_name = "unknown priority";
  }

  va_start(args, format);
  struct timeval now;
  double now_f;

  gettimeofday(&now, nullptr);
  now_f = now.tv_sec + now.tv_usec / 1000000.0f;

  fprintf(output, "<%.4f> [%s]: ", now_f, priority_name);
  vfprintf(output, format, args);
  fflush(output);

  va_end(args);
}

/****************************************************************************

  BaseMetaInfo methods

*****************************************************************************/

/*
 * Given a file name (with or without the full path, but without the extension name),
 * this function creates appends the ".meta" extension and stores it in the local
 * variable
 */
void
BaseMetaInfo::_build_name(const char *filename)
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
    ink_string_concatenate_strings(_filename, ".", filename, ".meta", nullptr);
  } else {
    memcpy(_filename, filename, i + 1);
    ink_string_concatenate_strings(&_filename[i + 1], ".", &filename[i + 1], ".meta", nullptr);
  }
}

/*
 * Reads in meta info into the local variables
 */
void
BaseMetaInfo::_read_from_file()
{
  _flags |= DATA_FROM_METAFILE; // mark attempt
  int fd = elevating_open(_filename, O_RDONLY);
  if (fd < 0) {
    log_log_error("Could not open metafile %s for reading: %s\n", _filename, strerror(errno));
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
            _creation_time = (time_t)ink_atoi64(t);
            _flags |= VALID_CREATION_TIME;
          }
        } else if (strcmp(t, "object_signature") == 0) {
          t = tok.getNext();
          if (t) {
            _log_object_signature = ink_atoi64(t);
            _flags |= VALID_SIGNATURE;
            log_log_trace("BaseMetaInfo::_read_from_file\n"
                          "\tfilename = %s\n"
                          "\tsignature string = %s\n"
                          "\tsignature value = %" PRIu64 "\n",
                          _filename, t, _log_object_signature);
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

/*
 * Writes out metadata info onto disk
 */
void
BaseMetaInfo::_write_to_file()
{
  int fd = elevating_open(_filename, O_WRONLY | O_CREAT | O_TRUNC, LOGFILE_DEFAULT_PERMS);
  if (fd < 0) {
    log_log_error("Could not open metafile %s for writing: %s\n", _filename, strerror(errno));
    return;
  }
  log_log_trace("Successfully opened metafile=%s\n", _filename);

  int n;
  if (_flags & VALID_CREATION_TIME) {
    log_log_trace("Writing creation time to %s\n", _filename);
    n = snprintf(_buffer, BUF_SIZE, "creation_time = %lu\n", (unsigned long)_creation_time);
    // TODO modify this runtime check so that it is not an assertion
    ink_release_assert(n <= BUF_SIZE);
    if (write(fd, _buffer, n) == -1) {
      log_log_trace("Could not write creation_time");
    }
  }

  if (_flags & VALID_SIGNATURE) {
    log_log_trace("Writing signature to %s\n", _filename);
    n = snprintf(_buffer, BUF_SIZE, "object_signature = %" PRIu64 "\n", _log_object_signature);
    // TODO modify this runtime check so that it is not an assertion
    ink_release_assert(n <= BUF_SIZE);
    if (write(fd, _buffer, n) == -1) {
      log_log_error("Could not write object_signature\n");
    }
    log_log_trace("BaseMetaInfo::_write_to_file\n"
                  "\tfilename = %s\n"
                  "\tsignature value = %" PRIu64 "\n"
                  "\tsignature string = %s\n",
                  _filename, _log_object_signature, _buffer);
  }

  close(fd);
}

/*
 * This routine will convert a timestamp (seconds) into a short string,
 * of the format "%Y%m%d.%Hh%Mm%Ss".
 *
 * Since the resulting buffer is passed in, this routine is thread-safe.
 * Return value is the number of characters placed into the array, not
 * including the nullptr.
 */

int
BaseLogFile::timestamp_to_str(long timestamp, char *buf, int size)
{
  static const char *format_str = "%Y%m%d.%Hh%Mm%Ss";
  struct tm res;
  struct tm *tms;
  tms = ink_localtime_r((const time_t *)&timestamp, &res);
  return strftime(buf, size, format_str, tms);
}
