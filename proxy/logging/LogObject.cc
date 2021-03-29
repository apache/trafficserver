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
#include "tscore/ink_platform.h"
#include "tscore/CryptoHash.h"
#include "P_EventSystem.h"
#include "LogUtils.h"
#include "LogField.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogAccess.h"
#include "Log.h"
#include "tscore/TestBox.h"

#include <algorithm>
#include <vector>
#include <thread>

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
  LogBuffer *b = nullptr;
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

LogObject::LogObject(LogConfig *cfg, const LogFormat *format, const char *log_dir, const char *basename, LogFileFormat file_format,
                     const char *header, Log::RollingEnabledValues rolling_enabled, int flush_threads, int rolling_interval_sec,
                     int rolling_offset_hr, int rolling_size_mb, bool auto_created, int rolling_max_count, int rolling_min_count,
                     bool reopen_after_rolling, int pipe_buffer_size)
  : m_alt_filename(nullptr),
    m_flags(0),
    m_signature(0),
    m_flush_threads(flush_threads),
    m_rolling_interval_sec(rolling_interval_sec),
    m_rolling_offset_hr(rolling_offset_hr),
    m_rolling_size_mb(rolling_size_mb),
    m_last_roll_time(0),
    m_max_rolled(rolling_max_count),
    m_min_rolled(rolling_min_count),
    m_reopen_after_rolling(reopen_after_rolling),
    m_buffer_manager_idx(0),
    m_pipe_buffer_size(pipe_buffer_size)
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

  m_logFile =
    new LogFile(m_filename, header, file_format, m_signature, cfg->ascii_buffer_size, cfg->max_line_size, m_pipe_buffer_size);

  if (m_reopen_after_rolling) {
    m_logFile->open_file();
  }

  LogBuffer *b = new LogBuffer(cfg, this, cfg->log_buffer_size);
  ink_assert(b);
  SET_FREELIST_POINTER_VERSION(m_log_buffer, b, 0);

  _setup_rolling(cfg, rolling_enabled, rolling_interval_sec, rolling_offset_hr, rolling_size_mb);

  Debug("log-config", "exiting LogObject constructor, filename=%s this=%p", m_filename, this);
}

LogObject::LogObject(LogObject &rhs)
  : RefCountObj(rhs),
    m_basename(ats_strdup(rhs.m_basename)),
    m_filename(ats_strdup(rhs.m_filename)),
    m_alt_filename(ats_strdup(rhs.m_alt_filename)),
    m_flags(rhs.m_flags),
    m_signature(rhs.m_signature),
    m_rolling_enabled(rhs.m_rolling_enabled),
    m_flush_threads(rhs.m_flush_threads),
    m_rolling_interval_sec(rhs.m_rolling_interval_sec),
    m_rolling_offset_hr(rhs.m_rolling_offset_hr),
    m_rolling_size_mb(rhs.m_rolling_size_mb),
    m_last_roll_time(rhs.m_last_roll_time),
    m_max_rolled(rhs.m_max_rolled),
    m_min_rolled(rhs.m_min_rolled),
    m_reopen_after_rolling(rhs.m_reopen_after_rolling),
    m_buffer_manager_idx(rhs.m_buffer_manager_idx),
    m_pipe_buffer_size(rhs.m_pipe_buffer_size)
{
  m_format         = new LogFormat(*(rhs.m_format));
  m_buffer_manager = new LogBufferManager[m_flush_threads];

  if (rhs.m_logFile) {
    m_logFile = new LogFile(*(rhs.m_logFile));

    if (m_reopen_after_rolling) {
      m_logFile->open_file();
    }
  } else {
    m_logFile = nullptr;
  }

  LogFilter *filter;
  for (filter = rhs.m_filter_list.first(); filter; filter = rhs.m_filter_list.next(filter)) {
    add_filter(filter);
  }

  // copy gets a fresh log buffer
  //
  LogBuffer *b = new LogBuffer(Log::config, this, Log::config->log_buffer_size);
  ink_assert(b);
  SET_FREELIST_POINTER_VERSION(m_log_buffer, b, 0);

  Debug("log-config",
        "exiting LogObject copy constructor, "
        "filename=%s this=%p",
        m_filename, this);
}

LogObject::~LogObject()
{
  Debug("log-config", "entering LogObject destructor, this=%p", this);

  preproc_buffers();
  ats_free(m_basename);
  ats_free(m_filename);
  ats_free(m_alt_filename);
  delete m_format;
  delete[] m_buffer_manager;
  delete static_cast<LogBuffer *>(FREELIST_POINTER(m_log_buffer));
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

  const char *ext = nullptr;
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

  int dir_len      = static_cast<int>(strlen(log_dir));
  int basename_len = len + ext_len + 1;          // include null terminator
  int total_len    = dir_len + 1 + basename_len; // include '/'

  m_filename = static_cast<char *>(ats_malloc(total_len));
  m_basename = static_cast<char *>(ats_malloc(basename_len));

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
  for (f = list.first(); f != nullptr; f = list.next(f)) {
    m_filter_list.add(f, copy);
  }
  m_filter_list.set_conjunction(list.does_conjunction());
}

// we compute the object signature from the fieldlist_str and the printf_str
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
    char *buffer = static_cast<char *>(ats_malloc(buf_size));

    ink_string_concatenate_strings(buffer, fl, ps, filename,
                                   flags & LogObject::BINARY ? "B" : (flags & LogObject::WRITES_TO_PIPE ? "P" : "A"), NULL);

    CryptoHash hash;
    CryptoContext().hash_immediate(hash, buffer, buf_size - 1);
    signature = hash.fold();

    ats_free(buffer);
  }
  return signature;
}

void
LogObject::display(FILE *fd)
{
  fprintf(fd, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  fprintf(fd,
          "LogObject [%p]: format = %s (%p)\nbasename = %s\n"
          "flags = %u\n"
          "signature = %" PRIu64 "\n",
          this, m_format->name(), m_format, m_basename, m_flags, m_signature);

  fprintf(fd, "full path = %s\n", get_full_filename());
  m_filter_list.display(fd);
  fprintf(fd, "++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
}

static head_p
increment_pointer_version(head_p *dst)
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
write_pointer_version(head_p *dst, head_p old_h, void *ptr, head_p::version_type vers)
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
  LogBuffer *new_buffer = nullptr;
  bool retry            = true;
  head_p old_h;

  do {
    // To avoid a race condition, we keep a count of held references in
    // the pointer itself and add this to m_outstanding_references.

    // Increment the version of m_log_buffer, returning the previous version.
    head_p h = increment_pointer_version(&m_log_buffer);

    buffer           = static_cast<LogBuffer *>(FREELIST_POINTER(h));
    result_code      = buffer->checkout_write(write_offset, bytes_needed);
    bool decremented = false;

    switch (result_code) {
    case LogBuffer::LB_OK:
      // checkout succeeded
      retry = false;
      break;

    case LogBuffer::LB_FULL_ACTIVE_WRITERS:
    case LogBuffer::LB_FULL_NO_WRITERS:
      // no more room in current buffer, create a new one
      new_buffer = new LogBuffer(Log::config, this, Log::config->log_buffer_size);

      // swap the new buffer for the old one
      INK_WRITE_MEMORY_BARRIER;

      do {
        INK_QUEUE_LD(old_h, m_log_buffer);
        // we may depend on comparing the old pointer to the new pointer to detect buffer swaps
        // without worrying about pointer collisions because we always allocate a new LogBuffer
        // before freeing the old one
        if (FREELIST_POINTER(old_h) != FREELIST_POINTER(h)) {
          ink_atomic_increment(&buffer->m_references, -1);

          // another thread is already creating a new buffer,
          // so delete new_buffer and try again next loop iteration
          delete new_buffer;
          new_buffer = nullptr;
          break;
        }
      } while (write_pointer_version(&m_log_buffer, old_h, new_buffer, 0) == false);

      if (FREELIST_POINTER(old_h) == FREELIST_POINTER(h)) {
        ink_atomic_increment(&buffer->m_references, FREELIST_VERSION(old_h) - 1);

        int idx = m_buffer_manager_idx++ % m_flush_threads;
        Debug("log-logbuffer", "adding buffer %d to flush list after checkout", buffer->get_id());
        m_buffer_manager[idx].add_to_flush_queue(buffer);
        Log::preproc_notify[idx].signal();
        buffer = nullptr;
      }

      decremented = true;
      break;

    case LogBuffer::LB_RETRY:
      // no more room, but another thread should be taking care of creating a new buffer, so yield to let
      // the other thread finish, then try again
      std::this_thread::yield();
      break;

    case LogBuffer::LB_BUFFER_TOO_SMALL:
      // return a null buffer to signal the caller that this transaction cannot be logged
      retry = false;
      break;

    default:
      ink_assert(false);
    }

    if (!decremented) {
      head_p old_h;

      // The do-while loop protects us from races while we're examining ptr(old_h) and ptr(h)
      // (essentially an optimistic lock)
      do {
        INK_QUEUE_LD(old_h, m_log_buffer);
        if (FREELIST_POINTER(old_h) != FREELIST_POINTER(h)) {
          // Another thread's allocated a new LogBuffer, we don't need to do anything more
          break;
        }

      } while (!write_pointer_version(&m_log_buffer, old_h, FREELIST_POINTER(h), FREELIST_VERSION(old_h) - 1));

      if (FREELIST_POINTER(old_h) != FREELIST_POINTER(h)) {
        // Another thread's allocated a new LogBuffer, meaning this LogObject is no longer referencing the old LogBuffer
        ink_atomic_increment(&buffer->m_references, -1);
      }
    } else {
#ifdef __clang_analyzer__
      if (new_buffer != nullptr) {
        delete new_buffer;
      }
#endif
    }

  } while (retry && write_offset); // if write_offset is null, we do
  // not retry because we really do not want to write to the buffer,
  // only to mark the buffer as full
  if (result_code == LogBuffer::LB_BUFFER_TOO_SMALL) {
    buffer = nullptr;
  }

  return buffer;
}

int
LogObject::va_log(LogAccess *lad, const char *fmt, va_list ap)
{
  static const unsigned MAX_ENTRY = 16 * LOG_KILOBYTE; // 16K? Really?
  char entry[MAX_ENTRY];
  unsigned len = 0;

  ink_assert(fmt != nullptr);
  len = 0;

  if (this->m_flags & LOG_OBJECT_FMT_TIMESTAMP) {
    len = LogUtils::timestamp_to_str(LogUtils::timestamp(), entry, MAX_ENTRY);
    if (unlikely(len <= 0 || len >= MAX_ENTRY)) {
      return Log::FAIL;
    }

    // Add a space after the timestamp
    entry[len++] = ' ';

    if (unlikely(len >= MAX_ENTRY)) {
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
  // Clang doesn't like initializing a view with nullptr, have to check.
  return this->log(lad, std::string_view{text_entry ? text_entry : ""});
}

int
LogObject::log(LogAccess *lad, std::string_view text_entry)
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
  if (!lad && text_entry.empty()) {
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
    if (m_format->m_agg_marshal_space == nullptr) {
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
      val = (f->is_time_field()) ? time_now : *(reinterpret_cast<int64_t *>(data_ptr));
      f->update_aggregate(val);
      data_ptr += INK_MIN_ALIGN;
    }

    if (time_now < m_format->m_interval_next) {
      Debug("log-agg",
            "Time now = %ld, next agg = %ld; not time "
            "for aggregate entry",
            time_now, m_format->m_interval_next);
      return Log::AGGR;
    }
    // can easily compute bytes_needed because all fields are INTs
    // and will use INK_MIN_ALIGN each
    bytes_needed = m_format->field_count() * INK_MIN_ALIGN;
  } else if (lad) {
    bytes_needed = m_format->m_field_list.marshal_len(lad);
  } else if (!text_entry.empty()) {
    bytes_needed = INK_ALIGN_DEFAULT(text_entry.size() + 1); // must include null terminator.
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
  } else if (!text_entry.empty()) {
    char *dst = &(*buffer)[offset];
    memcpy(dst, text_entry.data(), text_entry.size());
    memset(dst + text_entry.size(), 0, bytes_needed - text_entry.size());
  }

  buffer->checkin_write(offset);

  return Log::LOG_OK;
}

void
LogObject::_setup_rolling(LogConfig *cfg, Log::RollingEnabledValues rolling_enabled, int rolling_interval_sec,
                          int rolling_offset_hr, int rolling_size_mb)
{
  if (!LogRollingEnabledIsValid(static_cast<int>(rolling_enabled))) {
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
        while (Log::MAX_ROLLING_INTERVAL_SEC % ++m_rolling_interval_sec) {
          ;
        }
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
    cfg->register_rolled_log_auto_delete(m_basename, m_min_rolled);
    m_rolling_enabled = rolling_enabled;
  }
}

unsigned
LogObject::roll_files(long time_now)
{
  if (!m_rolling_enabled) {
    return 0;
  }

  unsigned num_rolled = 0;
  bool roll_on_time   = false;
  bool roll_on_size   = false;

  if (!time_now) {
    time_now = LogUtils::timestamp();
  }

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
      num_rolled += m_logFile->roll(last_roll_time, time_now, m_reopen_after_rolling);

      if (Log::config->auto_delete_rolled_files && m_max_rolled > 0) {
        m_logFile->trim_rolled(m_max_rolled);
      }
    }
  }

  m_last_roll_time = time_now;
  return num_rolled;
}

void
LogObject::check_buffer_expiration(long time_now)
{
  LogBuffer *b = static_cast<LogBuffer *>(FREELIST_POINTER(m_log_buffer));
  if (b && time_now > b->expiration_time()) {
    force_new_buffer();
  }
}

/*-------------------------------------------------------------------------
  TextLogObject::TextLogObject
  -------------------------------------------------------------------------*/
const LogFormat *TextLogObject::textfmt = MakeTextLogFormat();

TextLogObject::TextLogObject(const char *name, const char *log_dir, bool timestamps, const char *header,
                             Log::RollingEnabledValues rolling_enabled, int flush_threads, int rolling_interval_sec,
                             int rolling_offset_hr, int rolling_size_mb, int rolling_max_count, int rolling_min_count,
                             bool reopen_after_rolling)
  : LogObject(Log::config, TextLogObject::textfmt, log_dir, name, LOG_FILE_ASCII, header, rolling_enabled, flush_threads,
              rolling_interval_sec, rolling_offset_hr, rolling_size_mb, /* auto_created */ false, rolling_max_count,
              rolling_min_count, reopen_after_rolling)
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

  ink_assert(format != nullptr);
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
  return this->va_log(nullptr, format, ap);
}

/*-------------------------------------------------------------------------
  LogObjectManager
  -------------------------------------------------------------------------*/

LogObjectManager::LogObjectManager()
{
  _APImutex = new ink_mutex;
  ink_mutex_init(_APImutex);
}

LogObjectManager::~LogObjectManager()
{
  for (auto &_object : _objects) {
    if (_object->refcount_dec() == 0) {
      delete _object;
    }
  }

  for (auto &_APIobject : _APIobjects) {
    if (_APIobject->refcount_dec() == 0) {
      delete _APIobject;
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

  int retVal = _solve_internal_filename_conflicts(log_object, maxConflicts);

  if (retVal == NO_FILENAME_CONFLICTS) {
    if (retVal = _solve_filename_conflicts(log_object, maxConflicts), retVal == NO_FILENAME_CONFLICTS) {
      // do filesystem checks
      //
      {
        // no conflicts, add object to the list of managed objects
        //
        log_object->refcount_inc();
        if (is_api_object) {
          _APIobjects.push_back(log_object);
        } else {
          _objects.push_back(log_object);
        }

        ink_release_assert(retVal == NO_FILENAME_CONFLICTS);

        Debug("log",
              "LogObjectManager managing object %s (%s) "
              "[signature = %" PRIu64 ", address = %p]",
              log_object->get_base_filename(), log_object->get_full_filename(), log_object->get_signature(), log_object);

        if (log_object->has_alternate_name()) {
          Warning("The full path for the (%s) LogObject "
                  "with signature %" PRIu64 " "
                  "has been set to %s rather than %s because the latter "
                  "is being used by another LogObject",
                  log_object->get_base_filename(), log_object->get_signature(), log_object->get_full_filename(),
                  log_object->get_original_filename());
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
      Debug("log",
            "LogObjectManager::_solve_filename_conflicts\n"
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
        // Either the meta file could not be read, or the new object's
        // signature and the metafile signature do not match.
        // Roll the old filename so the new object can use the filename
        // it requested (previously we used to rename the NEW file
        // but now we roll the OLD file). However, if the log object writes to
        // a pipe don't roll because rolling is not applicable to pipes.

        bool roll_file = true;

        if (log_object->writes_to_pipe()) {
          // Verify whether the existing file is a pipe. If it is,
          // disable the roll_file flag so we don't attempt rolling.
          struct stat s;
          if (stat(filename, &s) < 0) {
            const char *msg = "Cannot stat log file %s: %s";
            char *se        = strerror(errno);

            Error(msg, filename, se);
            LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, msg, filename, se);
            retVal    = ERROR_DETERMINING_FILE_INFO;
            roll_file = false;
          } else {
            if (S_ISFIFO(s.st_mode)) {
              roll_file = false;
            }
          }
        }
        if (roll_file) {
          Warning("File %s will be rolled because a LogObject with "
                  "different format is requesting the same "
                  "filename",
                  filename);
          LogFile logfile(filename, nullptr, LOG_FILE_ASCII, 0);
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
  for (auto &object : objects) {
    // an internal conflict exists if two objects request the
    // same filename, regardless of the object signatures, since
    // two objects writing to the same file would produce a
    // log with duplicate entries and non monotonic timestamps
    if (strcmp(object->get_full_filename(), filename) == 0) {
      return true;
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
  for (auto obj : this->_objects) {
    if (obj->get_signature() == signature) {
      return obj;
    }
  }
  return nullptr;
}

void
LogObjectManager::check_buffer_expiration(long time_now)
{
  for (auto &_object : this->_objects) {
    _object->check_buffer_expiration(time_now);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::check_buffer_expiration");

  for (auto &_APIobject : this->_APIobjects) {
    _APIobject->check_buffer_expiration(time_now);
  }

  RELEASE_API_MUTEX("R LogObjectManager::check_buffer_expiration");
}

size_t
LogObjectManager::preproc_buffers(int idx)
{
  size_t buffers_preproced = 0;

  for (auto &_object : this->_objects) {
    buffers_preproced += _object->preproc_buffers(idx);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::preproc_buffers");

  for (auto &_APIobject : this->_APIobjects) {
    buffers_preproced += _APIobject->preproc_buffers(idx);
  }

  RELEASE_API_MUTEX("R LogObjectManager::preproc_buffers");

  return buffers_preproced;
}

bool
LogObjectManager::unmanage_api_object(LogObject *logObject)
{
  if (!logObject) {
    return false;
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::unmanage_api_object");

  auto index = std::find(this->_APIobjects.begin(), this->_APIobjects.end(), logObject);

  if (index != this->_APIobjects.end()) {
    this->_APIobjects.erase(index);

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
  for (unsigned i = 0; i < this->_objects.size(); i++) {
    _objects[i]->add_filter(filter);
  }
}

void
LogObjectManager::open_local_pipes()
{
  // for all local objects that write to a pipe, call open_file to force
  // the creation of the pipe so that any potential reader can see it
  //
  for (unsigned i = 0; i < this->_objects.size(); i++) {
    LogObject *obj = _objects[i];
    if (obj->writes_to_pipe()) {
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
    for (auto &_object : old_mgr._objects) {
      Debug("log-config-transfer", "%s", _object->get_original_filename());
    }

    Debug("log-config-transfer", "TRANSFER OBJECTS : list of new objects");
    for (unsigned i = 0; i < this->_objects.size(); i++) {
      Debug("log-config-transfer", "%s", _objects[i]->get_original_filename());
    }
  }

  // Transfer the API objects from the old manager. The old manager will retain its refcount.
  for (auto &_APIobject : old_mgr._APIobjects) {
    manage_api_object(_APIobject);
  }

  for (auto old_obj : old_mgr._objects) {
    LogObject *new_obj;

    Debug("log-config-transfer", "examining existing object %s", old_obj->get_base_filename());

    // See if any of the new objects is just a copy of an old one. If so, transfer the
    // old one to the new manager and delete the new one. We don't use Vec::in here because
    // we need to compare the object hash, not the pointers.
    for (unsigned j = 0; j < _objects.size(); j++) {
      new_obj = _objects[j];

      Debug("log-config-transfer", "comparing existing object %s to new object %s", old_obj->get_base_filename(),
            new_obj->get_base_filename());

      if (*new_obj == *old_obj) {
        Debug("log-config-transfer", "keeping existing object %s", old_obj->get_base_filename());

        old_obj->refcount_inc();
        this->_objects[j] = old_obj;

        if (new_obj->refcount_dec() == 0) {
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

  for (auto &_object : this->_objects) {
    num_rolled += _object->roll_files(time_now);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::roll_files");

  for (auto &_APIobject : this->_APIobjects) {
    num_rolled += _APIobject->roll_files(time_now);
  }

  RELEASE_API_MUTEX("R LogObjectManager::roll_files");

  return num_rolled;
}

void
LogObjectManager::display(FILE *str)
{
  for (unsigned i = 0; i < this->_objects.size(); i++) {
    _objects[i]->display(str);
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::display");
  for (unsigned i = 0; i < this->_APIobjects.size(); i++) {
    _APIobjects[i]->display(str);
  }
  RELEASE_API_MUTEX("R LogObjectManager::display");
}

LogObject *
LogObjectManager::find_by_format_name(const char *name) const
{
  for (auto _object : this->_objects) {
    if (_object && _object->m_format->name_id() == LogFormat::id_from_name(name)) {
      return _object;
    }
  }
  return nullptr;
}

int
LogObjectManager::log(LogAccess *lad)
{
  int ret           = Log::SKIP;
  ProxyMutex *mutex = this_thread()->mutex.get();

  for (unsigned i = 0; i < this->_objects.size(); i++) {
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
    ink_release_assert(!"Unexpected result");
  }

  return ret;
}

void
LogObjectManager::flush_all_objects()
{
  for (auto &_object : this->_objects) {
    _object->force_new_buffer();
  }

  ACQUIRE_API_MUTEX("A LogObjectManager::flush_all_objects");

  for (auto &_APIobject : this->_APIobjects) {
    _APIobject->force_new_buffer();
  }

  RELEASE_API_MUTEX("R LogObjectManager::flush_all_objects");
}

#if TS_HAS_TESTS

static LogObject *
MakeTestLogObject(const char *name)
{
  const char *tmpdir = getenv("TMPDIR");
  LogFormat format("testfmt", nullptr);

  if (!tmpdir) {
    tmpdir = "/tmp";
  }

  return new LogObject(Log::config, &format, tmpdir, name, LOG_FILE_ASCII /* file_format */, name /* header */,
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

    rprintf(t, "mgr1 has %d objects, mgr2 has %d objects\n", static_cast<int>(mgr1.get_num_objects()),
            static_cast<int>(mgr2.get_num_objects()));
    box.check(mgr1.get_num_objects() == 0, "Testing that manager 1 has 0 objects");
    box.check(mgr2.get_num_objects() == 4, "Testing that manager 2 has 4 objects");

    rprintf(t, "running Log::periodoc_tasks()\n");
    Log::periodic_tasks(Thread::get_hrtime() / HRTIME_SECOND);
    rprintf(t, "Log::periodoc_tasks() done\n");
  }

  box = REGRESSION_TEST_PASSED;
}

#endif
