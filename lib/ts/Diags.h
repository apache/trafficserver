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

#pragma once

#include <cstdarg>
#include "ink_mutex.h"
#include "Regex.h"
#include "ink_apidefs.h"
#include "ContFlags.h"
#include "ink_inet.h"
#include "BaseLogFile.h"
#include "SourceLocation.h"

#define DIAGS_MAGIC 0x12345678
#define BYTES_IN_MB 1000000

class Diags;

// extern int diags_on_for_plugins;
enum DiagsTagType {
  DiagsTagType_Debug  = 0, // do not renumber --- used as array index
  DiagsTagType_Action = 1
};

struct DiagsModeOutput {
  bool to_stdout;
  bool to_stderr;
  bool to_syslog;
  bool to_diagslog;
};

enum DiagsLevel { // do not renumber --- used as array index
  DL_Diag = 0,    // process does not die
  DL_Debug,       // process does not die
  DL_Status,      // process does not die
  DL_Note,        // process does not die
  DL_Warning,     // process does not die
  DL_Error,       // process does not die
  DL_Fatal,       // causes process termination
  DL_Alert,       // causes process termination
  DL_Emergency,   // causes process termination
  DL_Undefined    // must be last, used for size!
};

enum StdStream { STDOUT = 0, STDERR };

enum RollingEnabledValues { NO_ROLLING = 0, ROLL_ON_TIME, ROLL_ON_SIZE, ROLL_ON_TIME_OR_SIZE, INVALID_ROLLING_VALUE };

enum DiagsShowLocation { SHOW_LOCATION_NONE = 0, SHOW_LOCATION_DEBUG, SHOW_LOCATION_ALL };

#define DiagsLevel_Count DL_Undefined

#define DiagsLevel_IsTerminal(_l) (((_l) >= DL_Fatal) && ((_l) < DL_Undefined))

// Cleanup Function Prototype - Called before ink_fatal to
//   cleanup process state
typedef void (*DiagsCleanupFunc)();

struct DiagsConfigState {
  // this is static to eliminate many loads from the critical path
  static int enabled[2];                     // one debug, one action
  DiagsModeOutput outputs[DiagsLevel_Count]; // where each level prints
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
  Diags(const char *prefix_string, const char *base_debug_tags, const char *base_action_tags, BaseLogFile *_diags_log,
        int diags_log_perm = -1, int output_log_perm = -1);
  ~Diags();

  BaseLogFile *diags_log;
  BaseLogFile *stdout_log;
  BaseLogFile *stderr_log;

  const unsigned int magic;
  DiagsConfigState config;
  DiagsShowLocation show_location;
  DiagsCleanupFunc cleanup_func;

  ///////////////////////////
  // conditional debugging //
  ///////////////////////////

  bool
  get_override() const
  {
    return get_cont_flag(ContFlags::DEBUG_OVERRIDE);
  }

  bool
  test_override_ip(IpEndpoint const &test_ip)
  {
    return this->debug_client_ip == test_ip;
  }

  bool
  on(DiagsTagType mode = DiagsTagType_Debug) const
  {
    return ((config.enabled[mode] == 1) || (config.enabled[mode] == 2 && this->get_override()));
  }

  bool
  on(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const
  {
    return this->on(mode) && tag_activated(tag, mode);
  }

  /////////////////////////////////////
  // low-level tag inquiry functions //
  /////////////////////////////////////

  bool tag_activated(const char *tag, DiagsTagType mode = DiagsTagType_Debug) const;

  /////////////////////////////
  // raw printing interfaces //
  /////////////////////////////

  const char *level_name(DiagsLevel level) const;

  ///////////////////////////////////////////////////////////////////////
  // user diagnostic output interfaces --- enabled on or off based     //
  // on the value of the enable flag, and the state of the debug tags. //
  ///////////////////////////////////////////////////////////////////////

  void
  print(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(5, 6)
  {
    va_list ap;
    va_start(ap, fmt);
    print_va(tag, level, loc, fmt, ap);
    va_end(ap);
  }

  void print_va(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap) const;

  void
  log(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(5, 6)
  {
    if (on(tag)) {
      va_list ap;
      va_start(ap, fmt);
      print_va(tag, level, loc, fmt, ap);
      va_end(ap);
    }
  }

  void
  log_va(const char *tag, DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap)
  {
    if (on(tag)) {
      print_va(tag, level, loc, fmt, ap);
    }
  }

  void
  error(DiagsLevel level, const SourceLocation *loc, const char *fmt, ...) const TS_PRINTFLIKE(4, 5)
  {
    va_list ap;
    va_start(ap, fmt);
    error_va(level, loc, fmt, ap);
    va_end(ap);
  }

  void error_va(DiagsLevel level, const SourceLocation *loc, const char *fmt, va_list ap) const;

  void dump(FILE *fp = stdout) const;

  void activate_taglist(const char *taglist, DiagsTagType mode = DiagsTagType_Debug);

  void deactivate_all(DiagsTagType mode = DiagsTagType_Debug);

  bool setup_diagslog(BaseLogFile *blf);
  void config_roll_diagslog(RollingEnabledValues re, int ri, int rs);
  void config_roll_outputlog(RollingEnabledValues re, int ri, int rs);
  bool should_roll_diagslog();
  bool should_roll_outputlog();

  bool set_std_output(StdStream stream, const char *file);

  const char *base_debug_tags;  // internal copy of default debug tags
  const char *base_action_tags; // internal copy of default action tags

  IpAddr debug_client_ip;

private:
  const char *prefix_str;
  mutable ink_mutex tag_table_lock; // prevents reconfig/read races
  DFA *activated_tags[2];           // 1 table for debug, 1 for action

  // These are the default logfile permissions
  int diags_logfile_perm  = -1;
  int output_logfile_perm = -1;

  // log rotation variables
  RollingEnabledValues outputlog_rolling_enabled;
  int outputlog_rolling_size;
  int outputlog_rolling_interval;
  RollingEnabledValues diagslog_rolling_enabled;
  int diagslog_rolling_interval;
  int diagslog_rolling_size;
  time_t outputlog_time_last_roll;
  time_t diagslog_time_last_roll;

  bool rebind_std_stream(StdStream stream, int new_fd);

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
#define __FUNCTION__ nullptr
#endif
#endif

extern inkcoreapi Diags *diags;

#define DiagsError(level, fmt, ...)                \
  do {                                             \
    SourceLocation loc = MakeSourceLocation();     \
    diags->error(level, &loc, fmt, ##__VA_ARGS__); \
  } while (0)

#define Status(...) DiagsError(DL_Status, __VA_ARGS__)
#define Note(...) DiagsError(DL_Note, __VA_ARGS__)
#define Warning(...) DiagsError(DL_Warning, __VA_ARGS__)
#define Error(...) DiagsError(DL_Error, __VA_ARGS__)
#define Fatal(...) DiagsError(DL_Fatal, __VA_ARGS__)
#define Alert(...) DiagsError(DL_Alert, __VA_ARGS__)
#define Emergency(...) DiagsError(DL_Emergency, __VA_ARGS__)

#define DiagsErrorV(level, fmt, ap)                  \
  do {                                               \
    const SourceLocation loc = MakeSourceLocation(); \
    diags->error_va(level, &loc, fmt, ap);           \
  } while (0)

#define StatusV(fmt, ap) DiagsErrorV(DL_Status, fmt, ap)
#define NoteV(fmt, ap) DiagsErrorV(DL_Note, fmt, ap)
#define WarningV(fmt, ap) DiagsErrorV(DL_Warning, fmt, ap)
#define ErrorV(fmt, ap) DiagsErrorV(DL_Error, fmt, ap)
#define FatalV(fmt, ap) DiagsErrorV(DL_Fatal, fmt, ap)
#define AlertV(fmt, ap) DiagsErrorV(DL_Alert, fmt, ap)
#define EmergencyV(fmt, ap) DiagsErrorV(DL_Emergency, fmt, ap)

#ifdef TS_USE_DIAGS

#define Diag(tag, ...)                                 \
  do {                                                 \
    if (unlikely(diags->on())) {                       \
      const SourceLocation loc = MakeSourceLocation(); \
      diags->log(tag, DL_Diag, &loc, __VA_ARGS__);     \
    }                                                  \
  } while (0)

#define Debug(tag, ...)                                \
  do {                                                 \
    if (unlikely(diags->on())) {                       \
      const SourceLocation loc = MakeSourceLocation(); \
      diags->log(tag, DL_Debug, &loc, __VA_ARGS__);    \
    }                                                  \
  } while (0)

#define DiagSpecific(flag, tag, ...)                                                                      \
  do {                                                                                                    \
    if (unlikely(diags->on())) {                                                                          \
      const SourceLocation loc = MakeSourceLocation();                                                    \
      flag ? diags->print(tag, DL_Diag, &loc, __VA_ARGS__) : diags->log(tag, DL_Diag, &loc, __VA_ARGS__); \
    }                                                                                                     \
  } while (0)

#define SpecificDebug(flag, tag, ...)                                                                       \
  do {                                                                                                      \
    if (unlikely(diags->on())) {                                                                            \
      const SourceLocation loc = MakeSourceLocation();                                                      \
      flag ? diags->print(tag, DL_Debug, &loc, __VA_ARGS__) : diags->log(tag, DL_Debug, &loc, __VA_ARGS__); \
    }                                                                                                       \
  } while (0)

#define is_debug_tag_set(_t) unlikely(diags->on(_t, DiagsTagType_Debug))
#define is_action_tag_set(_t) unlikely(diags->on(_t, DiagsTagType_Action))
#define debug_tag_assert(_t, _a) (is_debug_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define action_tag_assert(_t, _a) (is_action_tag_set(_t) ? (ink_release_assert(_a), 0) : 0)
#define is_diags_on(_t) unlikely(diags->on(_t))

#else // TS_USE_DIAGS

#define Diag(tag, fmt, ...)
#define Debug(tag, fmt, ...)
#define DiagSpecific(flag, tag, ...)
#define SpecificDebug(flag, tag, ...)

#define is_debug_tag_set(_t) 0
#define is_action_tag_set(_t) 0
#define debug_tag_assert(_t, _a)  /**/
#define action_tag_assert(_t, _a) /**/
#define is_diags_on(_t) 0

#endif // TS_USE_DIAGS
