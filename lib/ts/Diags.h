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

  Diags.h

  This file contains code to manipulate run-time diagnostics, and print
  warnings and errors at runtime.  Action tags and debugging tags are
  supported, allowing run-time conditionals affecting diagnostics.


 ****************************************************************************/

#ifndef __DIAGS_H___
#define __DIAGS_H___

#include <stdarg.h>
#include "ink_mutex.h"
#include "Regex.h"
#include "ink_apidefs.h"
#include "ContFlags.h"
#include "ink_inet.h"
#include "BaseLogFile.h"

#define DIAGS_MAGIC 0x12345678
#define BYTES_IN_MB 1000000

class Diags;

// extern int diags_on_for_plugins;
typedef enum {
  DiagsTagType_Debug  = 0, // do not renumber --- used as array index
  DiagsTagType_Action = 1
} DiagsTagType;

struct DiagsModeOutput {
  bool to_stdout;
  bool to_stderr;
  bool to_syslog;
  bool to_diagslog;
};

typedef enum {  // do not renumber --- used as array index
  DL_Diag = 0,  // process does not die
  DL_Debug,     // process does not die
  DL_Status,    // process does not die
  DL_Note,      // process does not die
  DL_Warning,   // process does not die
  DL_Error,     // process does not die
  DL_Fatal,     // causes process termination
  DL_Alert,     // causes process termination
  DL_Emergency, // causes process termination
  DL_Undefined  // must be last, used for size!
} DiagsLevel;

enum RollingEnabledValues { NO_ROLLING = 0, ROLL_ON_TIME, ROLL_ON_SIZE, INVALID_ROLLING_VALUE };

#define DiagsLevel_Count DL_Undefined

#define DiagsLevel_IsTerminal(_l) (((_l) >= DL_Fatal) && ((_l) < DL_Undefined))

// Cleanup Function Prototype - Called before ink_fatal to
//   cleanup process state
typedef void (*DiagsCleanupFunc)();

struct DiagsConfigState {
  // this is static to eliminate many loads from the critical path
  static bool enabled[2];                    // one debug, one action
  DiagsModeOutput outputs[DiagsLevel_Count]; // where each level prints
};

//////////////////////////////////////////////////////////////////////////////
//
//      class SrcLoc
//
//      The SrcLoc class wraps up a source code location, including file
//      name, function name, and line number, and contains a method to
//      format the result into a string buffer.
//
//////////////////////////////////////////////////////////////////////////////

#define DiagsMakeLocation() SrcLoc(__FILE__, __FUNCTION__, __LINE__)

class SrcLoc
{
public:
  const char *file;
  const char *func;
  int line;

  bool
  valid() const
  {
    return file && line;
  }

  SrcLoc(const SrcLoc &rhs) : file(rhs.file), func(rhs.func), line(rhs.line) {}
  SrcLoc(const char *_file, const char *_func, int _line) : file(_file), func(_func), line(_line) {}
  SrcLoc &
  operator=(const SrcLoc &rhs)
  {
    this->file = rhs.file;
    this->func = rhs.func;
    this->line = rhs.line;
    return *this;
  }

  char *str(char *buf, int buflen) const;
};

//////////////////////////////////////////////////////////////////////////////
//
//      class Diags
//
//      The Diags class is used for global configuration of the run-time
//      diagnostics system.  This class provides the following services:
//
//      * run-time notices, debugging, warnings, errors
//      * debugging tags to selectively enable & disable diagnostics
//      * action tags to selectively enable & disable code paths
//      * configurable output to stdout, stderr, syslog, error logs
//      * traffic_manager interface supporting on-the-fly reconfiguration
//
//////////////////////////////////////////////////////////////////////////////

class Diags
{
public:
  Diags(const char *base_debug_tags, const char *base_action_tags, BaseLogFile *_diags_log);
  ~Diags();

  BaseLogFile *diags_log;
  BaseLogFile *stdout_log;
  BaseLogFile *stderr_log;

  const unsigned int magic;
  volatile DiagsConfigState config;
  int show_location;
  DiagsCleanupFunc cleanup_func;
  const char *prefix_str;

  ///////////////////////////
  // conditional debugging //
  ///////////////////////////

  bool
  get_override() const
  {
    return get_cont_flag(ContFlags::DEBUG_OVERRIDE);
  }

  bool
  on(DiagsTagType mode = DiagsTagType_Debug) const
  {
    return (config.enabled[mode]);
  }

  bool
  on(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const
  {
    return (config.enabled[mode] && tag_activated(tag, mode));
  }

  /////////////////////////////////////
  // low-level tag inquiry functions //
  /////////////////////////////////////

  inkcoreapi bool tag_activated(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const;

  /////////////////////////////
  // raw printing interfaces //
  /////////////////////////////

  const char *level_name(DiagsLevel dl) const;

  inkcoreapi void print_va(const char *tag, DiagsLevel dl, const SrcLoc *loc, const char *format_string, va_list ap) const;

  //////////////////////////////
  // user printing interfaces //
  //////////////////////////////

  void
  print(const char *tag, DiagsLevel dl, const char *file, const char *func, const int line, const char *format_string, ...) const
    TS_PRINTFLIKE(7, 8)
  {
    va_list ap;
    va_start(ap, format_string);
    if (show_location) {
      SrcLoc lp(file, func, line);
      print_va(tag, dl, &lp, format_string, ap);
    } else {
      print_va(tag, dl, NULL, format_string, ap);
    }
    va_end(ap);
  }

  ///////////////////////////////////////////////////////////////////////
  // user diagnostic output interfaces --- enabled on or off based     //
  // on the value of the enable flag, and the state of the debug tags. //
  ///////////////////////////////////////////////////////////////////////

  void
  log_va(const char *tag, DiagsLevel dl, const SrcLoc *loc, const char *format_string, va_list ap)
  {
    if (!on(tag))
      return;
    print_va(tag, dl, loc, format_string, ap);
  }

  void log(const char *tag, DiagsLevel dl, const char *file, const char *func, const int line, const char *format_string, ...) const
    TS_PRINTFLIKE(7, 8);

  void error_va(DiagsLevel dl, const char *file, const char *func, const int line, const char *format_string, va_list ap) const;

  void
  error(DiagsLevel level, const char *file, const char *func, const int line, const char *format_string, ...) const
    TS_PRINTFLIKE(6, 7)
  {
    va_list ap;
    va_start(ap, format_string);
    error_va(level, file, func, line, format_string, ap);
    va_end(ap);
  }

  void dump(FILE *fp = stdout) const;

  void activate_taglist(const char *taglist, DiagsTagType mode = DiagsTagType_Debug);

  void deactivate_all(DiagsTagType mode = DiagsTagType_Debug);

  void config_roll_diagslog(RollingEnabledValues re, int ri, int rs);
  void config_roll_outputlog(RollingEnabledValues re, int ri, int rs);
  bool should_roll_diagslog();
  bool should_roll_outputlog();

  bool set_stdout_output(const char *_bind_stdout);
  bool set_stderr_output(const char *_bind_stderr);

  const char *base_debug_tags;  // internal copy of default debug tags
  const char *base_action_tags; // internal copy of default action tags

private:
  mutable ink_mutex tag_table_lock; // prevents reconfig/read races
  DFA *activated_tags[2];           // 1 table for debug, 1 for action

  // log rotation variables
  RollingEnabledValues outputlog_rolling_enabled;
  int outputlog_rolling_size;
  int outputlog_rolling_interval;
  RollingEnabledValues diagslog_rolling_enabled;
  int diagslog_rolling_interval;
  int diagslog_rolling_size;
  time_t outputlog_time_last_roll;
  time_t diagslog_time_last_roll;

  bool rebind_stdout(int new_fd);
  bool rebind_stderr(int new_fd);
  void
  lock() const
  {
    ink_mutex_acquire(&tag_table_lock);
  }
  void
  unlock() const
  {
    ink_mutex_release(&tag_table_lock);
  }
};

//////////////////////////////////////////////////////////////////////////
//                                                                      //
//      Macros                                                          //
//                                                                      //
//      The following are diagnostic macros that wrap up the compiler   //
//      __FILE__, __FUNCTION__, and __LINE__ macros into closures       //
//      and then invoke the closure on the remaining arguments.         //
//                                                                      //
//      This closure hack is done, because the cpp preprocessor doesn't //
//      support manipulation and union of varargs parameters.           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#if !defined(__GNUC__)
#ifndef __FUNCTION__
#define __FUNCTION__ NULL
#endif
#endif

extern inkcoreapi Diags *diags;

#define DTA(l) l, __FILE__, __FUNCTION__, __LINE__
void dummy_debug(const char *tag, const char *fmt, ...) TS_PRINTFLIKE(2, 3);
inline void
dummy_debug(const char *tag, const char *fmt, ...)
{
  (void)tag;
  (void)fmt;
}

#define Status(...) diags->error(DTA(DL_Status), __VA_ARGS__)
#define Note(...) diags->error(DTA(DL_Note), __VA_ARGS__)
#define Warning(...) diags->error(DTA(DL_Warning), __VA_ARGS__)
#define Error(...) diags->error(DTA(DL_Error), __VA_ARGS__)
#define Fatal(...) diags->error(DTA(DL_Fatal), __VA_ARGS__)
#define Alert(...) diags->error(DTA(DL_Alert), __VA_ARGS__)
#define Emergency(...) diags->error(DTA(DL_Emergency), __VA_ARGS__)

#define StatusV(fmt, ap) diags->error_va(DTA(DL_Status), fmt, ap)
#define NoteV(fmt, ap) diags->error_va(DTA(DL_Note), fmt, ap)
#define WarningV(fmt, ap) diags->error_va(DTA(DL_Warning), fmt, ap)
#define ErrorV(fmt, ap) diags->error_va(DTA(DL_Error), fmt, ap)
#define FatalV(fmt, ap) diags->error_va(DTA(DL_Fatal), fmt, ap)
#define AlertV(fmt, ap) diags->error_va(DTA(DL_Alert), fmt, ap)
#define EmergencyV(fmt, ap) diags->error_va(DTA(DL_Emergency), fmt, ap)

#ifdef TS_USE_DIAGS
#define Diag(tag, ...)       \
  if (unlikely(diags->on())) \
  diags->log(tag, DTA(DL_Diag), __VA_ARGS__)
#define Debug(tag, ...)      \
  if (unlikely(diags->on())) \
    diags->log(tag, DTA(DL_Debug), __VA_ARGS__);
#define DiagSpecific(flag, tag, ...) \
  if (unlikely(diags->on()))         \
  flag ? diags->print(tag, DTA(DL_Diag), __VA_ARGS__) : diags->log(tag, DTA(DL_Diag), __VA_ARGS__)
#define DebugSpecific(flag, tag, ...) \
  if (unlikely(diags->on()))          \
  flag ? diags->print(tag, DTA(DL_Debug), __VA_ARGS__) : diags->log(tag, DTA(DL_Debug), __VA_ARGS__)

#define is_debug_tag_set(_t) unlikely(diags->on(_t, DiagsTagType_Debug))
#define is_action_tag_set(_t) unlikely(diags->on(_t, DiagsTagType_Action))
#define debug_tag_assert(_t, _a) (is_debug_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define action_tag_assert(_t, _a) (is_action_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define is_diags_on(_t) unlikely(diags->on(_t))

#else // TS_USE_DIAGS

#define Diag(tag, fmt, ...) \
  if (0)                    \
  dummy_debug(tag, __VA_ARGS__)
#define Debug(tag, fmt, ...) \
  if (0)                     \
  dummy_debug(tag, __VA_ARGS__)
#define DiagSpecific(flag, tag, ...) \
  if (0 && tag)                      \
    dummy_debug(tag, __VA_ARGS__);
#define DebugSpecific(flag, tag, ...) \
  if (0 && tag)                       \
    dummy_debug(tag, __VA_ARGS__);

#define is_debug_tag_set(_t) 0
#define is_action_tag_set(_t) 0
#define debug_tag_assert(_t, _a)  /**/
#define action_tag_assert(_t, _a) /**/
#define is_diags_on(_t) 0

#endif // TS_USE_DIAGS
#endif /*_Diags_h_*/
