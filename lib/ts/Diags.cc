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

#include "ink_platform.h"
#include "ink_memory.h"
#include "ink_defs.h"
#include "ink_error.h"
#include "ink_assert.h"
#include "ink_time.h"
#include "ink_hrtime.h"
#include "Diags.h"
#include "Compatability.h"


int diags_on_for_plugins = 0;
bool DiagsConfigState::enabled[2] = { false, false };

// Global, used for all diagnostics
inkcoreapi Diags *diags = NULL;

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
  const char * shortname;

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

Diags::Diags(const char *bdt, const char *bat, FILE * _diags_log_fp)
  : diags_log_fp(_diags_log_fp), magic(DIAGS_MAGIC), show_location(0),
    base_debug_tags(NULL), base_action_tags(NULL)
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

  config.enabled[DiagsTagType_Debug] = (base_debug_tags != NULL);
  config.enabled[DiagsTagType_Action] = (base_action_tags != NULL);
  diags_on_for_plugins = config.enabled[DiagsTagType_Debug];

  for (i = 0; i < DiagsLevel_Count; i++) {
    config.outputs[i].to_stdout = false;
    config.outputs[i].to_stderr = false;
    config.outputs[i].to_syslog = false;
    config.outputs[i].to_diagslog = true;
  }

  //////////////////////////////////////////////////////////////////
  // start off with empty tag tables, will build in reconfigure() //
  //////////////////////////////////////////////////////////////////

  activated_tags[DiagsTagType_Debug] = NULL;
  activated_tags[DiagsTagType_Action] = NULL;
  prefix_str = "";

}

Diags::~Diags()
{
  diags_log_fp = NULL;

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
Diags::print_va(const char *debug_tag, DiagsLevel diags_level, const SrcLoc *loc,
                const char *format_string, va_list ap) const
{
  struct timeval tp;
  const char *s;
  char *buffer, *d, timestamp_buf[48];
  char format_buf[1024], format_buf_w_ts[1024], *end_of_format;

  ////////////////////////////////////////////////////////////////////////
  // there are 2 format buffers that hold a printf-style format string  //
  // format_buf contains <prefix_string>: (<debug_tag>) <format_string> //
  // and format_buf_w_ts has the same thing with a prepended timestamp. //
  ////////////////////////////////////////////////////////////////////////

  format_buf[0] = NUL;
  format_buf_w_ts[0] = NUL;

  /////////////////////////////////////////////////////
  // format_buf holds 1024 characters, end_of_format //
  // points to the current available character       //
  /////////////////////////////////////////////////////

  end_of_format = format_buf;
  *end_of_format = NUL;

  // add the thread id
  pthread_t id = pthread_self();
  end_of_format += snprintf(end_of_format, sizeof(format_buf), "{0x%" PRIx64 "} ", (uint64_t) id);

  //////////////////////////////////////
  // start with the diag level prefix //
  //////////////////////////////////////

  for (s = level_name(diags_level); *s; *end_of_format++ = *s++);
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
      for (s = lp; *s; *end_of_format++ = *s++);
      *end_of_format++ = '>';
      *end_of_format++ = ' ';
    }
  }
  //////////////////////////
  // append debugging tag //
  //////////////////////////

  if (debug_tag) {
    *end_of_format++ = '(';
    for (s = debug_tag; *s; *end_of_format++ = *s++);
    *end_of_format++ = ')';
    *end_of_format++ = ' ';
  }
  //////////////////////////////////////////////////////
  // append original format string, and NUL terminate //
  //////////////////////////////////////////////////////

  for (s = format_string; *s; *end_of_format++ = *s++);
  *end_of_format++ = NUL;


  //////////////////////////////////////////////////////////////////
  // prepend timestamp into the timestamped version of the buffer //
  //////////////////////////////////////////////////////////////////

  ink_gethrtimeofday(&tp, NULL);
  time_t cur_clock = (time_t) tp.tv_sec;
  buffer = ink_ctime_r(&cur_clock, timestamp_buf);
  snprintf(&(timestamp_buf[19]), (sizeof(timestamp_buf) - 20), ".%03d", (int) (tp.tv_usec / 1000));

  d = format_buf_w_ts;
  *d++ = '[';
  for (int i = 4; buffer[i]; i++)
    *d++ = buffer[i];
  *d++ = ']';
  *d++ = ' ';

  for (int k = 0; prefix_str[k]; k++)
    *d++ = prefix_str[k];
  for (s = format_buf; *s; *d++ = *s++);
  *d++ = NUL;

  //////////////////////////////////////
  // now, finally, output the message //
  //////////////////////////////////////

  lock();
  if (config.outputs[diags_level].to_diagslog) {
    if (diags_log_fp) {
      va_list ap_scratch;
      va_copy(ap_scratch, ap);
      buffer = format_buf_w_ts;
      vfprintf(diags_log_fp, buffer, ap_scratch);
      {
        int len = strlen(buffer);
        if (len > 0 && buffer[len - 1] != '\n') {
          putc('\n', diags_log_fp);
        }
      }
    }
  }

  if (config.outputs[diags_level].to_stdout) {
    va_list ap_scratch;
    va_copy(ap_scratch, ap);
    buffer = format_buf_w_ts;
    vfprintf(stdout, buffer, ap_scratch);
    {
      int len = strlen(buffer);
      if (len > 0 && buffer[len - 1] != '\n') {
        putc('\n', stdout);
      }
    }
  }

  if (config.outputs[diags_level].to_stderr) {
    va_list ap_scratch;
    va_copy(ap_scratch, ap);
    buffer = format_buf_w_ts;
    vfprintf(stderr, buffer, ap_scratch);
    {
      int len = strlen(buffer);
      if (len > 0 && buffer[len - 1] != '\n') {
        putc('\n', stderr);
      }
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
Diags::dump(FILE * fp) const
{
  int i;

  fprintf(fp, "Diags:\n");
  fprintf(fp, "  debug.enabled: %d\n", config.enabled[DiagsTagType_Debug]);
  fprintf(fp, "  debug default tags: '%s'\n", (base_debug_tags ? base_debug_tags : "NULL"));
  fprintf(fp, "  action.enabled: %d\n", config.enabled[DiagsTagType_Action]);
  fprintf(fp, "  action default tags: '%s'\n", (base_action_tags ? base_action_tags : "NULL"));
  fprintf(fp, "  outputs:\n");
  for (i = 0; i < DiagsLevel_Count; i++) {
    fprintf(fp, "    %10s [stdout=%d, stderr=%d, syslog=%d, diagslog=%d]\n",
            level_name((DiagsLevel) i),
            config.outputs[i].to_stdout,
            config.outputs[i].to_stderr, config.outputs[i].to_syslog, config.outputs[i].to_diagslog);
  }
}

void
Diags::log(const char *tag, DiagsLevel level,
           const char *file, const char *func, const int line,
           const char *format_string ...) const
{
  if (! on(tag))
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
Diags::error_va(DiagsLevel level,
             const char *file, const char *func, const int line,
             const char *format_string, va_list ap) const
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
    if (cleanup_func)
      cleanup_func();
    ink_fatal_va(1, format_string, ap2);
  }
}
