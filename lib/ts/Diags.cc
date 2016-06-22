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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_defs.h"
#include "ts/ink_error.h"
#include "ts/ink_assert.h"
#include "ts/ink_time.h"
#include "ts/ink_hrtime.h"
#include "ts/ink_thread.h"
#include "ts/Diags.h"

int diags_on_for_plugins          = 0;
bool DiagsConfigState::enabled[2] = {false, false};

// Global, used for all diagnostics
inkcoreapi Diags *diags = NULL;

static bool setup_diagslog(BaseLogFile *blf);

template <int Size>
static void
vprintline(FILE *fp, char (&buffer)[Size], va_list ap)
{
  int nbytes = strlen(buffer);

  vfprintf(fp, buffer, ap);
  if (nbytes > 0 && buffer[nbytes - 1] != '\n') {
    ink_assert(nbytes < Size);
    putc('\n', fp);
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      char *SrcLoc::str(char *buf, int buflen)
//
//      This method takes a SrcLoc source location data structure and
//      converts it to a human-readable representation, in the buffer <buf>
//      with length <buflen>.  The buffer will always be NUL-terminated, and
//      must always have a length of at least 1.  The buffer address is
//      returned on success.  The routine will only fail if the SrcLoc is
//      not valid, or the buflen is less than 1.
//
//////////////////////////////////////////////////////////////////////////////

char *
SrcLoc::str(char *buf, int buflen) const
{
  const char *shortname;

  if (!this->valid() || buflen < 1)
    return (NULL);

  shortname = strrchr(file, '/');
  shortname = shortname ? (shortname + 1) : file;

  if (func != NULL) {
    snprintf(buf, buflen, "%s:%d (%s)", shortname, line, func);
  } else {
    snprintf(buf, buflen, "%s:%d", shortname, line);
  }
  buf[buflen - 1] = NUL;
  return (buf);
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
//      If bdt is not NULL, and not "", it overrides records.config settings.
//      If bat is not NULL, and not "", it overrides records.config settings.
//
//      When the constructor is done, records.config callbacks will be set,
//      the initial values read, and the Diags instance will be ready to use.
//
//////////////////////////////////////////////////////////////////////////////

Diags::Diags(const char *bdt, const char *bat, BaseLogFile *_diags_log)
  : diags_log(NULL),
    stdout_log(NULL),
    stderr_log(NULL),
    magic(DIAGS_MAGIC),
    show_location(0),
    base_debug_tags(NULL),
    base_action_tags(NULL)
{
  int i;

  cleanup_func = NULL;
  ink_mutex_init(&tag_table_lock, "Diags::tag_table_lock");

  ////////////////////////////////////////////////////////
  // initialize the default, base debugging/action tags //
  ////////////////////////////////////////////////////////

  if (bdt && *bdt) {
    base_debug_tags = ats_strdup(bdt);
  }
  if (bat && *bat) {
    base_action_tags = ats_strdup(bat);
  }

  config.enabled[DiagsTagType_Debug]  = (base_debug_tags != NULL);
  config.enabled[DiagsTagType_Action] = (base_action_tags != NULL);
  diags_on_for_plugins                = config.enabled[DiagsTagType_Debug];

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

  if (setup_diagslog(_diags_log)) {
    diags_log = _diags_log;
  }

  //////////////////////////////////////////////////////////////////
  // start off with empty tag tables, will build in reconfigure() //
  //////////////////////////////////////////////////////////////////

  activated_tags[DiagsTagType_Debug]  = NULL;
  activated_tags[DiagsTagType_Action] = NULL;
  prefix_str                          = "";

  outputlog_rolling_enabled  = RollingEnabledValues::NO_ROLLING;
  outputlog_rolling_interval = -1;
  outputlog_rolling_size     = -1;
  diagslog_rolling_enabled   = RollingEnabledValues::NO_ROLLING;
  diagslog_rolling_interval  = -1;
  diagslog_rolling_size      = -1;

  outputlog_time_last_roll = time(0);
  diagslog_time_last_roll  = time(0);
}

Diags::~Diags()
{
  if (diags_log) {
    delete diags_log;
    diags_log = NULL;
  }

  if (stdout_log) {
    delete stdout_log;
    stdout_log = NULL;
  }

  if (stderr_log) {
    delete stderr_log;
    stderr_log = NULL;
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
//      parentheses if its value is not NULL.  It takes a <diags_level>,
//      which is converted to a prefix string.
//      print_va takes an optional source location structure pointer <loc>,
//      which can be NULL.  If <loc> is not NULL, the source code location
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
Diags::print_va(const char *debug_tag, DiagsLevel diags_level, const SrcLoc *loc, const char *format_string, va_list ap) const
{
  struct timeval tp;
  const char *s;
  char *buffer, *d, timestamp_buf[48];
  char format_buf[1024], format_buf_w_ts[1024], *end_of_format;

  ink_release_assert(diags_level < DiagsLevel_Count);

  ////////////////////////////////////////////////////////////////////////
  // there are 2 format buffers that hold a printf-style format string  //
  // format_buf contains <prefix_string>: (<debug_tag>) <format_string> //
  // and format_buf_w_ts has the same thing with a prepended timestamp. //
  ////////////////////////////////////////////////////////////////////////

  format_buf[0]      = NUL;
  format_buf_w_ts[0] = NUL;

  /////////////////////////////////////////////////////
  // format_buf holds 1024 characters, end_of_format //
  // points to the current available character       //
  /////////////////////////////////////////////////////

  end_of_format  = format_buf;
  *end_of_format = NUL;

  // add the thread id
  end_of_format += snprintf(end_of_format, sizeof(format_buf), "{0x%" PRIx64 "} ", (uint64_t)ink_thread_self());

  //////////////////////////////////////
  // start with the diag level prefix //
  //////////////////////////////////////

  for (s = level_name(diags_level); *s; *end_of_format++ = *s++)
    ;
  *end_of_format++ = ':';
  *end_of_format++ = ' ';

  /////////////////////////////
  // append location, if any //
  /////////////////////////////

  if (loc && loc->valid()) {
    char *lp, buf[256];
    lp = loc->str(buf, sizeof(buf));
    if (lp) {
      *end_of_format++ = '<';
      for (s = lp; *s; *end_of_format++ = *s++)
        ;
      *end_of_format++ = '>';
      *end_of_format++ = ' ';
    }
  }
  //////////////////////////
  // append debugging tag //
  //////////////////////////

  if (debug_tag) {
    *end_of_format++ = '(';
    for (s = debug_tag; *s; *end_of_format++ = *s++)
      ;
    *end_of_format++ = ')';
    *end_of_format++ = ' ';
  }
  //////////////////////////////////////////////////////
  // append original format string, and NUL terminate //
  //////////////////////////////////////////////////////

  for (s = format_string; *s; *end_of_format++ = *s++)
    ;
  *end_of_format++ = NUL;

  //////////////////////////////////////////////////////////////////
  // prepend timestamp into the timestamped version of the buffer //
  //////////////////////////////////////////////////////////////////

  tp               = ink_gettimeofday();
  time_t cur_clock = (time_t)tp.tv_sec;
  buffer           = ink_ctime_r(&cur_clock, timestamp_buf);
  snprintf(&(timestamp_buf[19]), (sizeof(timestamp_buf) - 20), ".%03d", (int)(tp.tv_usec / 1000));

  d    = format_buf_w_ts;
  *d++ = '[';
  for (int i = 4; buffer[i]; i++)
    *d++ = buffer[i];
  *d++   = ']';
  *d++   = ' ';

  for (int k = 0; prefix_str[k]; k++)
    *d++ = prefix_str[k];
  for (s = format_buf; *s; *d++ = *s++)
    ;
  *d++ = NUL;

  //////////////////////////////////////
  // now, finally, output the message //
  //////////////////////////////////////

  lock();
  if (config.outputs[diags_level].to_diagslog) {
    if (diags_log && diags_log->m_fp) {
      va_list tmp;
      va_copy(tmp, ap);
      vprintline(diags_log->m_fp, format_buf_w_ts, tmp);
      va_end(tmp);
    }
  }

  if (config.outputs[diags_level].to_stdout) {
    if (stdout_log && stdout_log->m_fp) {
      va_list tmp;
      va_copy(tmp, ap);
      vprintline(stdout_log->m_fp, format_buf_w_ts, tmp);
      va_end(tmp);
    }
  }

  if (config.outputs[diags_level].to_stderr) {
    if (stderr_log && stderr_log->m_fp) {
      va_list tmp;
      va_copy(tmp, ap);
      vprintline(stderr_log->m_fp, format_buf_w_ts, tmp);
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
    vsnprintf(syslog_buffer, sizeof(syslog_buffer) - 1, format_buf, ap);
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
//      isn't.  If <tag> is NULL, true is returned.  The call uses a lock
//      to get atomic access to the tag tables.
//
//////////////////////////////////////////////////////////////////////////////

bool
Diags::tag_activated(const char *tag, DiagsTagType mode) const
{
  bool activated = false;

  if (tag == NULL)
    return (true);

  lock();
  if (activated_tags[mode])
    activated = (activated_tags[mode]->match(tag) != -1);
  unlock();

  return (activated);
}

//////////////////////////////////////////////////////////////////////////////
//
//      void Diags::activate_taglist(char * taglist, DiagsTagType mode)
//
//      This routine adds all tags in the vertical-bar-seperated taglist
//      to the tag table of type <mode>.  Each addition is done under a lock.
//      If an individual tag is already set, that tag is ignored.  If
//      <taglist> is NULL, this routine exits immediately.
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
    activated_tags[mode] = NULL;
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
Diags::log(const char *tag, DiagsLevel level, const char *file, const char *func, const int line,
           const char *format_string...) const
{
  if (!on(tag))
    return;

  va_list ap;
  va_start(ap, format_string);
  if (show_location) {
    SrcLoc lp(file, func, line);
    print_va(tag, level, &lp, format_string, ap);
  } else {
    print_va(tag, level, NULL, format_string, ap);
  }
  va_end(ap);
}

void
Diags::error_va(DiagsLevel level, const char *file, const char *func, const int line, const char *format_string, va_list ap) const
{
  va_list ap2;

  if (DiagsLevel_IsTerminal(level)) {
    va_copy(ap2, ap);
  }

  if (show_location) {
    SrcLoc lp(file, func, line);
    print_va(NULL, level, &lp, format_string, ap);
  } else {
    print_va(NULL, level, NULL, format_string, ap);
  }

  if (DiagsLevel_IsTerminal(level)) {
    if (cleanup_func) {
      cleanup_func();
    }
    ink_fatal_va(format_string, ap2);
  }

  va_end(ap2);
}

/*
 * Sets up and error handles the given BaseLogFile object to work
 * with this instance of Diags.
 *
 * Returns true on success, false otherwise
 */
static bool
setup_diagslog(BaseLogFile *blf)
{
  if (blf != NULL) {
    if (blf->open_file() != BaseLogFile::LOG_FILE_NO_ERROR) {
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

  log_log_trace("should_roll_diagslog() was called\n");
  log_log_trace("rolling_enabled = %d, output_rolling_size = %d, output_rolling_interval = %d\n", diagslog_rolling_enabled,
                diagslog_rolling_size, diagslog_rolling_interval);
  log_log_trace("RollingEnabledValues::ROLL_ON_TIME = %d\n", RollingEnabledValues::ROLL_ON_TIME);
  log_log_trace("time(0) - last_roll_time = %d\n", time(0) - diagslog_time_last_roll);

  // Roll diags_log if necessary
  if (diags_log && diags_log->is_init()) {
    if (diagslog_rolling_enabled == RollingEnabledValues::ROLL_ON_SIZE) {
      // if we can't even check the file, we can forget about rotating
      struct stat buf;
      if (fstat(fileno(diags_log->m_fp), &buf) != 0)
        return false;

      int size = buf.st_size;
      if (diagslog_rolling_size != -1 && size >= (diagslog_rolling_size * BYTES_IN_MB)) {
        fflush(diags_log->m_fp);
        if (diags_log->roll()) {
          char *oldname = ats_strdup(diags_log->get_name());
          log_log_trace("in should_roll_logs() for diags.log, oldname=%s\n", oldname);
          BaseLogFile *n = new BaseLogFile(oldname);
          if (setup_diagslog(n)) {
            delete diags_log;
            diags_log = n;
          }
          ats_free(oldname);
          ret_val = true;
        }
      }
    } else if (diagslog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME) {
      time_t now = time(0);
      if (diagslog_rolling_interval != -1 && (now - diagslog_time_last_roll) >= diagslog_rolling_interval) {
        fflush(diags_log->m_fp);
        if (diags_log->roll()) {
          diagslog_time_last_roll = now;
          char *oldname           = ats_strdup(diags_log->get_name());
          log_log_trace("in should_roll_logs() for diags.log, oldname=%s\n", oldname);
          BaseLogFile *n = new BaseLogFile(oldname);
          if (setup_diagslog(n)) {
            delete diags_log;
            diags_log = n;
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
  // stdout_log and stderr_log should never be NULL as this point in time
  ink_assert(stdout_log != NULL);
  ink_assert(stderr_log != NULL);

  bool ret_val              = false;
  bool need_consider_stderr = true;

  /*
  log_log_trace("should_roll_outputlog() was called\n");
  log_log_trace("rolling_enabled = %d, output_rolling_size = %d, output_rolling_interval = %d\n", outputlog_rolling_enabled,
                outputlog_rolling_size, outputlog_rolling_interval);
  log_log_trace("RollingEnabledValues::ROLL_ON_TIME = %d\n", RollingEnabledValues::ROLL_ON_TIME);
  log_log_trace("time(0) - last_roll_time = %d\n", time(0) - outputlog_time_last_roll);
  log_log_trace("stdout_log = %p\n", stdout_log);
  */

  // Roll stdout_log if necessary
  if (stdout_log->is_init()) {
    if (outputlog_rolling_enabled == RollingEnabledValues::ROLL_ON_SIZE) {
      // if we can't even check the file, we can forget about rotating
      struct stat buf;
      if (fstat(fileno(stdout_log->m_fp), &buf) != 0)
        return false;

      int size = buf.st_size;
      if (outputlog_rolling_size != -1 && size >= outputlog_rolling_size * BYTES_IN_MB) {
        // since usually stdout and stderr are the same file on disk, we should just
        // play it safe and just flush both BaseLogFiles
        if (stderr_log->is_init())
          fflush(stderr_log->m_fp);
        fflush(stdout_log->m_fp);

        if (stdout_log->roll()) {
          char *oldname = ats_strdup(stdout_log->get_name());
          log_log_trace("in should_roll_logs(), oldname=%s\n", oldname);
          set_stdout_output(oldname);

          // if stderr and stdout are redirected to the same place, we should
          // update the stderr_log object as well
          if (!strcmp(oldname, stderr_log->get_name())) {
            log_log_trace("oldname == stderr_log->get_name()\n");
            set_stderr_output(oldname);
            need_consider_stderr = false;
          }
          ats_free(oldname);
          ret_val = true;
        }
      }
    } else if (outputlog_rolling_enabled == RollingEnabledValues::ROLL_ON_TIME) {
      time_t now = time(0);
      if (outputlog_rolling_interval != -1 && (now - outputlog_time_last_roll) >= outputlog_rolling_interval) {
        // since usually stdout and stderr are the same file on disk, we should just
        // play it safe and just flush both BaseLogFiles
        if (stderr_log->is_init())
          fflush(stderr_log->m_fp);
        fflush(stdout_log->m_fp);

        if (stdout_log->roll()) {
          outputlog_time_last_roll = now;
          char *oldname            = ats_strdup(stdout_log->get_name());
          log_log_trace("in should_roll_logs(), oldname=%s\n", oldname);
          set_stdout_output(oldname);

          // if stderr and stdout are redirected to the same place, we should
          // update the stderr_log object as well
          if (!strcmp(oldname, stderr_log->get_name())) {
            log_log_trace("oldname == stderr_log->get_name()\n");
            set_stderr_output(oldname);
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
  if (ret_val)
    ink_assert(!need_consider_stderr);

  return ret_val;
}

/*
 * Binds stdout to _bind_stdout, provided that _bind_stdout != "".
 * Also sets up a BaseLogFile for stdout.
 *
 * Returns true on binding and setup, false otherwise
 *
 * TODO make this a generic function (ie combine set_stdout_output and
 * set_stderr_output
 */
bool
Diags::set_stdout_output(const char *_bind_stdout)
{
  if (strcmp(_bind_stdout, "") == 0)
    return false;

  if (stdout_log) {
    delete stdout_log;
    stdout_log = NULL;
  }

  // create backing BaseLogFile for stdout
  stdout_log = new BaseLogFile(_bind_stdout);

  // on any errors we quit
  if (!stdout_log || stdout_log->open_file() != BaseLogFile::LOG_FILE_NO_ERROR) {
    fprintf(stderr, "[Warning]: unable to open file=%s to bind stdout to\n", _bind_stdout);
    delete stdout_log;
    stdout_log = NULL;
    return false;
  }
  if (!stdout_log->m_fp) {
    fprintf(stderr, "[Warning]: file pointer for stdout %s = NULL\n", _bind_stdout);
    delete stdout_log;
    stdout_log = NULL;
    return false;
  }

  return rebind_stdout(fileno(stdout_log->m_fp));
}

/*
 * Binds stderr to _bind_stderr, provided that _bind_stderr != "".
 * Also sets up a BaseLogFile for stderr.
 *
 * Returns true on binding and setup, false otherwise
 */
bool
Diags::set_stderr_output(const char *_bind_stderr)
{
  if (strcmp(_bind_stderr, "") == 0)
    return false;

  if (stderr_log) {
    delete stderr_log;
    stderr_log = NULL;
  }

  // create backing BaseLogFile for stdout
  stderr_log = new BaseLogFile(_bind_stderr);

  // on any errors we quit
  if (!stderr_log || stderr_log->open_file() != BaseLogFile::LOG_FILE_NO_ERROR) {
    fprintf(stderr, "[Warning]: unable to open file=%s to bind stderr to\n", _bind_stderr);
    delete stderr_log;
    stderr_log = NULL;
    return false;
  }
  if (!stderr_log->m_fp) {
    fprintf(stderr, "[Warning]: file pointer for stderr %s = NULL\n", _bind_stderr);
    delete stderr_log;
    stderr_log = NULL;
    return false;
  }

  return rebind_stderr(fileno(stderr_log->m_fp));
}

/*
 * Helper function that rebinds stdout to specified file descriptor
 *
 * Returns true on success, false otherwise
 */
bool
Diags::rebind_stdout(int new_fd)
{
  if (new_fd < 0)
    fprintf(stdout, "[Warning]: TS unable to bind stdout to new file descriptor=%d", new_fd);
  else {
    dup2(new_fd, STDOUT_FILENO);
    return true;
  }
  return false;
}

/*
 * Helper function that rebinds stderr to specified file descriptor
 *
 * Returns true on success, false otherwise
 */
bool
Diags::rebind_stderr(int new_fd)
{
  if (new_fd < 0)
    fprintf(stdout, "[Warning]: TS unable to bind stderr to new file descriptor=%d", new_fd);
  else {
    dup2(new_fd, STDERR_FILENO);
    return true;
  }
  return false;
}
