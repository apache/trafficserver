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
 LogObject.cc


 ***************************************************************************/
#include "ts/ink_platform.h"
#include "ts/CryptoHash.h"
#include "ts/INK_MD5.h"
#include "Error.h"
#include "P_EventSystem.h"
#include "LogUtils.h"
#include "LogField.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogAccess.h"
#include "Log.h"
#include "ts/TestBox.h"

static bool
should_roll_on_time(Log::RollingEnabledValues roll)
{
  return roll == Log::ROLL_ON_TIME_ONLY || roll == Log::ROLL_ON_TIME_OR_SIZE;
}

static bool
should_roll_on_size(Log::RollingEnabledValues roll)
{
  return roll == Log::ROLL_ON_SIZE_ONLY || roll == Log::ROLL_ON_TIME_OR_SIZE;
}

size_t
LogBufferManager::preproc_buffers(LogBufferSink *sink)
{
  SList(LogBuffer, write_link) q(write_list.popall()), new_q;
  LogBuffer *b = NULL;
  while ((b = q.pop())) {
    if (b->m_references || b->m_state.s.num_writers) {
      // Still has outstanding references.
      write_list.push(b);
    } else if (_num_flush_buffers > FLUSH_ARRAY_SIZE) {
      ink_atomic_increment(&_num_flush_buffers, -1);
      Warning("Dropping log buffer, can't keep up.");
      RecIncrRawStat(log_rsb, this_thread()->mutex->thread_holding, log_stat_bytes_lost_before_preproc_stat,
                     b->header()->byte_count);
      delete b;
    } else {
      new_q.push(b);
    }
  }

  int prepared = 0;
  while ((b = new_q.pop())) {
    b->update_header_data();
    sink->preproc_and_try_delete(b);
    ink_atomic_increment(&_num_flush_buffers, -1);
    prepared++;
  }

  Debug("log-logbuffer", "prepared %d buffers", prepared);
  return prepared;
}

/*-------------------------------------------------------------------------
  LogObject
  -------------------------------------------------------------------------*/

LogObject::LogObject(const LogFormat *format, const char *log_dir, const char *basename, LogFileFormat file_format,
                     const char *header, Log::RollingEnabledValues rolling_enabled, int flush_threads, int rolling_interval_sec,
                     int rolling_offset_hr, int rolling_size_mb, bool auto_created)
  : m_auto_created(auto_created),
    m_alt_filename(NULL),
    m_flags(0),
    m_signature(0),
    m_flush_threads(flush_threads),
    m_rolling_interval_sec(rolling_interval_sec),
    m_rolling_offset_hr(rolling_offset_hr),
    m_rolling_size_mb(rolling_size_mb),
    m_last_roll_time(0),
    m_buffer_manager_idx(0)
{
  ink_release_assert(format);
  m_format         = new LogFormat(*format);
  m_buffer_manager = new LogBufferManager[m_flush_threads];

  if (file_format == LOG_FILE_BINARY) {
    m_flags |= BINARY;
  } else if (file_format == LOG_FILE_PIPE) {
    m_flags |= WRITES_TO_PIPE;
  }

  generate_filenames(log_dir, basename, file_format);

  // compute_signature is a static function
  m_signature = compute_signature(m_format, m_basename, m_flags);

  // by default, create a LogFile for this object, if a loghost is
  // later specified, then we will delete the LogFile object
  //
  m_logFile = new LogFile(m_filename, header, file_format, m_signature, Log::config->ascii_buffer_size, Log::config->max_line_size);

  LogBuffer *b = new LogBuffer(this, Log::config->log_buffer_size);
  ink_assert(b);
  SET_FREELIST_POINTER_VERSION(m_log_buffer, b, 0);

  _setup_rolling(rolling_enabled, rolling_interval_sec, rolling_offset_hr, rolling_size_mb);

  Debug("log-config", "exiting LogObject constructor, filename=%s this=%p", m_filename, this);
}

LogObject::LogObject(LogObject &rhs)
  : m_basename(ats_strdup(rhs.m_basename)),
    m_filename(ats_strdup(rhs.m_filename)),
    m_alt_filename(ats_strdup(rhs.m_alt_filename)),
    m_flags(rhs.m_flags),
    m_signature(rhs.m_signature),
    m_flush_threads(rhs.m_flush_threads),
    m_rolling_interval_sec(rhs.m_rolling_interval_sec),
    m_last_roll_time(rhs.m_last_roll_time)
{
  m_format         = new LogFormat(*(rhs.m_format));
  m_buffer_manager = new LogBufferManager[m_flush_threads];

  if (rhs.m_logFile) {
    m_logFile = new LogFile(*(rhs.m_logFile));
  } else {
    m_logFile = NULL;
  }

  LogFilter *filter;
  for (filter = rhs.m_filter_list.first(); filter; filter = rhs.m_filter_list.next(filter)) {
    add_filter(filter);
  }

  LogHost *host;
  for (host = rhs.m_host_list.first(); host; host = rhs.m_host_list.next(host)) {
    add_loghost(host);
  }

  // copy gets a fresh log buffer
  //
  LogBuffer *b = new LogBuffer(this, Log::config->log_buffer_size);
  ink_assert(b);
  SET_FREELIST_POINTER_VERSION(m_log_buffer, b, 0);

  Debug("log-config", "exiting LogObject copy constructor, "
                      "filename=%s this=%p",
        m_filename, this);
}

LogObject::~LogObject()
{
  Debug("log-config", "entering LogObject destructor, this=%p", this);

  preproc_buffers();

  // here we need to free LogHost if it is remote logging.
  if (is_collation_client()) {
    if (m_host_list.count()) {
      m_host_list.clear();
    }
  }
  ats_free(m_basename);
  ats_free(m_filename);
  ats_free(m_alt_filename);
  delete m_format;
  delete[] m_buffer_manager;
  delete (LogBuffer *)FREELIST_POINTER(m_log_buffer);
}

//-----------------------------------------------------------------------------
//
// This function generates an object filename according to the following rules:
//
// 1.- if no extension is given, add .log for ascii logs, and .blog for
//     binary logs
// 2.- if an extension is given, then do not modify filename and use that
//     extension regardless of type of log
// 3.- if there is a '.' at the end of the name, then do not add an extension
//     and remove the '.'. To have a dot at the end of the filename, specify
//     two ('..').
//
void
LogObject::generate_filenames(const char *log_dir, const char *basename, LogFileFormat file_format)
{
  ink_assert(log_dir && basename);

  int i = -1, len = 0;
  char c;
  while (c = basename[len], c != 0) {
    if (c == '.') {
      i = len;
    }
    ++len;
  }
  if (i == len - 1) {
    --len;
  }; // remove dot at end of name

  const char *ext = 0;
  int ext_len     = 0;
  if (i < 0) { // no extension, add one
    switch (file_format) {
    case LOG_FILE_ASCII:
      ext     = LOG_FILE_ASCII_OBJECT_FILENAME_EXTENSION;
      ext_len = 4;
      break;
    case LOG_FILE_BINARY:
      ext     = LOG_FILE_BINARY_OBJECT_FILENAME_EXTENSION;
      ext_len = 5;
      break;
    case LOG_FILE_PIPE:
      ext     = LOG_FILE_PIPE_OBJECT_FILENAME_EXTENSION;
      ext_len = 5;
      break;
    default:
      ink_assert(!"unknown file format");
    }
  }

  int dir_len      = (int)strlen(log_dir);
  int basename_len = len + ext_len + 1;          // include null terminator
  int total_len    = dir_len + 1 + basename_len; // include '/'

  m_filename = (char *)ats_malloc(total_len);
  m_basename = (char *)ats_malloc(basename_len);

  memcpy(m_filename, log_dir, dir_len);
  m_filename[dir_len++] = '/';
  memcpy(&m_filename[dir_len], basename, len);
  memcpy(m_basename, basename, len);

  if (ext_len) {
    memcpy(&m_filename[dir_len + len], ext, ext_len);
    memcpy(&m_basename[len], ext, ext_len);
  }
  m_filename[total_len - 1]    = 0;
  m_basename[basename_len - 1] = 0;
}

void
LogObject::rename(char *new_name)
{
  // NOTE: this function is intended to be called by the LogObjectManager
  // while solving filename conflicts. It DOES NOT modify the signature of
  // the LogObject to match the new filename.
  //
  ats_free(m_alt_filename);
  m_alt_filename = ats_strdup(new_name);
  m_logFile->change_name(new_name);
}

void
LogObject::add_filter(LogFilter *filter, bool copy)
{
  if (!filter) {
    return;
  }
  m_filter_list.add(filter, copy);
}

void
LogObject::set_filter_list(const LogFilterList &list, bool copy)
{
  LogFilter *f;

  m_filter_list.clear();
  for (f = list.first(); f != NULL; f = list.next(f)) {
    m_filter_list.add(f, copy);
  }
  m_filter_list.set_conjunction(list.does_conjunction());
}

void
LogObject::add_loghost(LogHost *host, bool copy)
{
  if (!host) {
    return;
  }
  m_host_list.add(host, copy);

  // A LogObject either writes to a file, or sends to a collation host, but
  // not both. By default, it writes to a file. If a LogHost is specified,
  // then clear the intelligent Ptr containing LogFile.
  //
  m_logFile.clear();
}

// we conpute the object signature from the fieldlist_str and the printf_str
// of the LogFormat rather than from the format_str because the format_str
// is not part of a LogBuffer header
//
uint64_t
LogObject::compute_signature(LogFormat *format, char *filename, unsigned int flags)
{
  char *fl           = format->fieldlist();
  char *ps           = format->printf_str();
  uint64_t signature = 0;

  if (fl && ps && filename) {
    int buf_size = strlen(fl) + strlen(ps) + strlen(filename) + 2;
    char *buffer = (char *)ats_malloc(buf_size);

    ink_string_concatenate_strings(buffer, fl, ps, filename,
                                   flags & LogObject::BINARY ? "B" : (flags & LogObject::WRITES_TO_PIPE ? "P" : "A"), NULL);

    CryptoHash hash;
    MD5Context().hash_immediate(hash, buffer, buf_size - 1);
    signature = hash.fold();

    ats_free(buffer);
  }
  return signature;
}

void
LogObject::display(FILE *fd)
{
  fprintf(fd, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  fprintf(fd, "LogObject [%p]: format = %s (%p)\nbasename = %s\n"
              "flags = %u\n"
              "signature = %" PRIu64 "\n",
          this, m_format->name(), m_format, m_basename, m_flags, m_signature);
  if (is_collation_client()) {
    m_host_list.display(fd);
  } else {
    fprintf(fd, "full path = %s\n", get_full_filename());
  }
  m_filter_list.display(fd);
  fprintf(fd, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
}

void
LogObject::displayAsXML(FILE *fd, bool extended)
{
  if (extended) {
    // display format and filter objects
    m_format->displayAsXML(fd);
    m_filter_list.display_as_XML(fd);
  }

  fprintf(fd, "<LogObject>\n"
              "  <Mode        = \"%s\"/>\n"
              "  <Format      = \"%s\"/>\n"
              "  <Filename    = \"%s\"/>\n",
          (m_flags & BINARY ? "binary" : "ascii"), m_format->name(), m_filename);

  LogFilter *filter;
  for (filter = m_filter_list.first(); filter != NULL; filter = m_filter_list.next(filter)) {
    fprintf(fd, "  <Filter      = \"%s\"/>\n", filter->name());
  }

  LogHost *host;
  for (host = m_host_list.first(); host != NULL; host = m_host_list.next(host)) {
    fprintf(fd, "  <LogHostName = \"%s\"/>\n", host->name());
  }

  fprintf(fd, "</LogObject>\n");
}

static head_p
increment_pointer_version(volatile head_p *dst)
{
  head_p h;
  head_p new_h;

  do {
    INK_QUEUE_LD(h, *dst);
    SET_FREELIST_POINTER_VERSION(new_h, FREELIST_POINTER(h), FREELIST_VERSION(h) + 1);
  } while (ink_atomic_cas(&dst->data, h.data, new_h.data) == false);

  return h;
}

static bool
write_pointer_version(volatile head_p *dst, head_p old_h, void *ptr, head_p::version_type vers)
{
  head_p tmp_h;

  SET_FREELIST_POINTER_VERSION(tmp_h, ptr, vers);
  return ink_atomic_cas(&dst->data, old_h.data, tmp_h.data);
}

LogBuffer *
LogObject::_checkout_write(size_t *write_offset, size_t bytes_needed)
{
  LogBuffer::LB_ResultCode result_code;
  LogBuffer *buffer;
  LogBuffer *new_buffer;
  bool retry = true;

  do {
    // To avoid a race condition, we keep a count of held references in
    // the pointer itself and add this to m_outstanding_references.

    // Increment the version of m_log_buffer, returning the previous version.
    head_p h = increment_pointer_version(&m_log_buffer);

    buffer           = (LogBuffer *)FREELIST_POINTER(h);
    result_code      = buffer->checkout_write(write_offset, bytes_needed);
    bool decremented = false;

    switch (result_code) {
    case LogBuffer::LB_OK:
      // checkout succeded
      //
      retry = false;
      break;

    case LogBuffer::LB_FULL_ACTIVE_WRITERS:
    case LogBuffer::LB_FULL_NO_WRITERS:
      // no more room in current buffer, create a new one
      new_buffer = new LogBuffer(this, Log::config->log_buffer_size);

      // swap the new buffer for the old one
      INK_WRITE_MEMORY_BARRIER;
      head_p old_h;

      do {
        INK_QUEUE_LD(old_h, m_log_buffer);
        if (FREELIST_POINTER(old_h) != FREELIST_POINTER(h)) {
          ink_atomic_increment(&buffer->m_references, -1);

          // another thread should be taking care of creating a new
          // buffer, so delete new_buffer and try again
          delete new_buffer;
          break;
        }
      } while (!write_pointer_version(&m_log_buffer, old_h, new_buffer, 0));

      if (FREELIST_POINTER(old_h) == FREELIST_POINTER(h)) {
        ink_atomic_increment(&buffer->m_references, FREELIST_VERSION(old_h) - 1);

        int idx = m_buffer_manager_idx++ % m_flush_threads;
        Debug("log-logbuffer", "adding buffer %d to flush list after checkout", buffer->get_id());
        m_buffer_manager[idx].add_to_flush_queue(buffer);
        Log::preproc_notify[idx].signal();
        buffer = NULL;
      }

      decremented = true;
      break;

    case LogBuffer::LB_RETRY:
      // no more room, but another thread should be taking care of
      // creating a new buffer, so try again
      //
      break;

    case LogBuffer::LB_BUFFER_TOO_SMALL:

      // return a null buffer to signal the caller that this
      // transaction cannot be logged
      //
      retry = false;
      break;

    default:
      ink_assert(false);
    }

    if (!decremented) {
      head_p old_h;

      do {
        INK_QUEUE_LD(old_h, m_log_buffer);
        if (FREELIST_POINTER(old_h) != FREELIST_POINTER(h))
          break;
      } while (!write_pointer_version(&m_log_buffer, old_h, FREELIST_POINTER(h), FREELIST_VERSION(old_h) - 1));

      if (FREELIST_POINTER(old_h) != FREELIST_POINTER(h))
        ink_atomic_increment(&buffer->m_references, -1);
    }

  } while (retry && write_offset); // if write_offset is null, we do
  // not retry because we really do
  // not want to write to the buffer
  // only to set it as full
  if (result_code == LogBuffer::LB_BUFFER_TOO_SMALL) {
    buffer = NULL;
  }
  return buffer;
}

int
LogObject::va_log(LogAccess *lad, const char *fmt, va_list ap)
{
  static const unsigned MAX_ENTRY = 16 * LOG_KILOBYTE; // 16K? Really?
  char entry[MAX_ENTRY];
  unsigned len = 0;

  ink_assert(fmt != NULL);
  len = 0;

  if (this->m_flags & LOG_OBJECT_FMT_TIMESTAMP) {
    len = LogUtils::timestamp_to_str(LogUtils::timestamp(), entry, MAX_ENTRY);
    if (len <= 0) {
      return Log::FAIL;
    }

    // Add a space after the timestamp
    entry[len++] = ' ';

    if (len >= MAX_ENTRY) {
      return Log::FAIL;
    }
  }

  vsnprintf(&entry[len], MAX_ENTRY - len, fmt, ap);

  // Now that we have an entry and it's length (len), we can place it
  // into the associated logbuffer.
  return this->log(lad, entry);
}

int
LogObject::log(LogAccess *lad, const char *text_entry)
{
  LogBuffer *buffer;
  size_t offset       = 0; // prevent warning
  size_t bytes_needed = 0, bytes_used = 0;

  // log to a pipe even if space is exhausted since pipe uses no space
  // likewise, send data to a remote client even if local space is exhausted
  // (if there is a remote client, m_logFile will be NULL
  if (Log::config->logging_space_exhausted && !writes_to_pipe() && m_logFile) {
    Debug("log", "logging space exhausted, can't write to:%s, drop this entry", m_logFile->get_name());
    return Log::FULL;
  }
  // this verification must be done here in order to avoid 'dead' LogBuffers
  // with none zero 'in usage' counters (see _checkout_write for more details)
  if (!lad && !text_entry) {
    Note("Call to LogAccess without LAD or text entry; skipping");
    return Log::FAIL;
  }

  if (lad && m_filter_list.toss_this_entry(lad)) {
    Debug("log", "entry filtered, skipping ...");
    return Log::SKIP;
  }

  if (lad && m_filter_list.wipe_this_entry(lad)) {
    Debug("log", "entry wiped, ...");
  }

  if (lad && m_format->is_aggregate()) {
    // marshal the field data into the temp space provided by the
    // LogFormat object for aggregate formats
    if (m_format->m_agg_marshal_space == NULL) {
      Note("No temp space to marshal aggregate fields into");
      return Log::FAIL;
    }

    long time_now = LogUtils::timestamp();
    m_format->m_field_list.marshal(lad, m_format->m_agg_marshal_space);

    // step through each of the fields and update the LogField object
    // with the newly-marshalled data
    LogFieldList *fl = &m_format->m_field_list;
    char *data_ptr   = m_format->m_agg_marshal_space;
    LogField *f;
    int64_t val;
    for (f = fl->first(); f; f = fl->next(f)) {
      // convert to host order to do computations
      val = (f->is_time_field()) ? time_now : *((int64_t *)data_ptr);
      f->update_aggregate(val);
      data_ptr += INK_MIN_ALIGN;
    }

    if (time_now < m_format->m_interval_next) {
      Debug("log-agg", "Time now = %ld, next agg = %ld; not time "
                       "for aggregate entry",
            time_now, m_format->m_interval_next);
      return Log::AGGR;
    }
    // can easily compute bytes_needed because all fields are INTs
    // and will use INK_MIN_ALIGN each
    bytes_needed = m_format->field_count() * INK_MIN_ALIGN;
  } else if (lad) {
    bytes_needed = m_format->m_field_list.marshal_len(lad);
  } else if (text_entry) {
    bytes_needed = LogAccess::strlen(text_entry);
  }

  if (bytes_needed == 0) {
    Debug("log-buffer", "Nothing to log, bytes_needed = 0");
    return Log::SKIP;
  }

  // Now try to place this entry in the current LogBuffer.
  buffer = _checkout_write(&offset, bytes_needed);

  if (!buffer) {
    Note("Skipping the current log entry for %s because its size (%zu) exceeds "
         "the maximum payload space in a log buffer",
         m_basename, bytes_needed);
    return Log::FAIL;
  }
  //
  // Ok, the checkout_write was successful, which means we have a valid
  // offset into the current buffer.  Marshal the entry into the buffer,
  // and the commit (checkin) the changes.
  //

  if (lad && m_format->is_aggregate()) {
    // the "real" entry data is contained in the LogField objects
    // themselves, not in this lad.
    bytes_used = m_format->m_field_list.marshal_agg(&(*buffer)[offset]);
    ink_assert(bytes_needed >= bytes_used);
    m_format->m_interval_next += m_format->m_interval_sec;
    Debug("log-agg", "Aggregate entry created; next time is %ld", m_format->m_interval_next);
  } else if (lad) {
    bytes_used = m_format->m_field_list.marshal(lad, &(*buffer)[offset]);
    ink_assert(bytes_needed >= bytes_used);
  } else if (text_entry) {
    ink_strlcpy(&(*buffer)[offset], text_entry, bytes_needed);
  }

  buffer->checkin_write(offset);

  return Log::LOG_OK;
}

void
LogObject::_setup_rolling(Log::RollingEnabledValues rolling_enabled, int rolling_interval_sec, int rolling_offset_hr,
                          int rolling_size_mb)
{
  if (!LogRollingEnabledIsValid((int)rolling_enabled)) {
    m_rolling_enabled      = Log::NO_ROLLING;
    m_rolling_interval_sec = 0;
    m_rolling_offset_hr    = 0;
    m_rolling_size_mb      = 0;
    if (rolling_enabled != Log::NO_ROLLING) {
      Warning("Valid rolling_enabled values are %d to %d, invalid value "
              "(%d) specified for %s, rolling will be disabled for this file.",
              Log::NO_ROLLING, Log::INVALID_ROLLING_VALUE - 1, rolling_enabled, m_filename);
    } else {
      Status("Rolling disabled for %s", m_filename);
    }
  } else {
    // do checks for rolling based on time
    //
    if (rolling_enabled == Log::ROLL_ON_TIME_ONLY || rolling_enabled == Log::ROLL_ON_TIME_OR_SIZE ||
        rolling_enabled == Log::ROLL_ON_TIME_AND_SIZE) {
      if (rolling_interval_sec < Log::MIN_ROLLING_INTERVAL_SEC) {
        // check minimum
        m_rolling_interval_sec = Log::MIN_ROLLING_INTERVAL_SEC;
      } else if (rolling_interval_sec > Log::MAX_ROLLING_INTERVAL_SEC) {
        // 1 day maximum
        m_rolling_interval_sec = Log::MAX_ROLLING_INTERVAL_SEC;
      } else if (Log::MAX_ROLLING_INTERVAL_SEC % rolling_interval_sec == 0) {
        // OK, divides day evenly
        m_rolling_interval_sec = rolling_interval_sec;
      } else {
        m_rolling_interval_sec = rolling_interval_sec;
        // increase so it divides day evenly
        while (Log::MAX_ROLLING_INTERVAL_SEC % ++m_rolling_interval_sec)
          ;
      }

      if (m_rolling_interval_sec != rolling_interval_sec) {
        Note("Rolling interval adjusted from %d sec to %d sec for %s", rolling_interval_sec, m_rolling_interval_sec, m_filename);
      }

      if (rolling_offset_hr < 0 || rolling_offset_hr > 23) {
        rolling_offset_hr = 0;
        Note("Rolling offset out of bounds for %s, setting it to %d", m_filename, rolling_offset_hr);
      }

      m_rolling_offset_hr = rolling_offset_hr;
      m_rolling_size_mb   = 0; // it is safe to set it as 0, if we set SIZE rolling,
                               // it will be updated later
    }

    if (rolling_enabled == Log::ROLL_ON_SIZE_ONLY || rolling_enabled == Log::ROLL_ON_TIME_OR_SIZE ||
        rolling_enabled == Log::ROLL_ON_TIME_AND_SIZE) {
      if (rolling_size_mb < 10) {
        m_rolling_size_mb = 10;
        Note("Rolling size invalid(%d) for %s, setting it to 10 MB", rolling_size_mb, m_filename);
      } else {
        m_rolling_size_mb = rolling_size_mb;
      }
    }

    m_rolling_enabled = rolling_enabled;
  }
}

unsigned
LogObject::roll_files(long time_now)
{
  if (!m_rolling_enabled)
    return 0;

  unsigned num_rolled = 0;
  bool roll_on_time   = false;
  bool roll_on_size   = false;

  if (!time_now)
    time_now = LogUtils::timestamp();

  if (m_rolling_enabled != Log::ROLL_ON_SIZE_ONLY) {
    if (m_rolling_interval_sec > 0) {
      // We make no assumptions about the current time not having
      // changed underneath us. This could happen during daylight
      // savings time adjustments, or if time is adjusted via NTP.
      //
      // For this reason we don't cache the number of seconds
      // remaining until the next roll, but we calculate this figure
      // every time ...
      //
      int secs_to_next = LogUtils::seconds_to_next_roll(time_now, m_rolling_offset_hr, m_rolling_interval_sec);

      // ... likewise, we make sure we compute the absolute value
      // of the seconds since the last roll (which would otherwise
      // be negative if time "went back"). We will use this value
      // to make sure we don't roll twice if time goes back shortly
      // after rolling.
      //
      int secs_since_last = (m_last_roll_time < time_now ? time_now - m_last_roll_time : m_last_roll_time - time_now);

      // number of seconds we allow for periodic_tasks() not to be
      // called and still be able to roll
      //
      const int missed_window = 10;

      roll_on_time =
        ((secs_to_next == 0 || secs_to_next >= m_rolling_interval_sec - missed_window) && secs_since_last > missed_window);
    }
  }

  if (m_rolling_enabled != Log::ROLL_ON_TIME_ONLY) {
    if (m_rolling_size_mb) {
      // Get file size and check if the file size if greater than the
      // configured file size for rolling
      roll_on_size = (get_file_size_bytes() > m_rolling_size_mb * LOG_MEGABYTE);
    }
  }

  if ((roll_on_time && should_roll_on_time(m_rolling_enabled)) || (roll_on_size && should_roll_on_size(m_rolling_enabled)) ||
      (roll_on_time && roll_on_size && m_rolling_enabled == Log::ROLL_ON_TIME_AND_SIZE)) {
    num_rolled = _roll_files(m_last_roll_time, time_now ? time_now : LogUtils::timestamp());
  }

  return num_rolled;
}

unsigned
LogObject::_roll_files(long last_roll_time, long time_now)
{
  unsigned num_rolled = 0;

  if (m_logFile) {
    // no need to roll if object writes to a pipe
    if (!writes_to_pipe()) {
      num_rolled += m_logFile->roll(last_roll_time, time_now);
    }
  } else {
    LogHost *host;
    for (host = m_host_list.first(); host; host = m_host_list.next(host)) {
      LogFile *orphan_logfile = host->get_orphan_logfile();
      if (orphan_logfile) {
        num_rolled += orphan_logfile->roll(last_roll_time, time_now);
      }
    }
  }
  m_last_roll_time = time_now;
  return num_rolled;
}

void
LogObject::check_buffer_expiration(long time_now)
{
  LogBuffer *b = (LogBuffer *)FREELIST_POINTER(m_log_buffer);
  if (b && time_now > b->expiration_time()) {
    force_new_buffer();
  }
}

// make sure that we will be able to write the logs to the disk
//
int
LogObject::do_filesystem_checks()
{
  if (m_logFile) {
    return m_logFile->do_filesystem_checks();
  } else {
    return m_host_list.do_filesystem_checks();
  }
}

/*-------------------------------------------------------------------------
  TextLogObject::TextLogObject
  -------------------------------------------------------------------------*/
const LogFormat *TextLogObject::textfmt = MakeTextLogFormat();

TextLogObject::TextLogObject(const char *name, const char *log_dir, bool timestamps, const char *header,
                             Log::RollingEnabledValues rolling_enabled, int flush_threads, int rolling_interval_sec,
                             int rolling_offset_hr, int rolling_size_mb)
  : LogObject(TextLogObject::textfmt, log_dir, name, LOG_FILE_ASCII, header, rolling_enabled, flush_threads, rolling_interval_sec,
              rolling_offset_hr, rolling_size_mb)
{
  if (timestamps) {
    this->set_fmt_timestamps();
  }
}

/*-------------------------------------------------------------------------
  TextLogObject::write

  This routine will take a printf-style format string and variable number
  of arguments, and write them to the text file.

  It really just creates a va_list and calls va_write to do the work.
  Returns the number of bytes written to the file.
  -------------------------------------------------------------------------*/
int
TextLogObject::write(const char *format, ...)
{
  int ret_val;

  ink_assert(format != NULL);
  va_list ap;
  va_start(ap, format);
  ret_val = va_write(format, ap);
  va_end(ap);

  return ret_val;
}

/*-------------------------------------------------------------------------
  TextLogObject::va_write

  This routine will take a format string and va_list and write it as a
  single entry (line) in the text file.  If timestamps are on, then the
  entry will be preceeded by a timestamp.

  Returns ReturnCodeFlags.
  -------------------------------------------------------------------------*/
int
TextLogObject::va_write(const char *format, va_list ap)
{
  return this->va_log(NULL, format, ap);
}

/*-------------------------------------------------------------------------
  LogObjectManager
  -------------------------------------------------------------------------*/

LogObjectManager::LogObjectManager()
{
  _APImutex = new ink_mutex;
  ink_mutex_init(_APImutex, "_APImutex");
}

LogObjectManager::~LogObjectManager()
{
  for (unsigned i = 0; i < _objects.length(); ++i) {
    if (REF_COUNT_OBJ_REFCOUNT_DEC(_objects[i]) == 0) {
      delete _objects[i];
    }
  }

  for (unsigned i = 0; i < _APIobjects.length(); ++i) {
    if (REF_COUNT_OBJ_REFCOUNT_DEC(_APIobjects[i]) == 0) {
      delete _APIobjects[i];
    }
  }

  delete _APImutex;
}

int
LogObjectManager::_manage_object(LogObject *log_object, bool is_api_object, int maxConflicts)
{
  if (is_api_object) {
    ACQUIRE_API_MUTEX("A LogObjectManager::_manage_object");
  }

  bool col_client = log_object->is_collation_client();
  int retVal      = _solve_internal_filename_conflicts(log_object, maxConflicts);

  if (retVal == NO_FILENAME_CONFLICTS) {
    // check for external conflicts only if the object is not a collation
    // client
    //
    if (col_client || (retVal = _solve_filename_conflicts(log_object, maxConflicts), retVal == NO_FILENAME_CONFLICTS)) {
      // do filesystem checks
      //
      if (log_object->do_filesystem_checks() < 0) {
        const char *msg = "The log file %s did not pass filesystem checks. "
                          "No output will be produced for this log";
        Error(msg, log_object->get_full_filename());
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, log_object->get_full_filename());
        retVal = ERROR_DOING_FILESYSTEM_CHECKS;

      } else {
        // no conflicts, add object to the list of managed objects
        //
        REF_COUNT_OBJ_REFCOUNT_INC(log_object);
        if (is_api_object) {
          _APIobjects.push_back(log_object);
        } else {
          _objects.push_back(log_object);
        }

        ink_release_assert(retVal == NO_FILENAME_CONFLICTS);

        Debug("log", "LogObjectManager managing object %s (%s) "
                     "[signature = %" PRIu64 ", address = %p]",
              log_object->get_base_filename(), col_client ? "collation client" : log_object->get_full_filename(),
              log_object->get_signature(), log_object);

        if (log_object->has_alternate_name()) {
          Warning("The full path for the (%s) LogObject %s "
                  "with signature %" PRIu64 " "
                  "has been set to %s rather than %s because the latter "
                  "is being used by another LogObject",
                  log_object->receives_remote_data() ? "remote" : "local", log_object->get_base_filename(),
                  log_object->get_signature(), log_object->get_full_filename(), log_object->get_original_filename());
        }
      }
    }
  }

  if (is_api_object) {
    RELEASE_API_MUTEX("R LogObjectManager::_manage_object");
  }

  return retVal;
}

int
LogObjectManager::_solve_filename_conflicts(LogObject *log_object, int maxConflicts)
{
  int retVal = NO_FILENAME_CONFLICTS;

  const char *filename = log_object->get_full_filename();

  if (access(filename, F_OK)) {
    if (errno != ENOENT) {
      const char *msg = "Cannot access log file %s: %s";
      const char *se  = strerror(errno);

      Error(msg, filename, se);
      LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename, se);
      retVal = ERROR_ACCESSING_LOG_FILE;
    }
  } else {
    // file exists, try to read metafile to get object signature
    //
    uint64_t signature = 0;
    BaseMetaInfo meta_info(filename);
    bool conflicts = true;

    if (meta_info.file_open_successful()) {
      bool got_sig     = meta_info.get_log_object_signature(&signature);
      uint64_t obj_sig = log_object->get_signature();

      if (got_sig && signature == obj_sig) {
        conflicts = false;
      }
      Debug("log", "LogObjectManager::_solve_filename_conflicts\n"
                   "\tfilename = %s\n"
                   "\tmeta file signature = %" PRIu64 "\n"
                   "\tlog object signature = %" PRIu64 "\n"
                   "\tconflicts = %d",
            filename, signature, obj_sig, conflicts);
    }

    if (conflicts) {
      if (maxConflicts == 0) {
        // do not take any action, and return an error status
        //
        const char *msg = "Cannot solve filename conflicts for log file %s";

        Error(msg, filename);
        LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename);
        retVal = CANNOT_SOLVE_FILENAME_CONFLICTS;
      } else {
        // either the meta file could not be read, or the new object's
        // signature and the metafile signature do not match ==>
        // roll old filename so the new object can use the filename
        // it requested (previously we used to rename the NEW file
        // but now we roll the OLD file), or if the log object writes
        // to a pipe, just remove the file if it was open as a pipe

        bool roll_file = true;

        if (log_object->writes_to_pipe()) {
          // determine if existing file is a pipe, and remove it if
          // that is the case so the right metadata for the new pipe
          // is created later
          //
          struct stat s;
          if (stat(filename, &s) < 0) {
            // an error happened while trying to get file info
            //
            const char *msg = "Cannot stat log file %s: %s";
            char *se        = strerror(errno);

            Error(msg, filename, se);
            LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename, se);
            retVal    = ERROR_DETERMINING_FILE_INFO;
            roll_file = false;
          } else {
            if (S_ISFIFO(s.st_mode)) {
              unlink(filename);
              roll_file = false;
            }
          }
        }
        if (roll_file) {
          Warning("File %s will be rolled because a LogObject with "
                  "different format is requesting the same "
                  "filename",
                  filename);
          LogFile logfile(filename, NULL, LOG_FILE_ASCII, 0);
          if (logfile.open_file() == LogFile::LOG_FILE_NO_ERROR) {
            long time_now = LogUtils::timestamp();

            if (logfile.roll(time_now - log_object->get_rolling_interval(), time_now) == 0) {
              // an error happened while trying to roll the file
              //
              _filename_resolution_abort(filename);
              retVal = CANNOT_SOLVE_FILENAME_CONFLICTS;
            }
          } else {
            _filename_resolution_abort(filename);
            retVal = CANNOT_SOLVE_FILENAME_CONFLICTS;
          }
        }
      }
    }
  }
  return retVal;
}

void
LogObjectManager::_filename_resolution_abort(const char *filename)
{
  const char *msg = "Cannot roll log file %s to fix log "
                    "conflicts (filename or log format): %s";
  const char *err = strerror(errno);
  Error(msg, filename, err);
  LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename, err);
}

bool
LogObjectManager::_has_internal_filename_conflict(const char *filename, LogObjectList &objects)
{
  for (unsigned i = 0; i < objects.length(); i++) {
    if (!objects[i]->is_collation_client()) {
      // an internal conflict exists if two objects request the
      // same filename, regardless of the object signatures, since
      // two objects writing to the same file would produce a
      // log with duplicate entries and non monotonic timestamps
      if (strcmp(objects[i]->get_full_filename(), filename) == 0) {
        return true;
      }
    }
  }
  return false;
}

int
LogObjectManager::_solve_internal_filename_conflicts(LogObject *log_object, int maxConflicts, int fileNum)
{
  int retVal           = NO_FILENAME_CONFLICTS;
  const char *filename = log_object->get_full_filename();

  if (_has_internal_filename_conflict(filename, _objects) || _has_internal_filename_conflict(filename, _APIobjects)) {
    if (fileNum < maxConflicts) {
      char new_name[MAXPATHLEN];

      snprintf(new_name, sizeof(new_name), "%s%s%d", log_object->get_original_filename(), LOGFILE_SEPARATOR_STRING, ++fileNum);
      log_object->rename(new_name);
      retVal = _solve_internal_filename_conflicts(log_object, maxConflicts, fileNum);
    } else {
      const char *msg = "Cannot solve filename conflicts for log file %s";

      Error(msg, filename);
      LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename);
      retVal = CANNOT_SOLVE_FILENAME_CONFLICTS;
    }
  }
  return retVal;
}

LogObject *
LogObjectManager::get_object_with_signature(uint64_t signature)
{
  for (unsigned i = 0; i < this->_objects.length(); i++) {
    LogObject *obj = this->_objects[i];

    if (obj->get_signature() == signature) {
      return obj;
    }
  }
  return NULL;
}

void
LogObjectManager::check_buffer_expiration(long time_now)
{
  for (unsigned i = 0; i < this->_objects.length(); i++) {
    this->_objects[i]->check_buffer_expiration(time_now);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::check_buffer_expiration");

  for (unsigned i = 0; i < this->_APIobjects.length(); i++) {
    this->_APIobjects[i]->check_buffer_expiration(time_now);
  }

  RELEASE_API_MUTEX("R LogObjectManager::check_buffer_expiration");
}

size_t
LogObjectManager::preproc_buffers(int idx)
{
  size_t buffers_preproced = 0;

  for (unsigned i = 0; i < this->_objects.length(); i++) {
    buffers_preproced += this->_objects[i]->preproc_buffers(idx);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::preproc_buffers");

  for (unsigned i = 0; i < this->_APIobjects.length(); i++) {
    buffers_preproced += this->_APIobjects[i]->preproc_buffers(idx);
  }

  RELEASE_API_MUTEX("R LogObjectManager::preproc_buffers");

  return buffers_preproced;
}

bool
LogObjectManager::unmanage_api_object(LogObject *logObject)
{
  ACQUIRE_API_MUTEX("A LogObjectManager::unmanage_api_object");

  if (this->_APIobjects.in(logObject)) {
    this->_APIobjects.remove(logObject);

    // Force a buffer flush, then schedule this LogObject to be deleted on the eventProcessor.
    logObject->force_new_buffer();
    new_Derefer(logObject, HRTIME_SECONDS(60));

    RELEASE_API_MUTEX("R LogObjectManager::unmanage_api_object");
    return true;
  }

  RELEASE_API_MUTEX("R LogObjectManager::unmanage_api_object");
  return false;
}

void
LogObjectManager::add_filter_to_all(LogFilter *filter)
{
  for (unsigned i = 0; i < this->_objects.length(); i++) {
    _objects[i]->add_filter(filter);
  }
}

void
LogObjectManager::open_local_pipes()
{
  // for all local objects that write to a pipe, call open_file to force
  // the creation of the pipe so that any potential reader can see it
  //
  for (unsigned i = 0; i < this->_objects.length(); i++) {
    LogObject *obj = _objects[i];
    if (obj->writes_to_pipe() && !obj->is_collation_client()) {
      obj->m_logFile->open_file();
    }
  }
}

void
LogObjectManager::transfer_objects(LogObjectManager &old_mgr)
{
  unsigned num_kept_objects = 0;

  Debug("log-config-transfer", "transferring objects from LogObjectManager %p, to %p", &old_mgr, this);

  if (is_debug_tag_set("log-config-transfer")) {
    Debug("log-config-transfer", "TRANSFER OBJECTS: list of old objects");
    for (unsigned i = 0; i < old_mgr._objects.length(); i++) {
      Debug("log-config-transfer", "%s", old_mgr._objects[i]->get_original_filename());
    }

    Debug("log-config-transfer", "TRANSFER OBJECTS : list of new objects");
    for (unsigned i = 0; i < this->_objects.length(); i++) {
      Debug("log-config-transfer", "%s", _objects[i]->get_original_filename());
    }
  }

  // Transfer the API objects from the old manager. The old manager will retain its refcount.
  for (unsigned i = 0; i < old_mgr._APIobjects.length(); ++i) {
    manage_api_object(old_mgr._APIobjects[i]);
  }

  for (unsigned i = 0; i < old_mgr._objects.length(); ++i) {
    LogObject *old_obj = old_mgr._objects[i];
    LogObject *new_obj;

    Debug("log-config-transfer", "examining existing object %s", old_obj->get_base_filename());

    // See if any of the new objects is just a copy of an old one. If so, transfer the
    // old one to the new manager and delete the new one. We don't use Vec::in here because
    // we need to compare the object hash, not the pointers.
    for (unsigned j = 0; j < _objects.length(); j++) {
      new_obj = _objects[j];

      Debug("log-config-transfer", "comparing existing object %s to new object %s", old_obj->get_base_filename(),
            new_obj->get_base_filename());

      if (*new_obj == *old_obj) {
        Debug("log-config-transfer", "keeping existing object %s", old_obj->get_base_filename());

        REF_COUNT_OBJ_REFCOUNT_INC(old_obj);
        this->_objects[j] = old_obj;

        if (REF_COUNT_OBJ_REFCOUNT_DEC(new_obj) == 0) {
          delete new_obj;
        }
        ++num_kept_objects;
        break;
      }
    }
  }

  if (is_debug_tag_set("log-config-transfer")) {
    Debug("log-config-transfer", "Log Object List after transfer:");
    display();
  }
}

unsigned
LogObjectManager::roll_files(long time_now)
{
  int num_rolled = 0;

  for (unsigned i = 0; i < this->_objects.length(); i++) {
    num_rolled += this->_objects[i]->roll_files(time_now);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::roll_files");

  for (unsigned i = 0; i < this->_APIobjects.length(); i++) {
    num_rolled += this->_APIobjects[i]->roll_files(time_now);
  }

  RELEASE_API_MUTEX("R LogObjectManager::roll_files");

  return num_rolled;
}

void
LogObjectManager::display(FILE *str)
{
  for (unsigned i = 0; i < this->_objects.length(); i++) {
    _objects[i]->display(str);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::display");
  for (unsigned i = 0; i < this->_APIobjects.length(); i++) {
    _APIobjects[i]->display(str);
  }
  RELEASE_API_MUTEX("R LogObjectManager::display");
}

LogObject *
LogObjectManager::find_by_format_name(const char *name) const
{
  for (unsigned i = 0; i < this->_objects.length(); ++i) {
    if (this->_objects[i] && this->_objects[i]->m_format->name_id() == LogFormat::id_from_name(name)) {
      return this->_objects[i];
    }
  }
  return NULL;
}

unsigned
LogObjectManager::get_num_collation_clients() const
{
  unsigned coll_clients = 0;

  for (unsigned i = 0; i < this->_objects.length(); ++i) {
    if (this->_objects[i] && this->_objects[i]->is_collation_client()) {
      ++coll_clients;
    }
  }
  return coll_clients;
}

int
LogObjectManager::log(LogAccess *lad)
{
  int ret           = Log::SKIP;
  ProxyMutex *mutex = this_thread()->mutex;

  for (unsigned i = 0; i < this->_objects.length(); i++) {
    //
    // Auto created LogObject is only applied to LogBuffer
    // data received from network in collation host. It should
    // be ignored here.
    //
    if (_objects[i]->m_auto_created)
      continue;

    ret |= _objects[i]->log(lad);
  }

  //
  // The bit-field code in *ret* are priority chain:
  // FAIL > FULL > LOG_OK > AGGR > SKIP
  // The if-statement should keep step with the priority order.
  //
  if (unlikely(ret & Log::FAIL)) {
    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_access_fail_stat, 1);
  } else if (unlikely(ret & Log::FULL)) {
    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_access_full_stat, 1);
  } else if (likely(ret & Log::LOG_OK)) {
    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_access_ok_stat, 1);
  } else if (unlikely(ret & Log::AGGR)) {
    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_access_aggr_stat, 1);
  } else if (likely(ret & Log::SKIP)) {
    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_access_skip_stat, 1);
  } else {
    ink_release_assert("Unexpected result");
  }

  return ret;
}

void
LogObjectManager::flush_all_objects()
{
  for (unsigned i = 0; i < this->_objects.length(); ++i) {
    this->_objects[i]->force_new_buffer();
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::flush_all_objects");

  for (unsigned i = 0; i < this->_APIobjects.length(); ++i) {
    this->_APIobjects[i]->force_new_buffer();
  }

  RELEASE_API_MUTEX("R LogObjectManager::flush_all_objects");
}

#if TS_HAS_TESTS

static LogObject *
MakeTestLogObject(const char *name)
{
  const char *tmpdir = getenv("TMPDIR");
  LogFormat format("testfmt", NULL);

  if (!tmpdir) {
    tmpdir = "/tmp";
  }

  return new LogObject(&format, tmpdir, name, LOG_FILE_ASCII /* file_format */, name /* header */,
                       Log::ROLL_ON_TIME_ONLY /* rolling_enabled */, 1 /* flush_threads */);
}

REGRESSION_TEST(LogObjectManager_Transfer)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);

  // There used to be a lot of confusion around whether LogObjects were owned by ome or more LogObjectManager
  // objects, or handed off to static storage in the Log class. This test just verifies that this is no longer
  // the case.
  {
    LogObjectManager mgr1;
    LogObjectManager mgr2;

    mgr1.manage_object(MakeTestLogObject("object1"));
    mgr1.manage_object(MakeTestLogObject("object2"));
    mgr1.manage_object(MakeTestLogObject("object3"));
    mgr1.manage_object(MakeTestLogObject("object4"));

    mgr2.transfer_objects(mgr1);

    rprintf(t, "mgr1 has %d objects, mgr2 has %d objects\n", (int)mgr1.get_num_objects(), (int)mgr2.get_num_objects());
    box.check(mgr1.get_num_objects() == 0, "Testing that manager 1 has 0 objects");
    box.check(mgr2.get_num_objects() == 4, "Testing that manager 2 has 4 objects");

    rprintf(t, "running Log::periodoc_tasks()\n");
    Log::periodic_tasks(Thread::get_hrtime() / HRTIME_SECOND);
    rprintf(t, "Log::periodoc_tasks() done\n");
  }

  box = REGRESSION_TEST_PASSED;
}

#endif
