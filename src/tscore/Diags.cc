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

/****************************************************************************

  Diags.cc

  This file contains code to manipulate run-time diagnostics, and print
  warnings and errors at runtime.  Action tags and debugging tags are
  supported, allowing run-time conditionals affecting diagnostics.

  Joe User should only need to use the macros at the bottom of Diags.h


 ****************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_error.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_time.h"
#include "tscore/ink_hrtime.h"
#include "tscore/ink_thread.h"
#include "tscore/BufferWriter.h"
#include "tscore/Diags.h"

int diags_on_for_plugins         = 0;
int DiagsConfigState::enabled[2] = {0, 0};

// Global, used for all diagnostics
inkcoreapi Diags *diags = nullptr;

static bool
location(const SourceLocation *loc, DiagsShowLocation show, DiagsLevel level)
{
  if (loc && loc->valid()) {
    switch (show) {
    case SHOW_LOCATION_ALL:
      return true;
    case SHOW_LOCATION_DEBUG:
      return level <= DL_Debug;
    default:
      return false;
    }
  }

  return false;
}

//////////////////////////////////////////////////////////////////////////////
//
//      Diags::Diags(char *bdt, char *bat)
//
//      This is the constructor for the Diags class.  The constructor takes
//      two strings called the "base debug tags" (bdt) and the
//      "base action tags" (bat).  These represent debug/action overrides,
//      to override the records.config values.  They current come from
//      command-line options.
//
//      If bdt is not nullptr, and not "", it overrides records.config settings.
//      If bat is not nullptr, and not "", it overrides records.config settings.
//
//      When the constructor is done, records.config callbacks will be set,
//      the initial values read, and the Diags instance will be ready to use.
//
//////////////////////////////////////////////////////////////////////////////

Diags::Diags(const char *prefix_string, const char *bdt, const char *bat, BaseLogFile *_diags_log, int dl_perm, int ol_perm)
  : diags_log(nullptr),
    stdout_log(nullptr),
    stderr_log(nullptr),
    magic(DIAGS_MAGIC),
    show_location(SHOW_LOCATION_NONE),
    base_debug_tags(nullptr),
    base_action_tags(nullptr)
{
  int i;

  cleanup_func = nullptr;
  ink_mutex_init(&tag_table_lock);

  ////////////////////////////////////////////////////////
  // initialize the default, base debugging/action tags //
  ////////////////////////////////////////////////////////

  if (bdt && *bdt) {
    base_debug_tags = ats_strdup(bdt);
  }
  if (bat && *bat) {
    base_action_tags = ats_strdup(bat);
  }

  config.enabled[DiagsTagType_Debug]  = (base_debug_tags != nullptr);
  config.enabled[DiagsTagType_Action] = (base_action_tags != nullptr);
  diags_on_for_plugins                = config.enabled[DiagsTagType_Debug];
  prefix_str                          = prefix_string;

  // The caller must always provide a non-empty prefix.
  ink_release_assert(prefix_str);
  ink_release_assert(*prefix_str);

  for (i = 0; i < DiagsLevel_Count; i++) {
    config.outputs[i].to_stdout   = false;
    config.outputs[i].to_stderr   = false;
    config.outputs[i].to_syslog   = false;
    config.outputs[i].to_diagslog = true;
  }

  // create default stdout and stderr BaseLogFile objects
  // (in case the user of this class doesn't specify in the future)
  stdout_log = new BaseLogFile("stdout");
  stderr_log = new BaseLogFile("stderr");
  stdout_log->open_file(); // should never fail
  stderr_log->open_file(); // should never fail

  //////////////////////////////////////////////////////////////////
  // start off with empty tag tables, will build in reconfigure() //
  //////////////////////////////////////////////////////////////////

  activated_tags[DiagsTagType_Debug]  = nullptr;
  activated_tags[DiagsTagType_Action] = nullptr;

  outputlog_rolling_enabled  = RollingEnabledValues::NO_ROLLING;
  outputlog_rolling_interval = -1;
  outputlog_rolling_size     = -1;
  diagslog_rolling_enabled   = RollingEnabledValues::NO_ROLLING;
  diagslog_rolling_interval  = -1;
  diagslog_rolling_size      = -1;

  outputlog_time_last_roll = time(nullptr);
  diagslog_time_last_roll  = time(nullptr);

  diags_logfile_perm  = dl_perm;
  output_logfile_perm = ol_perm;

  if (setup_diagslog(_diags_log)) {
    diags_log = _diags_log;
  }
}

Diags::~Diags()
{
  if (diags_log) {
    delete diags_log;
    diags_log = nullptr;
  }

  if (stdout_log) {
    delete stdout_log;
    stdout_log = nullptr;
  }

  if (stderr_log) {
    delete stderr_log;
    stderr_log = nullptr;
  }

  ats_free((void *)base_debug_tags);
  ats_free((void *)base_action_tags);

  deactivate_all(DiagsTagType_Debug);
  deactivate_all(DiagsTagType_Action);
}

//////////////////////////////////////////////////////////////////////////////
//
//      void Diags::print_va(...)
//
//      This is the lowest-level diagnostic printing routine, that does the
//      work of formatting and outputting diagnostic and error messages,
//      in the standard format.
//
//      This routine takes an optional <debug_tag>, which is printed in
//      parentheses if its value is not nullptr.  It takes a <diags_level>,
//      which is converted to a prefix string.
//      print_va takes an optional source location structure pointer <loc>,
//      which can be nullptr.  If <loc> is not NULL, the source code location
//      is converted to a string, and printed between angle brackets.
//      Finally, it takes a printf format string <format_string>, and a
//      va_list list of varargs.
//
//      This routine outputs to all of the output targets enabled for this
//      debugging level in config.outputs[diags_level].  Many higher level
//      diagnosting printing routines are built upon print_va, including:
//
//              void print(...)
//              void log_va(...)
//              void log(...)
//
//////////////////////////////////////////////////////////////////////////////

void
Diags::print_va(const char *debug_tag, DiagsLevel diags_level, const SourceLocation *loc, const char *format_string,
                va_list ap) const
{
  ink_release_assert(diags_level < DiagsLevel_Count);

  using ts::LocalBufferWriter;
  LocalBufferWriter<1024> format_writer;

  // Save room for optional newline and terminating NUL bytes.
  format_writer.clip(2);

  //////////////////////
  // append timestamp //
  //////////////////////
  {
    struct timeval tp = ink_gettimeofday();
    time_t cur_clock  = (time_t)tp.tv_sec;
    char timestamp_buf[48];
    char *buffer = ink_ctime_r(&cur_clock, timestamp_buf);

    int num_bytes_written = snprintf(&(timestamp_buf[19]), (sizeof(timestamp_buf) - 20), ".%03d", (int)(tp.tv_usec / 1000));

    if (num_bytes_written > 0) {
      format_writer.write('[');
      format_writer.write(buffer + 4, strlen(buffer + 4));
      format_writer.write("] ", 2);
    }
  }

  size_t timestamp_end_offset = format_writer.size();

  ///////////////////////
  // add the thread id //
  ///////////////////////
  format_writer.fill(
    snprintf(format_writer.auxBuffer(), format_writer.remaining(), "{0x%" PRIx64 "} ", (uint64_t)ink_thread_self()));

  //////////////////////////////////
  // append the diag level prefix //
  //////////////////////////////////

  format_writer.write(level_name(diags_level), strlen(level_name(diags_level)));
  format_writer.write(": ", 2);

  /////////////////////////////
  // append location, if any //
  /////////////////////////////

  if (location(loc, show_location, diags_level)) {
    char *lp, buf[256];
    lp = loc->str(buf, sizeof(buf));
    if (lp) {
      format_writer.write('<');
      format_writer.write(lp, std::min(strlen(lp), sizeof(buf)));
      format_writer.write("> ", 2);
    }
  }
  //////////////////////////
  // append debugging tag //
  //////////////////////////

  if (debug_tag != nullptr) {
    format_writer.write('(');
    format_writer.write(debug_tag, strlen(debug_tag));
    format_writer.write(") ", 2);
  }
  //////////////////////////////////////////////////////
  // append original format string, ensure there is a //
  // newline, and NUL terminate                       //
  //////////////////////////////////////////////////////

  format_writer.write(format_string, strlen(format_string));
  format_writer.extend(2);
  if (format_writer.data()[format_writer.size() - 1] != '\n') {
    format_writer.write('\n');
  }
  format_writer.write('\0');

  //////////////////////////////////////
  // now, finally, output the message //
  //////////////////////////////////////

  lock();
  if (config.outputs[diags_level].to_diagslog) {
    if (diags_log && diags_log->m_fp) {
      va_list tmp;
      va_copy(tmp, ap);
      vfprintf(diags_log->m_fp, format_writer.data(), tmp);
      va_end(tmp);
    }
  }

  if (config.outputs[diags_level].to_stdout) {
    if (stdout_log && stdout_log->m_fp) {
      va_list tmp;
      va_copy(tmp, ap);
      vfprintf(stdout_log->m_fp, format_writer.data(), tmp);
      va_end(tmp);
    }
  }

  if (config.outputs[diags_level].to_stderr) {
    if (stderr_log && stderr_log->m_fp) {
      va_list tmp;
      va_copy(tmp, ap);
      vfprintf(stderr_log->m_fp, format_writer.data(), tmp);
      va_end(tmp);
    }
  }

#if !defined(freebsd)
  unlock();
#endif

  if (config.outputs[diags_level].to_syslog) {
    int priority;
    char syslog_buffer[2048];

    switch (diags_level) {
    case DL_Diag:
    case DL_Debug:
      priority = LOG_DEBUG;

      break;
    case DL_Status:
      priority = LOG_INFO;
      break;
    case DL_Note:
      priority = LOG_NOTICE;
      break;
    case DL_Warning:
      priority = LOG_WARNING;
      break;
    case DL_Error:
      priority = LOG_ERR;
      break;
    case DL_Fatal:
      priority = LOG_CRIT;
      break;
    case DL_Alert:
      priority = LOG_ALERT;
      break;
    case DL_Emergency:
      priority = LOG_EMERG;
      break;
    default:
      priority = LOG_NOTICE;
      break;
    }
    vsnprintf(syslog_buffer, sizeof(syslog_buffer), format_writer.data() + timestamp_end_offset, ap);
    syslog(priority, "%s", syslog_buffer);
  }

#if defined(freebsd)
  unlock();
#endif
}

//////////////////////////////////////////////////////////////////////////////
//
//      bool Diags::tag_activated(char * tag, DiagsTagType mode)
//
//      This routine inquires if a particular <tag> in the tag table of
//      type <mode> is activated, returning true if it is, false if it
//      isn't.  If <tag> is nullptr, true is returned.  The call uses a lock
//      to get atomic access to the tag tables.
//
//////////////////////////////////////////////////////////////////////////////

bool
Diags::tag_activated(const char *tag, DiagsTagType mode) const
{
  bool activated = false;

  if (tag == nullptr) {
    return (true);
  }

  lock();
  if (activated_tags[mode]) {
    activated = (activated_tags[mode]->match(tag) != -1);
  }
  unlock();

  return (activated);
}

//////////////////////////////////////////////////////////////////////////////
//
//      void Diags::activate_taglist(char * taglist, DiagsTagType mode)
//
//      This routine adds all tags in the vertical-bar-separated taglist
//      to the tag table of type <mode>.  Each addition is done under a lock.
//      If an individual tag is already set, that tag is ignored.  If
//      <taglist> is nullptr, this routine exits immediately.
//
//////////////////////////////////////////////////////////////////////////////

void
Diags::activate_taglist(const char *taglist, DiagsTagType mode)
{
  if (taglist) {
    lock();
    if (activated_tags[mode]) {
      delete activated_tags[mode];
    }
    activated_tags[mode] = new DFA;
    activated_tags[mode]->compile(taglist);
    unlock();
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      void Diags::deactivate_all(DiagsTagType mode)
//
//      This routine deactivates all tags in the tag table of type <mode>.
//      The deactivation is done under a lock.  When done, the taglist will
//      be empty.
//
//////////////////////////////////////////////////////////////////////////////

void
Diags::deactivate_all(DiagsTagType mode)
{
  lock();
  if (activated_tags[mode]) {
    delete activated_tags[mode];
    activated_tags[mode] = nullptr;
  }
  unlock();
}

//////////////////////////////////////////////////////////////////////////////
//
//      const char *Diags::level_name(DiagsLevel dl)
//
//      This routine returns a string name corresponding to the error
//      level <dl>, suitable for us as an output log entry prefix.
//
//////////////////////////////////////////////////////////////////////////////

const char *
Diags::level_name(DiagsLevel dl) const
{
  switch (dl) {
  case DL_Diag:
    return ("DIAG");
  case DL_Debug:
    return ("DEBUG");
  case DL_Status:
    return ("STATUS");
  case DL_Note:
    return ("NOTE");
  case DL_Warning:
    return ("WARNING");
  case DL_Error:
    return ("ERROR");
  case DL_Fatal:
    return ("FATAL");
  case DL_Alert:
    return ("ALERT");
  case DL_Emergency:
    return ("EMERGENCY");
  default:
    return ("DIAG");
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      void Diags::dump(FILE *fp)
//
//////////////////////////////////////////////////////////////////////////////

void
Diags::dump(FILE *fp) const
{
  int i;

  fprintf(fp, "Diags:\n");
  fprintf(fp, "  debug.enabled: %d\n", config.enabled[DiagsTagType_Debug]);
  fprintf(fp, "  debug default tags: '%s'\n", (base_debug_tags ? base_debug_tags : "NULL"));
  fprintf(fp, "  action.enabled: %d\n", config.enabled[DiagsTagType_Action]);
  fprintf(fp, "  action default tags: '%s'\n", (base_action_tags ? base_action_tags : "NULL"));
  fprintf(fp, "  outputs:\n");
  for (i = 0; i < DiagsLevel_Count; i++) {
    fprintf(fp, "    %10s [stdout=%d, stderr=%d, syslog=%d, diagslog=%d]\n", level_name((DiagsLevel)i), config.outputs[i].to_stdout,
            config.outputs[i].to_stderr, config.outputs[i].to_syslog, config.outputs[i].to_diagslog);
  }
}

void
Diags::error_va(DiagsLevel level, const SourceLocation *loc, const char *format_string, va_list ap) const
{
  va_list ap2;

  if (DiagsLevel_IsTerminal(level)) {
    va_copy(ap2, ap);
  }

  print_va(nullptr, level, loc, format_string, ap);

  if (DiagsLevel_IsTerminal(level)) {
    if (cleanup_func) {
      cleanup_func();
    }

    // DL_Emergency means the process cannot recover from a reboot
    if (level == DL_Emergency) {
      ink_emergency_va(format_string, ap2);
    } else {
      ink_fatal_va(format_string, ap2);
    }
  }

  va_end(ap2);
}

/*
 * Sets up and error handles the given BaseLogFile object to work
 * with this instance of Diags.
 *
 * Returns true on success, false otherwise
 */
bool
Diags::setup_diagslog(BaseLogFile *blf)
{
  if (blf != nullptr) {
    if (blf->open_file(diags_logfile_perm) != BaseLogFile::LOG_FILE_NO_ERROR) {
      log_log_error("Could not open diags log file: %s\n", strerror(errno));
      delete blf;
      return false;
    }
  }

  return true;
}

void
Diags::config_roll_diagslog(RollingEnabledValues re, int ri, int rs)
{
  diagslog_rolling_enabled  = re;
  diagslog_rolling_interval = ri;
  diagslog_rolling_size     = rs;
}

void
Diags::config_roll_outputlog(RollingEnabledValues re, int ri, int rs)
{
  outputlog_rolling_enabled  = re;
  outputlog_rolling_interval = ri;
  outputlog_rolling_size     = rs;
}

/*
 * Checks diags_log 's underlying file on disk and see if it needs to be rolled,
 * and does so if necessary.
 *
 * This function will replace the current BaseLogFile object with a new one
 * (if we choose to roll), as each BaseLogFile object logically represents one
 * file on disk.
 *
 * Note that, however, cross process race conditions may still exist, especially with
 * the metafile, and further work with flock() for fcntl() may still need to be done.
 *
 * Returns true if any logs rolled, false otherwise
 */
bool
Diags::should_roll_diagslog()
{
  bool ret_val = false;

  log_log_trace("%s was called\n", __func__);
  log_log_trace("%s: rolling_enabled = %d, output_rolling_size = %d, output_rolling_interval = %d\n", __func__,
                diagslog_rolling_enabled, diagslog_rolling_size, diagslog_rolling_interval);
  log_log_trace("%s: RollingEnabledValues::ROLL_ON_TIME = %d\n", __func__, RollingEnabledValues::ROLL_ON_TIME);
  log_log_trace("%s: time(0) - last_roll_time = %d\n", __func__, time(nullptr) - diagslog_time_last_roll);

  // Roll diags_log if necessary
  if (diags_log && diags_log->is_init()) {
    if (diagslog_rolling_enabled == RollingEnabledValues::ROLL_ON_SIZE ||
        diagslog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME_OR_SIZE) {
      // if we can't even check the file, we can forget about rotating
      struct stat buf;
      if (fstat(fileno(diags_log->m_fp), &buf) != 0) {
        return false;
      }

      off_t size = buf.st_size;
      if (diagslog_rolling_size != -1 && size >= (static_cast<off_t>(diagslog_rolling_size) * BYTES_IN_MB)) {
        fflush(diags_log->m_fp);
        if (diags_log->roll()) {
          char *oldname = ats_strdup(diags_log->get_name());
          log_log_trace("in %s for diags.log, oldname=%s\n", __func__, oldname);
          BaseLogFile *n = new BaseLogFile(oldname);
          if (setup_diagslog(n)) {
            BaseLogFile *old_diags = diags_log;
            lock();
            diags_log = n;
            unlock();
            delete old_diags;
          }
          ats_free(oldname);
          ret_val = true;
        }
      }
    }

    if (diagslog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME ||
        diagslog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME_OR_SIZE) {
      time_t now = time(nullptr);
      if (diagslog_rolling_interval != -1 && (now - diagslog_time_last_roll) >= diagslog_rolling_interval) {
        fflush(diags_log->m_fp);
        if (diags_log->roll()) {
          diagslog_time_last_roll = now;
          char *oldname           = ats_strdup(diags_log->get_name());
          log_log_trace("in %s for diags.log, oldname=%s\n", __func__, oldname);
          BaseLogFile *n = new BaseLogFile(oldname);
          if (setup_diagslog(n)) {
            BaseLogFile *old_diags = diags_log;
            lock();
            diags_log = n;
            unlock();
            delete old_diags;
          }
          ats_free(oldname);
          ret_val = true;
        }
      }
    }
  }

  return ret_val;
}

/*
 * Checks stdout_log and stderr_log if their underlying files on disk need to be
 * rolled, and does so if necessary.
 *
 * This function will replace the current BaseLogFile objects with a
 * new one (if we choose to roll), as each BaseLogFile object logically
 * represents one file on disk
 *
 * Note that, however, cross process race conditions may still exist, especially with
 * the metafile, and further work with flock() for fcntl() may still need to be done.
 *
 * Returns true if any logs rolled, false otherwise
 */
bool
Diags::should_roll_outputlog()
{
  // stdout_log and stderr_log should never be nullptr as this point in time
  ink_assert(stdout_log != nullptr);
  ink_assert(stderr_log != nullptr);

  bool ret_val              = false;
  bool need_consider_stderr = true;

  log_log_trace("%s was called\n", __func__);
  log_log_trace("%s: rolling_enabled = %d, output_rolling_size = %d, output_rolling_interval = %d\n", __func__,
                outputlog_rolling_enabled, outputlog_rolling_size, outputlog_rolling_interval);
  log_log_trace("%s: RollingEnabledValues::ROLL_ON_TIME = %d\n", __func__, RollingEnabledValues::ROLL_ON_TIME);
  log_log_trace("%s: time(0) - last_roll_time = %d\n", __func__, time(nullptr) - outputlog_time_last_roll);
  log_log_trace("%s: stdout_log = %p\n", __func__, stdout_log);

  // Roll stdout_log if necessary
  if (stdout_log->is_init()) {
    if (outputlog_rolling_enabled == RollingEnabledValues::ROLL_ON_SIZE ||
        outputlog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME_OR_SIZE) {
      // if we can't even check the file, we can forget about rotating
      struct stat buf;
      if (fstat(fileno(stdout_log->m_fp), &buf) != 0) {
        return false;
      }

      off_t size = buf.st_size;
      if (outputlog_rolling_size != -1 && size >= static_cast<off_t>(outputlog_rolling_size) * BYTES_IN_MB) {
        // since usually stdout and stderr are the same file on disk, we should just
        // play it safe and just flush both BaseLogFiles
        if (stderr_log->is_init()) {
          fflush(stderr_log->m_fp);
        }
        fflush(stdout_log->m_fp);

        if (stdout_log->roll()) {
          char *oldname = ats_strdup(stdout_log->get_name());
          log_log_trace("in %s(), oldname=%s\n", __func__, oldname);
          set_std_output(StdStream::STDOUT, oldname);

          // if stderr and stdout are redirected to the same place, we should
          // update the stderr_log object as well
          if (!strcmp(oldname, stderr_log->get_name())) {
            log_log_trace("oldname == stderr_log->get_name()\n");
            set_std_output(StdStream::STDERR, oldname);
            need_consider_stderr = false;
          }
          ats_free(oldname);
          ret_val = true;
        }
      }
    }

    if (outputlog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME ||
        outputlog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME_OR_SIZE) {
      time_t now = time(nullptr);
      if (outputlog_rolling_interval != -1 && (now - outputlog_time_last_roll) >= outputlog_rolling_interval) {
        // since usually stdout and stderr are the same file on disk, we should just
        // play it safe and just flush both BaseLogFiles
        if (stderr_log->is_init()) {
          fflush(stderr_log->m_fp);
        }
        fflush(stdout_log->m_fp);

        if (stdout_log->roll()) {
          outputlog_time_last_roll = now;
          char *oldname            = ats_strdup(stdout_log->get_name());
          log_log_trace("in %s, oldname=%s\n", __func__, oldname);
          set_std_output(StdStream::STDOUT, oldname);

          // if stderr and stdout are redirected to the same place, we should
          // update the stderr_log object as well
          if (!strcmp(oldname, stderr_log->get_name())) {
            log_log_trace("oldname == stderr_log->get_name()\n");
            set_std_output(StdStream::STDERR, oldname);
            need_consider_stderr = false;
          }
          ats_free(oldname);
          ret_val = true;
        }
      }
    }
  }

  // This assertion has to be true since log rolling for traffic.out is only ever enabled
  // (and useful) when traffic_server is NOT running in stand alone mode. If traffic_server
  // is NOT running in stand alone mode, then stderr and stdout SHOULD ALWAYS be pointing
  // to the same file (traffic.out).
  //
  // If for some reason, someone wants the feature to have stdout pointing to some file on
  // disk, and stderr pointing to a different file on disk, and then also wants both files to
  // rotate according to the (same || different) scheme, it would not be difficult to add
  // some more config options in records.config and said feature into this function.
  if (ret_val) {
    ink_assert(!need_consider_stderr);
  }

  return ret_val;
}

/*
 * Sets up a BaseLogFile for the specified file. Then it binds the specified standard steam
 * to the aforementioned BaseLogFile.
 *
 * Returns true on successful binding and setup, false otherwise
 */
bool
Diags::set_std_output(StdStream stream, const char *file)
{
  const char *target_stream;
  BaseLogFile **current;
  BaseLogFile *old_log, *new_log;

  // If the caller is stupid, we give up
  if (strcmp(file, "") == 0) {
    return false;
  }

  // Figure out which standard stream we want to redirect
  if (stream == StdStream::STDOUT) {
    target_stream = "stdout";
    current       = &stdout_log;
  } else {
    target_stream = "stderr";
    current       = &stderr_log;
  }
  (void)target_stream; // silence clang-analyzer for now
  old_log = *current;
  new_log = new BaseLogFile(file);

  // On any errors we quit
  if (!new_log || new_log->open_file(output_logfile_perm) != BaseLogFile::LOG_FILE_NO_ERROR) {
    log_log_error("[Warning]: unable to open file=%s to bind %s to\n", file, target_stream);
    log_log_error("[Warning]: %s is currently not bound to anything\n", target_stream);
    delete new_log;
    lock();
    *current = nullptr;
    unlock();
    return false;
  }
  if (!new_log->is_open()) {
    log_log_error("[Warning]: file pointer for %s %s = nullptr\n", target_stream, file);
    log_log_error("[Warning]: %s is currently not bound to anything\n", target_stream);
    delete new_log;
    lock();
    *current = nullptr;
    unlock();
    return false;
  }

  // Now exchange the pointer to the standard stream in question
  lock();
  *current = new_log;
  bool ret = rebind_std_stream(stream, fileno(new_log->m_fp));
  unlock();

  // Free the BaseLogFile we rotated out
  if (old_log) {
    delete old_log;
  }

  // "this should never happen"^{TM}
  ink_release_assert(ret);

  return ret;
}

/*
 * Helper function that rebinds a specified stream to specified file descriptor
 *
 * Returns true on success, false otherwise
 */
bool
Diags::rebind_std_stream(StdStream stream, int new_fd)
{
  const char *target_stream;
  int stream_fd;

  // Figure out which stream to dup2
  if (stream == StdStream::STDOUT) {
    target_stream = "stdout";
    stream_fd     = STDOUT_FILENO;
  } else {
    target_stream = "stderr";
    stream_fd     = STDERR_FILENO;
  }
  (void)target_stream; // silence clang-analyzer for now

  if (new_fd < 0) {
    log_log_error("[Warning]: TS unable to bind %s to new file descriptor=%d", target_stream, new_fd);
  } else {
    dup2(new_fd, stream_fd);
    return true;
  }
  return false;
}
